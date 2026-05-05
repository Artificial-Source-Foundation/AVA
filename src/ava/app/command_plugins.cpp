#include "ava/app/command_plugins.h"

#include <utility>

#include "ava/app/command_format.h"
#include "ava/app/command_plugin_support.h"
#include "ava/app/command_tools.h"
#include "ava/core/ids.h"
#include "ava/plugin/enablement.h"
#include "ava/plugin/manifest.h"
#include "ava/plugin/resources.h"
#include "ava/plugin/runner.h"

namespace ava::app {

ava::core::Result<CommandResult> run_plugins_command(RuntimeSession& session, CommandRequest const& request)
{
  CommandResult result;
  result.handled = true;
  auto const usage = [&]() {
    add_output(result, missing_argument(
                           "/plugins <list|inspect|enable|disable|validate|failures|prompts|prompt|skills|skill> ..."));
    return result;
  };

  auto const argument = command_argument(request.command, "/plugins");
  auto const args = split_command_arguments(argument);
  if (args.empty()) return usage();

  auto const& subcommand = args[0];
  if (subcommand == "list") {
    if (args.size() != 1) return usage();
    add_output(result, detail::format_plugin_list_text(detail::plugin_diagnostics(session), session));
    return result;
  }

  if (subcommand == "failures") {
    if (args.size() != 1) return usage();
    add_output(result, detail::format_plugin_failures_text(detail::plugin_diagnostics(session), session));
    return result;
  }

  if (subcommand == "inspect") {
    if (args.size() != 2) return usage();
    auto const diagnostics = detail::plugin_diagnostics(session);
    auto const* status = detail::find_plugin_status(diagnostics, args[1]);
    if (!status) {
      add_output(result, detail::plugin_not_found_text(diagnostics, args[1]));
      return result;
    }
    add_output(result, detail::format_plugin_inspect_text(*status, session));
    return result;
  }

  if (subcommand == "prompts" || subcommand == "skills") {
    if (args.size() != 2) return usage();
    auto const diagnostics = detail::plugin_diagnostics(session);
    auto const* status = detail::find_plugin_status(diagnostics, args[1]);
    if (!status) {
      add_output(result, detail::plugin_not_found_text(diagnostics, args[1]));
      return result;
    }
    auto const& resources = subcommand == "prompts" ? status->plugin.manifest.contributes.prompts
                                                    : status->plugin.manifest.contributes.skills;
    add_output(result, detail::format_plugin_resource_list_text(*status, session, subcommand, resources));
    return result;
  }

  if (subcommand == "prompt" || subcommand == "skill") {
    if (args.size() != 3) return usage();
    auto const diagnostics = detail::plugin_diagnostics(session);
    auto const* status = detail::find_plugin_status(diagnostics, args[1]);
    if (!status) {
      add_output(result, detail::plugin_not_found_text(diagnostics, args[1]));
      return result;
    }
    auto const& resources = subcommand == "prompt" ? status->plugin.manifest.contributes.prompts
                                                   : status->plugin.manifest.contributes.skills;
    auto const* resource = ava::plugin::find_plugin_resource(resources, args[2]);
    if (!resource) {
      add_output(result, std::string(subcommand) + " not found: " + sanitize_inline_text(args[2]));
      return result;
    }
    auto content = ava::plugin::read_plugin_resource_text(status->plugin.manifest, *resource);
    if (!content) {
      add_output(result, content.error().format());
      return result;
    }
    add_output(result, detail::format_plugin_resource_text(status->plugin.manifest, *resource, subcommand, session,
                                                           std::move(*content)));
    return result;
  }

  if (subcommand == "enable" || subcommand == "disable") {
    if (args.size() != 2) return usage();
    bool const enabled = subcommand == "enable";
    auto const diagnostics = detail::plugin_diagnostics(session);
    auto const* status = detail::find_plugin_status(diagnostics, args[1]);
    if (!status) {
      add_output(result, detail::plugin_not_found_text(diagnostics, args[1]));
      return result;
    }
    auto stored = ava::plugin::set_plugin_enabled(detail::plugin_enablement_file(session), session.workspace_dir,
                                                  args[1], enabled, status->plugin.scope);
    if (!stored) {
      add_output(result, stored.error().format());
      return result;
    }
    add_output(result, std::string(enabled ? "Enabled " : "Disabled ") +
                           detail::plugin_scope_text(status->plugin.scope) + " plugin " +
                           sanitize_inline_text(args[1]) +
                           (enabled ? ". No plugin process was started." : ". No plugin process was stopped."));
    return result;
  }

  if (subcommand == "validate") {
    auto const path_text = detail::plugin_validate_argument(argument);
    if (path_text.empty()) return usage();
    auto manifest = ava::plugin::load_plugin_manifest(detail::plugin_validate_path(session, path_text));
    if (!manifest) {
      add_output(result, manifest.error().format());
      return result;
    }
    add_output(result, detail::format_valid_plugin_manifest_text(*manifest, session));
    return result;
  }

  return usage();
}

ava::core::Result<CommandResult> run_plugin_command(RuntimeSession& session, CommandRequest const& request)
{
  CommandResult result;
  result.handled = true;
  auto run_args = detail::parse_plugin_run_arguments(command_argument(request.command, "/plugin"));
  if (!run_args) {
    add_output(result, run_args.error().message());
    return result;
  }

  auto const diagnostics = detail::plugin_diagnostics(session);
  auto const* status = detail::find_plugin_status(diagnostics, run_args->plugin_id);
  if (!status) {
    add_output(result, detail::plugin_not_found_text(diagnostics, run_args->plugin_id));
    return result;
  }
  if (!status->enabled) {
    add_output(result, "plugin is disabled: " + sanitize_inline_text(run_args->plugin_id));
    return result;
  }
  auto const* command = detail::find_plugin_command(status->plugin.manifest, run_args->command_name);
  if (!command) {
    add_output(result, "plugin command not found: " + sanitize_inline_text(run_args->command_name));
    return result;
  }

  auto context = make_tool_context(session, request.permission_resolver);
  context.permission_tool_name = "plugin_command";
  auto const call_id = ava::core::make_id("cmd");
  auto const command_label = status->plugin.manifest.id + ":" + command->name;
  if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "plugin_command", command_label);
      !recorded) {
    return std::unexpected(std::move(recorded.error()));
  }

  auto fail = [&](ava::core::Error const& error) -> ava::core::Result<CommandResult> {
    auto const text = error.format();
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
