#include "ava/app/commands.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include "ava/app/plugin_event_hooks.h"
#include "ava/config/auth.h"
#include "ava/config/provider_profiles.h"
#include "ava/config/reasoning_profiles.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/mcp/config.h"
#include "ava/mcp/stdio_client.h"
#include "ava/mcp/tool_broker.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/enablement.h"
#include "ava/plugin/manifest.h"
#include "ava/plugin/runner.h"
#include "ava/provider/registry.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/stats.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/keybindings.h"

namespace ava::app {
namespace {

void add_output(CommandResult& result, std::string text) { result.output.push_back(std::move(text)); }

std::vector<CommandHotkey> default_command_hotkeys() {
  std::vector<CommandHotkey> hotkeys;
  for (const auto& item : ava::tui::key_binding_help_items(ava::tui::default_key_bindings())) {
    hotkeys.push_back(CommandHotkey{.action = item.action, .description = item.description, .keys = item.keys});
  }
  return hotkeys;
}

std::vector<CommandHotkey> effective_hotkeys(const std::vector<CommandHotkey>& hotkeys) {
  return hotkeys.empty() ? default_command_hotkeys() : hotkeys;
}

std::string model_key(std::string_view provider_id, std::string_view model_id) {
  return std::string(provider_id) + "\n" + std::string(model_id);
}

std::vector<ava::config::ModelInfo> effective_models(const ava::config::ModelRegistry& registry) {
  std::vector<ava::config::ModelInfo> models;
  std::vector<std::string> seen;
  for (auto model = registry.models.rbegin(); model != registry.models.rend(); ++model) {
    const auto key = model_key(model->provider_id, model->model_id);
    if (std::ranges::find(seen, key) != seen.end()) continue;
    seen.push_back(key);
    models.push_back(*model);
  }
  std::reverse(models.begin(), models.end());
  return models;
}

std::string optional_bool_text(const std::optional<bool>& value) {
  if (!value) return "unknown";
  return *value ? "yes" : "no";
}

std::string joined_strings(const std::vector<std::string>& values, std::string_view separator) {
  std::string output;
  for (const auto& value : values) {
    if (!output.empty()) output += separator;
    output += value;
  }
  return output;
}

std::string format_models_text(const RuntimeSession& session, const ava::config::ModelRegistry& registry) {
  const auto providers = ava::provider::builtin_provider_registry();
  auto models = effective_models(registry);
  bool current_in_catalog = false;

  std::string output = "Models:\n";
  output += "current " + session.model.provider_id + "/" + session.model.model_id + "\n";
  output += session.reasoning ? "reasoning current " + session.reasoning->level + "\n" : "reasoning current default\n";
  output += "default " + registry.default_provider_id + "/" + registry.default_model_id + "\n";
  for (const auto& model : models) {
    current_in_catalog = current_in_catalog ||
                         (model.provider_id == session.model.provider_id && model.model_id == session.model.model_id);
    const bool registered = providers.contains(model.provider_id);
    output += model.provider_id == session.model.provider_id && model.model_id == session.model.model_id ? "* " : "  ";
    output += model.provider_id + "/" + model.model_id;
    if (!model.display_name.empty()) output += "  " + model.display_name;
    output += registered ? "\n" : "  unavailable: provider not registered\n";
    output += "    tools=" + optional_bool_text(model.supports_tools) +
              " streaming=" + optional_bool_text(model.supports_streaming) +
              " reasoning=" + optional_bool_text(model.supports_reasoning) +
              " usage=" + optional_bool_text(model.reports_usage);
    if (model.context_window_tokens) output += " context=" + std::to_string(*model.context_window_tokens);
    if (model.max_output_tokens) output += " max_output=" + std::to_string(*model.max_output_tokens);
    output += '\n';
    if (!model.reasoning_levels.empty()) {
      output += "    reasoning levels: " + joined_strings(model.reasoning_levels, ", ") + "\n";
    }
    if (model.supports_reasoning.value_or(false)) {
      output += "    reasoning params: " + ava::config::reasoning_parameter_text(model) + "\n";
      if (!model.reasoning_format.empty()) output += "    reasoning format: " + model.reasoning_format + "\n";
    }
  }
  if (!current_in_catalog) {
    output += "* " + session.model.provider_id + "/" + session.model.model_id + "  current model outside catalog\n";
  }
  output +=
      "\nModel switching is not enabled here. In the TUI, Ctrl+T cycles the current model's declared "
      "reasoning levels using the provider/model-specific reasoning parameters above.";
  return output;
}

std::string aliases_text(const CommandCatalogEntry& entry) {
  std::string text;
  for (const auto& alias : entry.aliases) {
    if (!text.empty()) text += ", ";
    text += alias;
  }
  return text;
}

std::string command_display(const CommandCatalogEntry& entry) {
  auto text = entry.command;
  if (!entry.hint.empty()) text += " " + entry.hint;
  const auto aliases = aliases_text(entry);
  if (!aliases.empty()) text += " (alias: " + aliases + ")";
  return text;
}

std::string command_rows(bool enabled) {
  std::size_t width = 0;
  std::vector<const CommandCatalogEntry*> entries;
  for (const auto& entry : command_catalog()) {
    if (entry.enabled != enabled) continue;
    entries.push_back(&entry);
    width = std::max(width, command_display(entry).size());
  }

  std::string output;
  for (const auto* entry : entries) {
    auto display = command_display(*entry);
    output += "  " + display;
    if (display.size() < width) output += std::string(width - display.size(), ' ');
    output += "  " + entry->description;
    if (!entry->enabled && !entry->disabled_reason.empty()) output += " — disabled: " + entry->disabled_reason;
    output += '\n';
  }
  return output;
}

std::string display_path(const std::filesystem::path& path, const std::filesystem::path& base) {
  std::error_code error;
  const auto relative = std::filesystem::relative(path, base, error);
  if (!error) return relative.generic_string();
  return path.generic_string();
}

std::string sanitize_inline_text(std::string text) {
  for (auto& ch : text) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) ch = '?';
  }
  return text;
}

std::string plugin_display_path(const std::filesystem::path& path, const RuntimeSession& session) {
  return sanitize_inline_text(display_path(path, session.current_dir));
}

ava::plugin::PluginDiscoveryOptions plugin_discovery_options(const RuntimeSession& session) {
  return ava::plugin::PluginDiscoveryOptions{.global_plugins_dir = session.paths.ava_config_dir / "plugins",
                                             .project_plugins_dir = session.workspace_dir / ".ava" / "plugins"};
}

std::filesystem::path plugin_enablement_file(const RuntimeSession& session) {
  return session.paths.ava_state_dir / "plugin-enablement.json";
}

ava::plugin::PluginDiagnostics plugin_diagnostics(const RuntimeSession& session) {
  return ava::plugin::collect_plugin_diagnostics(plugin_discovery_options(session), plugin_enablement_file(session),
                                                 session.workspace_dir);
}

ava::mcp::McpConfigLoadOptions mcp_config_options(const RuntimeSession& session) {
  auto options = ava::mcp::default_mcp_config_options(session.workspace_dir);
  options.global_config_file = session.paths.ava_config_dir / "mcp.json";
  options.project_config_file = session.workspace_dir / ".ava" / "mcp.json";
  return options;
}

std::string plugin_scope_text(ava::plugin::PluginScope scope) { return std::string(ava::plugin::to_string(scope)); }

std::string mcp_scope_text(ava::mcp::McpServerScope scope) { return std::string(ava::mcp::to_string(scope)); }

std::string plugin_status_text(bool enabled) { return enabled ? "enabled" : "disabled"; }

std::string mcp_status_text(bool enabled) { return enabled ? "enabled" : "disabled"; }

std::string plugin_capabilities_text(const ava::plugin::PluginManifest& manifest) {
  if (manifest.capabilities.empty()) return "none";
  std::vector<std::string> capabilities;
  capabilities.reserve(manifest.capabilities.size());
  for (const auto& capability : manifest.capabilities) capabilities.push_back(sanitize_inline_text(capability));
  return joined_strings(capabilities, ", ");
}

