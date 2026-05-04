#include "ava/app/command_tools.h"

#include <utility>

#include "ava/app/command_format.h"
#include "ava/core/ids.h"
#include "ava/session/session_store.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/search_tools.h"

namespace ava::app {
namespace {

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

}  // namespace

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

}  // namespace ava::app
