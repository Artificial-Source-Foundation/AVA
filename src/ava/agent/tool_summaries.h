#pragma once

#include <string>

#include "ava/agent/tool_types.h"

namespace ava::agent {

[[nodiscard]] std::string summarize_tool_arguments(const ProviderToolCall& call);
[[nodiscard]] std::string summarize_tool_result(const ToolDispatchResult& result);

}  // namespace ava::agent
