#include "sys.h"
#include "ExtensionResourcePolicy.h"
#include "Session.h"
#include "command_names.h"
#include "markdown_files.h"
#include "ava/diagnostics/runtime_diagnostics.h"
#include "ava/app/command_catalog.h"
#include "ava/app/command_format.h"
#include "ava/app/command_tools.h"
#include "ava/app/project_trust.h"
#include "ava/app/reasoning_controls.h"
#include "ava/app/rpc/serialization_detail.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime_json.h"
#include "ava/app/runtime_model.h"
#include "ava/app/runtime_prompt.h"
#include "ava/app/runtime_reasoning.h"
#include "ava/app/session_title_coordinator.h"
#include "ava/app/subagent_delivery_manager.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/static_resources.h"
#include "ava/config/session_title_config.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_metadata.h"
#include "ava/context/markdown_resource.h"
#include "ava/context/skill_loader.h"
#include "ava/core/AnchorSet.h"
#include "ava/core/ids.h"
#include "ava/core/string_utils.h"
#include "ava/core/trusted_home.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "debug.h"

namespace ava::app::runtime {
namespace {

constexpr std::size_t kMaxCommandAuthorityRoots = 8;

struct RegistryBuilder
{
  CommandRegistry registry;
  std::unordered_map<std::string, std::size_t> occupied;
};

bool valid_command_token(std::string_view command)
{
  if (!command.starts_with('/') || command.size() < 2 || command.size() > kMaxCommandTokenBytes)
    return false;
  for (char const ch : command)
  {
    auto const byte = static_cast<unsigned char>(ch);
    bool const allowed = std::isalnum(byte) != 0 || ch == '/' || ch == '.' || ch == '_' || ch == '-' || ch == ':';
    if (!allowed || byte < 0x20 || byte == 0x7F)
      return false;
  }
  return true;
}

void add_diagnostic(RegistryBuilder& builder, CommandRegistryDiagnostic diagnostic)
{
  builder.registry.diagnostics.push_back(std::move(diagnostic));
}

// Comma-separated list of reasoning levels acceptable on the CLI for `model`,
// always including "off" first; non-empty, non-"off" supported levels follow.
std::string cli_supported_reasoning_levels(ava::config::ModelInfo const& model)
{
  std::string levels = "off";
  for (auto const& level : ava::config::supported_reasoning_levels(model))
  {
    if (level.empty() || level == "off")
      continue;
    levels += ", ";
    levels += level;
  }
  return levels;
}

// Attach the --thinking option name and the model's supported levels to `error`
// so CLI failures point the user at the levels they may pass.
void add_cli_reasoning_context(ava::core::Error& error, ava::config::ModelInfo const& model)
{
  error.with_context("option", "--thinking");
  error.with_context("supported_levels", cli_supported_reasoning_levels(model));
}

// Compare two optional reasoning selections for field-wise equality so the
// setter can short-circuit a no-op change without writing an entry.
bool same_reasoning_selection(std::optional<ReasoningSelection> const& left, std::optional<ReasoningSelection> const& right)
{
  if (!left || !right)
    return !left && !right;
  return left->level == right->level && left->provider_level == right->provider_level && left->budget_tokens == right->budget_tokens &&
         left->display == right->display;
}

bool add_entry(RegistryBuilder& builder, CommandRegistryEntry entry)
{
  std::vector<std::string> tokens;
  tokens.push_back(entry.command);
  for (auto const& alias : entry.aliases) tokens.push_back(alias);
  for (auto const& token : tokens)
  {
    if (!valid_command_token(token))
    {
      add_diagnostic(builder, CommandRegistryDiagnostic{.command = token,
                                                        .source = to_string(entry.source),
                                                        .source_id = entry.source_id,
                                                        .path = entry.source_path,
                                                        .message = "command token is invalid"});
      return false;
    }
    auto const existing = builder.occupied.find(token);
    if (existing == builder.occupied.end())
      continue;
    auto const& winner = builder.registry.entries[existing->second];
    add_diagnostic(builder, CommandRegistryDiagnostic{.command = token,
                                                      .source = to_string(entry.source),
                                                      .source_id = entry.source_id,
                                                      .path = entry.source_path,
                                                      .message = "command collision; existing command kept",
                                                      .winner_source = to_string(winner.source),
                                                      .winner_source_id = winner.source_id,
                                                      .winner_path = winner.source_path});
    return false;
  }

  auto const index = builder.registry.entries.size();
  builder.registry.entries.push_back(std::move(entry));
  for (auto& token : tokens) builder.occupied.emplace(std::move(token), index);
  return true;
}

void load_builtin_commands(RegistryBuilder& builder)
{
  for (auto const& entry : command_catalog())
  {
    add_entry(builder, CommandRegistryEntry{.command = entry.command,
                                            .aliases = entry.aliases,
                                            .description = entry.description,
                                            .hint = entry.hint,
                                            .category = entry.category,
                                            .enabled = entry.enabled,
                                            .disabled_reason = entry.disabled_reason,
                                            .source = UnifiedCommandSource::Builtin,
                                            .kind = UnifiedCommandKind::Backend,
                                            .source_id = "builtin"});
  }
}

void load_prompt_command_dir(RegistryBuilder& builder, std::filesystem::path const& root, UnifiedCommandSource source, std::string source_scope)
{
  auto files = markdown_files(root, builder.registry.diagnostics, source);
  for (auto const& file : files)
  {
    auto name = command_name_for_file(root, file);
    if (!name)
    {
      add_diagnostic(builder,
                     CommandRegistryDiagnostic{.source = to_string(source), .path = file, .message = "command file name does not form a safe slash command"});
      continue;
    }
    auto content = ava::context::read_resource_file(file, {.max_bytes = kMaxCommandFileBytes, .resource_description = "command file"});
    if (!content)
    {
      add_diagnostic(builder,
                     CommandRegistryDiagnostic{.command = "/" + *name, .source = to_string(source), .path = file, .message = content.error().format()});
      continue;
    }
    auto parsed = ava::context::parse_markdown(*content);
    auto description = ava::context::markdown_field(parsed, "description");
    auto hint = ava::context::markdown_field(parsed, "argument-hint");
    if (hint.empty())
      hint = ava::context::markdown_field(parsed, "argument_hint");
    if (hint.empty())
      hint = ava::context::markdown_field(parsed, "hint");
    auto body = ava::context::markdown_field(parsed, "template");
    if (body.empty())
      body = std::move(parsed.body);
    if (core::trim_view(body).empty())
    {
      add_diagnostic(builder,
                     CommandRegistryDiagnostic{.command = "/" + *name, .source = to_string(source), .path = file, .message = "command template is empty"});
      continue;
    }
    add_entry(builder, CommandRegistryEntry{.command = "/" + *name,
                                            .description = description.empty() ? "Prompt command " + *name : description,
                                            .hint = std::move(hint),
                                            .category = "Prompts",
                                            .source = source,
                                            .kind = UnifiedCommandKind::PromptTemplate,
                                            .source_id = std::move(*name),
                                            .source_path = file,
                                            .source_scope = source_scope,
                                            .template_text = std::move(body)});
  }
}

// Called from Session::load_command_registry that passes *this: session is already locked.
void load_prompt_commands(RegistryBuilder& builder, Session const& session, ExtensionResourcePolicy const& policy)
{
  if (policy.include_project_resources)
  {
    load_prompt_command_dir(builder, session.workspace_dir() / ".ava" / "commands", UnifiedCommandSource::PromptProject, "project");
    load_prompt_command_dir(builder, session.workspace_dir() / ".ava" / "command", UnifiedCommandSource::PromptProject, "project");
  }
  load_prompt_command_dir(builder, session.paths().ava_config_dir / "commands", UnifiedCommandSource::PromptGlobal, "global");
  load_prompt_command_dir(builder, session.paths().ava_config_dir / "command", UnifiedCommandSource::PromptGlobal, "global");
}

std::string namespaced_command(std::string_view prefix, std::string_view id, std::string_view name = {})
{
  std::string command = "/";
  command += prefix;
  command += ':';
  command += id;
  if (!name.empty())
  {
    command += ':';
    command += name;
  }
  return command;
}

// Called from Session::load_command_registry that passes *this: session is already locked.
void load_plugin_commands(RegistryBuilder& builder, Session const& session, ExtensionResourcePolicy const& policy)
{
  auto diagnostics = ava::plugin::collect_plugin_diagnostics(policy.plugin_discovery, policy.plugin_enablement_file, session.workspace_dir());
  for (auto const& failure : diagnostics.failures)
  {
    add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::PluginCommand),
                                                      .path = failure.path,
                                                      .message = failure.details.empty() ? failure.message : failure.details});
  }

  for (auto const& status : diagnostics.plugins)
  {
    auto const& manifest = status.plugin.manifest;
    for (auto const& command : manifest.contributes.commands)
    {
      if (!valid_command_segment(command.name) || !valid_command_segment(manifest.id))
      {
        add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::PluginCommand),
                                                          .source_id = manifest.id,
                                                          .path = manifest.path,
                                                          .message = "plugin command id does not form a safe slash command"});
        continue;
      }
      CommandRegistryEntry entry{.command = namespaced_command("plugin", manifest.id, command.name),
                                 .description = command.description.empty() ? "Plugin command " + command.name : command.description,
                                 .category = "Plugins",
                                 .enabled = status.enabled,
                                 .disabled_reason = status.enabled ? "" : "plugin is disabled",
                                 .source = UnifiedCommandSource::PluginCommand,
                                 .kind = UnifiedCommandKind::PluginCommand,
                                 .source_id = manifest.id,
                                 .source_path = manifest.path,
                                 .source_scope = std::string(ava::plugin::to_string(status.plugin.scope)),
                                 .plugin_id = manifest.id,
                                 .plugin_command_name = command.name};
      add_entry(builder, entry);
      entry.command = "/" + command.name;
      add_entry(builder, std::move(entry));
    }
  }
}

