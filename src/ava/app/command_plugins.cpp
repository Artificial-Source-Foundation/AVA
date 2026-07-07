#include "ava/app/command_format.h"
#include "ava/app/command_plugins.h"
#include "ava/app/command_tools.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/enablement.h"
#include "ava/plugin/manifest.h"
#include "ava/plugin/runner.h"
#include "ava/plugin/tool_broker.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

namespace ava::app {
namespace {

std::string plugin_display_path(std::filesystem::path const& path, RuntimeSession const& session)
{
  return sanitize_inline_text(display_path(path, session.current_dir));
}

ava::plugin::PluginDiscoveryOptions plugin_discovery_options(RuntimeSession const& session)
{
  return ava::plugin::PluginDiscoveryOptions{
      .global_plugins_dir = session.paths.ava_config_dir / "plugins",
      .project_plugins_dir = project_resources_trusted(session.project_trust) ? session.workspace_dir / ".ava" / "plugins" : std::filesystem::path{}};
}

std::filesystem::path plugin_enablement_file(RuntimeSession const& session)
{
  return session.paths.ava_state_dir / "plugin-enablement.json";
}

ava::plugin::PluginDiagnostics plugin_diagnostics(RuntimeSession const& session)
{
  return ava::plugin::collect_plugin_diagnostics(plugin_discovery_options(session), plugin_enablement_file(session), session.workspace_dir);
}

std::string plugin_scope_text(ava::plugin::PluginScope scope)
{
  return std::string(ava::plugin::to_string(scope));
}

std::string plugin_status_text(bool enabled)
{
  return enabled ? "enabled" : "disabled";
}

std::string plugin_capabilities_text(ava::plugin::PluginManifest const& manifest)
{
  if (manifest.capabilities.empty())
    return "none";
  std::vector<std::string> capabilities;
  capabilities.reserve(manifest.capabilities.size());
  for (auto const& capability : manifest.capabilities) capabilities.push_back(sanitize_inline_text(capability));
  return joined_strings(capabilities, ", ");
}

std::string plugin_entrypoint_text(ava::plugin::PluginEntrypoint const& entrypoint)
{
  std::string text = sanitize_inline_text(entrypoint.command);
  for (auto const& arg : entrypoint.args) text += " " + sanitize_inline_text(arg);
  return text;
}

ava::plugin::PluginCommandContribution const* find_plugin_command(ava::plugin::PluginManifest const& manifest, std::string_view command_name)
{
  for (auto const& command : manifest.contributes.commands)
  {
    if (command.name == command_name)
      return &command;
  }
  return nullptr;
}

ava::plugin::PluginResourceContribution const* find_plugin_resource(std::vector<ava::plugin::PluginResourceContribution> const& resources,
                                                                    std::string_view name)
{
  for (auto const& resource : resources)
  {
    if (resource.name == name)
      return &resource;
  }
  return nullptr;
}

bool path_is_within(std::filesystem::path const& base, std::filesystem::path const& target)
{
  if (target == base)
    return true;
  std::error_code relative_error;
  auto const relative = std::filesystem::relative(target, base, relative_error);
  if (relative_error || relative.empty())
    return false;
  return *relative.begin() != "..";
}

ava::core::Result<std::filesystem::path> plugin_resource_path(ava::plugin::PluginManifest const& manifest,
                                                              ava::plugin::PluginResourceContribution const& resource)
{
  std::error_code base_error;
  auto const canonical_base = std::filesystem::weakly_canonical(manifest.directory, base_error);
  auto const base_path = base_error ? std::filesystem::absolute(manifest.directory).lexically_normal() : canonical_base;
  auto const raw_target = manifest.directory / resource.path;
  std::error_code target_error;
  auto const canonical_target = std::filesystem::weakly_canonical(raw_target, target_error);
  auto const target_path = target_error ? std::filesystem::absolute(raw_target).lexically_normal() : canonical_target;
  if (!path_is_within(base_path, target_path))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin resource path escapes plugin directory");
    error.with_context("plugin", manifest.id);
    error.with_context("resource", resource.name);
    error.with_context("path", resource.path);
    return std::unexpected(std::move(error));
  }
  return target_path;
}

ava::core::Result<std::string> read_plugin_resource(ava::plugin::PluginManifest const& manifest, ava::plugin::PluginResourceContribution const& resource)
{
  constexpr std::size_t max_resource_bytes = ava::plugin::kPluginResourceContentMaxBytes;
  auto path = plugin_resource_path(manifest, resource);
  if (!path)
    return std::unexpected(std::move(path.error()));

  std::error_code type_error;
  if (!std::filesystem::is_regular_file(*path, type_error))
  {
    auto error =
        ava::core::Error(type_error ? ava::core::ErrorCategory::Io : ava::core::ErrorCategory::InvalidArgument, "plugin resource must be a regular file");
    error.with_context("plugin", manifest.id);
    error.with_context("resource", resource.name);
    error.with_context("path", path->string());
    if (type_error)
      error.with_context("cause", type_error.message());
    return std::unexpected(std::move(error));
  }

  std::error_code size_error;
  auto const size = std::filesystem::file_size(*path, size_error);
  if (size_error)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect plugin resource size")
                               .with_context("plugin", manifest.id)
                               .with_context("resource", resource.name)
                               .with_context("path", path->string())
                               .with_context("cause", size_error.message()));
  }
  if (size > max_resource_bytes)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin resource exceeds size cap")
                               .with_context("plugin", manifest.id)
                               .with_context("resource", resource.name)
                               .with_context("path", path->string())
                               .with_context("max_bytes", std::to_string(max_resource_bytes)));
  }

  std::ifstream file(*path, std::ios::binary);
  if (!file)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to open plugin resource")
                               .with_context("plugin", manifest.id)
                               .with_context("resource", resource.name)
                               .with_context("path", path->string()));
  }
  std::string contents;
  std::array<char, 4096> buffer{};
  while (file)
  {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto const count = file.gcount();
    if (count <= 0)
      continue;
    if (contents.size() + static_cast<std::size_t>(count) > max_resource_bytes)
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin resource exceeds size cap while reading")
                                 .with_context("plugin", manifest.id)
                                 .with_context("resource", resource.name)
                                 .with_context("path", path->string())
                                 .with_context("max_bytes", std::to_string(max_resource_bytes)));
    }
    contents.append(buffer.data(), static_cast<std::size_t>(count));
  }
  if (file.bad())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to read plugin resource")
                               .with_context("plugin", manifest.id)
                               .with_context("resource", resource.name)
                               .with_context("path", path->string()));
  }
  return contents;
}