std::string plugin_entrypoint_text(const ava::plugin::PluginEntrypoint& entrypoint) {
  std::string text = sanitize_inline_text(entrypoint.command);
  for (const auto& arg : entrypoint.args) text += " " + sanitize_inline_text(arg);
  return text;
}

const ava::plugin::PluginCommandContribution* find_plugin_command(const ava::plugin::PluginManifest& manifest,
                                                                  std::string_view command_name) {
  for (const auto& command : manifest.contributes.commands) {
    if (command.name == command_name) return &command;
  }
  return nullptr;
}

const ava::plugin::PluginResourceContribution* find_plugin_resource(
    const std::vector<ava::plugin::PluginResourceContribution>& resources, std::string_view name) {
  for (const auto& resource : resources) {
    if (resource.name == name) return &resource;
  }
  return nullptr;
}

bool path_is_within(const std::filesystem::path& base, const std::filesystem::path& target) {
  if (target == base) return true;
  std::error_code relative_error;
  const auto relative = std::filesystem::relative(target, base, relative_error);
  if (relative_error || relative.empty()) return false;
  return *relative.begin() != "..";
}

ava::core::Result<std::filesystem::path> plugin_resource_path(const ava::plugin::PluginManifest& manifest,
                                                              const ava::plugin::PluginResourceContribution& resource) {
  std::error_code base_error;
  const auto canonical_base = std::filesystem::weakly_canonical(manifest.directory, base_error);
  const auto base_path = base_error ? std::filesystem::absolute(manifest.directory).lexically_normal() : canonical_base;
  const auto raw_target = manifest.directory / resource.path;
  std::error_code target_error;
  const auto canonical_target = std::filesystem::weakly_canonical(raw_target, target_error);
  const auto target_path = target_error ? std::filesystem::absolute(raw_target).lexically_normal() : canonical_target;
  if (!path_is_within(base_path, target_path)) {
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin resource path escapes plugin directory");
    error.with_context("plugin", manifest.id);
    error.with_context("resource", resource.name);
    error.with_context("path", resource.path);
    return std::unexpected(std::move(error));
  }
  return target_path;
}

ava::core::Result<std::string> read_plugin_resource(const ava::plugin::PluginManifest& manifest,
                                                    const ava::plugin::PluginResourceContribution& resource) {
  constexpr std::size_t max_resource_bytes = 64 * 1024;
  auto path = plugin_resource_path(manifest, resource);
  if (!path) return std::unexpected(std::move(path.error()));

  std::error_code type_error;
  if (!std::filesystem::is_regular_file(*path, type_error)) {
    auto error = ava::core::Error(type_error ? ava::core::ErrorCategory::Io : ava::core::ErrorCategory::InvalidArgument,
                                  "plugin resource must be a regular file");
    error.with_context("plugin", manifest.id);
    error.with_context("resource", resource.name);
    error.with_context("path", path->string());
    if (type_error) error.with_context("cause", type_error.message());
    return std::unexpected(std::move(error));
  }

  std::error_code size_error;
  const auto size = std::filesystem::file_size(*path, size_error);
  if (size_error) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect plugin resource size")
                               .with_context("plugin", manifest.id)
                               .with_context("resource", resource.name)
                               .with_context("path", path->string())
                               .with_context("cause", size_error.message()));
  }
  if (size > max_resource_bytes) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin resource exceeds size cap")
            .with_context("plugin", manifest.id)
            .with_context("resource", resource.name)
            .with_context("path", path->string())
            .with_context("max_bytes", std::to_string(max_resource_bytes)));
  }

  std::ifstream file(*path, std::ios::binary);
  if (!file) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to open plugin resource")
                               .with_context("plugin", manifest.id)
                               .with_context("resource", resource.name)
                               .with_context("path", path->string()));
  }
  std::string contents;
  std::array<char, 4096> buffer{};
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = file.gcount();
    if (count <= 0) continue;
    if (contents.size() + static_cast<std::size_t>(count) > max_resource_bytes) {
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "plugin resource exceeds size cap while reading")
              .with_context("plugin", manifest.id)
              .with_context("resource", resource.name)
              .with_context("path", path->string())
              .with_context("max_bytes", std::to_string(max_resource_bytes)));
    }
    contents.append(buffer.data(), static_cast<std::size_t>(count));
  }
  if (file.bad()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to read plugin resource")
                               .with_context("plugin", manifest.id)
                               .with_context("resource", resource.name)
                               .with_context("path", path->string()));
  }
  return contents;
}

std::string format_plugin_resource_list_text(const ava::plugin::PluginStatus& status, const RuntimeSession& session,
                                             std::string_view label,
                                             const std::vector<ava::plugin::PluginResourceContribution>& resources) {
  std::ostringstream output;
  output << "Plugin " << label << " for " << sanitize_inline_text(status.plugin.manifest.id) << "\n";
  if (!status.enabled) output << "  status: disabled\n";
  if (resources.empty()) {
    output << "  none";
    return output.str();
  }
  for (const auto& resource : resources) {
    output << "  " << sanitize_inline_text(resource.name);
    if (!resource.description.empty()) output << " - " << sanitize_inline_text(resource.description);
    output << "  " << plugin_display_path(status.plugin.manifest.directory / resource.path, session) << "\n";
  }
  auto text = output.str();
  if (!text.empty() && text.back() == '\n') text.pop_back();
  return text;
}

std::string format_plugin_resource_text(const ava::plugin::PluginManifest& manifest,
                                        const ava::plugin::PluginResourceContribution& resource, std::string_view label,
                                        const RuntimeSession& session, std::string content) {
  std::ostringstream output;
  output << "Plugin " << label << " " << sanitize_inline_text(manifest.id) << "/" << sanitize_inline_text(resource.name)
         << "\n";
  output << "  path: " << plugin_display_path(manifest.directory / resource.path, session) << "\n\n";
  output << content;
  return output.str();
}

const ava::plugin::PluginStatus* find_plugin_status(const ava::plugin::PluginDiagnostics& diagnostics,
                                                    std::string_view plugin_id) {
  for (const auto& status : diagnostics.plugins) {
    if (status.plugin.manifest.id == plugin_id) return &status;
  }
  return nullptr;
}

bool has_duplicate_plugin_failure(const ava::plugin::PluginDiagnostics& diagnostics, std::string_view plugin_id) {
  const auto details = "plugin=" + std::string(plugin_id);
  return std::ranges::any_of(diagnostics.failures, [&](const ava::plugin::PluginFailure& failure) {
    return failure.message == "duplicate plugin id discovered" && failure.details == details;
  });
}

std::string plugin_not_found_text(const ava::plugin::PluginDiagnostics& diagnostics, std::string_view plugin_id) {
  if (has_duplicate_plugin_failure(diagnostics, plugin_id)) {
    return "plugin id is ambiguous: " + sanitize_inline_text(std::string(plugin_id)) + " (see /plugins failures)";
  }
  return "plugin not found: " + sanitize_inline_text(std::string(plugin_id));
}

