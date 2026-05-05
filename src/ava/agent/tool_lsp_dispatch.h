#pragma once

#include "ava/agent/tool_metadata.h"
#include "ava/agent/tool_types.h"
#include "ava/tools/file_tools.h"

namespace ava::agent::detail {

[[nodiscard]] bool is_lsp_diagnostics_metadata(ToolMetadata const& tool);
[[nodiscard]] ToolDispatchResult lsp_diagnostics_result(ava::tools::ToolContext const& context,
                                                        ProviderToolCall const& call);

}  // namespace ava::agent::detail