std::string format_plugin_resource_list_text(ava::plugin::PluginStatus const& status, RuntimeSession const& session, std::string_view label,
                                             std::vector<ava::plugin::PluginResourceContribution> const& resources)
{
  std::ostringstream output;
  output << "Plugin " << label << " for " << sanitize_inline_text(status.plugin.manifest.id) << "\n";
  if (!status.enabled)
    output << "  status: disabled\n";
  if (resources.empty())
  {
    output << "  none";
    return output.str();
  }
  for (auto const& resource : resources)
  {
    output << "  " << sanitize_inline_text(resource.name);
    if (!resource.description.empty())
      output << " - " << sanitize_inline_text(resource.description);
    output << "  " << plugin_display_path(status.plugin.manifest.directory / resource.path, session) << "\n";
  }
  auto text = output.str();
  if (!text.empty() && text.back() == '\n')
    text.pop_back();
  return text;
}

std::string format_plugin_resource_text(ava::plugin::PluginManifest const& manifest, ava::plugin::PluginResourceContribution const& resource,
                                        std::string_view label, RuntimeSession const& session, std::string content)
{
  std::ostringstream output;
  output << "Plugin " << label << " " << sanitize_inline_text(manifest.id) << "/" << sanitize_inline_text(resource.name) << "\n";
  output << "  path: " << plugin_display_path(manifest.directory / resource.path, session) << "\n\n";
  output << content;
  return output.str();
}

std::string dynamic_resource_plural(ava::plugin::PluginDynamicResourceKind kind)
{
  switch (kind)
  {
    case ava::plugin::PluginDynamicResourceKind::Prompt:
      return "prompts";
    case ava::plugin::PluginDynamicResourceKind::Skill:
      return "skills";
  }
  return "resources";
}

std::string_view dynamic_resource_capability(ava::plugin::PluginDynamicResourceKind kind)
{
  return ava::plugin::plugin_dynamic_resource_capability(kind);
}

std::string_view dynamic_resource_contribution_kind(ava::plugin::PluginDynamicResourceKind kind)
{
  switch (kind)
  {
    case ava::plugin::PluginDynamicResourceKind::Prompt:
      return "dynamic_prompt";
    case ava::plugin::PluginDynamicResourceKind::Skill:
      return "dynamic_skill";
  }
  return "dynamic_resource";
}

std::string invalid_dynamic_resource_name_text(ava::plugin::PluginDynamicResourceKind kind, std::string_view name)
{
  return "invalid dynamic " + std::string(ava::plugin::plugin_dynamic_resource_kind_name(kind)) + " name: " + sanitize_inline_text(std::string(name));
}

std::string dynamic_resource_unsupported_text(ava::plugin::PluginManifest const& manifest, ava::plugin::PluginDynamicResourceKind kind)
{
  return "plugin does not declare " + std::string(dynamic_resource_capability(kind)) + ": " + sanitize_inline_text(manifest.id);
}

