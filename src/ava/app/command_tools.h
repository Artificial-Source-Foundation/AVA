#pragma once

#include <string>

#include "ava/app/commands.h"
#include "ava/tools/file_tools.h"

namespace ava::app {

[[nodiscard]] ava::tools::ToolContext make_tool_context(RuntimeSession& session,
                                                        ava::permissions::PermissionResolver permission_resolver);

[[nodiscard]] ava::core::VoidResult record_tool_start(const RuntimeSession& session, const RuntimeEventSink& sink,
                                                      CommandResult& result, const std::string& call_id,
                                                      std::string name, std::string argument_summary);

[[nodiscard]] ava::core::VoidResult record_tool_result(const RuntimeSession& session, const RuntimeEventSink& sink,
                                                       CommandResult& result, const std::string& call_id,
                                                       std::string name, ava::agent::ToolTimelineStatus status,
                                                       std::string result_summary);

[[nodiscard]] ava::core::Result<CommandResult> run_tool_command(RuntimeSession& session, CommandRequest& request);

}  // namespace ava::app
