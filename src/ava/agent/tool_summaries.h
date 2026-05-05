#pragma once

#include "ava/agent/tool_types.h"

#include <string>

namespace ava::agent {

[[nodiscard]] std::string summarize_tool_arguments(ProviderToolCall const& call);
[[nodiscard]] std::string summarize_tool_result(ToolDispatchResult const& result);

}  // namespace ava::agent