std::string mcp_command_text(ava::mcp::McpServerConfig const& server)
{
  std::string text = server.command;
  for (auto const& arg : server.args) text += " " + arg;
  return text;
}

ava::core::VoidResult ensure_mcp_prompt_server_permission(ava::tools::ToolContext const& context, ava::mcp::McpServerConfig const& server)
{
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerLaunch, server.source_path, mcp_command_text(server),
                                                      "mcp_prompts", "MCP server launch requires permission");
      !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerConnect, server.source_path, server.id, "mcp_prompts",
                                                      "MCP server connection requires permission");
      !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }
  return {};
}

// Called from Session::load_command_registry that passes *this: session is already locked.
void load_mcp_prompt_commands(RegistryBuilder& builder, Session& session, CommandRegistryOptions const& options, ExtensionResourcePolicy const& policy)
{
  auto config = ava::mcp::load_mcp_config(policy.mcp_config);
  if (!config)
  {
    add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt), .message = config.error().format()});
    return;
  }

  auto context = make_tool_context(session, options.permission_resolver);
  context.permission_tool_name = "mcp_prompts";
  context.current_tool_name = "mcp_prompts";
  context.cancel_requested = options.cancel_requested;

  for (auto const& server : config->servers)
  {
    if (!server.enabled)
      continue;
    if (!valid_command_segment(server.id))
    {
      add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt),
                                                        .source_id = server.id,
                                                        .path = server.source_path,
                                                        .message = "MCP server id does not form a safe slash command"});
      continue;
    }
    if (auto permission = ensure_mcp_prompt_server_permission(context, server); !permission)
    {
      add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt),
                                                        .source_id = server.id,
                                                        .path = server.source_path,
                                                        .message = permission.error().format()});
      continue;
    }
    auto client = ava::mcp::McpStdioClient::start(server, ava::mcp::McpStdioClientOptions{.workspace_dir = session.workspace_dir()}, options.cancel_requested);
    if (!client)
    {
      add_diagnostic(
          builder,
          CommandRegistryDiagnostic{
              .source = to_string(UnifiedCommandSource::McpPrompt), .source_id = server.id, .path = server.source_path, .message = client.error().format()});
      continue;
    }
    auto prompts = (*client)->list_prompts(options.cancel_requested);
    auto shutdown = (*client)->shutdown();
    if (!prompts)
    {
      add_diagnostic(
          builder,
          CommandRegistryDiagnostic{
              .source = to_string(UnifiedCommandSource::McpPrompt), .source_id = server.id, .path = server.source_path, .message = prompts.error().format()});
      continue;
    }
    if (!shutdown)
    {
      add_diagnostic(
          builder,
          CommandRegistryDiagnostic{
              .source = to_string(UnifiedCommandSource::McpPrompt), .source_id = server.id, .path = server.source_path, .message = shutdown.error().format()});
      continue;
    }
    for (auto const& prompt : *prompts)
    {
      if (!valid_command_segment(prompt.name))
      {
        add_diagnostic(builder, CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::McpPrompt),
                                                          .source_id = server.id,
                                                          .path = server.source_path,
                                                          .message = "MCP prompt name does not form a safe slash command"});
        continue;
      }
      CommandRegistryEntry entry{.command = namespaced_command("mcp", server.id, prompt.name),
                                 .description = prompt.description.empty() ? "MCP prompt " + prompt.name : prompt.description,
                                 .category = "MCP",
                                 .source = UnifiedCommandSource::McpPrompt,
                                 .kind = UnifiedCommandKind::McpPrompt,
                                 .source_id = server.id,
                                 .source_path = server.source_path,
                                 .source_scope = std::string(ava::mcp::to_string(server.scope)),
                                 .mcp_server_id = server.id,
                                 .mcp_prompt_name = prompt.name,
                                 .mcp_arguments = prompt.arguments};
      std::vector<std::string> required;
      for (auto const& argument : prompt.arguments)
      {
        if (argument.required)
          required.push_back(argument.name);
      }
      if (!prompt.arguments.empty())
        entry.hint = "<" + joined_strings(required.empty() ? std::vector<std::string>{"args"} : required, "> <") + ">";
      add_entry(builder, entry);
      entry.command = "/" + prompt.name;
      add_entry(builder, std::move(entry));
    }
  }
}

