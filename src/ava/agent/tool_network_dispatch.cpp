#include "ava/agent/tool_network_dispatch.h"

#include <string>

#include "ava/agent/tool_arguments.h"
#include "ava/agent/tool_dispatch_support.h"
#include "ava/agent/tool_result_json.h"
#include "ava/core/json.h"
#include "ava/tools/webfetch_tool.h"

namespace ava::agent::detail {

ToolDispatchResult webfetch_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto url = required_safe_string_arg(call.arguments_json, "url", call.name);
  if (!url) return tool_error_result(call, url.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::webfetch(
      tool_context, *url,
      ava::tools::WebFetchOptions{
          .max_bytes = optional_size_arg(call.arguments_json, "max_bytes", 1024 * 1024, 5 * 1024 * 1024),
          .timeout_ms = static_cast<int>(optional_size_arg(call.arguments_json, "timeout_ms", 30000, 120000)),
      });
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{
      .call_id = call.id,
      .name = call.name,
      .success = true,
      .result_text = "{\"tool\":\"webfetch\",\"ok\":true,\"url\":\"" + ava::core::json::escape(result->url) +
                     "\",\"status_code\":" + std::to_string(result->status_code) + ",\"content_type\":\"" +
                     ava::core::json::escape(result->content_type) + "\",\"content\":\"" +
                     ava::core::json::escape(result->content) + "\",\"truncated\":" +
                     json_bool_literal(result->truncated) + ",\"total_bytes\":" + std::to_string(result->total_bytes) +
                     ",\"output_bytes\":" + std::to_string(result->output_bytes) + "}"};
}

}  // namespace ava::agent::detail
