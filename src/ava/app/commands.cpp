#include "ava/app/commands.h"

#include <filesystem>
#include <utility>

#include "ava/core/ids.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"

namespace ava::app {
namespace {

void add_output(CommandResult& result, std::string text) { result.output.push_back(std::move(text)); }

std::string display_path(const std::filesystem::path& path, const std::filesystem::path& base) {
  std::error_code error;
  const auto relative = std::filesystem::relative(path, base, error);
  if (!error) return relative.generic_string();
  return path.generic_string();
}

ava::tools::ToolContext make_tool_context(RuntimeSession& session,
                                           ava::permissions::PermissionResolver permission_resolver) {
  return ava::tools::ToolContext{.workspace_dir = session.workspace_dir,
                                  .mode = session.mode,
                                  .permission_resolver = std::move(permission_resolver),
                                  .permission_audit_sink = [&store = session.store](
                                                               const ava::tools::PermissionAuditEvent& event)
                                      -> ava::core::VoidResult {
                                    return store.append(ava::session::SessionEntry{
                                        .id = ava::core::make_id("entry"),
                                        .parent_id = "",
                                        .type = ava::session::EntryType::PermissionDecision,
                                        .timestamp = ava::session::now_timestamp(),
                                        .data_json = ava::tools::permission_audit_data_json(event),
                                    });
                                  }};
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
                                        CommandResult& result,
                                        const std::string& call_id, std::string name, std::string argument_summary) {
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

bool is_backend_command(std::string_view line) noexcept {
  return line == "/quit" || line == "/exit" || line == "/help" || line == "/sessions" || line == "/mode" ||
         line == "/context" || line == "/export" || starts_with_command(line, "/compact") ||
         line.starts_with("/read ") || line.starts_with("/glob ") || line.starts_with("/grep ") ||
         line.starts_with("/write ") || line.starts_with("/bash ");
}

std::string command_help_text() {
  return "Commands:\n  /help                 Show this help\n  /mode                 Toggle build/plan mode\n  "
         "/sessions             List sessions for this workspace\n  /context              List loaded context "
         "sources\n  "
         "/compact [text]       Record a manual compaction entry\n  /export               Export this session as "
         "markdown\n  "
         "/glob <pattern>       List files matching a glob pattern\n  /grep <text> [glob]   Search matching files "
         "for literal text\n  /read <path>          Read a file through the permissioned read tool\n  "
         "/write <path> <txt>   Write text through the permissioned write tool\n  /bash <command>       Run a "
         "permissioned shell command\n  /quit                 Exit";
}

ava::core::Result<CommandResult> run_command(RuntimeSession& session, CommandRequest request) {
  CommandResult result;
  if (request.command.empty()) return result;

  if (request.command == "/quit" || request.command == "/exit") {
    result.handled = true;
    result.quit = true;
    return result;
  }
  if (request.command == "/help") {
    result.handled = true;
    add_output(result, command_help_text());
    return result;
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
  if (starts_with_command(request.command, "/compact")) {
    result.handled = true;
    const auto instructions = command_argument(request.command, "/compact");
    auto entries = session.store.load();
    if (!entries) {
      add_output(result, entries.error().format());
      return result;
    }
    auto config = ava::session::load_compaction_config(session.paths);
    if (!config) {
      add_output(result, config.error().format());
      return result;
    }
    const auto estimated_tokens = ava::session::estimate_session_tokens(*entries);
    auto appended = ava::session::append_manual_compaction(
        session.store,
        ava::session::ManualCompactionRequest{
            .summary = "", .instructions = instructions, .config = *config, .estimated_tokens = estimated_tokens});
    if (!appended) {
      add_output(result, appended.error().format());
      return result;
    }
    add_output(result, "manual compaction recorded");
    return result;
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

  return run_tool_command(session, request);
}

}  // namespace ava::app