struct DynamicResourceListEntry
{
  std::string plugin_id;
  ava::plugin::PluginDynamicResource resource;
};

struct DynamicResourceFailure
{
  std::string plugin_id;
  std::string message;
};

std::string format_dynamic_resource_list_text(ava::plugin::PluginDynamicResourceKind kind, std::vector<DynamicResourceListEntry> const& entries,
                                              std::vector<DynamicResourceFailure> const& failures)
{
  std::ostringstream output;
  output << "Dynamic plugin " << dynamic_resource_plural(kind) << ":\n";
  if (entries.empty() && failures.empty())
  {
    output << "  none";
    return output.str();
  }
  for (auto const& entry : entries)
  {
    output << "  " << sanitize_inline_text(entry.plugin_id) << "/" << sanitize_inline_text(entry.resource.name);
    if (!entry.resource.description.empty())
      output << " - " << sanitize_inline_text(entry.resource.description);
    output << "\n";
  }
  for (auto const& failure : failures)
  {
    output << "  " << sanitize_inline_text(failure.plugin_id) << "  error: " << sanitize_inline_text(failure.message) << "\n";
  }
  auto text = output.str();
  if (!text.empty() && text.back() == '\n')
    text.pop_back();
  return text;
}

std::string format_dynamic_resource_text(ava::plugin::PluginManifest const& manifest, ava::plugin::PluginDynamicResourceKind kind, std::string_view name,
                                         std::string content)
{
  std::ostringstream output;
  output << "Plugin dynamic " << ava::plugin::plugin_dynamic_resource_kind_name(kind) << " " << sanitize_inline_text(manifest.id) << "/"
         << sanitize_inline_text(std::string(name)) << "\n\n";
  output << content;
  return output.str();
}

ava::core::VoidResult record_dynamic_resource_result(RuntimeSession const& session, RuntimeEventSink const& sink, CommandResult& result,
                                                     std::string const& call_id, ava::agent::ToolTimelineStatus status, std::string result_summary,
                                                     std::string result_content, ava::tools::ToolContext const& context)
{
  auto const permission_ids = context.permission_request_ids ? *context.permission_request_ids : std::vector<std::string>{};
  return record_tool_result(session, sink, result, call_id, "plugin_resource", status, std::move(result_summary), std::move(result_content), permission_ids);
}

ava::plugin::PluginStatus const* find_plugin_status(ava::plugin::PluginDiagnostics const& diagnostics, std::string_view plugin_id)
{
  for (auto const& status : diagnostics.plugins)
  {
    if (status.plugin.manifest.id == plugin_id)
      return &status;
  }
  return nullptr;
}

bool has_duplicate_plugin_failure(ava::plugin::PluginDiagnostics const& diagnostics, std::string_view plugin_id)
{
  auto const details = "plugin=" + std::string(plugin_id);
  return std::ranges::any_of(diagnostics.failures, [&](ava::plugin::PluginFailure const& failure) {
    return failure.message == "duplicate plugin id discovered" && failure.details == details;
  });
}

std::string plugin_not_found_text(ava::plugin::PluginDiagnostics const& diagnostics, std::string_view plugin_id)
{
  if (has_duplicate_plugin_failure(diagnostics, plugin_id))
  {
    return "plugin id is ambiguous: " + sanitize_inline_text(std::string(plugin_id)) + " (see /plugins failures)";
  }
  return "plugin not found: " + sanitize_inline_text(std::string(plugin_id));
}

bool plugin_command_error_is_canceled(ava::core::Error const& error)
{
  for (auto const& context : error.context())
  {
    if (context.key == "canceled" && context.value == "true")
      return true;
  }
  return error.message().find("canceled") != std::string::npos;
}

std::string format_plugin_list_text(ava::plugin::PluginDiagnostics const& diagnostics, RuntimeSession const& session)
{
  std::ostringstream output;
  output << "Plugins:\n";
  if (diagnostics.plugins.empty())
  {
    output << "  none discovered\n";
  }
  else
  {
    for (auto const& status : diagnostics.plugins)
    {
      auto const& manifest = status.plugin.manifest;
      output << "  " << sanitize_inline_text(manifest.id) << "  " << plugin_status_text(status.enabled) << "  " << plugin_scope_text(status.plugin.scope)
             << "  " << sanitize_inline_text(manifest.name) << "  " << sanitize_inline_text(manifest.version) << "\n";
    }
  }
  output << "\nDiscovery paths:\n";
  output << "  global: " << plugin_display_path(diagnostics.discovery_options.global_plugins_dir, session) << "\n";
  output << "  project: " << plugin_display_path(diagnostics.discovery_options.project_plugins_dir, session) << "\n";
  output << "Enablement: " << plugin_display_path(diagnostics.enablement_file, session);
  if (!diagnostics.failures.empty())
  {
    output << "\nFailures: " << diagnostics.failures.size() << " (use /plugins failures)";
  }
  return output.str();
}

