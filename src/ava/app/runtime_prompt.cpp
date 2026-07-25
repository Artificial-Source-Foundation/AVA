#include "sys.h"

#include "command_registry.h"
#include "plugin_event_hooks.h"
#include "runtime_compaction.h"
#include "runtime_prompt.h"
#include "runtime_reasoning.h"
#include "runtime_retry.h"
#include "runtime/Session.h"
#include "runtime_sessions.h"
#include "runtime/markdown_files.h"
#include "runtime/command_names.h"
#include "session_title_coordinator.h"
#include "subagent_delivery_manager.h"

#include "ava/diagnostics/runtime_diagnostics.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/agent/subagent_config.h"
#include "ava/tools/file_tools.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/static_resources.h"
#include "ava/config/prompt_config.h"
#include "ava/permissions/permission_rules.h"
#include "ava/provider/curl_transport.h"
#include "ava/provider/registry.h"
#include "ava/context/skill_loader.h"
#include "ava/lsp/configured_provider.h"
#include "ava/core/error.h"
#include "ava/core/fingerprint.h"
#include "ava/core/ids.h"
#include "ava/core/path.h"
#include "ava/core/string_utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::app::runtime {

namespace {

constexpr std::size_t kMaxRuntimeFreshnessBytes = 256 * 1024;
constexpr std::size_t kMaxPluginResourceFreshnessBytes = 64 * 1024;

struct RuntimePromptResource
{
  FreshnessSourceKind kind = FreshnessSourceKind::SystemPrompt;
  std::string scope;
  std::string name;
  std::filesystem::path path;
  std::string text;
};

struct LoadedPluginPromptResource
{
  std::string scope;
  std::string plugin_id;
  std::string name;
  std::filesystem::path path;
  std::string text;
};

struct LoadedPluginSkillResource
{
  std::string scope;
  std::string plugin_id;
  std::string name;
  std::size_t byte_count = 0;
  std::uint64_t content_fingerprint = 0;
  ava::context::LoadedSkill skill;
};

struct PluginResourceLoadFailure
{
  FreshnessSourceKind kind = FreshnessSourceKind::PluginPrompt;
  std::string scope;
  std::string plugin_id;
  std::string name;
  std::filesystem::path path;
};

struct PluginRuntimeResources
{
  ava::plugin::PluginDiagnostics diagnostics;
  std::vector<LoadedPluginPromptResource> prompts;
  std::vector<LoadedPluginSkillResource> skills;
  std::vector<PluginResourceLoadFailure> failures;
};

//FIXME: this is virtually identical to `read_bounded_file` and thus a duplicate: remove code duplication!
ava::core::Result<std::string> read_freshness_file(std::filesystem::path const& path, std::size_t max_bytes)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "freshness source is not a regular file");
    error.with_context("path", path.string());
    if (status_error)
      error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }

  std::error_code size_error;
  auto const size = std::filesystem::file_size(path, size_error);
  if (size_error || size > max_bytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "freshness source is too large");
    error.with_context("path", path.string());
    error.with_context("max_bytes", std::to_string(max_bytes));
    if (size_error)
      error.with_context("cause", size_error.message());
    return std::unexpected(std::move(error));
  }

  std::ifstream file(path, std::ios::binary);
  if (!file)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to open freshness source").with_context("path", path.string()));

  std::string content;
  std::array<char, 4096> buffer{};
  while (file)
  {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.gcount() > 0)
      content.append(buffer.data(), static_cast<std::size_t>(file.gcount()));
    if (content.size() > max_bytes)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "freshness source is too large");
      error.with_context("path", path.string());
      error.with_context("max_bytes", std::to_string(max_bytes));
      return std::unexpected(std::move(error));
    }
  }
  if (!file.eof() && file.fail())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading freshness source").with_context("path", path.string()));
  return content;
}

void add_freshness_file(std::vector<FreshnessSourceMetadata>& sources, FreshnessSourceKind kind, std::string scope, std::string source_id, std::string name,
                        std::filesystem::path const& path, std::size_t max_bytes = kMaxRuntimeFreshnessBytes)
{
  auto content = read_freshness_file(path, max_bytes);
  if (!content)
    return;
  sources.push_back(FreshnessSourceMetadata{.kind = kind,
                                            .scope = std::move(scope),
                                            .source_id = std::move(source_id),
                                            .name = std::move(name),
                                            .path = ava::core::normalized_absolute_path(path),
                                            .byte_count = content->size(),
                                            .content_fingerprint = ava::core::content_fingerprint(*content)});
}

void add_prompt_command_source_files(std::vector<PromptCommandSourceFile>& sources, std::filesystem::path const& root, UnifiedCommandSource source,
                                     std::string_view scope)
{
  std::vector<CommandRegistryDiagnostic> diagnostics;
  auto files = markdown_files(root, diagnostics, source);
  for (auto const& file : files)
  {
    auto name = command_name_for_file(root, file);
    if (!name)
      continue;
    auto content = read_bounded_file(file);
    if (!content)
      continue;
    auto parsed = parse_markdown(*content);
    auto body = markdown_field(parsed, "template");
    if (body.empty())
      body = std::move(parsed.body);
    if (core::trim_view(body).empty())
      continue;
    sources.push_back(PromptCommandSourceFile{.command_name = std::move(*name),
                                              .scope = std::string(scope),
                                              .path = file,
                                              .byte_count = content->size(),
                                              .content_fingerprint = ava::core::content_fingerprint(*content)});
  }
}

std::vector<PromptCommandSourceFile> prompt_command_source_files(std::filesystem::path const& workspace_dir, ava::config::XdgPaths const& paths,
                                                                 bool include_project_commands)
{
  std::vector<PromptCommandSourceFile> sources;
  if (include_project_commands)
  {
    add_prompt_command_source_files(sources, workspace_dir / ".ava" / "commands", UnifiedCommandSource::PromptProject, "project");
    add_prompt_command_source_files(sources, workspace_dir / ".ava" / "command", UnifiedCommandSource::PromptProject, "project");
  }
  add_prompt_command_source_files(sources, paths.ava_config_dir / "commands", UnifiedCommandSource::PromptGlobal, "global");
  add_prompt_command_source_files(sources, paths.ava_config_dir / "command", UnifiedCommandSource::PromptGlobal, "global");
  std::ranges::sort(sources, [](PromptCommandSourceFile const& left, PromptCommandSourceFile const& right) {
    if (left.scope != right.scope)
      return left.scope < right.scope;
    return left.command_name < right.command_name;
  });
  return sources;
}

