#include "ava/app/commands.h"

#include <string_view>
#include <utility>

#include "ava/app/command_connect.h"
#include "ava/app/command_format.h"
#include "ava/app/command_help.h"
#include "ava/app/command_mcp.h"
#include "ava/app/command_models.h"
#include "ava/app/command_plugins.h"
#include "ava/app/command_sessions.h"
#include "ava/app/command_tools.h"
#include "ava/app/plugin_event_hooks.h"

namespace ava::app {
namespace {

bool starts_with_command(std::string_view line, std::string_view command) noexcept {
  return line == command || (line.starts_with(command) && line.size() > command.size() && line[command.size()] == ' ');
}

CommandResult handled_text(std::string text) {
  CommandResult result;
  result.handled = true;
  add_output(result, std::move(text));
  return result;
}

}  // namespace

bool is_backend_command(std::string_view line) noexcept { return find_command_catalog_entry(line) != nullptr; }

ava::core::Result<CommandResult> run_command(RuntimeSession& session, CommandRequest request) {
  CommandResult result;
  if (request.command.empty()) return result;

  const auto* entry = find_command_catalog_entry(request.command);
  if (!entry) return result;
  request.command = normalize_command_line(request.command, *entry);

  if (!entry->enabled) {
    return handled_text(entry->command + " is disabled: " + entry->disabled_reason);
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
    return handled_text(command_help_text(request.hotkeys));
  }
  if (request.command == "/hotkeys") {
    return handled_text(command_hotkeys_text(request.hotkeys));
  }
  if (request.command == "/details") {
    return handled_text("Tool details are a TUI display toggle. Use /details inside the TUI to switch views.");
  }
  if (request.command == "/thinking") {
    return handled_text("Thinking visibility is a TUI display toggle. It does not change provider reasoning mode.");
  }
  if (starts_with_command(request.command, "/models")) {
    return run_models_command(session, command_argument(request.command, "/models"));
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
  if (starts_with_command(request.command, "/sessions")) {
    return run_sessions_command(session, command_argument(request.command, "/sessions"));
  }
  if (request.command == "/mode") {
    return run_mode_command(session);
  }
  if (starts_with_command(request.command, "/context")) {
    return run_context_command(session, command_argument(request.command, "/context"));
  }
  if (request.command == "/stats" || request.command == "/status") {
    return run_stats_command(session);
  }
  if (starts_with_command(request.command, "/compact")) {
    return run_compact_command(session, request);
  }
  if (request.command == "/export") {
    return run_export_command(session);
  }

  if (entry->hint.empty() && starts_with_command(request.command, entry->command)) {
    return handled_text(missing_argument(entry->command));
  }

  if (request.command == "/glob") {
    return handled_text(missing_argument("/glob <pattern>"));
  }
  if (request.command == "/grep") {
    return handled_text(missing_argument("/grep <text> [glob]"));
  }
  if (request.command == "/read") {
    return handled_text(missing_argument("/read <path>"));
  }
  if (request.command == "/write") {
    return handled_text(missing_argument("/write <path> <text>"));
  }
  if (request.command == "/bash") {
    return handled_text(missing_argument("/bash <command>"));
  }

  return run_tool_command(session, request);
}

}  // namespace ava::app