std::vector<ava::context::DeclaredSkillFileOptions> declared_plugin_skill_files(ava::plugin::PluginDiagnostics const& diagnostics)
{
  std::vector<ava::context::DeclaredSkillFileOptions> files;
  for (auto const& skill : ava::plugin::enabled_plugin_static_skill_files(diagnostics))
  {
    files.push_back(ava::context::DeclaredSkillFileOptions{.path = skill.path,
                                                           .name = skill.name,
                                                           .description = skill.description,
                                                           .source_type = ava::context::SkillSourceType::Plugin,
                                                           .preloaded_content = skill.content});
  }
  return files;
}

// Called from Session::load_command_registry that passes *this: session is already locked.
void load_skill_commands(RegistryBuilder& builder, Session const& session, ExtensionResourcePolicy const& policy)
{
  auto plugin_diagnostics = ava::plugin::collect_plugin_diagnostics(policy.plugin_discovery, policy.plugin_enablement_file, session.workspace_dir());
  auto loaded = ava::context::load_skills(ava::context::SkillLoadOptions{
      .workspace_root = session.workspace_dir(),
      .global_skill_dirs = policy.global_skill_dirs,
      .project_skill_dirs = policy.project_skill_dirs,
      .declared_skill_files = declared_plugin_skill_files(plugin_diagnostics),
      .include_project_skills = policy.include_project_resources,
  });
  for (auto const& diagnostic : loaded.diagnostics)
  {
    add_diagnostic(builder,
                   CommandRegistryDiagnostic{.source = to_string(UnifiedCommandSource::Skill), .path = diagnostic.path, .message = diagnostic.message});
  }
  for (auto const& skill : loaded.skills)
  {
    CommandRegistryEntry entry{.command = namespaced_command("skill", skill.name),
                               .description = skill.description,
                               .category = "Skills",
                               .source = UnifiedCommandSource::Skill,
                               .kind = UnifiedCommandKind::SkillPrompt,
                               .source_id = skill.name,
                               .source_path = skill.path,
                               .source_scope = ava::context::to_string(skill.source_type),
                               .template_text = skill.content,
                               .skill_name = skill.name};
    add_entry(builder, entry);
    entry.command = "/" + skill.name;
    add_entry(builder, std::move(entry));
  }
}

ava::core::Result<std::string> resolve_session_id(std::filesystem::path const& workspace_dir, std::filesystem::path const& root_dir,
                                                  std::string_view requested_id)
{
  auto sessions = ava::session::SessionStore::list_sessions(workspace_dir, root_dir);
  if (!sessions)
    return std::unexpected(sessions.error());

  std::vector<std::string> matches;
  for (auto const& session : *sessions)
  {
    if (session.session_id == requested_id || session.session_id.starts_with(requested_id))
    {
      matches.push_back(session.session_id);
    }
  }
  if (matches.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "session not found");
    error.with_context("session_id", std::string(requested_id));
    return std::unexpected(std::move(error));
  }
  if (matches.size() > 1)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session id prefix is ambiguous");
    error.with_context("session_id", std::string(requested_id));
    error.with_context("matches", std::to_string(matches.size()));
    return std::unexpected(std::move(error));
  }
  return matches.front();
}

ava::core::Result<std::pair<std::filesystem::path, std::filesystem::path>> resolve_runtime_directories(OpenContext const& context)
{
  auto cwd = ava::core::launch_workspace_root();
  if (!cwd)
    return std::unexpected(std::move(cwd.error()));
  auto workspace_dir = context.workspace_dir.empty() ? *cwd : context.workspace_dir;
  auto current_dir = context.current_dir.empty() ? workspace_dir : context.current_dir;
  return std::pair<std::filesystem::path, std::filesystem::path>{std::move(workspace_dir), std::move(current_dir)};
}

ava::core::Result<ava::session::SessionReadAuthority> bind_runtime_read_authority(ava::session::SessionStore const& store,
                                                                                  ava::session::SessionLease const& lease,
                                                                                  ava::session::SessionReadLimits read_limits)
{
  return store.is_ephemeral() ? ava::session::SessionReadAuthority::create_ephemeral(store, read_limits)
                              : ava::session::SessionReadAuthority::create_persistent(store, lease, read_limits);
}

ava::core::VoidResult reconcile_committed_function_calls(std::shared_ptr<ava::session::SessionAppendTarget> const& append_target,
                                                         ava::session::SessionReadLimits limits)
{
  auto read_authority = append_target->read_authority();
  if (!read_authority)
    return std::unexpected(std::move(read_authority.error()));
  return ava::agent::reconcile_unresolved_committed_function_calls(
      *read_authority, [append_target](ava::session::SessionEntry entry) { return append_target->append(entry); }, limits);
}

ava::core::Result<std::shared_ptr<SubagentDeliveryManager>> delivery_manager_for_context(OpenContext const& context)
{
  if (context.subagent_delivery_manager)
    return context.subagent_delivery_manager;
  auto coordinator = context.subagent_coordinator ? ava::core::Result<std::shared_ptr<ava::agent::SubagentCoordinator>>(context.subagent_coordinator)
                                                  : ava::agent::SubagentCoordinator::create();
  if (!coordinator)
    return std::unexpected(std::move(coordinator.error()));
  return SubagentDeliveryManager::create({.coordinator = std::move(*coordinator)});
}