void add_prompt_command_freshness_sources(std::vector<FreshnessSourceMetadata>& sources, ava::config::XdgPaths const& paths,
                                          std::filesystem::path const& workspace_dir, bool include_project_resources)
{
  for (auto const& command : prompt_command_source_files(workspace_dir, paths, include_project_resources))
  {
    sources.push_back(FreshnessSourceMetadata{.kind = FreshnessSourceKind::PromptCommand,
                                              .scope = command.scope,
                                              .source_id = command.command_name,
                                              .name = command.command_name,
                                              .path = ava::core::normalized_absolute_path(command.path),
                                              .byte_count = command.byte_count,
                                              .content_fingerprint = command.content_fingerprint});
  }
}

ava::core::Result<std::optional<RuntimePromptResource>> load_prompt_resource(ava::config::XdgPaths const& paths, std::filesystem::path const& workspace_dir,
                                                                             bool include_project_resources, FreshnessSourceKind kind, std::string name)
{
  std::filesystem::path selected_path;
  std::string scope;
  auto const project_path = workspace_dir / ".ava" / name;
  if (include_project_resources && std::filesystem::exists(project_path))
  {
    selected_path = project_path;
    scope = "project";
  }
  else
  {
    auto const global_path = paths.ava_config_dir / name;
    if (!std::filesystem::exists(global_path))
      return std::optional<RuntimePromptResource>{};
    selected_path = global_path;
    scope = "global";
  }

  auto text = read_freshness_file(selected_path, kMaxRuntimeFreshnessBytes);
  if (!text)
    return std::unexpected(std::move(text.error()));
  return RuntimePromptResource{
      .kind = kind, .scope = std::move(scope), .name = std::move(name), .path = ava::core::normalized_absolute_path(selected_path), .text = std::move(*text)};
}

void add_prompt_resource_freshness_source(std::vector<FreshnessSourceMetadata>& sources, RuntimePromptResource const& resource)
{
  sources.push_back(FreshnessSourceMetadata{.kind = resource.kind,
                                            .scope = resource.scope,
                                            .source_id = resource.name,
                                            .name = resource.name,
                                            .path = resource.path,
                                            .byte_count = resource.text.size(),
                                            .content_fingerprint = ava::core::content_fingerprint(resource.text)});
}

void add_inline_prompt_freshness_source(std::vector<FreshnessSourceMetadata>& sources, FreshnessSourceKind kind, std::string source_id, std::string name,
                                        std::string_view text)
{
  sources.push_back(FreshnessSourceMetadata{.kind = kind,
                                            .scope = "cli",
                                            .source_id = std::move(source_id),
                                            .name = std::move(name),
                                            .path = {},
                                            .byte_count = text.size(),
                                            .content_fingerprint = ava::core::content_fingerprint(text)});
}

ava::core::Result<LoadedPluginPromptResource> load_plugin_prompt_resource(ava::plugin::PluginStatus const& status,
                                                                          ava::plugin::PluginResourceContribution const& resource)
{
  auto const& manifest = status.plugin.manifest;
  auto loaded = ava::plugin::load_plugin_static_resource(manifest, resource, kMaxPluginResourceFreshnessBytes);
  if (!loaded)
    return std::unexpected(std::move(loaded.error()));
  return LoadedPluginPromptResource{.scope = std::string(ava::plugin::to_string(status.plugin.scope)),
                                    .plugin_id = manifest.id,
                                    .name = resource.name,
                                    .path = std::move(loaded->path),
                                    .text = std::move(loaded->content)};
}

ava::core::Result<LoadedPluginSkillResource> load_plugin_skill_resource(ava::plugin::PluginStatus const& status,
                                                                        ava::plugin::PluginResourceContribution const& resource)
{
  auto const& manifest = status.plugin.manifest;
  auto loaded = ava::plugin::load_plugin_static_resource(manifest, resource, kMaxPluginResourceFreshnessBytes);
  if (!loaded)
    return std::unexpected(std::move(loaded.error()));
  auto const byte_count = loaded->content.size();
  auto const fingerprint = ava::core::content_fingerprint(loaded->content);
  auto skill = ava::context::load_declared_skill_content(ava::context::DeclaredSkillFileOptions{.path = loaded->path,
                                                                                                .name = resource.name,
                                                                                                .description = resource.description,
                                                                                                .source_type = ava::context::SkillSourceType::Plugin,
                                                                                                .preloaded_content = std::nullopt,
                                                                                                .max_file_bytes = kMaxPluginResourceFreshnessBytes},
                                                         std::move(loaded->content));
  if (!skill)
    return std::unexpected(std::move(skill.error()));
  return LoadedPluginSkillResource{.scope = std::string(ava::plugin::to_string(status.plugin.scope)),
                                   .plugin_id = manifest.id,
                                   .name = resource.name,
                                   .byte_count = byte_count,
                                   .content_fingerprint = fingerprint,
                                   .skill = std::move(*skill)};
}

PluginRuntimeResources load_plugin_runtime_resources(ava::config::XdgPaths const& paths, std::filesystem::path const& workspace_dir,
                                                     bool include_project_resources)
{
  PluginRuntimeResources resources;
  resources.diagnostics = ava::plugin::collect_plugin_diagnostics(
      ava::plugin::PluginDiscoveryOptions{.global_plugins_dir = paths.ava_config_dir / "plugins",
                                          .project_plugins_dir = include_project_resources ? workspace_dir / ".ava" / "plugins" : std::filesystem::path{}},
      paths.ava_state_dir / "plugin-enablement.json", workspace_dir);
  for (auto const& status : resources.diagnostics.plugins)
  {
    if (!status.enabled)
      continue;
    auto const& manifest = status.plugin.manifest;
    for (auto const& prompt : manifest.contributes.prompts)
    {
      auto loaded = load_plugin_prompt_resource(status, prompt);
      if (loaded)
        resources.prompts.push_back(std::move(*loaded));
      else
        resources.failures.push_back(PluginResourceLoadFailure{.kind = FreshnessSourceKind::PluginPrompt,
                                                               .scope = std::string(ava::plugin::to_string(status.plugin.scope)),
                                                               .plugin_id = manifest.id,
                                                               .name = prompt.name,
                                                               .path = ava::plugin::plugin_static_resource_display_path(manifest, prompt)});
    }
    for (auto const& skill : manifest.contributes.skills)
    {
      auto loaded = load_plugin_skill_resource(status, skill);
      if (loaded)
        resources.skills.push_back(std::move(*loaded));
      else
        resources.failures.push_back(PluginResourceLoadFailure{.kind = FreshnessSourceKind::PluginSkill,
                                                               .scope = std::string(ava::plugin::to_string(status.plugin.scope)),
                                                               .plugin_id = manifest.id,
                                                               .name = skill.name,
                                                               .path = ava::plugin::plugin_static_resource_display_path(manifest, skill)});
    }
  }
  return resources;
}

void add_plugin_prompt_context_files(std::vector<ava::context::LoadedContextFile>& files, std::vector<LoadedPluginPromptResource> const& prompts)
{
  for (auto const& prompt : prompts)
  {
    files.push_back(ava::context::LoadedContextFile{
        .path = prompt.path, .source_type = ava::context::ContextSourceType::Plugin, .byte_count = prompt.text.size(), .content = prompt.text});
  }
}

