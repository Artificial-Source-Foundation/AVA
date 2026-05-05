#pragma once

#include <string>

#include "ava/agent/tool_result.h"
#include "ava/app/commands.h"
#include "ava/core/result.h"

namespace ava::app {

[[nodiscard]] ava::core::VoidResult record_tool_start(RuntimeSession const& session, RuntimeEventSink const& sink,
                                                      CommandResult& result, std::string const& call_id,
                                                      std::string name, std::string argument_summary);

[[nodiscard]] ava::core::VoidResult record_tool_result(RuntimeSession const& session, RuntimeEventSink const& sink,
                                                       CommandResult& result, std::string const& call_id,
                                                       std::string name, ava::agent::ToolTimelineStatus status,
                                                       std::string result_summary, std::string result_content = {});

}  // namespace ava::app