ava::core::Result<std::shared_ptr<SessionTitleCoordinator>> title_coordinator_for_context(OpenContext const& context,
                                                                                          std::shared_ptr<ava::core::AnchorSet> const& anchor_set)
{
  if (context.session_title_coordinator)
    return context.session_title_coordinator;
  auto config = ava::config::load_session_title_config(context.paths, *anchor_set);
  if (!config)
    return std::unexpected(std::move(config.error()));
  return SessionTitleCoordinator::create({.config = std::move(*config)});
}

bool path_contains(std::filesystem::path const& root, std::filesystem::path const& candidate)
{
  auto const relative = candidate.lexically_relative(root);
  auto const text = relative.generic_string();
  return !relative.empty() && relative != ".." && !text.starts_with("../");
}

void append_command_authority_root(std::vector<std::filesystem::path>& roots, std::filesystem::path root)
{
  if (root.empty())
    return;
  root = root.lexically_normal();
  if (std::ranges::any_of(roots, [&root](std::filesystem::path const& existing) { return path_contains(existing, root); }))
    return;
  std::erase_if(roots, [&root](std::filesystem::path const& existing) { return path_contains(root, existing); });
  if (roots.size() < kMaxCommandAuthorityRoots)
    roots.push_back(std::move(root));
}

} // namespace