void add_or_replace_loaded_skill(std::vector<ava::context::LoadedSkill>& skills, ava::context::LoadedSkill skill)
{
  auto const existing = std::ranges::find_if(skills, [&](ava::context::LoadedSkill const& item) { return item.name == skill.name; });
  if (existing != skills.end())
  {
    *existing = std::move(skill);
    return;
  }
  skills.push_back(std::move(skill));
}

void add_plugin_skills(std::vector<ava::context::LoadedSkill>& skills, std::vector<LoadedPluginSkillResource> const& plugin_skills)
{
  for (auto const& plugin_skill : plugin_skills)
  {
    add_or_replace_loaded_skill(skills, plugin_skill.skill);
  }
  std::ranges::sort(skills, [](ava::context::LoadedSkill const& left, ava::context::LoadedSkill const& right) { return left.name < right.name; });
}

void add_skill_freshness_sources(std::vector<FreshnessSourceMetadata>& sources, std::vector<ava::context::LoadedSkill> const& skills)
{
  for (auto const& skill : skills)
  {
    if (skill.source_type == ava::context::SkillSourceType::Plugin)
      continue;
    add_freshness_file(sources, FreshnessSourceKind::Skill, ava::context::to_string(skill.source_type), skill.name, skill.name, skill.path,
                       skill.byte_count == 0 ? kMaxRuntimeFreshnessBytes : skill.byte_count);
  }
}

void add_loaded_plugin_prompt_freshness_source(std::vector<FreshnessSourceMetadata>& sources, LoadedPluginPromptResource const& resource)
{
  sources.push_back(FreshnessSourceMetadata{.kind = FreshnessSourceKind::PluginPrompt,
                                            .scope = resource.scope,
                                            .source_id = resource.plugin_id,
                                            .name = resource.name,
                                            .path = resource.path,
                                            .byte_count = resource.text.size(),
                                            .content_fingerprint = ava::core::content_fingerprint(resource.text)});
}

void add_loaded_plugin_skill_freshness_source(std::vector<FreshnessSourceMetadata>& sources, LoadedPluginSkillResource const& resource)
{
  sources.push_back(FreshnessSourceMetadata{.kind = FreshnessSourceKind::PluginSkill,
                                            .scope = resource.scope,
                                            .source_id = resource.plugin_id,
                                            .name = resource.name,
                                            .path = resource.skill.path,
                                            .byte_count = resource.byte_count,
                                            .content_fingerprint = resource.content_fingerprint});
}

void add_failed_plugin_resource_freshness_source(std::vector<FreshnessSourceMetadata>& sources, PluginResourceLoadFailure const& failure)
{
  sources.push_back(FreshnessSourceMetadata{.kind = failure.kind,
                                            .scope = failure.scope,
                                            .source_id = failure.plugin_id,
                                            .name = failure.name,
                                            .path = failure.path,
                                            .byte_count = 0,
                                            .content_fingerprint = 0});
}

void add_plugin_freshness_sources(std::vector<FreshnessSourceMetadata>& sources, PluginRuntimeResources const& resources)
{
  auto const& diagnostics = resources.diagnostics;
  for (auto const& status : diagnostics.plugins)
  {
    auto const& manifest = status.plugin.manifest;
    add_freshness_file(sources, FreshnessSourceKind::PluginManifest, std::string(ava::plugin::to_string(status.plugin.scope)), manifest.id, "manifest",
                       manifest.path);
  }
  for (auto const& prompt : resources.prompts) add_loaded_plugin_prompt_freshness_source(sources, prompt);
  for (auto const& skill : resources.skills) add_loaded_plugin_skill_freshness_source(sources, skill);
  for (auto const& failure : resources.failures) add_failed_plugin_resource_freshness_source(sources, failure);
}

BasePromptMetadata base_prompt_metadata(ava::config::PromptSelection const& prompt)
{
  return BasePromptMetadata{.from_override = prompt.from_override,
                            .source_path = prompt.source_path,
                            .byte_count = prompt.text.size(),
                            .content_fingerprint = ava::core::content_fingerprint(prompt.text)};
}

}  // namespace

ava::core::Result<PromptState> load_runtime_prompt_state(ava::config::XdgPaths const& paths, ava::config::ModelInfo const& model, ava::agent::Mode mode,
                                                         std::filesystem::path const& workspace_dir, std::filesystem::path const& current_dir,
                                                         bool include_project_resources, PromptOverrides const& prompt_overrides)
{
  auto prompt = ava::config::select_prompt(paths, model, mode);
  if (!prompt)
    return std::unexpected(prompt.error());

  auto loaded_context = ava::context::load_context_files(ava::context::ContextLoadOptions{
      .workspace_root = workspace_dir,
      .current_dir = current_dir,
      .global_agents_file = paths.global_agents_file,
  });
  if (!loaded_context)
    return std::unexpected(loaded_context.error());

  auto plugin_resources = load_plugin_runtime_resources(paths, workspace_dir, include_project_resources);
  add_plugin_prompt_context_files(*loaded_context, plugin_resources.prompts);

  std::vector<ContextSourceMetadata> context_sources;
  context_sources.reserve(loaded_context->size());
  for (auto const& file : *loaded_context)
  {
    context_sources.push_back(ContextSourceMetadata{.path = file.path,
                                                    .source_type = file.source_type,
                                                    .byte_count = file.byte_count,
                                                    .content_fingerprint = ava::core::content_fingerprint(file.content)});
  }

  auto loaded_skills = ava::context::load_skills(ava::context::SkillLoadOptions{
      .workspace_root = workspace_dir,
      .include_project_skills = include_project_resources,
  });
  add_plugin_skills(loaded_skills.skills, plugin_resources.skills);
  auto loaded_subagents =
      ava::agent::load_subagents(ava::agent::SubagentLoadOptions{.workspace_root = workspace_dir, .include_project_agents = include_project_resources});
  std::vector<FreshnessSourceMetadata> freshness_sources;
  auto selected_prompt = std::move(*prompt);
  auto system_prompt = selected_prompt.text;

  if (prompt_overrides.system_prompt)
  {
    selected_prompt = ava::config::PromptSelection{.text = *prompt_overrides.system_prompt, .from_override = true};
    system_prompt = *prompt_overrides.system_prompt;
    add_inline_prompt_freshness_source(freshness_sources, FreshnessSourceKind::SystemPrompt, "--system-prompt", "--system-prompt",
                                       *prompt_overrides.system_prompt);
  }
  else
  {
    auto system_resource = load_prompt_resource(paths, workspace_dir, include_project_resources, FreshnessSourceKind::SystemPrompt, "SYSTEM.md");
    if (!system_resource)
      return std::unexpected(std::move(system_resource.error()));
    if (*system_resource)
    {
      add_prompt_resource_freshness_source(freshness_sources, **system_resource);
      system_prompt = (*system_resource)->text;
    }
  }

  if (!prompt_overrides.append_system_prompts.empty())
  {
    std::string append_text;
    std::size_t append_index = 0;
    for (auto const& append_prompt : prompt_overrides.append_system_prompts)
    {
      ++append_index;
      add_inline_prompt_freshness_source(freshness_sources, FreshnessSourceKind::AppendSystemPrompt, "--append-system-prompt", std::to_string(append_index),
                                         append_prompt);
      if (append_prompt.empty())
        continue;
      if (!append_text.empty())
        append_text += "\n\n";
      append_text += append_prompt;
    }
    if (!append_text.empty())
      system_prompt += "\n\n" + append_text;
  }
  else
  {
    auto append_resource = load_prompt_resource(paths, workspace_dir, include_project_resources, FreshnessSourceKind::AppendSystemPrompt, "APPEND_SYSTEM.md");
    if (!append_resource)
      return std::unexpected(std::move(append_resource.error()));
    if (*append_resource)
    {
      add_prompt_resource_freshness_source(freshness_sources, **append_resource);
      if (!(*append_resource)->text.empty())
        system_prompt += "\n\n" + (*append_resource)->text;
    }
  }

  add_prompt_command_freshness_sources(freshness_sources, paths, workspace_dir, include_project_resources);
  add_skill_freshness_sources(freshness_sources, loaded_skills.skills);
  add_plugin_freshness_sources(freshness_sources, plugin_resources);

  system_prompt += ava::context::format_context_for_prompt(*loaded_context) + ava::context::format_available_skills_for_prompt(loaded_skills.skills) +
                   ava::agent::format_available_subagents_for_prompt(loaded_subagents.subagents);
  return PromptState{.mode = mode,
                     .base_prompt = base_prompt_metadata(selected_prompt),
                     .context_sources = std::move(context_sources),
                     .freshness_sources = std::move(freshness_sources),
                     .system_prompt = std::move(system_prompt)};
}

}  // namespace ava::app::runtime