std::string format_plugin_list_text(const ava::plugin::PluginDiagnostics& diagnostics, const RuntimeSession& session) {
  std::ostringstream output;
  output << "Plugins:\n";
  if (diagnostics.plugins.empty()) {
    output << "  none discovered\n";
  } else {
    for (const auto& status : diagnostics.plugins) {
      const auto& manifest = status.plugin.manifest;
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

std::string format_plugin_failure_text(const ava::plugin::PluginFailure& failure, const RuntimeSession& session) {
  std::string text = "  " + plugin_scope_text(failure.scope) + "  " + plugin_display_path(failure.path, session) +
                     "\n    " + sanitize_inline_text(failure.message);
  if (!failure.details.empty()) text += "\n    " + sanitize_inline_text(failure.details);
  return text;
}

std::string format_plugin_failures_text(const ava::plugin::PluginDiagnostics& diagnostics,
                                        const RuntimeSession& session) {
  if (diagnostics.failures.empty()) return "No plugin discovery or enablement failures.";
  std::string output = "Plugin failures:\n";
  for (const auto& failure : diagnostics.failures) output += format_plugin_failure_text(failure, session) + "\n";
  if (!output.empty() && output.back() == '\n') output.pop_back();
  return output;
}

std::string format_plugin_inspect_text(const ava::plugin::PluginStatus& status, const RuntimeSession& session) {
  const auto& manifest = status.plugin.manifest;
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
  for (const auto& tool : manifest.contributes.tools) {
    output << "    " << sanitize_inline_text(tool.name);
    if (!tool.description.empty()) output << " - " << sanitize_inline_text(tool.description);
    output << "\n";
  }
  output << "  commands: "
         << (manifest.contributes.commands.empty() ? "none" : std::to_string(manifest.contributes.commands.size()))
         << "\n";
  for (const auto& command : manifest.contributes.commands) {
    output << "    " << sanitize_inline_text(command.name);
    if (!command.description.empty()) output << " - " << sanitize_inline_text(command.description);
    output << "\n";
  }
  output << "  prompts: "
         << (manifest.contributes.prompts.empty() ? "none" : std::to_string(manifest.contributes.prompts.size()))
         << "\n";
  for (const auto& prompt : manifest.contributes.prompts) {
    output << "    " << sanitize_inline_text(prompt.name);
    if (!prompt.description.empty()) output << " - " << sanitize_inline_text(prompt.description);
    output << "\n";
  }
  output << "  skills: "
         << (manifest.contributes.skills.empty() ? "none" : std::to_string(manifest.contributes.skills.size())) << "\n";
  for (const auto& skill : manifest.contributes.skills) {
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

std::string format_valid_plugin_manifest_text(const ava::plugin::PluginManifest& manifest,
                                              const RuntimeSession& session) {
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

std::filesystem::path plugin_validate_path(const RuntimeSession& session, std::string_view path_text) {
  auto path = std::filesystem::path(std::string(path_text));
  if (path.is_relative()) path = session.current_dir / path;
  return path.lexically_normal();
}

const ava::mcp::McpServerConfig* find_mcp_server(const ava::mcp::McpConfig& config, std::string_view server_id) {
  for (const auto& server : config.servers) {
    if (server.id == server_id) return &server;
  }
  return nullptr;
}

std::string mcp_command_text(const ava::mcp::McpServerConfig& server) {
  std::string text = sanitize_inline_text(server.command);
  for (const auto& arg : server.args) text += " " + sanitize_inline_text(arg);
  return text;
}

std::string mcp_config_path_text(const std::filesystem::path& path, const RuntimeSession& session) {
  if (path.empty()) return "none";
  return plugin_display_path(path, session);
}

std::string format_mcp_server_not_found_text(const ava::mcp::McpConfig& config, std::string_view server_id) {
  std::string output = "MCP server not found: " + sanitize_inline_text(std::string(server_id));
  if (!config.servers.empty()) {
    output += "\nConfigured MCP servers:";
    for (const auto& server : config.servers) output += "\n  " + sanitize_inline_text(server.id);
  }
  return output;
}

std::string format_mcp_list_text(const ava::mcp::McpConfig& config, const RuntimeSession& session) {
  std::ostringstream output;
  output << "MCP servers:\n";
  output << "  global config: " << mcp_config_path_text(config.global_config_file, session) << "\n";
  output << "  project config: " << mcp_config_path_text(config.project_config_file, session) << "\n";
  if (config.servers.empty()) {
    output << "  none";
    return output.str();
  }
  for (const auto& server : config.servers) {
    output << "  " << sanitize_inline_text(server.id) << "  " << mcp_status_text(server.enabled) << "  "
           << mcp_scope_text(server.scope) << "  " << sanitize_inline_text(server.name) << "\n";
  }
  auto text = output.str();
  if (!text.empty() && text.back() == '\n') text.pop_back();
  return text;
}

std::string format_mcp_inspect_text(const ava::mcp::McpServerConfig& server, const RuntimeSession& session) {
  std::ostringstream output;
  output << "MCP server " << sanitize_inline_text(server.id) << "\n";
  output << "  name: " << sanitize_inline_text(server.name) << "\n";
  output << "  status: " << mcp_status_text(server.enabled) << "\n";
  output << "  scope: " << mcp_scope_text(server.scope) << "\n";
  output << "  config: " << mcp_config_path_text(server.source_path, session) << "\n";
  output << "  command: " << mcp_command_text(server) << "\n";
  output << "  note: stdio MCP servers are launched per discovery or tool call and are not kept resident.";
  return output.str();
}

std::string format_mcp_tools_text(const ava::mcp::McpServerConfig& server,
                                  const ava::mcp::McpInitialization& initialization,
                                  const std::vector<ava::mcp::McpToolDescription>& tools) {
  std::ostringstream output;
  output << "MCP tools for " << sanitize_inline_text(server.id) << "\n";
  output << "  server: " << sanitize_inline_text(initialization.server_name);
  if (!initialization.server_version.empty()) output << " " << sanitize_inline_text(initialization.server_version);
  output << "\n";
  if (tools.empty()) {
    output << "  none";
    return output.str();
  }
  for (const auto& tool : tools) {
    output << "  " << sanitize_inline_text(tool.name) << "  "
           << sanitize_inline_text(ava::mcp::mcp_model_tool_name(server.id, tool.name));
    if (!tool.description.empty()) output << "  " << sanitize_inline_text(tool.description);
    output << "\n";
  }
  auto text = output.str();
  if (!text.empty() && text.back() == '\n') text.pop_back();
  return text;
}

ava::tools::ToolContext make_tool_context(RuntimeSession& session,
                                          ava::permissions::PermissionResolver permission_resolver) {
  return ava::tools::ToolContext{
      .workspace_dir = session.workspace_dir,
      .spill_dir = session.store.session_path().parent_path() / "spill",
      .mode = session.mode,
      .permission_resolver = std::move(permission_resolver),
      .permission_audit_sink =
          [&store = session.store](const ava::tools::PermissionAuditEvent& event) -> ava::core::VoidResult {
        return store.append(ava::session::SessionEntry{
            .id = ava::core::make_id("entry"),
            .parent_id = "",
            .type = ava::session::EntryType::PermissionDecision,
            .timestamp = ava::session::now_timestamp(),
            .data_json = ava::tools::permission_audit_data_json(event),
        });
      },
      .plugin_global_plugins_dir = session.paths.ava_config_dir / "plugins",
      .plugin_project_plugins_dir = session.workspace_dir / ".ava" / "plugins",
      .plugin_enablement_file = session.paths.ava_state_dir / "plugin-enablement.json",
      .mcp_global_config_file = session.paths.ava_config_dir / "mcp.json",
      .mcp_project_config_file = session.workspace_dir / ".ava" / "mcp.json"};
}

ava::core::VoidResult append_mode_change(ava::session::SessionStore& store, ava::agent::Mode mode) {
  return store.append(ava::session::SessionEntry{
      .id = ava::core::make_id("entry"),
      .parent_id = "",
      .type = ava::session::EntryType::ModeChange,
      .timestamp = ava::session::now_timestamp(),
      .data_json = "{\"mode\":\"" + ava::agent::to_string(mode) + "\"}",
  });
}

RuntimeEvent command_event(const RuntimeSession& session, RuntimeEventType type) {
  RuntimeEvent event;
  event.type = type;
  event.timestamp = ava::session::now_timestamp();
  event.session_id = session.store.session_id();
  event.mode = session.mode;
  event.provider_id = session.model.provider_id;
  event.model_id = session.model.model_id;
  return event;
}

ava::core::VoidResult emit_tool_event(const RuntimeSession& session, const RuntimeEventSink& sink,
                                      const ava::agent::ToolTimelineEntry& entry) {
  auto event =
      command_event(session, entry.status == ava::agent::ToolTimelineStatus::Running ? RuntimeEventType::ToolStart
                                                                                     : RuntimeEventType::ToolResult);
  event.call_id = entry.call_id;
  event.tool_name = entry.name;
  event.status = ava::agent::to_string(entry.status);
  event.text = entry.status == ava::agent::ToolTimelineStatus::Running ? entry.argument_summary : entry.result_summary;
  return emit_event(sink, event);
}

ava::core::VoidResult record_tool_event(const RuntimeSession& session, const RuntimeEventSink& sink,
                                        CommandResult& result, ava::agent::ToolTimelineEntry entry) {
  if (auto emitted = emit_tool_event(session, sink, entry); !emitted)
    return std::unexpected(std::move(emitted.error()));
  result.tool_timeline.push_back(std::move(entry));
  return {};
}

ava::core::VoidResult record_tool_start(const RuntimeSession& session, const RuntimeEventSink& sink,
                                        CommandResult& result, const std::string& call_id, std::string name,
                                        std::string argument_summary) {
  return record_tool_event(session, sink, result,
                           ava::agent::ToolTimelineEntry{.status = ava::agent::ToolTimelineStatus::Running,
                                                         .call_id = call_id,
                                                         .name = std::move(name),
                                                         .argument_summary = std::move(argument_summary)});
}

ava::core::VoidResult record_tool_result(const RuntimeSession& session, const RuntimeEventSink& sink,
                                         CommandResult& result, const std::string& call_id, std::string name,
                                         ava::agent::ToolTimelineStatus status, std::string result_summary) {
  return record_tool_event(
      session, sink, result,
      ava::agent::ToolTimelineEntry{
          .status = status, .call_id = call_id, .name = std::move(name), .result_summary = std::move(result_summary)});
}

bool starts_with_command(std::string_view line, std::string_view command) noexcept {
  return line == command || (line.starts_with(command) && line.size() > command.size() && line[command.size()] == ' ');
}

std::string missing_argument(std::string_view usage) { return "usage: " + std::string(usage); }

std::string command_argument(std::string_view line, std::string_view command) {
  if (line.size() <= command.size() || line[command.size()] != ' ') return {};
  return std::string(line.substr(command.size() + 1));
}

std::string trim_ascii_whitespace(std::string_view text) {
  std::size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) ++start;
  auto end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
  return std::string(text.substr(start, end - start));
}

std::vector<std::string> split_command_arguments(std::string_view text) {
  std::vector<std::string> parts;
  std::size_t index = 0;
  while (index < text.size()) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) ++index;
    const auto start = index;
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) == 0) ++index;
    if (start < index) parts.emplace_back(text.substr(start, index - start));
  }
  return parts;
}

std::string plugin_validate_argument(std::string_view plugins_argument) {
  std::size_t index = 0;
  while (index < plugins_argument.size() && std::isspace(static_cast<unsigned char>(plugins_argument[index])) != 0) {
    ++index;
  }
  while (index < plugins_argument.size() && std::isspace(static_cast<unsigned char>(plugins_argument[index])) == 0) {
    ++index;
  }
  return trim_ascii_whitespace(plugins_argument.substr(index));
}

ava::core::Result<CommandResult> run_mcp_command(RuntimeSession& session, const CommandRequest& request) {
  CommandResult result;
  result.handled = true;
  const auto usage = [&]() {
    add_output(result, missing_argument("/mcp <list|inspect|tools|restart> [server_id]"));
    return result;
  };

  const auto argument = command_argument(request.command, "/mcp");
  const auto args = split_command_arguments(argument);
  if (args.empty()) return usage();

  auto config = ava::mcp::load_mcp_config(mcp_config_options(session));
  if (!config) {
    add_output(result, config.error().format());
    return result;
  }

  const auto& subcommand = args[0];
  if (subcommand == "list") {
    if (args.size() != 1) return usage();
    add_output(result, format_mcp_list_text(*config, session));
    return result;
  }

  if ((subcommand == "inspect" || subcommand == "tools" || subcommand == "restart") && args.size() != 2) {
    return usage();
  }

  if (subcommand == "inspect") {
    const auto* server = find_mcp_server(*config, args[1]);
    if (!server) {
      add_output(result, format_mcp_server_not_found_text(*config, args[1]));
      return result;
    }
    add_output(result, format_mcp_inspect_text(*server, session));
    return result;
  }

  if (subcommand == "restart") {
    const auto* server = find_mcp_server(*config, args[1]);
    if (!server) {
      add_output(result, format_mcp_server_not_found_text(*config, args[1]));
      return result;
    }
    add_output(result, "MCP server " + sanitize_inline_text(server->id) +
                           " uses per-request stdio processes; the next discovery or tool call will launch a fresh "
                           "process.");
    return result;
  }

  if (subcommand == "tools") {
    const auto* server = find_mcp_server(*config, args[1]);
    if (!server) {
      add_output(result, format_mcp_server_not_found_text(*config, args[1]));
      return result;
    }
    if (!server->enabled) {
      add_output(result, "MCP server is disabled: " + sanitize_inline_text(server->id));
      return result;
    }

    auto context = make_tool_context(session, request.permission_resolver);
    context.permission_tool_name = "mcp_tools";
    const auto call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "mcp_tools", server->id);
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }

    auto fail = [&](const ava::core::Error& error) -> ava::core::Result<CommandResult> {
      const auto text = error.format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "mcp_tools",
                                             ava::agent::ToolTimelineStatus::Error, text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, text);
      return result;
    };

    if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerLaunch,
                                                        server->source_path, mcp_command_text(*server), "mcp_tools",
                                                        "MCP server launch requires permission");
        !permission) {
      return fail(permission.error());
    }
    if (auto permission =
            ava::tools::ensure_permission(context, ava::permissions::Operation::McpServerConnect, server->source_path,
                                          server->id, "mcp_tools", "MCP server connection requires permission");
        !permission) {
      return fail(permission.error());
    }

    ava::mcp::McpStdioClientOptions options;
    options.workspace_dir = session.workspace_dir;
    auto client = ava::mcp::McpStdioClient::start(*server, options);
    if (!client) return fail(client.error());
    auto tools = (*client)->list_tools();
    const auto initialization = (*client)->initialization();
    auto shutdown = (*client)->shutdown();
    if (!tools) return fail(tools.error());
    if (!shutdown) return fail(shutdown.error());

    if (auto recorded =
            record_tool_result(session, request.event_sink, result, call_id, "mcp_tools",
                               ava::agent::ToolTimelineStatus::Success, std::to_string(tools->size()) + " tools");
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, format_mcp_tools_text(*server, initialization, *tools));
    return result;
  }

  return usage();
}

