#pragma once

#include "ava/agent/tool_dispatch_services.h"
#include "ava/agent/tool_types.h"
#include "ava/tools/file_tools.h"

namespace ava::agent {

[[nodiscard]] ToolDispatchResult question_result(ava::tools::ToolContext const& context, ToolDispatchServices const& services, ProviderToolCall const& call);

}  // namespace ava::agent