std::string format_plugin_failure_text(ava::plugin::PluginFailure const& failure, RuntimeSession const& session)
{
  std::string text =
      "  " + plugin_scope_text(failure.scope) + "  " + plugin_display_path(failure.path, session) + "\n    " + sanitize_inline_text(failure.message);
  if (!failure.details.empty())
    text += "\n    " + sanitize_inline_text(failure.details);
  return text;
}

std::string format_plugin_failures_text(ava::plugin::PluginDiagnostics const& diagnostics, RuntimeSession const& session)
{
  if (diagnostics.failures.empty())
    return "No plugin discovery or enablement failures.";
  std::string output = "Plugin failures:\n";
  for (auto const& failure : diagnostics.failures) output += format_plugin_failure_text(failure, session) + "\n";
  if (!output.empty() && output.back() == '\n')
    output.pop_back();
  return output;
}

std::string format_plugin_inspect_text(ava::plugin::PluginStatus const& status, RuntimeSession const& session)
{
  auto const& manifest = status.plugin.manifest;
  std::ostringstream output;
  output << "Plugin " << sanitize_inline_text(manifest.id) << "\n";
  output << "  name: " << sanitize_inline_text(manifest.name) << "\n";
  output << "  version: " << sanitize_inline_text(manifest.version) << "\n";
  output << "  scope: " << plugin_scope_text(status.plugin.scope) << "\n";
  output << "  status: " << plugin_status_text(status.enabled) << "\n";
  output << "  manifest: " << plugin_display_path(manifest.path, session) << "\n";
  output << "  entrypoint: " << plugin_entrypoint_text(manifest.entrypoint) << " (not executed)\n";
  output << "  capabilities: " << plugin_capabilities_text(manifest) << "\n";
  output << "  tools: " << (manifest.contributes.tools.empty() ? "none" : std::to_string(manifest.contributes.tools.size())) << "\n";
  for (auto const& tool : manifest.contributes.tools)
  {
    output << "    " << sanitize_inline_text(tool.name);
    if (!tool.description.empty())
      output << " - " << sanitize_inline_text(tool.description);
    output << "\n";
  }
  output << "  commands: " << (manifest.contributes.commands.empty() ? "none" : std::to_string(manifest.contributes.commands.size())) << "\n";
  for (auto const& command : manifest.contributes.commands)
  {
    output << "    " << sanitize_inline_text(command.name);
    if (!command.description.empty())
      output << " - " << sanitize_inline_text(command.description);
    output << "\n";
  }
  output << "  prompts: " << (manifest.contributes.prompts.empty() ? "none" : std::to_string(manifest.contributes.prompts.size())) << "\n";
  for (auto const& prompt : manifest.contributes.prompts)
  {
    output << "    " << sanitize_inline_text(prompt.name);
    if (!prompt.description.empty())
      output << " - " << sanitize_inline_text(prompt.description);
    output << "\n";
  }
  output << "  skills: " << (manifest.contributes.skills.empty() ? "none" : std::to_string(manifest.contributes.skills.size())) << "\n";
  for (auto const& skill : manifest.contributes.skills)
  {
    output << "    " << sanitize_inline_text(skill.name);
    if (!skill.description.empty())
      output << " - " << sanitize_inline_text(skill.description);
    output << "\n";
  }
  output << "  event_hooks: " << (manifest.contributes.event_hooks.empty() ? "none" : std::to_string(manifest.contributes.event_hooks.size())) << "\n";
  output << "  note: inspection lists metadata only; no plugin process is started yet.";
  return output.str();
}

std::string format_valid_plugin_manifest_text(ava::plugin::PluginManifest const& manifest, RuntimeSession const& session)
{
  std::ostringstream output;
  output << "Valid plugin manifest\n";
  output << "  id: " << sanitize_inline_text(manifest.id) << "\n";
  output << "  name: " << sanitize_inline_text(manifest.name) << "\n";
  output << "  version: " << sanitize_inline_text(manifest.version) << "\n";
  output << "  api_version: " << sanitize_inline_text(manifest.api_version) << "\n";
  output << "  manifest: " << plugin_display_path(manifest.path, session) << "\n";
  output << "  capabilities: " << plugin_capabilities_text(manifest) << "\n";
  output << "  tools: " << manifest.contributes.tools.size() << "\n";
  output << "  commands: " << manifest.contributes.commands.size() << "\n";
  output << "  prompts: " << manifest.contributes.prompts.size() << "\n";
  output << "  skills: " << manifest.contributes.skills.size() << "\n";
  output << "  event_hooks: " << manifest.contributes.event_hooks.size() << "\n";
  output << "  note: validation parsed the manifest only; no entrypoint was executed.";
  return output.str();
}