ava::core::Result<CommandResult> run_plugins_command(RuntimeSession& session, const CommandRequest& request) {
  CommandResult result;
  result.handled = true;
  const auto usage = [&]() {
    add_output(result, missing_argument(
                           "/plugins <list|inspect|enable|disable|validate|failures|prompts|prompt|skills|skill> ..."));
    return result;
  };

  const auto argument = command_argument(request.command, "/plugins");
  const auto args = split_command_arguments(argument);
  if (args.empty()) return usage();

  const auto& subcommand = args[0];
  if (subcommand == "list") {
    if (args.size() != 1) return usage();
    add_output(result, format_plugin_list_text(plugin_diagnostics(session), session));
    return result;
  }

  if (subcommand == "failures") {
    if (args.size() != 1) return usage();
    add_output(result, format_plugin_failures_text(plugin_diagnostics(session), session));
    return result;
  }

  if (subcommand == "inspect") {
    if (args.size() != 2) return usage();
    const auto diagnostics = plugin_diagnostics(session);
    const auto* status = find_plugin_status(diagnostics, args[1]);
    if (!status) {
      add_output(result, plugin_not_found_text(diagnostics, args[1]));
      return result;
    }
    add_output(result, format_plugin_inspect_text(*status, session));
    return result;
  }

  if (subcommand == "prompts" || subcommand == "skills") {
    if (args.size() != 2) return usage();
    const auto diagnostics = plugin_diagnostics(session);
    const auto* status = find_plugin_status(diagnostics, args[1]);
    if (!status) {
      add_output(result, plugin_not_found_text(diagnostics, args[1]));
      return result;
    }
    const auto& resources = subcommand == "prompts" ? status->plugin.manifest.contributes.prompts
                                                    : status->plugin.manifest.contributes.skills;
    add_output(result, format_plugin_resource_list_text(*status, session, subcommand, resources));
    return result;
  }

  if (subcommand == "prompt" || subcommand == "skill") {
    if (args.size() != 3) return usage();
    const auto diagnostics = plugin_diagnostics(session);
    const auto* status = find_plugin_status(diagnostics, args[1]);
    if (!status) {
      add_output(result, plugin_not_found_text(diagnostics, args[1]));
      return result;
    }
    const auto& resources = subcommand == "prompt" ? status->plugin.manifest.contributes.prompts
                                                   : status->plugin.manifest.contributes.skills;
    const auto* resource = find_plugin_resource(resources, args[2]);
    if (!resource) {
      add_output(result, std::string(subcommand) + " not found: " + sanitize_inline_text(args[2]));
      return result;
    }
    auto content = read_plugin_resource(status->plugin.manifest, *resource);
    if (!content) {
      add_output(result, content.error().format());
      return result;
    }
    add_output(result, format_plugin_resource_text(status->plugin.manifest, *resource, subcommand, session,
                                                   std::move(*content)));
    return result;
  }

  if (subcommand == "enable" || subcommand == "disable") {
    if (args.size() != 2) return usage();
    const bool enabled = subcommand == "enable";
    const auto diagnostics = plugin_diagnostics(session);
    const auto* status = find_plugin_status(diagnostics, args[1]);
    if (!status) {
      add_output(result, plugin_not_found_text(diagnostics, args[1]));
      return result;
    }
    auto stored = ava::plugin::set_plugin_enabled(plugin_enablement_file(session), session.workspace_dir, args[1],
                                                  enabled, status->plugin.scope);
    if (!stored) {
      add_output(result, stored.error().format());
      return result;
    }
    add_output(result, std::string(enabled ? "Enabled " : "Disabled ") + plugin_scope_text(status->plugin.scope) +
                           " plugin " + sanitize_inline_text(args[1]) +
                           (enabled ? ". No plugin process was started." : ". No plugin process was stopped."));
    return result;
  }

  if (subcommand == "validate") {
    const auto path_text = plugin_validate_argument(argument);
    if (path_text.empty()) return usage();
    auto manifest = ava::plugin::load_plugin_manifest(plugin_validate_path(session, path_text));
    if (!manifest) {
      add_output(result, manifest.error().format());
      return result;
    }
    add_output(result, format_valid_plugin_manifest_text(*manifest, session));
    return result;
  }

  return usage();
}