// static
ava::core::Result<session_ts> Session::construct(OpenContext const& context, runtime::SessionLifecycleRequest const& request, ava::session::SessionStore& store,
                                                 ava::session::SessionLease& lease, bool created, bool load_existing_entries, bool should_append_session_start,
                                                 bool append_initial_session_name, std::shared_ptr<SubagentDeliveryManager> delivery_manager,
                                                 std::shared_ptr<SessionTitleCoordinator> title_coordinator)
{
  auto directories = resolve_runtime_directories(context);
  if (!directories)
    return std::unexpected(std::move(directories.error()));
  auto const& workspace_dir = directories->first;
  auto const& current_dir = directories->second;
  auto const session_read_limits = context.session_read_limits.value_or(ava::session::legacy_unbounded_session_read_limits());

  if (context.pin_model_override && !context.default_model_override)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "pinned runtime model override is missing"));

  ava::config::ModelRegistry registry;
  if (context.pin_model_override)
  {
    registry.default_provider_id = context.default_model_override->provider_id;
    registry.default_model_id = context.default_model_override->model_id;
    registry.models = {*context.default_model_override};
    registry.scoped_model_cycle = std::nullopt;
  }
  else
  {
    auto loaded_registry = ava::config::load_model_registry(context.paths);
    if (!loaded_registry)
      return std::unexpected(loaded_registry.error());
    registry = std::move(*loaded_registry);
  }
  auto model = context.default_model_override.value_or(ava::config::select_default_model(registry));

  std::optional<std::vector<ava::session::SessionEntry>> loaded_entries;
  if (load_existing_entries)
  {
    auto read_authority = bind_runtime_read_authority(store, lease, session_read_limits);
    if (!read_authority)
      return std::unexpected(std::move(read_authority.error()));
    auto entries = read_authority->load();
    if (!entries)
      return std::unexpected(std::move(entries.error()));
    if (!context.pin_model_override)
    {
      if (auto persisted_model = latest_persisted_model(registry, *entries))
        model = std::move(*persisted_model);
    }
    loaded_entries = std::move(*entries);
    if (request.expected_original_cwd)
    {
      auto summary = read_authority->inspect_bounded(session_read_limits);
      if (!summary)
        return std::unexpected(std::move(summary.error()));
      auto const persisted_cwd = summary->original_cwd.empty() ? workspace_dir : summary->original_cwd;
      if (persisted_cwd != *request.expected_original_cwd)
      {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "requested cwd does not match persisted session cwd");
        error.with_context("persisted_cwd", persisted_cwd.string()).with_context("requested_cwd", request.expected_original_cwd->string());
        return std::unexpected(std::move(error));
      }
    }
  }

  std::optional<ReasoningSelection> reasoning;
  if (loaded_entries)
    reasoning = latest_persisted_reasoning(*loaded_entries, model);

  auto project_trust = load_project_trust_state(context.paths, workspace_dir);
  auto prompt_state = load_runtime_prompt_state(context.paths, model, context.mode, workspace_dir, current_dir, project_resources_trusted(project_trust),
                                                context.prompt_overrides);
  if (!prompt_state)
    return std::unexpected(prompt_state.error());

  if (should_append_session_start)
  {
    if (!store.is_ephemeral() && lease.canonical_path().empty())
    {
      auto acquired = ava::session::SessionLease::create_and_acquire(store.session_path());
      if (!acquired)
        return std::unexpected(std::move(acquired.error()));
      lease = std::move(*acquired);
    }
    auto appended =
        store.is_ephemeral()
            ? append_session_start_ephemeral(store, context.mode, model, prompt_state->base_prompt, prompt_state->context_sources.size(), current_dir)
            : append_session_start(store, lease, context.mode, model, prompt_state->base_prompt, prompt_state->context_sources.size(), current_dir);
    if (!appended)
      return std::unexpected(std::move(appended.error()));
  }

  bool const sessionless = store.is_ephemeral();
  auto append_target = sessionless ? ava::session::SessionAppendTarget::create_ephemeral(store, session_read_limits)
                                   : ava::session::SessionAppendTarget::create_persistent(store, lease, session_read_limits);
  if (!append_target)
    return std::unexpected(std::move(append_target.error()));
  // Opening/resuming is the durable recovery boundary: the append target is
  // already authority-checked, so any committed function call left without an
  // exact result is closed without re-executing its tool.
  if (auto reconciled = reconcile_committed_function_calls(*append_target, session_read_limits); !reconciled)
    return std::unexpected(std::move(reconciled.error()));

  if (append_initial_session_name && request.initial_session_name)
  {
    auto read_authority = (*append_target)->read_authority();
    if (!read_authority)
      return std::unexpected(std::move(read_authority.error()));
    auto entries = read_authority->load();
    if (!entries)
      return std::unexpected(std::move(entries.error()));
    auto entry = ava::session::make_session_metadata_entry(ava::session::SessionMetadataUpdate{.name = request.initial_session_name, .actor = "cli"},
                                                           entries->empty() ? std::string{} : entries->back().id);
    if (!entry)
      return std::unexpected(std::move(entry.error()));
    if (auto appended = (*append_target)->append(*entry); !appended)
      return std::unexpected(std::move(appended.error()));
  }

  // Open the AnchorSet once for the session lifetime. The logical workspace,
  // AVA-owned roots, spill directory, and any additional writable directories
  // are opened as descriptors shared by parent delivery and every child loop.
  std::shared_ptr<ava::core::AnchorSet> anchor_set;
  {
    std::vector<std::filesystem::path> anchor_roots;
    anchor_roots.push_back(workspace_dir);
    auto const spill_dir = store.session_path().parent_path() / "spill";
    // Create the spill directory now so it can be opened as an anchor.
    // spill_files.cpp also creates it on first use, but we need it to exist
    // before AnchorSet::open so the anchor descriptor is pre-opened.
    std::error_code spill_error;
    std::filesystem::create_directories(spill_dir, spill_error);
    if (spill_error)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create the session spill anchor"));
    std::filesystem::permissions(spill_dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, spill_error);
    if (spill_error)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to secure the session spill anchor"));
    anchor_roots.push_back(spill_dir);

    std::error_code config_error;
    std::filesystem::create_directories(context.paths.ava_config_dir, config_error);
    if (config_error)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create the AVA config anchor"));
    std::filesystem::permissions(context.paths.ava_config_dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, config_error);
    if (config_error)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to secure the AVA config anchor"));
    anchor_roots.push_back(context.paths.ava_config_dir);

    std::error_code state_error;
    std::filesystem::create_directories(context.paths.ava_state_dir, state_error);
    if (state_error)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create the AVA state anchor"));
    std::filesystem::permissions(context.paths.ava_state_dir, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace, state_error);
    if (state_error)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to secure the AVA state anchor"));
    anchor_roots.push_back(context.paths.ava_state_dir);

    for (auto const& dir : context.additional_writable_dirs) anchor_roots.push_back(dir);
    if (context.anchor_set)
    {
      anchor_set = context.anchor_set;
    }
    else
    {
      auto opened = ava::core::AnchorSet::open(anchor_roots);
      if (!opened)
        return std::unexpected(std::move(opened.error()));
      anchor_set = std::move(*opened);
    }
    auto const config_anchor = anchor_set->find_anchor(context.paths.ava_config_dir);
    auto const state_anchor = anchor_set->find_anchor(context.paths.ava_state_dir);
    if (!anchor_set->find_anchor(workspace_dir) || !anchor_set->find_anchor(spill_dir) || !config_anchor || !config_anchor->relative().empty() ||
        !state_anchor || !state_anchor->relative().empty() ||
        std::ranges::any_of(context.additional_writable_dirs, [&](auto const& directory) { return !anchor_set->find_anchor(directory); }))
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to retain all required runtime anchors"));
  }

  // Resolve the trusted local account once per process and freeze it. The home
  // directory is read from HOME (passwd fallback) here, at startup, so that
  // later command planning reuses the cached value (ava::core::cached_trusted_account)
  // instead of re-reading the sensitive HOME environment variable after the AI
  // or user shell commands may have run. open is invoked for
  // every session (interactive, print, rpc, ACP, and forked/subagent sessions),
  // so the cache check keeps the actual HOME read to the very first call and
  // lets later sessions reuse the frozen result without tripping the freeze
  // assertion.
  if (auto result = ava::core::load_account_once_and_freeze(); !result)
    return std::unexpected(std::move(result.error()));

  if (context.diagnostics)
  {
    if (auto bound = context.diagnostics->bind_anchor_set(anchor_set); !bound)
      return std::unexpected(std::move(bound.error()));
  }

  if (!delivery_manager)
  {
    auto created_manager = delivery_manager_for_context(context);
    if (!created_manager)
      return std::unexpected(std::move(created_manager.error()));
    delivery_manager = std::move(*created_manager);
  }
  if (!title_coordinator)
  {
    auto created_coordinator = title_coordinator_for_context(context, anchor_set);
    if (!created_coordinator)
      return std::unexpected(std::move(created_coordinator.error()));
    title_coordinator = std::move(*created_coordinator);
  }

  InvocationInputs invocation_inputs{.workspace_dir = workspace_dir,
                                     .current_dir = current_dir,
                                     .tool_visibility = context.tool_visibility,
                                     .paths = context.paths,
                                     .sessionless = sessionless,
                                     .is_offline_ = context.offline,
                                     .additional_writable_dirs = context.additional_writable_dirs,
                                     .session_read_limits = session_read_limits,
                                     .prompt_overrides = context.prompt_overrides};
  ResolvedPromptState resolved_prompt_state{.mode = context.mode,
                                            .base_prompt = std::move(prompt_state->base_prompt),
                                            .context_sources = std::move(prompt_state->context_sources),
                                            .freshness_sources = std::move(prompt_state->freshness_sources),
                                            .system_prompt = std::move(prompt_state->system_prompt),
                                            .ambient_extension_free_system_prompt = std::move(prompt_state->ambient_extension_free_system_prompt)};
  ModelSelection model_selection{.model = std::move(model), .reasoning = std::move(reasoning), .scoped_model_cycle = registry.scoped_model_cycle};
  TrustState trust_state{.project_trust = std::move(project_trust)};
  SessionResources resources{.lease = std::move(lease),
                             .anchor_set = std::move(anchor_set),
                             .run_controller = std::make_shared<SessionRunController>(*append_target),
                             .append_target = std::move(*append_target),
                             .subagent_coordinator = delivery_manager->coordinator(),
                             .subagent_delivery_manager = std::move(delivery_manager),
                             .session_title_coordinator = std::move(title_coordinator),
                             .diagnostics = context.diagnostics};
  Session session({.invocation_inputs_ = std::move(invocation_inputs),
                   .resolved_prompt_state_ = std::move(resolved_prompt_state),
                   .model_selection_ = std::move(model_selection),
                   .trust_state_ = std::move(trust_state),
                   .resources_ = std::move(resources),
                   .store = std::move(store),
                   .created = created});

  if (request.initial_reasoning_level)
  {
    if (auto applied = session.apply_initial_reasoning_level(*request.initial_reasoning_level); !applied)
    {
      auto error = std::move(applied.error());
      store = std::move(session.store);
      lease = std::move(session.resources().lease);
      return std::unexpected(std::move(error));
    }
  }
  if (!sessionless)
    session.subagent_delivery_manager()->attach_parent(session.store.session_id());
  return session;
}

