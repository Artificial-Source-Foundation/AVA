#include "ava/agent/tool_dispatch_support.h"

#include <cstddef>
#include <utility>

#include "ava/agent/tool_arguments.h"
#include "ava/agent/tool_result_json.h"

namespace ava::agent::detail {
namespace {

constexpr std::size_t kMaxProviderToolCallIdBytes = 256;

}  // namespace

ProviderToolCall normalize_provider_tool_call(ProviderToolCall const& call)
{
  return ProviderToolCall{.id = call.id,
                          .name = call.name,
                          .arguments_json = call.arguments_json.empty() ? std::string("{}") : call.arguments_json};
}

ava::core::VoidResult validate_provider_tool_call(ProviderToolCall const& call)
{
  if (call.id.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool call id is required");
    error.with_context("tool", call.name);
    return std::unexpected(std::move(error));
  }
  if (call.id.size() > kMaxProviderToolCallIdBytes) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool call id is too long");
    error.with_context("tool", call.name);
    error.with_context("max_bytes", std::to_string(kMaxProviderToolCallIdBytes));
    return std::unexpected(std::move(error));
  }
  if (auto safe_id = reject_control_value(call.id, "id", "tool call id contains a forbidden control byte"); !safe_id) {
    safe_id.error().with_context("tool", call.name);
    return std::unexpected(std::move(safe_id.error()));
  }
  return {};
}

ava::tools::ToolContext context_for_provider_tool(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto tool_context = context;
  tool_context.permission_tool_name = call.name;
  tool_context.current_tool_name = call.name;
  tool_context.current_call_id = call.id;
  return tool_context;
}

bool is_canceled(ava::tools::ToolContext const& context)
{
  return context.cancel_requested && context.cancel_requested();
}

ava::core::Error canceled_error(ProviderToolCall const& call)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "tool canceled");
  error.with_context("tool", call.name);
  error.with_context("call_id", call.id);
  return error;
}

ava::core::VoidResult check_canceled(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  if (!is_canceled(context)) return {};
  return std::unexpected(canceled_error(call));
}

ToolDispatchResult tool_error_result(ProviderToolCall const& call, ava::core::Error const& error)
{
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = false,
                            .result_text = tool_error_result_json(call.name, error),
                            .payload = [&] {
                              ava::agent::ToolResultPayload payload;
                              if (error.message().find("canceled") != std::string::npos ||
                                  error.message().find("cancelled") != std::string::npos) {
                                payload.status = ava::agent::ToolResultStatus::Canceled;
                              }
                              return payload;
                            }()};
}

ToolDispatchResult lsp_error_result(ProviderToolCall const& call, ava::core::Error const& error)
{
  if (error.message().find("canceled") != std::string::npos || error.message().find("cancelled") != std::string::npos) {
    return tool_error_result(call, error);
  }
  if (error.category() == ava::core::ErrorCategory::PermissionDenied ||
      error.category() == ava::core::ErrorCategory::InvalidArgument) {
    return tool_error_result(call, error);
  }
  auto redacted = ava::core::Error(error.category(), "LSP diagnostics failed");
  redacted.with_context("tool", call.name);
  return tool_error_result(call, redacted);
}

ToolDispatchResult simple_error_result(ProviderToolCall const& call, ava::core::ErrorCategory category,
                                       std::string message)
{
  auto const error = ava::core::Error(category, std::move(message));
  return tool_error_result(call, error);
}

}  // namespace ava::agent::detail