std::filesystem::path plugin_validate_path(RuntimeSession const& session, std::string_view path_text)
{
  auto path = std::filesystem::path(std::string(path_text));
  if (path.is_relative())
    path = session.current_dir / path;
  return path.lexically_normal();
}

std::string trim_ascii_whitespace(std::string_view text)
{
  std::size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) ++start;
  auto end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
  return std::string(text.substr(start, end - start));
}

std::string plugin_validate_argument(std::string_view plugins_argument)
{
  std::size_t index = 0;
  while (index < plugins_argument.size() && std::isspace(static_cast<unsigned char>(plugins_argument[index])) != 0)
  {
    ++index;
  }
  while (index < plugins_argument.size() && std::isspace(static_cast<unsigned char>(plugins_argument[index])) == 0)
  {
    ++index;
  }
  return trim_ascii_whitespace(plugins_argument.substr(index));
}

struct PluginRunArguments
{
  std::string plugin_id;
  std::string command_name;
  std::string arguments_json = "{}";
};

std::optional<std::string_view> consume_token(std::string_view& text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
  if (text.empty())
    return std::nullopt;
  auto const end = text.find_first_of(" \t\r\n");
  auto const token = text.substr(0, end == std::string_view::npos ? text.size() : end);
  text.remove_prefix(token.size());
  return token;
}

ava::core::Result<PluginRunArguments> parse_plugin_run_arguments(std::string_view argument)
{
  auto const subcommand = consume_token(argument);
  auto const plugin_id = consume_token(argument);
  auto const command_name = consume_token(argument);
  while (!argument.empty() && std::isspace(static_cast<unsigned char>(argument.front())) != 0) argument.remove_prefix(1);
  if (!subcommand || *subcommand != "run" || !plugin_id || !command_name)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "usage: /plugin run <plugin_id> <command> [arguments_json]"));
  }
  auto arguments_json = argument.empty() ? std::string("{}") : std::string(argument);
  if (!ava::core::json::is_valid_object(arguments_json))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin command arguments must be a JSON object"));
  }
  return PluginRunArguments{.plugin_id = std::string(*plugin_id), .command_name = std::string(*command_name), .arguments_json = std::move(arguments_json)};
}

}  // namespace