struct PluginRunArguments {
  std::string plugin_id;
  std::string command_name;
  std::string arguments_json = "{}";
};

std::optional<std::string_view> consume_token(std::string_view& text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
  if (text.empty()) return std::nullopt;
  const auto end = text.find_first_of(" \t\r\n");
  const auto token = text.substr(0, end == std::string_view::npos ? text.size() : end);
  text.remove_prefix(token.size());
  return token;
}

ava::core::Result<PluginRunArguments> parse_plugin_run_arguments(std::string_view argument) {
  const auto subcommand = consume_token(argument);
  const auto plugin_id = consume_token(argument);
  const auto command_name = consume_token(argument);
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

ava::core::Result<CommandResult> run_plugin_command(RuntimeSession& session, const CommandRequest& request) {
  CommandResult result;
  result.handled = true;
  auto run_args = parse_plugin_run_arguments(command_argument(request.command, "/plugin"));
  if (!run_args) {
    add_output(result, run_args.error().message());
    return result;
  }

  const auto diagnostics = plugin_diagnostics(session);
  const auto* status = find_plugin_status(diagnostics, run_args->plugin_id);
  if (!status) {
    add_output(result, plugin_not_found_text(diagnostics, run_args->plugin_id));
    return result;
  }
  if (!status->enabled) {
    add_output(result, "plugin is disabled: " + sanitize_inline_text(run_args->plugin_id));
    return result;
  }
  const auto* command = find_plugin_command(status->plugin.manifest, run_args->command_name);
  if (!command) {
    add_output(result, "plugin command not found: " + sanitize_inline_text(run_args->command_name));
    return result;
  }

  auto context = make_tool_context(session, request.permission_resolver);
  context.permission_tool_name = "plugin_command";
  const auto call_id = ava::core::make_id("cmd");
  const auto command_label = status->plugin.manifest.id + ":" + command->name;
  if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "plugin_command", command_label);
      !recorded) {
    return std::unexpected(std::move(recorded.error()));
  }

  auto fail = [&](const ava::core::Error& error) -> ava::core::Result<CommandResult> {
    const auto text = error.format();
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "plugin_command",
                                           ava::agent::ToolTimelineStatus::Error, text);
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, text);
    return result;
  };

  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::PluginExecute,
                                                      status->plugin.manifest.path, status->plugin.manifest.id,
                                                      "plugin_command", "plugin command requires permission");
      !permission) {
    return fail(permission.error());
  }
  if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::PluginCommandRun,
                                                      status->plugin.manifest.path, command_label, "plugin_command",
                                                      "plugin command requires permission");
      !permission) {
    return fail(permission.error());
  }

  ava::plugin::PluginRunnerOptions options;
  options.workspace_dir = session.workspace_dir;
  auto process = ava::plugin::PluginProcess::start(status->plugin.manifest, options);
  if (!process) return fail(process.error());
  auto command_result = (*process)->call_command(command->name, run_args->arguments_json, call_id);
  auto shutdown = (*process)->shutdown();
  if (!command_result) return fail(command_result.error());
  if (!shutdown) return fail(shutdown.error());

  if (auto recorded = record_tool_result(
          session, request.event_sink, result, call_id, "plugin_command",
          command_result->ok ? ava::agent::ToolTimelineStatus::Success : ava::agent::ToolTimelineStatus::Error,
          command_result->ok ? "ok" : "plugin command returned error");
      !recorded) {
    return std::unexpected(std::move(recorded.error()));
  }
  add_output(result, command_result->content);
  return result;
}

bool is_valid_connect_provider_id(std::string_view provider_id) {
  if (provider_id.empty() || provider_id.size() > 128) return false;
  return std::ranges::all_of(provider_id, [](char ch) {
    const auto uch = static_cast<unsigned char>(ch);
    return std::isalnum(uch) != 0 || ch == '-' || ch == '_';
  });
}

std::string trim_secret_text(std::string secret) {
  auto is_edge_space = [](char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; };
  auto first = std::find_if_not(secret.begin(), secret.end(), is_edge_space);
  auto last = std::find_if_not(secret.rbegin(), secret.rend(), is_edge_space).base();
  if (first >= last) return {};
  return std::string(first, last);
}

std::string format_cost_usd(long double value) {
  std::ostringstream output;
  output << '$' << std::fixed << std::setprecision(6) << value;
  return output.str();
}

template <typename Value>
void append_known_value(std::ostringstream& output, bool& wrote_any, std::string_view label,
                        const std::optional<Value>& value) {
  if (!value) return;
  if (wrote_any) output << ' ';
  output << label << '=' << *value;
  wrote_any = true;
}

std::string shorten_middle(std::string text, std::size_t max_columns) {
  if (text.size() <= max_columns || max_columns < 8) return text;
  const auto front = (max_columns - 3) / 2;
  const auto back = max_columns - 3 - front;
  return text.substr(0, front) + "..." + text.substr(text.size() - back);
}

std::string compact_workspace_label(const std::filesystem::path& workspace) {
  const auto filename = workspace.filename().generic_string();
  if (!filename.empty()) return shorten_middle(filename, 32);
  return shorten_middle(workspace.generic_string(), 48);
}