// static
ava::core::Result<session_ts> Session::open(runtime::OpenContext const& context, runtime::SessionLifecycleRequest const& request)
{
  if (request.requested_session_id && request.continue_last_session)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "use either requested session id or continue, not both"));
  if (request.fork_session_id && (request.requested_session_id || request.continue_last_session))
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "use either fork or session resume options, not both"));
  if (request.sessionless && (request.requested_session_id || request.continue_last_session || request.fork_session_id))
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "use either no-session or session resume options, not both"));

  auto directories = resolve_runtime_directories(context);
  if (!directories)
    return std::unexpected(std::move(directories.error()));
  auto const& workspace_dir = directories->first;

  if (request.requested_session_id && context.subagent_delivery_manager)
  {
    auto retained = context.subagent_delivery_manager->retained_session(*request.requested_session_id, workspace_dir, context.exact_session_id);
    if (!retained)
      return std::unexpected(std::move(retained.error()));
    if (*retained)
      return std::move(**retained);
  }
  auto const session_read_limits = context.session_read_limits.value_or(ava::session::legacy_unbounded_session_read_limits());

  bool created = true;
  bool created_from_fork = false;
  bool load_existing_entries = false;
  bool should_append_session_start = true;
  ava::session::SessionLease lease;
  ava::core::Result<ava::session::SessionStore> store = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session was not initialized"));
  if (request.sessionless)
  {
    store = ava::session::SessionStore::create_ephemeral(workspace_dir);
  }
  else if (request.fork_session_id)
  {
    auto resolved = resolve_session_id(workspace_dir, context.paths.sessions_dir, *request.fork_session_id);
    if (!resolved)
      return std::unexpected(resolved.error());
    auto source = ava::session::SessionStore::open(workspace_dir, *resolved, context.paths.sessions_dir);
    if (!source)
      return std::unexpected(std::move(source.error()));
    auto source_lease = ava::session::SessionLease::acquire(source->session_path());
    if (!source_lease)
      return std::unexpected(std::move(source_lease.error()));
    auto recovered = source->recover_torn_tail(*source_lease, session_read_limits);
    if (!recovered)
      return std::unexpected(std::move(recovered.error()));
    auto staged_recovery = source->recover_incomplete_assistant_output_suffix(*source_lease, session_read_limits);
    if (!staged_recovery)
      return std::unexpected(std::move(staged_recovery.error()));
    auto branch = ava::session::create_session_branch(ava::session::SessionBranchOptions{.workspace_dir = workspace_dir,
                                                                                         .root_dir = context.paths.sessions_dir,
                                                                                         .source_session_id = *resolved,
                                                                                         .branch_from_entry_id = {},
                                                                                         .name = request.initial_session_name,
                                                                                         .labels = std::nullopt,
                                                                                         .read_limits = context.session_read_limits,
                                                                                         .source_lease = &*source_lease,
                                                                                         .mode = ava::session::SessionBranchMode::Fork,
                                                                                         .actor = "cli"});
    if (!branch)
      return std::unexpected(std::move(branch.error()));
    store = std::move(branch->store);
    lease = std::move(branch->lease);
    created_from_fork = true;
    load_existing_entries = true;
    should_append_session_start = false;
  }
  else if (request.requested_session_id)
  {
    if (context.exact_session_id)
    {
      store = ava::session::SessionStore::open(workspace_dir, *request.requested_session_id, context.paths.sessions_dir);
    }
    else
    {
      auto resolved = resolve_session_id(workspace_dir, context.paths.sessions_dir, *request.requested_session_id);
      if (!resolved)
        return std::unexpected(resolved.error());
      store = ava::session::SessionStore::open(workspace_dir, *resolved, context.paths.sessions_dir);
    }
    created = false;
    load_existing_entries = true;
  }
  else if (request.continue_last_session)
  {
    auto sessions = ava::session::SessionStore::list_sessions(workspace_dir, context.paths.sessions_dir);
    if (!sessions)
      return std::unexpected(sessions.error());
    if (!sessions->empty())
    {
      if (context.subagent_delivery_manager)
      {
        auto retained = context.subagent_delivery_manager->retained_session(sessions->front().session_id, workspace_dir, true);
        if (!retained)
          return std::unexpected(std::move(retained.error()));
        if (*retained)
          return std::move(**retained);
      }
      store = ava::session::SessionStore::open(workspace_dir, sessions->front().session_id, context.paths.sessions_dir);
      created = false;
      load_existing_entries = true;
    }
    else
    {
      store = ava::session::SessionStore::create(workspace_dir, context.paths.sessions_dir);
    }
  }
  else
  {
    store = ava::session::SessionStore::create(workspace_dir, context.paths.sessions_dir);
  }
  if (!store)
    return std::unexpected(store.error());

  if (!created)
  {
    auto acquired = ava::session::SessionLease::acquire(store->session_path());
    if (!acquired)
      return std::unexpected(std::move(acquired.error()));
    lease = std::move(*acquired);
    auto recovered = store->recover_torn_tail(lease, session_read_limits);
    if (!recovered)
      return std::unexpected(std::move(recovered.error()));
    auto staged_recovery = store->recover_incomplete_assistant_output_suffix(lease, session_read_limits);
    if (!staged_recovery)
      return std::unexpected(std::move(staged_recovery.error()));
  }

  auto unlocked_session_result =
      construct(context, request, *store, lease, created, load_existing_entries, created && should_append_session_start,
                request.initial_session_name.has_value() && !request.fork_session_id, context.subagent_delivery_manager, context.session_title_coordinator);
  if (!unlocked_session_result && created_from_fork)
  {
    auto error = std::move(unlocked_session_result.error());
    ava::session::rollback_created_session_with_context(*store, lease, error);
    return std::unexpected(std::move(error));
  }
  return unlocked_session_result;
}

// static
ava::core::Result<session_ts> Session::open_owned(OpenContext const& context, ava::session::SessionStore& store, ava::session::SessionLease& lease, bool created)
{
  if (store.is_ephemeral())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "owned runtime session handoff requires a persistent session"));
  if (lease.canonical_path().empty())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "owned runtime session handoff requires an active lease"));

  auto const session_read_limits = context.session_read_limits.value_or(ava::session::legacy_unbounded_session_read_limits());
  auto recovered = store.recover_torn_tail(lease, session_read_limits);
  if (!recovered)
    return std::unexpected(std::move(recovered.error()));
  auto staged_recovery = store.recover_incomplete_assistant_output_suffix(lease, session_read_limits);
  if (!staged_recovery)
    return std::unexpected(std::move(staged_recovery.error()));
  return construct(context, {}, store, lease, created, true, false, false, context.subagent_delivery_manager, context.session_title_coordinator);
}