ava::core::Result<CommandResult> run_plugins_command(RuntimeSession& session, CommandRequest const& request)
{
  CommandResult result;
  result.handled = true;
  auto const usage = [&]() {
    add_output(
        result,
        missing_argument(
            "/plugins <list|inspect|enable|disable|validate|failures|prompts|prompt|skills|skill|dynamic-prompts|dynamic-prompt|dynamic-skills|dynamic-skill> "
            "..."));
    return result;
  };

  auto const argument = command_argument(request.command, "/plugins");
  auto const args = split_command_arguments(argument);
  if (args.empty())
    return usage();

  auto const& subcommand = args[0];
  if (subcommand == "list")
  {
    if (args.size() != 1)
      return usage();
    add_output(result, format_plugin_list_text(plugin_diagnostics(session), session));
    return result;
  }

  if (subcommand == "failures")
  {
    if (args.size() != 1)
      return usage();
    add_output(result, format_plugin_failures_text(plugin_diagnostics(session), session));
    return result;
  }

  if (subcommand == "inspect")
  {
    if (args.size() != 2)
      return usage();
    auto const diagnostics = plugin_diagnostics(session);
    auto const* status = find_plugin_status(diagnostics, args[1]);
    if (!status)
    {
      add_output(result, plugin_not_found_text(diagnostics, args[1]));
      return result;
    }
    add_output(result, format_plugin_inspect_text(*status, session));
    return result;
  }

  if (subcommand == "prompts" || subcommand == "skills")
  {
    if (args.size() != 2)
      return usage();
    auto const diagnostics = plugin_diagnostics(session);
    auto const* status = find_plugin_status(diagnostics, args[1]);
    if (!status)
    {
      add_output(result, plugin_not_found_text(diagnostics, args[1]));
      return result;
    }
    auto const& resources = subcommand == "prompts" ? status->plugin.manifest.contributes.prompts : status->plugin.manifest.contributes.skills;
    add_output(result, format_plugin_resource_list_text(*status, session, subcommand, resources));
    return result;
  }

  if (subcommand == "prompt" || subcommand == "skill")
  {
    if (args.size() != 3)
      return usage();
    auto const diagnostics = plugin_diagnostics(session);
    auto const* status = find_plugin_status(diagnostics, args[1]);
    if (!status)
    {
      add_output(result, plugin_not_found_text(diagnostics, args[1]));
      return result;
    }
    auto const& resources = subcommand == "prompt" ? status->plugin.manifest.contributes.prompts : status->plugin.manifest.contributes.skills;
    auto const* resource = find_plugin_resource(resources, args[2]);
    if (!resource)
    {
      add_output(result, std::string(subcommand) + " not found: " + sanitize_inline_text(args[2]));
      return result;
    }
    auto content = read_plugin_resource(status->plugin.manifest, *resource);
    if (!content)
    {
      add_output(result, content.error().format());
      return result;
    }
    add_output(result, format_plugin_resource_text(status->plugin.manifest, *resource, subcommand, session, std::move(*content)));
    return result;
  }

  if (subcommand == "dynamic-prompts" || subcommand == "dynamic-skills")
  {
    if (args.size() != 1)
      return usage();
    auto const kind = subcommand == "dynamic-prompts" ? ava::plugin::PluginDynamicResourceKind::Prompt : ava::plugin::PluginDynamicResourceKind::Skill;
    auto const kind_name = std::string(ava::plugin::plugin_dynamic_resource_kind_name(kind));
    auto const diagnostics = plugin_diagnostics(session);
    std::vector<DynamicResourceListEntry> entries;
    std::vector<DynamicResourceFailure> failures;

    for (auto const& status : diagnostics.plugins)
    {
      if (!status.enabled)
        continue;
      if (!ava::plugin::plugin_has_capability(status.plugin.manifest, dynamic_resource_capability(kind)))
        continue;
      auto const call_id = ava::core::make_id("dynres");
      auto const call_label = status.plugin.manifest.id + ":" + kind_name + ":list";
      if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "plugin_resource", call_label); !recorded)
      {
        return std::unexpected(std::move(recorded.error()));
      }

      auto context = make_tool_context(session, request.permission_resolver);
      context.permission_tool_name = "plugin_resource";
      context.cancel_requested = request.cancel_requested;
      context.permission_request_ids = std::make_shared<std::vector<std::string>>();

      auto fail = [&](std::string text, ava::agent::ToolTimelineStatus timeline_status = ava::agent::ToolTimelineStatus::Error) -> ava::core::VoidResult {
        failures.push_back(DynamicResourceFailure{.plugin_id = status.plugin.manifest.id, .message = text});
        return record_dynamic_resource_result(session, request.event_sink, result, call_id, timeline_status, std::move(text), {}, context);
      };

      if (request.cancel_requested && request.cancel_requested())
      {
        if (auto recorded = fail("plugin resource list canceled", ava::agent::ToolTimelineStatus::Canceled); !recorded)
        {
          return std::unexpected(std::move(recorded.error()));
        }
        continue;
      }

      if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::PluginExecute, status.plugin.manifest.path, call_label,
                                                          "plugin_resource", "plugin dynamic resource lookup requires permission");
          !permission)
      {
        if (auto recorded = fail(permission.error().format()); !recorded)
        {
          return std::unexpected(std::move(recorded.error()));
        }
        continue;
      }

      ava::plugin::PluginRunnerOptions options;
      options.workspace_dir = session.workspace_dir;
      auto process = ava::plugin::PluginProcess::start(status.plugin.manifest, options, request.cancel_requested);
      if (!process)
      {
        auto const status =
            plugin_command_error_is_canceled(process.error()) ? ava::agent::ToolTimelineStatus::Canceled : ava::agent::ToolTimelineStatus::Error;
        if (auto recorded = fail(process.error().format(), status); !recorded)
        {
          return std::unexpected(std::move(recorded.error()));
        }
        continue;
      }

      auto proxy_handler = ava::plugin::make_core_service_proxy_handler(context, status.plugin.manifest, std::string(dynamic_resource_contribution_kind(kind)),
                                                                        "list", call_label, call_id);
      auto listed = (*process)->list_resources(kind, request.cancel_requested, std::move(proxy_handler));
      auto shutdown = (*process)->shutdown();
      if (!listed)
      {
        auto const status = plugin_command_error_is_canceled(listed.error()) ? ava::agent::ToolTimelineStatus::Canceled : ava::agent::ToolTimelineStatus::Error;
        if (auto recorded = fail(listed.error().format(), status); !recorded)
        {
          return std::unexpected(std::move(recorded.error()));
        }
        continue;
      }
      if (!shutdown)
      {
        if (auto recorded = fail(shutdown.error().format()); !recorded)
        {
          return std::unexpected(std::move(recorded.error()));
        }
        continue;
      }
      if (!listed->ok)
      {
        auto text = "plugin dynamic " + kind_name + " list failed: " + listed->content;
        if (auto recorded = fail(std::move(text)); !recorded)
        {
          return std::unexpected(std::move(recorded.error()));
        }
        continue;
      }

      for (auto const& resource : listed->resources)
      {
        entries.push_back(DynamicResourceListEntry{.plugin_id = status.plugin.manifest.id, .resource = resource});
      }
      if (auto recorded = record_dynamic_resource_result(session, request.event_sink, result, call_id, ava::agent::ToolTimelineStatus::Success,
                                                         std::to_string(listed->resources.size()) + " dynamic " + dynamic_resource_plural(kind), {}, context);
          !recorded)
      {
        return std::unexpected(std::move(recorded.error()));
      }
    }

    add_output(result, format_dynamic_resource_list_text(kind, entries, failures));
    return result;
  }

  if (subcommand == "dynamic-prompt" || subcommand == "dynamic-skill")
  {
    if (args.size() != 3)
      return usage();
    auto const kind = subcommand == "dynamic-prompt" ? ava::plugin::PluginDynamicResourceKind::Prompt : ava::plugin::PluginDynamicResourceKind::Skill;
    auto const kind_name = std::string(ava::plugin::plugin_dynamic_resource_kind_name(kind));
    auto const diagnostics = plugin_diagnostics(session);
    auto const* status = find_plugin_status(diagnostics, args[1]);
    if (!status)
    {
      add_output(result, plugin_not_found_text(diagnostics, args[1]));
      return result;
    }
    if (!status->enabled)
    {
      add_output(result, "plugin is disabled: " + sanitize_inline_text(args[1]));
      return result;
    }
    if (!ava::plugin::is_valid_dynamic_resource_name(args[2]))
    {
      add_output(result, invalid_dynamic_resource_name_text(kind, args[2]));
      return result;
    }
    if (!ava::plugin::plugin_has_capability(status->plugin.manifest, dynamic_resource_capability(kind)))
    {
      add_output(result, dynamic_resource_unsupported_text(status->plugin.manifest, kind));
      return result;
    }

    auto const call_id = ava::core::make_id("dynres");
    auto const call_label = status->plugin.manifest.id + ":" + kind_name + ":" + args[2];
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "plugin_resource", call_label); !recorded)
    {
      return std::unexpected(std::move(recorded.error()));
    }

    auto context = make_tool_context(session, request.permission_resolver);
    context.permission_tool_name = "plugin_resource";
    context.cancel_requested = request.cancel_requested;
    context.permission_request_ids = std::make_shared<std::vector<std::string>>();

    auto fail = [&](std::string text, ava::agent::ToolTimelineStatus status = ava::agent::ToolTimelineStatus::Error) -> ava::core::Result<CommandResult> {
      if (auto recorded = record_dynamic_resource_result(session, request.event_sink, result, call_id, status, text, {}, context); !recorded)
      {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, std::move(text));
      return result;
    };

    if (request.cancel_requested && request.cancel_requested())
    {
      return fail("plugin resource read canceled", ava::agent::ToolTimelineStatus::Canceled);
    }

    if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::PluginExecute, status->plugin.manifest.path, call_label,
                                                        "plugin_resource", "plugin dynamic resource read requires permission");
        !permission)
    {
      return fail(permission.error().format());
    }

    ava::plugin::PluginRunnerOptions options;
    options.workspace_dir = session.workspace_dir;
    auto process = ava::plugin::PluginProcess::start(status->plugin.manifest, options, request.cancel_requested);
    if (!process)
    {
      auto const status = plugin_command_error_is_canceled(process.error()) ? ava::agent::ToolTimelineStatus::Canceled : ava::agent::ToolTimelineStatus::Error;
      return fail(process.error().format(), status);
    }
    auto proxy_handler = ava::plugin::make_core_service_proxy_handler(context, status->plugin.manifest, std::string(dynamic_resource_contribution_kind(kind)),
                                                                      args[2], call_label, call_id);
    auto content = (*process)->read_resource(kind, args[2], request.cancel_requested, std::move(proxy_handler));
    auto shutdown = (*process)->shutdown();
    if (!content)
    {
      auto const status = plugin_command_error_is_canceled(content.error()) ? ava::agent::ToolTimelineStatus::Canceled : ava::agent::ToolTimelineStatus::Error;
      return fail(content.error().format(), status);
    }
    if (!shutdown)
      return fail(shutdown.error().format());
    if (!content->ok)
    {
      return fail("plugin dynamic " + kind_name + " " + status->plugin.manifest.id + "/" + args[2] + " failed: " + content->content);
    }

    if (auto recorded = record_dynamic_resource_result(session, request.event_sink, result, call_id, ava::agent::ToolTimelineStatus::Success, "ok",
                                                       content->content, context);
        !recorded)
    {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, format_dynamic_resource_text(status->plugin.manifest, kind, args[2], std::move(content->content)));
    return result;
  }

  if (subcommand == "enable" || subcommand == "disable")
  {
    if (args.size() != 2)
      return usage();
    bool const enabled = subcommand == "enable";
    auto const diagnostics = plugin_diagnostics(session);
    auto const* status = find_plugin_status(diagnostics, args[1]);
    if (!status)
    {
      add_output(result, plugin_not_found_text(diagnostics, args[1]));
      return result;
    }
    auto stored = ava::plugin::set_plugin_enabled(plugin_enablement_file(session), session.workspace_dir, args[1], enabled, status->plugin.scope);
    if (!stored)
    {
      add_output(result, stored.error().format());
      return result;
    }
    add_output(result, std::string(enabled ? "Enabled " : "Disabled ") + plugin_scope_text(status->plugin.scope) + " plugin " + sanitize_inline_text(args[1]) +
                           (enabled ? ". No plugin process was started." : ". No plugin process was stopped."));
    return result;
  }

  if (subcommand == "validate")
  {
    auto const path_text = plugin_validate_argument(argument);
    if (path_text.empty())
      return usage();
    auto manifest = ava::plugin::load_plugin_manifest(plugin_validate_path(session, path_text));
    if (!manifest)
    {
      add_output(result, manifest.error().format());
      return result;
    }
    add_output(result, format_valid_plugin_manifest_text(*manifest, session));
    return result;
  }

  return usage();
}