std::string compact_cwd_label(const std::filesystem::path& cwd, const std::filesystem::path& workspace) {
  auto text = display_path(cwd, workspace);
  if (text.empty()) text = ".";
  return shorten_middle(std::move(text), 48);
}

std::string known_values_text(const ava::session::SessionStats& stats) {
  std::ostringstream output;
  bool wrote_any = false;
  append_known_value(output, wrote_any, "input", stats.input_tokens);
  append_known_value(output, wrote_any, "output", stats.output_tokens);
  append_known_value(output, wrote_any, "reasoning", stats.reasoning_tokens);
  append_known_value(output, wrote_any, "cache_read", stats.cache_read_tokens);
  append_known_value(output, wrote_any, "cache_write", stats.cache_write_tokens);
  append_known_value(output, wrote_any, "total", stats.total_tokens);
  return wrote_any ? output.str() : std::string("unavailable");
}

std::string estimated_bytes_text(const ava::session::SessionStats& stats) {
  std::ostringstream output;
  bool wrote_any = false;
  append_known_value(output, wrote_any, "input", stats.estimated_input_bytes);
  append_known_value(output, wrote_any, "output", stats.estimated_output_bytes);
  append_known_value(output, wrote_any, "total", stats.estimated_total_bytes);
  return wrote_any ? output.str() : std::string("unavailable");
}

std::string cost_text(const ava::session::SessionStats& stats) {
  if (stats.cost_complete) return stats.total_cost_usd ? format_cost_usd(*stats.total_cost_usd) : "unavailable";
  if (stats.known_cost_usd) {
    return "at least " + format_cost_usd(*stats.known_cost_usd) + " (" + std::to_string(stats.unknown_cost_entries) +
           " unknown)";
  }
  return "incomplete (" + std::to_string(stats.unknown_cost_entries) + " unknown)";
}

std::string format_session_stats_text(const RuntimeSession& session, const ava::session::SessionStats& stats) {
  std::ostringstream output;
  output << "Session stats\n";
  output << "  session: " << shorten_middle(session.store.session_id(), 32) << "   entries: " << stats.entry_count
         << '\n';
  output << "  model: " << session.model.provider_id << '/' << session.model.model_id
         << "   mode: " << ava::agent::to_string(session.mode) << '\n';
  output << "  workspace: " << compact_workspace_label(session.workspace_dir)
         << "   cwd: " << compact_cwd_label(session.current_dir, session.workspace_dir) << '\n';
  if (!stats.first_timestamp.empty() || !stats.last_timestamp.empty()) {
    output << "  time: " << (stats.first_timestamp.empty() ? "unknown" : stats.first_timestamp) << " -> "
           << (stats.last_timestamp.empty() ? "unknown" : stats.last_timestamp) << '\n';
  }

  output << "\nMessages:\n";
  output << "  user " << stats.counts.user_message << "   assistant " << stats.counts.assistant_message << "   tools "
         << stats.counts.tool_call << '/' << stats.counts.tool_result << "   permissions "
         << stats.counts.permission_decision << '\n';
  output << "  compactions " << stats.counts.compaction << "   mode/model " << stats.counts.mode_change << '/'
         << stats.counts.model_change << "   errors/cancels " << stats.counts.error << '/' << stats.counts.cancel
         << '\n';

  output << "\nUsage:\n";
  output << "  tokens: " << known_values_text(stats) << '\n';
  output << "  est bytes: " << estimated_bytes_text(stats) << '\n';
  output << "  cost: " << cost_text(stats) << "   usage entries exact/estimated " << stats.exact_usage_entries << '/'
         << stats.estimated_usage_entries << '\n';

  output << "\nHints:\n";
  output << "  export: /export   resume: ava --session " << session.store.session_id();
  return output.str();
}

std::string credential_type_value(std::string_view method) {
  if (method == "api" || method == "api-key" || method == "apikey" || method == "key" || method == "api_key") {
    return "api_key";
  }
  if (method == "oauth" || method == "oauth-token" || method == "oauth_token" || method == "bearer" ||
      method == "token") {
    return "oauth";
  }
  return {};
}

std::string credential_type_label(std::string_view credential_type) {
  return credential_type == "oauth" ? "OAuth bearer token" : "API key";
}

std::string selected_or_custom_answer(const ava::agent::QuestionAnswer& answer) {
  if (!answer.custom_text.empty()) return answer.custom_text;
  if (!answer.selected_options.empty()) return answer.selected_options.front();
  return {};
}

ava::core::Result<ava::agent::QuestionAnswer> ask_connect_question(const CommandRequest& request,
                                                                   ava::agent::QuestionPrompt prompt) {
  if (!request.question_resolver) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                         "/connect requires the interactive TUI; use `ava connect <provider> --api-key-stdin`, "
                         "`--api-key-env ENV`, `--oauth-token-stdin`, or `--oauth-token-env ENV` for headless setup"));
  }
  return request.question_resolver(prompt);
}

std::vector<ava::agent::QuestionOption> provider_options(const RuntimeSession& session) {
  std::vector<ava::agent::QuestionOption> options;
  auto add = [&](std::string value, std::string label) {
    if (std::ranges::any_of(options, [&](const auto& option) { return option.value == value; })) return;
    options.push_back(ava::agent::QuestionOption{.value = std::move(value), .label = std::move(label)});
  };
  if (!session.model.provider_id.empty()) {
    add(session.model.provider_id, ava::config::provider_display_name(session.model.provider_id) + " (current)");
  }
  for (const auto& profile : ava::config::builtin_provider_profiles()) {
    auto label = profile.display_name;
    if (!profile.connect_detail.empty()) label += " - " + profile.connect_detail;
    add(profile.provider_id, std::move(label));
  }
  return options;
}

ava::core::Result<std::string> resolve_connect_provider(RuntimeSession& session, const CommandRequest& request,
                                                        const std::vector<std::string>& args) {
  if (!args.empty()) return args[0];
  auto answer = ask_connect_question(request, ava::agent::QuestionPrompt{.header = "Connect a provider",
                                                                         .question = "Select provider",
                                                                         .options = provider_options(session),
                                                                         .multiple = false,
                                                                         .allow_custom = true,
                                                                         .secret = false,
                                                                         .modal = true,
                                                                         .searchable = true});
  if (!answer) return std::unexpected(std::move(answer.error()));
  return selected_or_custom_answer(*answer);
}

ava::core::Result<std::string> resolve_connect_credential_type(const CommandRequest& request,
                                                               const std::vector<std::string>& args) {
  if (args.size() >= 2) {
    const auto credential_type = credential_type_value(args[1]);
    if (!credential_type.empty()) return credential_type;
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "/connect credential type must be api-key or oauth");
    error.with_context("usage", "/connect [provider] [api-key|oauth]");
    return std::unexpected(std::move(error));
  }

  auto answer = ask_connect_question(
      request, ava::agent::QuestionPrompt{
                   .header = "Connect a provider",
                   .question = "Choose credential type",
                   .options = {ava::agent::QuestionOption{.value = "api_key", .label = "API key"},
                               ava::agent::QuestionOption{.value = "oauth", .label = "OAuth bearer token"}},
                   .multiple = false,
                   .allow_custom = false,
                   .secret = false,
                   .modal = true});
  if (!answer) return std::unexpected(std::move(answer.error()));
  return selected_or_custom_answer(*answer);
}

ava::core::Result<std::string> prompt_connect_secret(const CommandRequest& request, std::string_view provider_id,
                                                     std::string_view credential_type) {
  auto answer = ask_connect_question(
      request, ava::agent::QuestionPrompt{
                   .header = "Connect a provider",
                   .question = "Paste " + credential_type_label(credential_type) + " for " + std::string(provider_id),
                   .options = {},
                   .multiple = false,
                   .allow_custom = true,
                   .secret = true,
                   .modal = true});
  if (!answer) return std::unexpected(std::move(answer.error()));
  auto secret = trim_secret_text(selected_or_custom_answer(*answer));
  if (secret.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "credential was empty"));
  }
  return secret;
}

