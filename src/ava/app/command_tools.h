#pragma once

#include "ava/app/commands.h"
#include "ava/tools/file_tools.h"

#include <string>
#include <vector>

namespace ava::app {

[[nodiscard]] ava::tools::ToolContext make_tool_context(runtime::Session& session, ava::permissions::PermissionResolver permission_resolver);

[[nodiscard]] ava::core::VoidResult record_tool_start(runtime::Session const& session, runtime::EventSink const& sink, CommandResult& result,
                                                      std::string const& call_id, std::string name, std::string argument_summary);

[[nodiscard]] ava::core::VoidResult record_tool_result(runtime::Session const& session, runtime::EventSink const& sink, CommandResult& result,
                                                       std::string const& call_id, std::string name, ava::agent::ToolTimelineStatus status,
                                                       std::string result_summary, std::string result_content = {},
                                                       std::vector<std::string> permission_request_ids = {});

[[nodiscard]] ava::core::Result<CommandResult> run_tool_command(runtime::Session& session, CommandRequest& request);

}  // namespace ava::app
