#include "ava/app/command_tools.h"

#include <cstddef>
#include <filesystem>
#include <utility>

#include "ava/app/command_format.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/session/session_store.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/search_tools.h"

namespace ava::app {
namespace {

std::string json_bool(bool value)
{
  return value ? "true" : "false";
}

void append_spill_fields(std::string& text, std::filesystem::path const& path, bool spill_truncated)
{
  if (path.empty()) return;
  text += ",\"spill_file\":\"" + ava::core::json::escape(path.filename().generic_string()) + "\"";
  text += ",\"spill_truncated\":" + json_bool(spill_truncated);
}

}  // namespace

ava::tools::ToolContext make_tool_context(RuntimeSession& session,
                                          ava::permissions::PermissionResolver permission_resolver)
{
  return ava::tools::ToolContext{
      .workspace_dir = session.workspace_dir,
      .spill_dir = session.store.session_path().parent_path() / "spill",
      .mode = session.mode,
      .permission_resolver = std::move(permission_resolver),
      .permission_audit_sink =
          [&store = session.store](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
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

ava::core::Result<CommandResult> run_tool_command(RuntimeSession& session, CommandRequest& request)
{
  CommandResult result;
  result.handled = true;
  auto const& line = request.command;
  auto context = make_tool_context(session, request.permission_resolver);

  if (line.starts_with("/read ")) {
    auto const argument = line.substr(6);
    auto const call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "read", argument); !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    auto const output = ava::tools::read_file(context, session.current_dir / argument);
    if (!output) {
      auto const text = output.error().format();
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
    auto const read_result_json = "{\"tool\":\"read\",\"ok\":true,\"path\":\"" + ava::core::json::escape(argument) +
                                  "\",\"content\":\"" + ava::core::json::escape(output->content) +
                                  "\",\"truncated\":" + json_bool(output->truncated) +
                                  ",\"total_bytes\":" + std::to_string(output->total_bytes) +
                                  ",\"output_bytes\":" + std::to_string(output->output_bytes) + "}";
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "read",
                                           ava::agent::ToolTimelineStatus::Success,
                                           std::to_string(output->output_bytes) + " bytes", read_result_json);
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, std::move(text));
    return result;
  }

  if (line.starts_with("/glob ")) {
    auto const pattern = line.substr(6);
    auto const call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "glob", pattern); !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    auto const glob = ava::tools::glob_files(context, pattern);
    if (!glob) {
      auto const text = glob.error().format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "glob",
                                             ava::agent::ToolTimelineStatus::Error, text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, text);
      return result;
    }
    std::string output;
    for (auto const& path : glob->paths) output += display_path(path, session.current_dir) + '\n';
    if (glob->truncated) {
      output += "[truncated " + std::to_string(glob->paths.size()) + '/' + std::to_string(glob->total_matches) +
                " matches]\n";
    }
    std::string glob_result_json =
        "{\"tool\":\"glob\",\"ok\":true,\"pattern\":\"" + ava::core::json::escape(pattern) + "\",\"paths\":[";
    for (std::size_t index = 0; index < glob->paths.size(); ++index) {
      if (index > 0) glob_result_json += ',';
      glob_result_json += "\"" + ava::core::json::escape(glob->paths[index].generic_string()) + "\"";
    }
    glob_result_json += "],\"truncated\":" + json_bool(glob->truncated) +
                        ",\"total_matches\":" + std::to_string(glob->total_matches) +
                        ",\"output_matches\":" + std::to_string(glob->paths.size());
    append_spill_fields(glob_result_json, glob->spill_path, glob->spill_truncated);
    glob_result_json += "}";
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "glob",
                                           ava::agent::ToolTimelineStatus::Success,
                                           std::to_string(glob->paths.size()) + " matches", glob_result_json);
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, std::move(output));
    return result;
  }

  if (line.starts_with("/grep ")) {
    auto const rest = line.substr(6);
    auto const split = rest.find(' ');
    auto const pattern = split == std::string::npos ? rest : rest.substr(0, split);
    auto const include = split == std::string::npos ? std::string("**/*") : rest.substr(split + 1);
    auto const call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "grep", pattern); !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    auto const grep = ava::tools::grep_files(context, pattern, include);
    if (!grep) {
      auto const text = grep.error().format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "grep",
                                             ava::agent::ToolTimelineStatus::Error, text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, text);
      return result;
    }
    std::string output;
    for (auto const& match : grep->matches) {
      output +=
          display_path(match.path, session.current_dir) + ':' + std::to_string(match.line_number) + ": " + match.line;
      if (match.line_truncated) output += " [line truncated]";
      output += '\n';
    }
    if (grep->truncated) {
      output += "[truncated " + std::to_string(grep->matches.size()) + '/' + std::to_string(grep->total_matches) +
                " matches]\n";
    }
    std::string grep_result_json = "{\"tool\":\"grep\",\"ok\":true,\"pattern\":\"" + ava::core::json::escape(pattern) +
                                   "\",\"include\":\"" + ava::core::json::escape(include) + "\",\"content\":\"" +
                                   ava::core::json::escape(output) + "\",\"truncated\":" + json_bool(grep->truncated) +
                                   ",\"total_matches\":" + std::to_string(grep->total_matches) +
                                   ",\"output_matches\":" + std::to_string(grep->matches.size());
    append_spill_fields(grep_result_json, grep->spill_path, grep->spill_truncated);
    grep_result_json += "}";
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "grep",
                                           ava::agent::ToolTimelineStatus::Success,
                                           std::to_string(grep->matches.size()) + " matches", grep_result_json);
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, std::move(output));
    return result;
  }

  if (line.starts_with("/write ")) {
    auto const rest = line.substr(7);
    auto const split = rest.find(' ');
    if (split == std::string::npos) {
      add_output(result, missing_argument("/write <path> <text>"));
      return result;
    }
    auto const path_text = rest.substr(0, split);
    auto const text = rest.substr(split + 1);
    auto const call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "write", path_text);
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    auto const write = ava::tools::write_file(context, session.current_dir / path_text, text);
    if (!write) {
      auto const error_text = write.error().format();
      if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "write",
                                             ava::agent::ToolTimelineStatus::Error, error_text);
          !recorded) {
        return std::unexpected(std::move(recorded.error()));
      }
      add_output(result, error_text);
      return result;
    }
    auto const output = "wrote " + std::to_string(write->bytes_written) + " bytes to " + write->path.string();
    auto const write_result_json = "{\"tool\":\"write\",\"ok\":true,\"path\":\"" +
                                   ava::core::json::escape(write->path.generic_string()) +
                                   "\",\"bytes_written\":" + std::to_string(write->bytes_written) + "}";
    if (auto recorded = record_tool_result(session, request.event_sink, result, call_id, "write",
                                           ava::agent::ToolTimelineStatus::Success, output, write_result_json);
        !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    add_output(result, output);
    return result;
  }

  if (line.starts_with("/bash ")) {
    auto const command = line.substr(6);
    auto const call_id = ava::core::make_id("cmd");
    if (auto recorded = record_tool_start(session, request.event_sink, result, call_id, "bash", command); !recorded) {
      return std::unexpected(std::move(recorded.error()));
    }
    auto const bash = ava::tools::run_bash(context, command);
    if (!bash) {
      auto const text = bash.error().format();
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
    std::string bash_result_json =
        "{\"tool\":\"bash\",\"ok\":" + json_bool(bash->exit_code == 0 && !bash->timed_out && !bash->canceled) +
        ",\"exit_code\":" + std::to_string(bash->exit_code) + ",\"timed_out\":" + json_bool(bash->timed_out) +
        ",\"canceled\":" + json_bool(bash->canceled) + ",\"truncated\":" + json_bool(bash->truncated) +
        ",\"total_bytes\":" + std::to_string(bash->total_bytes) + ",\"output\":\"" +
        ava::core::json::escape(bash->output) + "\"";
    append_spill_fields(bash_result_json, bash->spill_path, bash->spill_truncated);
    bash_result_json += "}";
    if (auto recorded =
            record_tool_result(session, request.event_sink, result, call_id, "bash",
                               bash->exit_code == 0 && !bash->canceled ? ava::agent::ToolTimelineStatus::Success
                                                                       : ava::agent::ToolTimelineStatus::Error,
                               "exit " + std::to_string(bash->exit_code), bash_result_json);
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