Session Session::create_detached(ava::session::SessionLease lease, ava::session::SessionReadAuthority authority,
                                 std::shared_ptr<ava::app::SubagentDeliveryManager> manager) const
{
  SessionResources session_resources{.lease = std::move(lease),
                                     .anchor_set = anchor_set(),
                                     .run_controller = run_controller(),
                                     .append_target = append_target(),
                                     .bound_read_authority = std::move(authority),
                                     .subagent_coordinator = subagent_coordinator(),
                                     .subagent_delivery_manager = std::move(manager),
                                     .session_title_coordinator = session_title_coordinator(),
                                     .diagnostics = diagnostics(),
                                     .mcp_config = mcp_config()};
  return Session(Session_aggregate_base{.invocation_inputs_ = invocation_inputs(),
                                        .resolved_prompt_state_ = resolve_prompt_state(),
                                        .model_selection_ = model_selection(),
                                        .trust_state_ = trust_state(),
                                        .resources_ = std::move(session_resources),
                                        .store = store,
                                        .created = created});
}

// static
ava::core::Result<session_ts> Session::create_at(OpenContext context, std::filesystem::path const& workspace_root, std::filesystem::path const& current_dir)
{
  context.workspace_dir = workspace_root;
  context.current_dir = current_dir;
  return Session::open(context);
}

// static
ava::core::Result<session_ts> Session::open_at(OpenContext context, std::filesystem::path const& workspace_root,
                                                            std::filesystem::path const& current_dir, SessionLifecycleRequest request)
{
  context.workspace_dir = workspace_root;
  context.current_dir = current_dir;
  return Session::open(context, request);
}

ava::core::Result<session_ts> Session::create_similar(OpenContext const& base_context) const
{
  return create_at(replacement_open_context(base_context), workspace_dir(), current_dir());
}

ava::core::Result<session_ts> Session::open_similar(OpenContext const& base_context, SessionLifecycleRequest request) const
{
  return open_at(replacement_open_context(base_context), workspace_dir(), current_dir(), std::move(request));
}

CommandRegistry Session::load_command_registry(CommandRegistryOptions options)
{
  RegistryBuilder builder;
  auto const resource_policy = make_extension_resource_policy_1(*this);
  if (options.include_builtins)
    load_builtin_commands(builder);
  if (options.include_prompt_commands)
    load_prompt_commands(builder, *this, resource_policy);
  if (options.include_plugin_commands)
    load_plugin_commands(builder, *this, resource_policy);
  if (options.include_mcp_prompts)
    load_mcp_prompt_commands(builder, *this, options, resource_policy);
  if (options.include_skills)
    load_skill_commands(builder, *this, resource_policy);
  return std::move(builder.registry);
}

ava::core::VoidResult Session::refresh_parent_configuration() const
{
  auto const& manager = subagent_delivery_manager();
  return manager ? manager->refresh_parent_configuration_1(*this) : ava::core::VoidResult{};
}

ava::core::VoidResult Session::replace_with(Session&& replacement)
{
  // Background ownership and diagnostics are application-scoped. Retire only
  // the visible session controller and preserve the exact services across navigation.
  auto coordinator = resources().subagent_coordinator;
  auto delivery_manager = resources().subagent_delivery_manager;
  auto title_coordinator = resources().session_title_coordinator;
  auto diagnostics = resources().diagnostics;
  auto const detached_parent_id = sessionless() ? std::string{} : store.session_id();
  bool const leaves_detached_parent = !detached_parent_id.empty() && (replacement.sessionless() || replacement.store.session_id() != detached_parent_id);
  resources().run_controller.reset();
  *this = std::move(replacement);
  if (delivery_manager)
  {
    resources().subagent_delivery_manager = delivery_manager;
    resources().subagent_coordinator = resources().subagent_delivery_manager->coordinator();
  }
  else if (coordinator)
    resources().subagent_coordinator = coordinator;
  if (title_coordinator)
    resources().session_title_coordinator = std::move(title_coordinator);
  if (diagnostics)
    resources().diagnostics = std::move(diagnostics);

  // This is an explicit visible-session detach boundary. The delivery manager
  // keeps the exact parent capsule while process-local work still needs it.
  if (leaves_detached_parent && delivery_manager)
    delivery_manager->release_detached_parent(detached_parent_id);
  return {};
}

ava::core::VoidResult Session::recover_source_for_mutation(std::string const& source_session_id,
                                                            std::optional<ava::session::SessionLease>& temporary_source_lease)
{
  if (source_session_id == store.session_id())
  {
    auto recovered = store.recover_torn_tail(lease(), session_read_limits());
    if (!recovered)
      return std::unexpected(std::move(recovered.error()));
    auto staged_recovery = store.recover_incomplete_assistant_output_suffix(lease(), session_read_limits());
    if (!staged_recovery)
      return std::unexpected(std::move(staged_recovery.error()));
    return {};
  }

  auto source = ava::session::SessionStore::open(workspace_dir(), source_session_id, paths().sessions_dir);
  if (!source)
    return std::unexpected(std::move(source.error()));
  auto acquired = ava::session::SessionLease::acquire(source->session_path());
  if (!acquired)
    return std::unexpected(std::move(acquired.error()));
  temporary_source_lease.emplace(std::move(*acquired));
  auto recovered = source->recover_torn_tail(*temporary_source_lease, session_read_limits());
  if (!recovered)
    return std::unexpected(std::move(recovered.error()));
  auto staged_recovery = source->recover_incomplete_assistant_output_suffix(*temporary_source_lease, session_read_limits());
  if (!staged_recovery)
    return std::unexpected(std::move(staged_recovery.error()));
  return {};
}

OpenContext Session::replacement_open_context(runtime::OpenContext const& base_context) const
{
  auto context = base_context;
  context.workspace_dir = workspace_dir();
  context.current_dir = current_dir();
  context.mode = mode();
  context.tool_visibility = tool_visibility();
  context.paths = paths();
  context.offline = is_offline();
  context.additional_writable_dirs = additional_writable_dirs();
  context.anchor_set = sessionless() ? nullptr : anchor_set();
  context.prompt_overrides = prompt_overrides();
  context.session_read_limits = session_read_limits();
  context.subagent_coordinator = subagent_coordinator();
  context.subagent_delivery_manager = subagent_delivery_manager();
  context.session_title_coordinator = session_title_coordinator();
  context.diagnostics = diagnostics();
  return context;
}

ava::core::Result<ava::session::SessionMetadataView> Session::append_metadata(ava::session::SessionMetadataUpdate update)
{
  auto read_authority = this->read_authority();
  if (!read_authority)
    return std::unexpected(std::move(read_authority.error()));
  auto entries = read_authority->load();
  if (!entries)
    return std::unexpected(std::move(entries.error()));
  auto entry = ava::session::make_session_metadata_entry(std::move(update), entries->empty() ? std::string{} : entries->back().id);
  if (!entry)
    return std::unexpected(std::move(entry.error()));
  if (auto appended = append_owned(*entry); !appended)
    return std::unexpected(std::move(appended.error()));
  entries->push_back(std::move(*entry));
  return ava::session::session_metadata_from_entries(store.session_id(), *entries);
}

