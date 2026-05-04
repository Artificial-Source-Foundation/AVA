#pragma once

#include <string>
#include <string_view>

#include "ava/agent/tool_types.h"

namespace ava::agent {

[[nodiscard]] std::string_view to_string(ToolResultStatus status) noexcept;
[[nodiscard]] ToolResultPayload parse_tool_result_payload(std::string_view tool_name, bool success,
                                                          std::string_view result_text);
[[nodiscard]] ToolDispatchResult with_tool_result_payload(ToolDispatchResult result);
[[nodiscard]] std::string serialize_tool_result_payload_json(ToolDispatchResult const& result);

}  // namespace ava::agent