ava::core::Result<CommandResult> run_connect_command(RuntimeSession& session, const CommandRequest& request) {
  CommandResult result;
  result.handled = true;
  const auto args = split_command_arguments(command_argument(request.command, "/connect"));
  if (args.size() > 2) {
    add_output(result, missing_argument("/connect [provider] [api-key|oauth]"));
    return result;
  }

  auto provider_id = resolve_connect_provider(session, request, args);
  if (!provider_id) {
    add_output(result, provider_id.error().format());
    return result;
  }
  if (!is_valid_connect_provider_id(*provider_id)) {
    add_output(result, "provider id must contain only letters, numbers, '-' or '_'");
    return result;
  }

  auto credential_type = resolve_connect_credential_type(request, args);
  if (!credential_type) {
    add_output(result, credential_type.error().format());
    return result;
  }

  auto secret = prompt_connect_secret(request, *provider_id, *credential_type);
  if (!secret) {
    add_output(result, secret.error().format());
    return result;
  }

  auto stored = ava::config::store_provider_credential(
      session.paths, ava::config::ProviderCredential{.provider_id = *provider_id,
                                                     .access_token = *secret,
                                                     .credential_type = *credential_type,
                                                     .account_id = "",
                                                     .source = "connect"});
  if (!stored) {
    add_output(result, stored.error().format());
    return result;
  }
  add_output(result, "Stored " + *provider_id + " " + credential_type_label(*credential_type) + " credential at " +
                         session.paths.auth_file.string());
  return result;
}

ava::core::Result<CommandResult> run_tool_command(RuntimeSession& session, CommandRequest& request) {
  CommandResult result;
  result.handled = true;
  const auto& line = request.command;
  auto context = make_tool_context(session, request.permission_resolver);

  if (line.starts_with("/read ")) {
    const auto argument = line.substr(6);
    const auto call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "read", argument); !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    const auto output = ava::tools::read_file(context, session.current_dir / argument);
    if (!output) {
      const auto text = output.error().format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "read",
                                             ava::agent::ToolTimelineStatus::Error, text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, text);
      return result;
    }
    std::string text = output->content;
    if (output->truncated) {
      text += "\n[truncated " + std::to_string(output->output_bytes) + '/' + std::to_string(output->total_bytes) +
              " bytes]";
    }
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "read",
                                           ava::agent::ToolTimelineStatus::Success,
                                           std::to_string(output->output_bytes) + " bytes");
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, std::move(text));
    return result;
  }

  if (line.starts_with("/glob ")) {
    const auto pattern = line.substr(6);
    const auto call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "glob", pattern); !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    const auto glob = ava::tools::glob_files(context, pattern);
    if (!glob) {
      const auto text = glob.error().format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "glob",
                                             ava::agent::ToolTimelineStatus::Error, text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, text);
      return result;
    }
    std::string output;
    for (const auto& path : glob->paths) output += display_path(path, session.current_dir) + '\n';
    if (glob->truncated) {
      output += "[truncated " + std::to_string(glob->paths.size()) + '/' + std::to_string(glob->total_matches) +
                " matches]\n";
    }
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "glob",
                                           ava::agent::ToolTimelineStatus::Success,
                                           std::to_string(glob->paths.size()) + " matches");
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, std::move(output));
    return result;
  }

  if (line.starts_with("/grep ")) {
    const auto rest = line.substr(6);
    const auto split = rest.find(' ');
    const auto pattern = split == std::string::npos ? rest : rest.substr(0, split);
    const auto include = split == std::string::npos ? std::string("**/*") : rest.substr(split + 1);
    const auto call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "grep", pattern); !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    const auto grep = ava::tools::grep_files(context, pattern, include);
    if (!grep) {
      const auto text = grep.error().format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "grep",
                                             ava::agent::ToolTimelineStatus::Error, text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, text);
      return result;
    }
    std::string output;
    for (const auto& match : grep->matches) {
      output +=
          display_path(match.path, session.current_dir) + ':' + std::to_string(match.line_number) + ": " + match.line;
      if (match.line_truncated) output += " [line truncated]";
      output += '\n';
    }
    if (grep->truncated) {
      output += "[truncated " + std::to_string(grep->matches.size()) + '/' + std::to_string(grep->total_matches) +
                " matches]\n";
    }
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "grep",
                                           ava::agent::ToolTimelineStatus::Success,
                                           std::to_string(grep->matches.size()) + " matches");
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, std::move(output));
    return result;
  }

  if (line.starts_with("/write ")) {
    const auto rest = line.substr(7);
    const auto split = rest.find(' ');
    if (split == std::string::npos) {
      add_output(result, missing_argument("/write <path> <text>"));
      return result;
    }
    const auto path_text = rest.substr(0, split);
    const auto text = rest.substr(split + 1);
    const auto call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "write", path_text);
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    const auto write = ava::tools::write_file(context, session.current_dir / path_text, text);
    if (!write) {
      const auto error_text = write.error().format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "write",
                                             ava::agent::ToolTimelineStatus::Error, error_text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, error_text);
      return result;
    }
    const auto output = "wrote " + std::to_string(write->bytes_written) + " bytes to " + write->path.string();
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "write",
                                           ava::agent::ToolTimelineStatus::Success, output);
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, output);
    return result;
  }

  if (line.starts_with("/bash ")) {
    const auto command = line.substr(6);
    const auto call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "bash", command); !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    const auto bash = ava::tools::run_bash(context, command);
    if (!bash) {
      const auto text = bash.error().format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "bash",
                                             ava::agent::ToolTimelineStatus::Error, text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, text);
      return result;
    }
    std::string output = "exit: " + std::to_string(bash->exit_code);
    if (bash->timed_out) output += " (timed out)";
    if (bash->truncated) {
      output += " (output truncated to last " + std::to_string(bash->output.size()) + '/' +
                std::to_string(bash->total_bytes) + " bytes)";
    }
    output += '\n' + bash->output;
    if (auto recorded = record_tool_result(
            session, request.event_sink, result, call_id, "bash",
            bash->exit_code == 0 ? ava::agent::ToolTimelineStatus::Success : ava::agent::ToolTimelineStatus::Error,
            "exit " + std::to_string(bash->exit_code));
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, std::move(output));
    return result;
  }

  result.handled = false;
  return result;
}

}  // namespace

bool is_backend_command(std::string_view line) noexcept { return find_command_catalog_entry(line) != nullptr; }

std::string command_hotkeys_text(const std::vector<CommandHotkey>& hotkeys) {
  const auto items = effective_hotkeys(hotkeys);
  std::size_t action_width = 0;
  std::size_t keys_width = 0;
  for (const auto& item : items) {
    action_width = std::max(action_width, item.action.size());
    keys_width = std::max(keys_width, item.keys.size());
  }

  std::string output = "Hotkeys:\n";
  for (const auto& item : items) {
    output += "  " + item.action;
    if (item.action.size() < action_width) output += std::string(action_width - item.action.size(), ' ');
    output += "  " + item.keys;
    if (item.keys.size() < keys_width) output += std::string(keys_width - item.keys.size(), ' ');
    output += "  " + item.description + '\n';
  }
  return output;
}

std::string command_help_text(const std::vector<CommandHotkey>& hotkeys) {
  std::string output = "Commands:\n";
  output += command_rows(true);
  output += "\nUnavailable commands:\n";
  output += command_rows(false);
  output += '\n';
  output += command_hotkeys_text(hotkeys);
  if (!output.empty() && output.back() == '\n') output.pop_back();
  return output;
}