ava::core::VoidResult Session::append_mode_change(ava::agent::Mode mode)
{
  return append_owned(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                 .parent_id = "",
                                                 .type = ava::session::EntryType::ModeChange,
                                                 .timestamp = ava::session::now_timestamp(),
                                                 .data_json = "{\"mode\":\"" + ava::agent::to_string(mode) + "\"}"});
}

ava::core::VoidResult Session::apply_initial_reasoning_level(std::string_view requested_level)
{
  auto level = core::trim(requested_level);
  if (level.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "reasoning level is required");
    add_cli_reasoning_context(error, model());
    return std::unexpected(std::move(error));
  }

  std::optional<ReasoningSelection> selection = std::nullopt;
  if (level != "off")
  {
    auto selected = reasoning_selection_for_level(model(), std::move(level));
    if (!selected)
    {
      auto error = std::move(selected.error());
      add_cli_reasoning_context(error, model());
      return std::unexpected(std::move(error));
    }
    selection = std::move(*selected);
  }

  auto changed = set_reasoning(std::move(selection));
  if (!changed)
  {
    auto error = std::move(changed.error());
    add_cli_reasoning_context(error, model());
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::VoidResult Session::apply_prompt_state(PromptState prompt_state)
{
  resolve_prompt_state() = ResolvedPromptState{.mode = prompt_state.mode,
                                               .base_prompt = std::move(prompt_state.base_prompt),
                                               .context_sources = std::move(prompt_state.context_sources),
                                               .freshness_sources = std::move(prompt_state.freshness_sources),
                                               .system_prompt = std::move(prompt_state.system_prompt),
                                               .ambient_extension_free_system_prompt = std::move(prompt_state.ambient_extension_free_system_prompt)};
  return refresh_parent_configuration();
}

ava::core::Result<bool> Session::switch_model(ava::config::ModelInfo model)
{
  if (this->model().provider_id == model.provider_id && this->model().model_id == model.model_id)
    return false;

  auto prompt_state =
      load_runtime_prompt_state(paths(), model, mode(), workspace_dir(), current_dir(), project_resources_trusted(project_trust()), prompt_overrides());
  if (!prompt_state)
    return std::unexpected(std::move(prompt_state.error()));

  auto const previous = this->model();
  auto appended = append_owned(make_model_change_entry(previous, model));
  if (!appended)
    return std::unexpected(std::move(appended.error()));

  model_selection().model = std::move(model);
  resolve_prompt_state() = ResolvedPromptState{.mode = prompt_state->mode,
                                               .base_prompt = std::move(prompt_state->base_prompt),
                                               .context_sources = std::move(prompt_state->context_sources),
                                               .freshness_sources = std::move(prompt_state->freshness_sources),
                                               .system_prompt = std::move(prompt_state->system_prompt),
                                               .ambient_extension_free_system_prompt = std::move(prompt_state->ambient_extension_free_system_prompt)};
  model_selection().reasoning = std::nullopt;
  if (auto refreshed = refresh_parent_configuration(); !refreshed)
    return std::unexpected(std::move(refreshed.error()));
  return true;
}

ava::core::Result<bool> Session::set_reasoning(std::optional<ReasoningSelection> selection)
{
  if (selection)
  {
    selection->level = core::trim(selection->level);
    selection->display = core::trim(selection->display);
    auto resolved = resolve_runtime_reasoning_selection(model(), std::move(*selection));
    if (!resolved)
    {
      return std::unexpected(std::move(resolved.error()));
    }
    selection = std::move(*resolved);
  }
  if (same_reasoning_selection(reasoning(), selection))
    return false;

  auto appended = append_owned(make_reasoning_change_entry(model(), selection));
  if (!appended)
    return std::unexpected(std::move(appended.error()));
  model_selection().reasoning = std::move(selection);
  if (auto refreshed = refresh_parent_configuration(); !refreshed)
    return std::unexpected(std::move(refreshed.error()));
  return true;
}

ava::core::Result<ava::session::SessionMetadataView> Session::load_metadata() const
{
  auto read_authority = this->read_authority();
  if (!read_authority)
    return std::unexpected(std::move(read_authority.error()));
  auto entries = read_authority->load();
  if (!entries)
    return std::unexpected(std::move(entries.error()));
  return ava::session::session_metadata_from_entries(store.session_id(), *entries);
}

std::vector<std::filesystem::path> Session::ava_authority_roots() const
{
  std::vector<std::filesystem::path> roots;
  roots.reserve(kMaxCommandAuthorityRoots);
  append_command_authority_root(roots, paths().ava_config_dir);
  append_command_authority_root(roots, paths().ava_state_dir);
  append_command_authority_root(roots, paths().sessions_dir);
  append_command_authority_root(roots, paths().auth_file);
  append_command_authority_root(roots, ava::config::legacy_ava_credentials_path());
  append_command_authority_root(roots, ava::config::legacy_compatible_auth_path());
  // Preserve the exact active store parent as a fallback for custom/test path
  // sets whose broader sessions directory is empty or disjoint. This path is
  // derived from the active store, never reconstructed from a session ID.
  append_command_authority_root(roots, store.session_path().parent_path());
  return roots;
}

std::string Session::state_result_json(bool cancel_requested) const
{
  return rpc::detail::SessionResultSerializer({}, *this).state_result_json(cancel_requested);
}

ava::core::Result<std::string> Session::messages_result_json() const
{
  return rpc::detail::MessagesResultSerializer({}, *this).run();
}

ava::core::Result<std::string> Session::list_sessions_result_json() const
{
  return rpc::detail::SessionResultSerializer({}, *this).list_sessions_result_json();
}

ava::core::Result<std::string> Session::tree_result_json() const
{
  return rpc::detail::SessionResultSerializer({}, *this).session_tree_result_json();
}

ava::core::Result<std::string> Session::list_models_result_json() const
{
  return rpc::detail::SessionResultSerializer({}, *this).list_models_result_json();
}

ava::core::Result<std::string> Session::stats_result_json() const
{
  return rpc::detail::SessionResultSerializer({}, *this).session_stats_result_json();
}

ava::core::Result<std::string> Session::validation_result_json() const
{
  return rpc::detail::SessionResultSerializer({}, *this).session_validation_result_json();
}

}  // namespace ava::app::runtime
