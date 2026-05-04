#include "ava/app/command_plugins.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

#include "ava/app/command_format.h"
#include "ava/app/command_tools.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/enablement.h"
#include "ava/plugin/manifest.h"
#include "ava/plugin/runner.h"

namespace ava::app {
namespace {

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

std::string plugin_scope_text(ava::plugin::PluginScope scope) { return std::string(ava::plugin::to_string(scope)); }

std::string plugin_status_text(bool enabled) { return enabled ? "enabled" : "disabled"; }

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

std::string trim_ascii_whitespace(std::string_view text) {
  std::size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) ++start;
  auto end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
  return std::string(text.substr(start, end - start));
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

}  // namespace

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
          command_result->ok ? "ok" : "plugin command returned error", command_result->content);
      !recorded) {
    return std::unexpected(std::move(recorded.error()));
  }
  add_output(result, command_result->content);
  return result;
}

}  // namespace ava::app
