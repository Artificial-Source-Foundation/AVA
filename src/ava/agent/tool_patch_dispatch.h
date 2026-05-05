#pragma once

#include "ava/agent/tool_types.h"
#include "ava/tools/file_tools.h"

namespace ava::agent::detail {

[[nodiscard]] ToolDispatchResult apply_patch_result(ava::tools::ToolContext const& context,
                                                    ProviderToolCall const& call);

}  // namespace ava::agent::detail