namespace ava::app {
namespace {

runtime::Event base_event(runtime::Session const& session, runtime::EventType type)
{
  runtime::Event event;
  event.type = type;
  event.timestamp = ava::session::now_timestamp();
  event.session_id = session.store.session_id();
  event.mode = session.mode();
  event.provider_id = session.model.provider_id;
  event.model_id = session.model.model_id;
  return event;
}

runtime::Event base_event_locked(runtime::Session const& session, runtime::EventType type, std::mutex* mutex)
{
  if (!mutex)
    return base_event(session, type);
  std::lock_guard lock(*mutex);
  return base_event(session, type);
}

bool is_agent_loop_canceled_error(ava::core::Error const& error)
{
  return error.message() == "agent loop canceled" || error.message() == "transport retry canceled" || error.message() == "transport request canceled";
}

StopReason outcome_reason_for_error(ava::core::Error const& error)
{
  if (is_agent_loop_canceled_error(error))
    return StopReason::UserCanceled;
  if (error.message().find("maximum tool iterations") != std::string::npos)
    return StopReason::MaxToolCalls;
  if (error.message().find("provider output event limit") != std::string::npos)
    return StopReason::MaxTurns;
  if (error.category() == ava::core::ErrorCategory::Tool)
    return StopReason::ToolError;
  if (error.category() == ava::core::ErrorCategory::Session || error.category() == ava::core::ErrorCategory::Io)
    return StopReason::PersistenceError;
  return StopReason::ProviderError;
}

std::optional<ava::diagnostics::RuntimeFailureClass> diagnostic_failure_class(ava::core::Error const& error) noexcept
{
  if (is_agent_loop_canceled_error(error))
    return std::nullopt;
  switch (error.category())
  {
    case ava::core::ErrorCategory::Configuration:
      return ava::diagnostics::RuntimeFailureClass::Configuration;
    case ava::core::ErrorCategory::Provider:
      return ava::diagnostics::RuntimeFailureClass::Provider;
    case ava::core::ErrorCategory::Session:
    case ava::core::ErrorCategory::Io:
      return ava::diagnostics::RuntimeFailureClass::Session;
    case ava::core::ErrorCategory::Tool:
      return ava::diagnostics::RuntimeFailureClass::Tool;
    case ava::core::ErrorCategory::Unknown:
      return ava::diagnostics::RuntimeFailureClass::Runtime;
    case ava::core::ErrorCategory::InvalidArgument:
    case ava::core::ErrorCategory::NotFound:
    case ava::core::ErrorCategory::PermissionDenied:
      return std::nullopt;
  }
  return std::nullopt;
}

constexpr std::size_t kMaxPromptFileReferences = 5;
constexpr std::size_t kPromptReferenceMaxBytes = 32 * 1024;
constexpr std::size_t kPromptReferenceMaxLines = 300;

struct PromptFileReference
{
  std::string path;
};

bool is_reference_start(std::string_view text, std::size_t index)
{
  return text[index] == '@' && (index == 0 || std::isspace(static_cast<unsigned char>(text[index - 1])) != 0);
}

bool is_trailing_reference_punctuation(char ch)
{
  switch (ch)
  {
    case '.':
    case ',':
    case ';':
    case ':':
    case '!':
    case '?':
    case ')':
    case ']':
    case '}':
      return true;
    default:
      return false;
  }
}

std::vector<PromptFileReference> prompt_file_references(std::string_view text)
{
  std::vector<PromptFileReference> references;
  auto add_reference = [&references](std::string path) {
    if (path.empty())
      return;
    if (std::ranges::any_of(references, [&](PromptFileReference const& existing) { return existing.path == path; }))
      return;
    references.push_back(PromptFileReference{.path = std::move(path)});
  };
  for (std::size_t index = 0; index < text.size(); ++index)
  {
    if (!is_reference_start(text, index))
      continue;
    if (index + 1 < text.size() && text[index + 1] == '"')
    {
      auto end = index + 2;
      while (end < text.size() && text[end] != '"') ++end;
      add_reference(std::string(text.substr(index + 2, end - index - 2)));
      index = end;
      continue;
    }
    auto end = index + 1;
    while (end < text.size() && std::isspace(static_cast<unsigned char>(text[end])) == 0) ++end;
    auto token_end = end;
    while (token_end > index + 1 && is_trailing_reference_punctuation(text[token_end - 1])) --token_end;
    if (token_end <= index + 1)
      continue;
    add_reference(std::string(text.substr(index + 1, token_end - index - 1)));
  }
  return references;
}

ava::tools::ToolContext prompt_file_reference_context(runtime::Session& session, runtime::RunOptions const& options)
{
  return ava::tools::ToolContext{.workspace_dir = session.workspace_dir(),
                                 .spill_dir = session.store.session_path().parent_path() / "spill",
                                 .mode = session.mode(),
                                 .permission_resolver = options.permission_resolver,
                                 .permission_audit_sink = [&session](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
                                   return ava::agent::append_permission_decision(session.owner_append_route(), event);
                                 },
                                 .cancel_requested = options.cancel_requested,
                                 .permission_tool_name = "file_reference",
                                 .permission_actor = "user",
                                 .anchor_set = session.anchor_set,
                                 .ava_authority_roots = command_authority_roots_for_session(session),
                                 .exact_file_access = options.exact_file_access,
                                 .command_executor = options.command_executor,
                                 .session_id = session.store.session_id(),
                                 .provider_id = session.model.provider_id,
                                 .model_id = session.model.model_id,
                                 .current_dir = session.current_dir(),
                                 .tool_visibility = session.tool_visibility()};
}

ava::core::Result<std::string> expand_prompt_file_references(runtime::Session& session, std::string const& user_message, runtime::RunOptions const& options)
{
  auto references = prompt_file_references(user_message);
  if (references.empty())
    return user_message;
  if (references.size() > kMaxPromptFileReferences)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "too many @ file references");
    error.with_context("max_references", std::to_string(kMaxPromptFileReferences));
    error.with_context("reference_count", std::to_string(references.size()));
    return std::unexpected(std::move(error));
  }

