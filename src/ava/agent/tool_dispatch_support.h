#pragma once

#include <string>

#include "ava/agent/tool_types.h"
#include "ava/core/result.h"
#include "ava/tools/file_tools.h"

namespace ava::agent::detail {

[[nodiscard]] ProviderToolCall normalize_provider_tool_call(ProviderToolCall const& call);
[[nodiscard]] ava::core::VoidResult validate_provider_tool_call(ProviderToolCall const& call);
[[nodiscard]] ava::tools::ToolContext context_for_provider_tool(ava::tools::ToolContext const& context,
                                                                ProviderToolCall const& call);
[[nodiscard]] bool is_canceled(ava::tools::ToolContext const& context);
[[nodiscard]] ava::core::Error canceled_error(ProviderToolCall const& call);
[[nodiscard]] ava::core::VoidResult check_canceled(ava::tools::ToolContext const& context,
                                                   ProviderToolCall const& call);
[[nodiscard]] ToolDispatchResult tool_error_result(ProviderToolCall const& call, ava::core::Error const& error);
[[nodiscard]] ToolDispatchResult lsp_error_result(ProviderToolCall const& call, ava::core::Error const& error);
[[nodiscard]] ToolDispatchResult simple_error_result(ProviderToolCall const& call, ava::core::ErrorCategory category,
                                                     std::string message);

}  // namespace ava::agent::detail
