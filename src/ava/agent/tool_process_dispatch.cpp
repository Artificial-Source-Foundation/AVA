#include "ava/agent/tool_process_dispatch.h"

#include <chrono>
#include <string>

#include "ava/agent/tool_arguments.h"
#include "ava/agent/tool_dispatch_support.h"
#include "ava/agent/tool_result_json.h"
#include "ava/core/json.h"
#include "ava/tools/bash_tool.h"

namespace ava::agent::detail {

ToolDispatchResult bash_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto command = required_safe_string_arg(call.arguments_json, "command", call.name);
  if (!command) return tool_error_result(call, command.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::run_bash(
      tool_context, *command,
      ava::tools::BashOptions{
          .timeout = std::chrono::milliseconds(optional_size_arg(call.arguments_json, "timeout_ms", 30000, 120000)),
          .max_bytes = optional_size_arg(call.arguments_json, "max_bytes", 50 * 1024, 512 * 1024)});
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{
      .call_id = call.id,
      .name = call.name,
      .success = result->exit_code == 0 && !result->timed_out && !result->canceled,
      .result_text =
          [&] {
            std::string text = "{\"tool\":\"bash\",\"ok\":" +
                               json_bool_literal(result->exit_code == 0 && !result->timed_out && !result->canceled) +
                               ",\"exit_code\":" + std::to_string(result->exit_code) +
                               ",\"timed_out\":" + json_bool_literal(result->timed_out) +
                               ",\"canceled\":" + json_bool_literal(result->canceled) +
                               ",\"truncated\":" + json_bool_literal(result->truncated) +
                               ",\"total_bytes\":" + std::to_string(result->total_bytes) + ",\"output\":\"" +
                               ava::core::json::escape(result->output) + "\"";
            append_tool_result_spill_fields(text, result->spill_path, result->spill_truncated);
            text += "}";
            return text;
          }(),
      .payload =
          [&] {
            ava::agent::ToolResultPayload payload;
            payload.status =
                result->canceled ? ava::agent::ToolResultStatus::Canceled : ava::agent::ToolResultStatus::Success;
            return payload;
          }()};
}

}  // namespace ava::agent::detail
