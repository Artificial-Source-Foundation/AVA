#include "ava/app/command_plugin_support.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

#include "ava/app/command_format.h"
#include "ava/core/json.h"
#include "ava/plugin/enablement.h"

namespace ava::app::detail {

std::string plugin_display_path(std::filesystem::path const& path, RuntimeSession const& session)
{
  return sanitize_inline_text(display_path(path, session.current_dir));
}

ava::plugin::PluginDiscoveryOptions plugin_discovery_options(RuntimeSession const& session)
{
  return ava::plugin::PluginDiscoveryOptions{.global_plugins_dir = session.paths.ava_config_dir / "plugins",
                                             .project_plugins_dir = session.workspace_dir / ".ava" / "plugins"};
}

std::filesystem::path plugin_enablement_file(RuntimeSession const& session)
{
  return session.paths.ava_state_dir / "plugin-enablement.json";
}

ava::plugin::PluginDiagnostics plugin_diagnostics(RuntimeSession const& session)
{
  return ava::plugin::collect_plugin_diagnostics(plugin_discovery_options(session), plugin_enablement_file(session),
                                                 session.workspace_dir);
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
  if (manifest.capabilities.empty()) return "none";
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

ava::plugin::PluginCommandContribution const* find_plugin_command(ava::plugin::PluginManifest const& manifest,
                                                                  std::string_view command_name)
{
  for (auto const& command : manifest.contributes.commands) {
    if (command.name == command_name) return &command;
  }
  return nullptr;
}

std::string format_plugin_resource_list_text(ava::plugin::PluginStatus const& status, RuntimeSession const& session,
                                             std::string_view label,
                                             std::vector<ava::plugin::PluginResourceContribution> const& resources)
{
  std::ostringstream output;
  output << "Plugin " << label << " for " << sanitize_inline_text(status.plugin.manifest.id) << "\n";
  if (!status.enabled) output << "  status: disabled\n";
  if (resources.empty()) {
    output << "  none";
    return output.str();
  }
  for (auto const& resource : resources) {
    output << "  " << sanitize_inline_text(resource.name);
    if (!resource.description.empty()) output << " - " << sanitize_inline_text(resource.description);
    output << "  " << plugin_display_path(status.plugin.manifest.directory / resource.path, session) << "\n";
  }
  auto text = output.str();
  if (!text.empty() && text.back() == '\n') text.pop_back();
  return text;
}

std::string format_plugin_resource_text(ava::plugin::PluginManifest const& manifest,
                                        ava::plugin::PluginResourceContribution const& resource, std::string_view label,
                                        RuntimeSession const& session, std::string content)
{
  std::ostringstream output;
  output << "Plugin " << label << " " << sanitize_inline_text(manifest.id) << "/" << sanitize_inline_text(resource.name)
         << "\n";
  output << "  path: " << plugin_display_path(manifest.directory / resource.path, session) << "\n\n";
  output << content;
  return output.str();
}

ava::plugin::PluginStatus const* find_plugin_status(ava::plugin::PluginDiagnostics const& diagnostics,
                                                    std::string_view plugin_id)
{
  for (auto const& status : diagnostics.plugins) {
    if (status.plugin.manifest.id == plugin_id) return &status;
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
  if (has_duplicate_plugin_failure(diagnostics, plugin_id)) {
    return "plugin id is ambiguous: " + sanitize_inline_text(std::string(plugin_id)) + " (see /plugins failures)";
  }
  return "plugin not found: " + sanitize_inline_text(std::string(plugin_id));
}

std::string format_plugin_list_text(ava::plugin::PluginDiagnostics const& diagnostics, RuntimeSession const& session)
{
  std::ostringstream output;
  output << "Plugins:\n";
  if (diagnostics.plugins.empty()) {
    output << "  none discovered\n";
  } else {
    for (auto const& status : diagnostics.plugins) {
      auto const& manifest = status.plugin.manifest;
      output << "  " << sanitize_inline_text(manifest.id) << "  " << plugin_status_text(status.enabled) << "  "
             << plugin_scope_text(status.plugin.scope) << "  " << sanitize_inline_text(manifest.name) << "  "
             << sanitize_inline_text(manifest.version) << "\n";
    }
  }
  output << "\nDiscovery paths:\n";
  output << "  global: " << plugin_display_path(diagnostics.discovery_options.global_plugins_dir, session) << "\n";
  output << "  project: " << plugin_display_path(diagnostics.discovery_options.project_plugins_dir, session) << "\n";
  output << "Enablement: " << plugin_display_path(diagnostics.enablement_file, session);
  if (!diagnostics.failures.empty()) {
    output << "\nFailures: " << diagnostics.failures.size() << " (use /plugins failures)";
  }
  return output.str();
}

std::string format_plugin_failure_text(ava::plugin::PluginFailure const& failure, RuntimeSession const& session)
{
  std::string text = "  " + plugin_scope_text(failure.scope) + "  " + plugin_display_path(failure.path, session) +
                     "\n    " + sanitize_inline_text(failure.message);
  if (!failure.details.empty()) text += "\n    " + sanitize_inline_text(failure.details);
  return text;
}

std::string format_plugin_failures_text(ava::plugin::PluginDiagnostics const& diagnostics,
                                        RuntimeSession const& session)
{
  if (diagnostics.failures.empty()) return "No plugin discovery or enablement failures.";
  std::string output = "Plugin failures:\n";
  for (auto const& failure : diagnostics.failures) output += format_plugin_failure_text(failure, session) + "\n";
  if (!output.empty() && output.back() == '\n') output.pop_back();
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
  output << "  tools: "
         << (manifest.contributes.tools.empty() ? "none" : std::to_string(manifest.contributes.tools.size())) << "\n";
  for (auto const& tool : manifest.contributes.tools) {
    output << "    " << sanitize_inline_text(tool.name);
    if (!tool.description.empty()) output << " - " << sanitize_inline_text(tool.description);
    output << "\n";
  }
  output << "  commands: "
         << (manifest.contributes.commands.empty() ? "none" : std::to_string(manifest.contributes.commands.size()))
         << "\n";
  for (auto const& command : manifest.contributes.commands) {
    output << "    " << sanitize_inline_text(command.name);
    if (!command.description.empty()) output << " - " << sanitize_inline_text(command.description);
    output << "\n";
  }
  output << "  prompts: "
         << (manifest.contributes.prompts.empty() ? "none" : std::to_string(manifest.contributes.prompts.size()))
         << "\n";
  for (auto const& prompt : manifest.contributes.prompts) {
    output << "    " << sanitize_inline_text(prompt.name);
    if (!prompt.description.empty()) output << " - " << sanitize_inline_text(prompt.description);
    output << "\n";
  }
  output << "  skills: "
         << (manifest.contributes.skills.empty() ? "none" : std::to_string(manifest.contributes.skills.size())) << "\n";
  for (auto const& skill : manifest.contributes.skills) {
    output << "    " << sanitize_inline_text(skill.name);
    if (!skill.description.empty()) output << " - " << sanitize_inline_text(skill.description);
    output << "\n";
  }
  output << "  event_hooks: "
         << (manifest.contributes.event_hooks.empty() ? "none"
                                                      : std::to_string(manifest.contributes.event_hooks.size()))
         << "\n";
  output << "  note: inspection lists metadata only; no plugin process is started yet.";
  return output.str();
}

std::string format_valid_plugin_manifest_text(ava::plugin::PluginManifest const& manifest,
                                              RuntimeSession const& session)
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
  if (path.is_relative()) path = session.current_dir / path;
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
  while (index < plugins_argument.size() && std::isspace(static_cast<unsigned char>(plugins_argument[index])) != 0) {
    ++index;
  }
  while (index < plugins_argument.size() && std::isspace(static_cast<unsigned char>(plugins_argument[index])) == 0) {
    ++index;
  }
  return trim_ascii_whitespace(plugins_argument.substr(index));
}

std::optional<std::string_view> consume_token(std::string_view& text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
  if (text.empty()) return std::nullopt;
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
  while (!argument.empty() && std::isspace(static_cast<unsigned char>(argument.front())) != 0)
    argument.remove_prefix(1);
  if (!subcommand || *subcommand != "run" || !plugin_id || !command_name) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "usage: /plugin run <plugin_id> <command> [arguments_json]"));
  }
  auto arguments_json = argument.empty() ? std::string("{}") : std::string(argument);
  if (!ava::core::json::is_valid_object(arguments_json)) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin command arguments must be a JSON object"));
  }
  return PluginRunArguments{.plugin_id = std::string(*plugin_id),
                            .command_name = std::string(*command_name),
                            .arguments_json = std::move(arguments_json)};
}

}  // namespace ava::app::detail