ava::core::Result<CommandResult> run_plugin_command(RuntimeSession& session, CommandRequest const& request)
{
  CommandResult result;
  result.handled = true;
  auto run_args = parse_plugin_run_arguments(command_argument(request.command, "/plugin"));
  if (!run_args)
  {
    add_output(result, run_args.error().message());
    return result;
  }

  auto const diagnostics = plugin_diagnostics(session);
  auto const* status = find_plugin_status(diagnostics, run_args->plugin_id);
  if (!status)
  {
    add_output(result, plugin_not_found_text(diagnostics, run_args->plugin_id));
    return result;
  }
  if (!status->enabled)
  {
    add_output(result, "plugin is disabled: " + sanitize_inline_text(run_args->plugin_id));
    return result;
  }
  auto const* command = find_plugin_command(status->plugin.manifest, run_args->command_name);
  if (!command)
  {
    add_output(result, "plugin command not found: " + sanitize_inline_text(run_args->command_name));
    return result;
  }

  auto context = make_tool_context(session, request.permission_resolver);
  context.permission_tool_name = "plugin_command";
  context.cancel_requested = request.cancel_requested;
  auto const call_id = ava::core::make_id("cmd");
  auto const command_label = status->plugin.manifest.id + ":" + command->name;
  if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "plugin_command", command_label); !recorded)
  {
    return std::unexpected(std::move(recorded.error()));
  }

  auto fail = [&](ava::core::Error const& error) -> ava::core::Result<CommandResult> {
    auto const text = error.format();
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "plugin_command", ava::agent::ToolTimelineStatus::Error, text);
        !recorded)
    {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, text);
    return result;
  };

  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::PluginExecute, status->plugin.manifest.path,
                                                      status->plugin.manifest.id, "plugin_command", "plugin command requires permission");
      !permission)
  {
    return fail(permission.error());
  }
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::PluginCommandRun, status->plugin.manifest.path, command_label,
                                                      "plugin_command", "plugin command requires permission");
      !permission)
  {
    return fail(permission.error());
  }

  ava::plugin::PluginRunnerOptions options;
  options.workspace_dir = session.workspace_dir;
  auto process = ava::plugin::PluginProcess::start(status->plugin.manifest, options, request.cancel_requested);
  if (!process)
    return fail(process.error());
  auto proxy_handler = ava::plugin::make_core_service_proxy_handler(context, status->plugin.manifest, "command", command->name, command_label, call_id);
  auto command_result = (*process)->call_command(command->name, run_args->arguments_json, call_id, request.cancel_requested, std::move(proxy_handler));
  auto shutdown = (*process)->shutdown();
  if (!command_result)
    return fail(command_result.error());
  if (!shutdown)
    return fail(shutdown.error());

  if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "plugin_command",
                                         command_result->ok ? ava::agent::ToolTimelineStatus::Success : ava::agent::ToolTimelineStatus::Error,
                                         command_result->ok ? "ok" : "plugin command returned error", command_result->content);
      !recorded)
  {
    return std::unexpected(std::move(recorded.error()));
  }
  add_output(result, command_result->content);
  return result;
}

}  // namespace ava::app