ava::core::Result<CommandResult> run_command(RuntimeSession& session, CommandRequest request) {
  CommandResult result;
  if (request.command.empty()) return result;

  const auto* entry = find_command_catalog_entry(request.command);
  if (!entry) return result;
  request.command = normalize_command_line(request.command, *entry);

  if (!entry->enabled) {
    result.handled = true;
    add_output(result, entry->command + " is disabled: " + entry->disabled_reason);
    return result;
  }

  // RPC command execution already serializes session-store access around run_command; reacquiring
  // the same mutex from event-hook permission audits would deadlock nested command events.
  request.event_sink = make_plugin_event_observer_sink(
      plugin_event_observer_options(session, request.permission_resolver, nullptr), std::move(request.event_sink));

  if (request.command == "/quit" || request.command == "/exit") {
    result.handled = true;
    result.quit = true;
    return result;
  }
  if (request.command == "/help") {
    result.handled = true;
    add_output(result, command_help_text(request.hotkeys));
    return result;
  }
  if (request.command == "/hotkeys") {
    result.handled = true;
    add_output(result, command_hotkeys_text(request.hotkeys));
    return result;
  }
  if (request.command == "/details") {
    result.handled = true;
    add_output(result, "Tool details are a TUI display toggle. Use /details inside the TUI to switch views.");
    return result;
  }
  if (request.command == "/models") {
    result.handled = true;
    auto registry = ava::config::load_model_registry(session.paths);
    if (!registry) return std::unexpected(std::move(registry.error()));
    add_output(result, format_models_text(session, *registry));
    return result;
  }
  if (starts_with_command(request.command, "/connect")) {
    return run_connect_command(session, request);
  }
  if (starts_with_command(request.command, "/mcp")) {
    return run_mcp_command(session, request);
  }
  if (starts_with_command(request.command, "/plugins")) {
    return run_plugins_command(session, request);
  }
  if (starts_with_command(request.command, "/plugin")) {
    return run_plugin_command(session, request);
  }
  if (request.command == "/sessions") {
    result.handled = true;
    auto sessions = ava::session::SessionStore::list_sessions(session.workspace_dir, session.paths.sessions_dir);
    if (!sessions) {
      add_output(result, sessions.error().format());
      return result;
    }
    if (sessions->empty()) {
      add_output(result, "No sessions for this workspace.");
      return result;
    }
    std::string output;
    for (const auto& summary : *sessions) {
      output += summary.session_id + "  entries=" + std::to_string(summary.entry_count);
      if (!summary.last_updated.empty()) output += "  updated=" + summary.last_updated;
      output += '\n';
    }
    add_output(result, std::move(output));
    return result;
  }
  if (request.command == "/mode") {
    result.handled = true;
    const auto new_mode = ava::agent::toggle_mode(session.mode);
    auto prompt_state = select_runtime_prompt_state(session, new_mode);
    if (!prompt_state) return std::unexpected(std::move(prompt_state.error()));
    if (auto appended = append_mode_change(session.store, new_mode); !appended) {
      return std::unexpected(std::move(appended.error()));
    }
    apply_runtime_prompt_state(session, std::move(*prompt_state));
    add_output(result, "mode switched to " + ava::agent::to_string(session.mode));
    return result;
  }
  if (request.command == "/context") {
    result.handled = true;
    if (session.context_sources.empty()) {
      add_output(result, "No context sources loaded.");
      return result;
    }
    std::string output;
    for (const auto& source : session.context_sources) {
      output += ava::context::to_string(source.source_type) + "  " + source.path.string() +
                "  bytes=" + std::to_string(source.byte_count) + '\n';
    }
    add_output(result, std::move(output));
    return result;
  }
  if (request.command == "/stats") {
    result.handled = true;
    auto entries = session.store.load();
    if (!entries) {
      add_output(result, entries.error().format());
      return result;
    }
    add_output(result, format_session_stats_text(session, ava::session::compute_session_stats(*entries)));
    return result;
  }
  if (starts_with_command(request.command, "/compact")) {
    result.handled = true;
    auto fail_compaction = [&](ava::core::Error error) -> ava::core::Result<CommandResult> {
      if (request.propagate_compaction_errors) return std::unexpected(std::move(error));
      add_output(result, error.format());
      return result;
    };
    const auto instructions = command_argument(request.command, "/compact");
    if (!request.compaction_summary_generator) {
      return fail_compaction(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                              "/compact requires provider-backed summary generation"));
    }
    auto config = ava::session::load_compaction_config(session.paths);
    if (!config) {
      return fail_compaction(std::move(config.error()));
    }

    constexpr std::size_t max_compaction_attempts = 2;
    std::size_t last_snapshot_entries = 0;
    std::size_t last_current_entries = 0;
    for (std::size_t attempt = 0; attempt < max_compaction_attempts; ++attempt) {
      ava::core::Result<std::vector<ava::session::SessionEntry>> entries =
          std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "session entries were not loaded"));
      if (request.session_mutex) {
        std::lock_guard lock(*request.session_mutex);
        entries = session.store.load();
      } else {
        entries = session.store.load();
      }
      if (!entries) {
        return fail_compaction(std::move(entries.error()));
      }
      const auto estimated_tokens = ava::session::estimate_session_tokens(*entries);
      auto summary = request.compaction_summary_generator(*entries, *config, instructions, estimated_tokens);
      if (!summary) {
        return fail_compaction(std::move(summary.error()));
      }
      if (summary->empty()) {
        return fail_compaction(ava::core::Error(ava::core::ErrorCategory::Provider,
                                                "compaction summary generation returned an empty summary"));
      }
      if (summary->size() > config->max_summary_bytes) {
        auto error =
            ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "generated compaction summary is too large");
        error.with_context("max_summary_bytes", std::to_string(config->max_summary_bytes));
        error.with_context("summary_bytes", std::to_string(summary->size()));
        return fail_compaction(std::move(error));
      }

      bool snapshot_stale = false;
      auto validate_and_append = [&]() -> ava::core::VoidResult {
        auto current_entries = session.store.load();
        if (!current_entries) return std::unexpected(std::move(current_entries.error()));
        if (!same_session_snapshot(*entries, *current_entries)) {
          snapshot_stale = true;
          last_snapshot_entries = entries->size();
          last_current_entries = current_entries->size();
          return {};
        }
        return ava::session::append_manual_compaction(
            session.store, ava::session::ManualCompactionRequest{.summary = *summary,
                                                                 .instructions = instructions,
                                                                 .config = *config,
                                                                 .estimated_tokens = estimated_tokens,
                                                                 .threshold_tokens = 0,
                                                                 .trigger = "manual",
                                                                 .recent_context = ""});
      };
      ava::core::VoidResult appended;
      if (request.session_mutex) {
        std::lock_guard lock(*request.session_mutex);
        appended = validate_and_append();
      } else {
        appended = validate_and_append();
      }
      if (!appended) {
        return fail_compaction(std::move(appended.error()));
      }
      if (!snapshot_stale) {
        add_output(result, "compaction summary recorded");
        return result;
      }
    }
    return fail_compaction(stale_compaction_snapshot_error("manual", last_snapshot_entries, last_current_entries));
  }
  if (request.command == "/export") {
    result.handled = true;
    auto entries = session.store.load();
    if (!entries) {
      add_output(result, entries.error().format());
      return result;
    }
    add_output(result, ava::session::format_session_markdown(*entries));
    return result;
  }

  if (entry->hint.empty() && starts_with_command(request.command, entry->command)) {
    result.handled = true;
    add_output(result, missing_argument(entry->command));
    return result;
  }

  if (request.command == "/glob") {
    result.handled = true;
    add_output(result, missing_argument("/glob <pattern>"));
    return result;
  }
  if (request.command == "/grep") {
    result.handled = true;
    add_output(result, missing_argument("/grep <text> [glob]"));
    return result;
  }
  if (request.command == "/read") {
    result.handled = true;
    add_output(result, missing_argument("/read <path>"));
    return result;
  }
  if (request.command == "/write") {
    result.handled = true;
    add_output(result, missing_argument("/write <path> <text>"));
    return result;
  }
  if (request.command == "/bash") {
    result.handled = true;
    add_output(result, missing_argument("/bash <command>"));
    return result;
  }

  return run_tool_command(session, request);
}

}  // namespace ava::app