  auto context = prompt_file_reference_context(session, options);
  std::string expanded = user_message;
  expanded += "\n\nReferenced files:";
  for (auto const& reference : references)
  {
    auto read = ava::tools::read_file(context, session.current_dir() / reference.path,
                                      ava::tools::ReadOptions{.max_bytes = kPromptReferenceMaxBytes, .offset_line = 1, .max_lines = kPromptReferenceMaxLines});
    if (!read)
    {
      auto error = read.error();
      error.with_context("file_reference", reference.path);
      return std::unexpected(std::move(error));
    }
    expanded += "\n\n--- ";
    expanded += reference.path;
    expanded += " ---\n";
    expanded += read->content;
    if (read->truncated)
    {
      expanded += "\n[reference truncated";
      if (read->next_offset_line > 0)
        expanded += "; next offset " + std::to_string(read->next_offset_line);
      if (read->byte_limited)
        expanded += "; byte cap reached";
      if (read->line_limited)
        expanded += "; line cap reached";
      expanded += "]";
    }
  }
  return expanded;
}

}  // namespace

ava::core::RuntimeTerminalOutcome runtime_outcome_for_stop_reason(StopReason reason) noexcept
{
  switch (reason)
  {
    case StopReason::Completed:
      return ava::core::RuntimeTerminalOutcome::Completed;
    case StopReason::UserCanceled:
      return ava::core::RuntimeTerminalOutcome::Cancelled;
    case StopReason::MaxTurns:
    case StopReason::MaxToolCalls:
    case StopReason::NoProgress:
      return ava::core::RuntimeTerminalOutcome::MaxTurnRequests;
    case StopReason::Deadline:
    case StopReason::ProviderError:
    case StopReason::ToolError:
    case StopReason::PersistenceError:
      return ava::core::RuntimeTerminalOutcome::Error;
  }
  return ava::core::RuntimeTerminalOutcome::Error;
}

StopReason stop_reason_for_runtime_outcome(ava::core::RuntimeTerminalOutcome outcome) noexcept
{
  switch (outcome)
  {
    case ava::core::RuntimeTerminalOutcome::Completed:
    case ava::core::RuntimeTerminalOutcome::MaxTokens:
    case ava::core::RuntimeTerminalOutcome::Refusal:
      return StopReason::Completed;
    case ava::core::RuntimeTerminalOutcome::MaxTurnRequests:
      return StopReason::MaxTurns;
    case ava::core::RuntimeTerminalOutcome::Cancelled:
      return StopReason::UserCanceled;
    case ava::core::RuntimeTerminalOutcome::Error:
      return StopReason::ProviderError;
  }
  return StopReason::ProviderError;
}

ava::core::Result<runtime::PromptState> select_runtime_prompt_state(runtime::Session const& session, ava::agent::Mode mode)
{
  return runtime::load_runtime_prompt_state(session.paths(), session.model, mode, session.workspace_dir(), session.current_dir(),
                                            project_resources_trusted(session.project_trust), session.prompt_overrides());
}

ava::core::Error offline_provider_error(std::string_view action)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "offline mode is enabled; provider model calls are disabled");
  if (!action.empty())
    error.with_context("action", std::string(action));
  error.with_context("hint", "rerun without --offline to send prompts to the provider");
  return error;
}

ava::core::VoidResult refresh_runtime_parent_configuration(runtime::Session const& session)
{
  return session.subagent_delivery_manager ? session.subagent_delivery_manager->refresh_parent_configuration(session) : ava::core::VoidResult{};
}

ava::core::VoidResult apply_runtime_prompt_state(runtime::Session& session, runtime::PromptState prompt_state)
{
  session.invocation_inputs().mode = prompt_state.mode;
  session.base_prompt = std::move(prompt_state.base_prompt);
  session.context_sources = std::move(prompt_state.context_sources);
  session.freshness_sources = std::move(prompt_state.freshness_sources);
  session.system_prompt = std::move(prompt_state.system_prompt);
  return refresh_runtime_parent_configuration(session);
}

ava::core::Result<ava::agent::AgentLoopResult> run_prompt(runtime::Session& session, std::string const& user_message, ava::provider::Provider const& provider,
                                                          ava::provider::Transport& transport, runtime::RunOptions const& options)
{
  if (session.is_offline() || options.offline)
    return std::unexpected(offline_provider_error("prompt"));
  if (!session.run_controller)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "runtime session controller is unavailable"));
  auto const request_id = options.request_id.value_or(ava::core::make_id("run"));
  auto const admission = session.run_controller->inspect_admission(RunRequest{.request_id = request_id});
  if (admission == AdmissionDisposition::JoinExistingOutcome)
  {
    auto joined = session.run_controller->wait_outcome(request_id);
    if (!joined)
      return std::unexpected(std::move(joined.error()));
    if (joined->reason == StopReason::PersistenceError && joined->error)
    {
      if (session.diagnostics)
        session.diagnostics->record_terminal_failure(ava::diagnostics::RuntimeFailureClass::Session, *joined->error);
      return std::unexpected(*joined->error);
    }
    return ava::agent::AgentLoopResult{.final_text = {},
                                       .usage = std::nullopt,
                                       .cost_usd = std::nullopt,
                                       .provider_iterations = 0,
                                       .tool_calls = 0,
                                       .initial_context_messages = 0,
                                       .used_compacted_context = false,
                                       .tool_iterations = 0,
                                       .outcome = runtime_outcome_for_stop_reason(joined->reason),
                                       .tool_timeline = {}};
  }
  if (admission == AdmissionDisposition::RejectDifferentRequest)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session already has an active run for a different request");
    error.with_context("request_id", request_id);
    return std::unexpected(std::move(error));
  }
  auto admitted = session.run_controller->admit(RunRequest{.request_id = request_id});
  if (!admitted)
    return std::unexpected(std::move(admitted.error()));
  return run_admitted_prompt(session, user_message, provider, transport, options, std::move(*admitted));
}

ava::core::Result<ava::agent::AgentLoopResult> run_admitted_prompt(runtime::Session& session, std::string const& user_message,
                                                                   ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                                                   runtime::RunOptions const& options, ActiveRunGuard guard)
{
  auto fail_run = [&guard, &session](ava::core::Error error) -> ava::core::Result<ava::agent::AgentLoopResult> {
    if (session.diagnostics)
      if (auto failure_class = diagnostic_failure_class(error))
        session.diagnostics->record_terminal_failure(*failure_class, error);
    auto completed = guard.complete(RunOutcome{.run_id = {}, .reason = outcome_reason_for_error(error), .error = error});
    if (completed && completed->reason == StopReason::PersistenceError && completed->error)
    {
      if (session.diagnostics)
        session.diagnostics->record_terminal_failure(ava::diagnostics::RuntimeFailureClass::Session, *completed->error);
      return std::unexpected(*completed->error);
    }
    return std::unexpected(std::move(error));
  };
  struct ParentRefresh final
  {
    std::shared_ptr<SubagentDeliveryManager> manager;
    std::string session_id;
    std::optional<SubagentDeliveryManager::CapsuleGeneration> generation = std::nullopt;
    ~ParentRefresh()
    {
      if (manager && generation)
        manager->release_parent_if_unused(session_id, *generation);
    }
  } refresh{options.synthetic_subagent_delivery ? nullptr : session.subagent_delivery_manager, session.store.session_id()};
  if (refresh.manager)
  {
    auto retained = refresh.manager->refresh_parent(session, options);
    if (!retained)
      return fail_run(std::move(retained.error()));
    refresh.generation = *retained;
  }
  if (session.is_offline() || options.offline)
    return fail_run(offline_provider_error("prompt"));
  if (!session.run_controller)
    return fail_run(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "runtime session controller is unavailable"));
  if (!guard.active())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "runtime prompt admission is inactive"));
  auto session_read_authority = session.read_authority();
  if (!session_read_authority)
    return fail_run(std::move(session_read_authority.error()));
  if (auto transitioned = guard.transition(RunPhase::BuildingContext); !transitioned)
    return fail_run(std::move(transitioned.error()));

  // Hook permission audits can overlap provider/tool work, so use this run's
  // immutable route rather than the legacy direct store callback. Isolated
  // runs bypass ambient plugin event hooks entirely.
  auto append_route = guard.append_route();
  auto append_batch_route = guard.append_batch_route();
  runtime::EventSink event_sink = options.event_sink;
  if (!options.isolate_project_resources)
  {
    auto plugin_observer_options = plugin_event_observer_options(session, options.permission_resolver, options.session_mutex);
    plugin_observer_options.permission_audit_sink = [append_route](ava::tools::PermissionAuditEvent const& event) {
      return ava::agent::append_permission_decision(append_route, event);
    };
    plugin_observer_options.cancel_requested = options.cancel_requested;
    event_sink = make_plugin_event_observer_sink(std::move(plugin_observer_options), options.event_sink);
  }
  auto runtime_options = options;
  if (session.diagnostics)
  {
    auto production_observation = session.diagnostics->observation();
    if (production_observation && production_observation->enabled())
      runtime_options.observation = std::move(production_observation);
  }
  runtime_options.active_append_route = append_route;
  runtime_options.active_append_batch_route = append_batch_route;
  auto const caller_cancel_requested = runtime_options.cancel_requested;
  runtime_options.cancel_requested = [guard_token = guard.stop_token(), caller_cancel_requested] {
    return guard_token.stop_requested() || (caller_cancel_requested && caller_cancel_requested());
  };
  runtime_options.event_sink = event_sink;
  auto take_steering_messages = runtime_options.take_steering_messages;
  runtime_options.take_steering_messages = [&session, take_steering_messages]() -> ava::core::Result<std::vector<std::string>> {
    if (!take_steering_messages)
      return std::vector<std::string>{};
    auto messages = take_steering_messages();
    if (!messages)
      return std::unexpected(std::move(messages.error()));
    auto const run_id = session.run_controller->snapshot().run_id;
    for (auto const& message : *messages)
    {
      // Frontends retain their own bounded visible queues. Controller overflow
      // rejects only the extra steering item at this adapter boundary; it must
      // not abort an otherwise healthy run or discard already admitted input.
      static_cast<void>(session.run_controller->wake(RunCommand{.kind = RunCommand::Kind::Steering, .correlation_id = run_id, .message = message}));
    }
    auto commands = session.run_controller->take_commands(run_id);
    if (!commands)
      return std::unexpected(std::move(commands.error()));
    std::vector<std::string> accepted;
    accepted.reserve(commands->size());
    for (auto& command : *commands)
      if (command.kind == RunCommand::Kind::Steering)
        accepted.push_back(std::move(command.message));
    return accepted;
  };
  if (runtime_options.observation && runtime_options.observation->enabled())
  {
    if (runtime_options.trace_context.run_id.empty())
      runtime_options.trace_context.run_id = runtime_options.observation->next_id("run");
    if (runtime_options.trace_context.turn_id.empty())
      runtime_options.trace_context.turn_id = runtime_options.observation->next_id("turn");
    if (runtime_options.trace_context.session_id.empty())
      runtime_options.trace_context.session_id = session.store.session_id();
    if (runtime_options.trace_context.provider_id.empty())
      runtime_options.trace_context.provider_id = session.model.provider_id;
  }
  std::optional<ava::provider::RetryTransport> retry_transport;
  ava::provider::Transport* runtime_transport = &transport;
  if (runtime_options.enable_transport_retries)
  {
    retry_transport.emplace(transport, runtime::runtime_retry_options(session, runtime_options));
    runtime_transport = &*retry_transport;
    runtime_options.enable_transport_retries = false;
  }
  ava::permissions::register_enforceable_permission_rule_files(permission_rule_store_for_session(session));

  auto expanded_user_message = runtime_options.expand_prompt_file_references ? expand_prompt_file_references(session, user_message, runtime_options)
                                                                             : ava::core::Result<std::string>(user_message);
  if (!expanded_user_message)
    return fail_run(std::move(expanded_user_message.error()));

  auto session_event = base_event_locked(session, runtime::EventType::SessionStart, options.session_mutex);
  if (auto emitted = emit_event(event_sink, session_event); !emitted)
  {
    return fail_run(std::move(emitted.error()));
  }

  auto user_event = base_event_locked(session, runtime::EventType::UserMessage, options.session_mutex);
  user_event.text = *expanded_user_message;
  if (auto emitted = emit_event(event_sink, user_event); !emitted)
  {
    return fail_run(std::move(emitted.error()));
  }

  std::shared_ptr<ava::lsp::DiagnosticsProvider> configured_lsp_provider;
  std::vector<ava::agent::SubagentDefinition> subagents;
  if (!runtime_options.isolate_project_resources)
  {
    auto lsp_provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
        .global_config_file = session.paths().ava_config_dir / "lsp.json",
        .project_config_file = project_resources_trusted(session.project_trust) ? session.workspace_dir() / ".ava" / "lsp.json" : std::filesystem::path{},
        .workspace_root = session.workspace_dir(),
        .anchor_set = session.anchor_set,
        .mode = session.mode(),
        .permission_resolver = runtime_options.permission_resolver,
    });
    configured_lsp_provider = lsp_provider ? *lsp_provider : nullptr;
    subagents = ava::agent::load_subagents(ava::agent::SubagentLoadOptions{.workspace_root = session.workspace_dir(),
                                                                           .include_project_agents = project_resources_trusted(session.project_trust)})
                    .subagents;
  }

  std::optional<ava::core::Error> sink_error;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = session.workspace_dir(),
      .current_dir = session.current_dir(),
      .additional_writable_dirs = session.additional_writable_dirs(),
      .anchor_set = session.anchor_set,
      .mode = session.mode(),
      .provider_id = session.model.provider_id,
      .model_id = session.model.model_id,
      .system_prompt = session.system_prompt,
      .access_token = options.access_token,
      .credential_type = options.openai_oauth && options.credential_type == "bearer" ? "oauth" : options.credential_type,
      .openai_oauth = options.openai_oauth,
      .openai_account_id = options.openai_account_id,
      .stream = runtime_options.stream,
      .model_supports_tools = session.model.supports_tools.value_or(true),
      .model_supports_streaming = session.model.supports_streaming.value_or(true),
      .include_project_resources = !runtime_options.isolate_project_resources && project_resources_trusted(session.project_trust),
      .plugin_global_plugins_dir = runtime_options.isolate_project_resources ? std::filesystem::path{} : session.paths().ava_config_dir / "plugins",
      .plugin_project_plugins_dir = !runtime_options.isolate_project_resources && project_resources_trusted(session.project_trust)
                                        ? session.workspace_dir() / ".ava" / "plugins"
                                        : std::filesystem::path{},
      .plugin_enablement_file = runtime_options.isolate_project_resources ? std::filesystem::path{} : session.paths().ava_state_dir / "plugin-enablement.json",
      .session_mcp_config = runtime_options.disable_session_mcp ? std::make_shared<ava::mcp::McpConfig const>() : session.mcp_config,
      .exact_builtin_tool_names = runtime_options.exact_builtin_tool_names,
      .require_descriptor_secure_workspace = runtime_options.require_descriptor_secure_workspace,
      .announce_execution_after_permission = runtime_options.announce_execution_after_permission,
      .redact_permission_audit_arguments = runtime_options.redact_permission_audit_arguments,
      .require_explicit_file_permissions = runtime_options.require_explicit_file_permissions,
      .ava_authority_roots = command_authority_roots_for_session(session),
      .exact_file_access = runtime_options.exact_file_access,
      .command_executor = runtime_options.command_executor,
      .subagents = std::move(subagents),
      .tool_visibility = session.tool_visibility(),
      .model_input_modalities = session.model.input_modalities,
      .model_max_output_tokens = session.model.max_output_tokens,
      .reasoning = session.reasoning ? std::optional(runtime::provider_reasoning_options(*session.reasoning)) : std::nullopt,
      .on_tool_event =
          [&session, &options, &event_sink, &sink_error](ava::agent::ToolTimelineEntry const& entry) {
            if (sink_error)
              return;
            auto event = base_event_locked(
                session, entry.status == ava::agent::ToolTimelineStatus::Running ? runtime::EventType::ToolStart : runtime::EventType::ToolResult,
                options.session_mutex);
            event.call_id = entry.call_id;
            event.tool_name = entry.name;
            event.text = entry.status == ava::agent::ToolTimelineStatus::Running ? entry.argument_summary : entry.result_summary;
            event.tool_arguments_json = entry.arguments_json;
            event.tool_result_json = entry.result_json;
            event.tool_structured_result_json = entry.structured_result_json;
            event.content_type = entry.content_type;
            event.error_category = entry.error_category;
            event.error_code = entry.error_code;
            event.error_message = entry.error_message;
            event.error_details = entry.error_details;
            event.diff = entry.diff;
            event.diff_truncated = entry.diff_truncated;
            event.changed_paths = entry.changed_paths;
            event.permission_request_ids = entry.permission_request_ids;
            event.truncated = entry.truncated;
            event.byte_limited = entry.byte_limited;
            event.line_limited = entry.line_limited;
            event.spill_path = entry.spill_path;
            event.spill_truncated = entry.spill_truncated;
            if (entry.output_bytes)
              event.output_bytes = *entry.output_bytes;
            if (entry.total_bytes)
              event.total_bytes = *entry.total_bytes;
            if (entry.output_lines)
              event.output_lines = *entry.output_lines;
            if (entry.total_lines)
              event.total_lines = *entry.total_lines;
            if (entry.start_line)
              event.start_line = *entry.start_line;
            if (entry.end_line)
              event.end_line = *entry.end_line;
            if (entry.next_offset_line)
              event.next_offset_line = *entry.next_offset_line;
            if (entry.omitted_bytes)
              event.omitted_bytes = *entry.omitted_bytes;
            if (entry.omitted_lines)
              event.omitted_lines = *entry.omitted_lines;
            if (entry.visible_matches)
              event.visible_matches = *entry.visible_matches;
            if (entry.total_matches)
              event.total_matches = *entry.total_matches;
            event.status = ava::agent::to_string(entry.status);
            if (auto emitted = emit_event(event_sink, event); !emitted)
            {
              sink_error = std::move(emitted.error());
            }
          },
      .on_tool_progress = [&session, &options, &event_sink, &sink_error](ava::agent::ToolProgressEntry const& entry) -> ava::core::VoidResult {
        if (sink_error)
          return std::unexpected(*sink_error);
        auto event = base_event_locked(session, runtime::EventType::ToolProgress, options.session_mutex);
        event.call_id = entry.call_id;
        event.tool_name = entry.name;
        event.text = entry.text;
        event.status = entry.status;
        if (auto emitted = emit_event(event_sink, event); !emitted)
        {
          sink_error = std::move(emitted.error());
          return std::unexpected(*sink_error);
        }
        return {};
      },
      .on_stream_event = [&session, &options, &event_sink, &sink_error](ava::provider::StreamEvent const& stream_event) -> ava::core::VoidResult {
        if (sink_error)
          return std::unexpected(*sink_error);
        auto event = base_event_locked(session,
                                       stream_event.type == ava::provider::StreamEventType::TextDelta        ? runtime::EventType::MessageUpdate
                                       : stream_event.type == ava::provider::StreamEventType::ReasoningStart ? runtime::EventType::ReasoningStart
                                       : stream_event.type == ava::provider::StreamEventType::ReasoningDelta ? runtime::EventType::ReasoningDelta
                                       : stream_event.type == ava::provider::StreamEventType::ReasoningEnd   ? runtime::EventType::ReasoningEnd
                                       : stream_event.type == ava::provider::StreamEventType::Done           ? runtime::EventType::MessageEnd
                                                                                                             : runtime::EventType::ProviderEvent,
                                       options.session_mutex);
        event.text = stream_event.text;
        event.call_id = stream_event.tool_call_id;
        event.tool_name = stream_event.tool_name;
        event.status = ava::provider::to_string(stream_event.type);
        event.error_message = stream_event.error_message;
        if (stream_event.finish_reason)
          event.stop_reason = std::string(ava::provider::to_string(*stream_event.finish_reason));
        event.reasoning_format = stream_event.reasoning_format;
        event.reasoning_redacted = stream_event.redacted;
        event.reasoning_signature_present = stream_event.reasoning_signature_present || !stream_event.reasoning_signature.empty();
        if (auto emitted = emit_event(event_sink, event); !emitted)
        {
          sink_error = std::move(emitted.error());
          return std::unexpected(*sink_error);
        }
        return {};
      },
      .permission_resolver = runtime_options.permission_resolver,
      .command_deny_preflight = ava::permissions::build_persistent_permission_deny_preflight(permission_rule_store_for_session(session)),
      .question_resolver = runtime_options.question_resolver,
      .cancel_requested = [&runtime_options,
                           &sink_error] { return sink_error.has_value() || (runtime_options.cancel_requested && runtime_options.cancel_requested()); },
      .take_steering_messages = runtime_options.take_steering_messages,
      .lsp_diagnostics_provider = configured_lsp_provider,
      .compact_context = runtime_options.access_token.empty() ? decltype(ava::agent::AgentLoopOptions{}.compact_context){}
                                                              : [&](ava::session::SessionReadAuthority read_authority, std::string_view trigger,
                                                                    std::vector<std::string> const& replayed_user_messages) -> ava::core::Result<bool> {
        return runtime::compact_runtime_context(session, std::move(read_authority), trigger, provider, *runtime_transport, runtime_options,
                                                replayed_user_messages);
      },
      .background_provider_factory = [provider_id = session.model.provider_id]() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        return ava::provider::builtin_provider_registry().create(provider_id);
      },
      .background_transport_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
        std::unique_ptr<ava::provider::Transport> transport = std::make_unique<ava::provider::CurlCliTransport>();
        return transport;
      },
      .subagent_coordinator = session.subagent_coordinator,
      .session_mutex = runtime_options.session_mutex,
      .append_entry = append_route,
      .append_batch = std::move(append_batch_route),
      .session_read_authority = std::move(*session_read_authority),
      .session_read_limits = session.session_read_limits(),
      .synthetic_user_message_provenance = runtime_options.synthetic_subagent_delivery ? runtime_options.synthetic_user_message_provenance : std::nullopt,
      .on_phase = [&guard, &runtime_options](ava::agent::RunPhase phase) -> ava::core::VoidResult {
        if (phase == ava::agent::RunPhase::Completing && runtime_options.on_terminal_commit)
        {
          if (auto committed = runtime_options.on_terminal_commit(); !committed)
            return committed;
        }
        if (auto transitioned = guard.transition(phase); !transitioned)
          return transitioned;
        if (runtime_options.on_phase)
          return runtime_options.on_phase(phase);
        return {};
      },
      .model_pricing = session.model.pricing,
      .observation = runtime_options.observation,
      .trace_context = runtime_options.trace_context,
      .api_family = session.model.api_family,
      .reasoning_format = session.model.reasoning_format});

  auto result = loop.run_turn(*expanded_user_message, runtime_options.image_attachments, session.store, provider, *runtime_transport);
  if (sink_error)
  {
    return fail_run(std::move(*sink_error));
  }
  if (!result)
  {
    auto event = base_event_locked(session, is_agent_loop_canceled_error(result.error()) ? runtime::EventType::Canceled : runtime::EventType::Error,
                                   options.session_mutex);
    event.error_category = ava::core::to_string(result.error().category());
    event.error_message = result.error().message();
    event.error_details = result.error().format();
    if (event.type == runtime::EventType::Canceled)
    {
      event.text = "stopped by user";
      event.reason = result.error().message();
    }
    static_cast<void>(emit_event(event_sink, event));
    return fail_run(result.error());
  }

  auto assistant_event = base_event_locked(session, runtime::EventType::AssistantMessage, options.session_mutex);
  assistant_event.text = result->final_text;
  if (auto emitted = emit_event(event_sink, assistant_event); !emitted)
  {
    return fail_run(std::move(emitted.error()));
  }

  auto done_event = base_event_locked(session, runtime::EventType::Done, options.session_mutex);
  done_event.stop_reason = std::string(ava::core::to_string(result->outcome));
  done_event.provider_iterations = result->provider_iterations;
  done_event.tool_calls = result->tool_calls;
  if (auto emitted = emit_event(event_sink, done_event); !emitted)
  {
    return fail_run(std::move(emitted.error()));
  }
  if (auto transitioned = guard.transition(RunPhase::Completing); !transitioned)
    return fail_run(std::move(transitioned.error()));
  auto const proposed_reason = stop_reason_for_runtime_outcome(result->outcome);
  auto completed = guard.complete(RunOutcome{.run_id = {}, .reason = proposed_reason});
  if (!completed)
    return std::unexpected(std::move(completed.error()));
  if (completed->reason == StopReason::PersistenceError && completed->error)
  {
    if (session.diagnostics)
      session.diagnostics->record_terminal_failure(ava::diagnostics::RuntimeFailureClass::Session, *completed->error);
    return std::unexpected(*completed->error);
  }
  if (completed->reason != proposed_reason)
    result->outcome = runtime_outcome_for_stop_reason(completed->reason);

  // This boundary is deliberately after AdmissionGuard completion, not the
  // earlier Done event. The coordinator is best-effort and cannot change the
  // already committed ordinary user turn.
  if (result->committed_turn_id && !options.synthetic_subagent_delivery && !session.sessionless() && session.session_title_coordinator)
  {
    session.session_title_coordinator->schedule(session, user_message, *result->committed_turn_id, options);
  }

  return result;
}

}  // namespace ava::app
