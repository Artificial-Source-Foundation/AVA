#include "sys.h"
#include "ava/agent/tool_dispatch_bash.h"
#include "ava/agent/tool_dispatch_common.h"
#include "ava/tools/bash_tool.h"
#include "ava/core/json.h"

#include <chrono>

namespace ava::agent {

ToolDispatchResult bash_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto command = tool_dispatch::required_safe_string_arg(call.arguments_json, "command", call.name);
  if (!command)
    return tool_dispatch::tool_error_result(call, command.error());
  auto max_lines = tool_dispatch::optional_size_arg(call.arguments_json, "max_lines", 0, 100000);
  if (max_lines == 0)
    max_lines = tool_dispatch::optional_size_arg(call.arguments_json, "limit", 200, 100000);
  auto const tool_context = tool_dispatch::context_for_provider_tool(context, call);
  auto result = ava::tools::run_bash(
      tool_context, *command,
      ava::tools::BashOptions{.timeout = std::chrono::milliseconds(tool_dispatch::optional_size_arg(call.arguments_json, "timeout_ms", 30000, 120000)),
                              .max_bytes = tool_dispatch::optional_size_arg(call.arguments_json, "max_bytes", 50 * 1024, 512 * 1024),
                              .max_lines = max_lines,
                              .invocation_source = ava::tools::BashOptions::InvocationSource::ModelCompatibility});
  if (!result)
    return tool_dispatch::tool_error_result(call, result.error());
  return ToolDispatchResult{
      .call_id = call.id,
      .name = call.name,
      .success = result->exit_code == 0 && !result->timed_out && !result->canceled,
      .result_text =
          [&] {
            std::string text = "{\"tool\":\"bash\",\"ok\":" + tool_dispatch::json_bool(result->exit_code == 0 && !result->timed_out && !result->canceled) +
                               ",\"exit_code\":" + std::to_string(result->exit_code) + ",\"timed_out\":" + tool_dispatch::json_bool(result->timed_out) +
                               ",\"canceled\":" + tool_dispatch::json_bool(result->canceled) + ",\"truncated\":" + tool_dispatch::json_bool(result->truncated) +
                               ",\"byte_limited\":" + tool_dispatch::json_bool(result->byte_limited) +
                               ",\"line_limited\":" + tool_dispatch::json_bool(result->line_limited);
            if (result->totals_known)
              text += ",\"total_bytes\":" + std::to_string(result->total_bytes);
            text += ",\"output_bytes\":" + std::to_string(result->output_bytes);
            if (result->totals_known)
            {
              text += ",\"total_lines\":" + std::to_string(result->total_lines);
            }
            text += ",\"output_lines\":" + std::to_string(result->output_lines);
            if (result->totals_known)
              text += ",\"omitted_lines\":" + std::to_string(result->omitted_lines);
            text += ",\"output\":\"" + ava::core::json::escape(result->output) + "\"";
            if (result->truncated && result->line_limited)
            {
              text += ",\"truncation_hint\":\"Increase max_lines to retain more command output.\"";
            }
            else if (result->truncated && result->byte_limited)
            {
              text += ",\"truncation_hint\":\"Increase max_bytes to retain longer output lines.\"";
            }
            tool_dispatch::append_spill_fields(text, result->spill_path, result->spill_truncated);
            text += "}";
            return text;
          }(),
      .payload =
          [&] {
            ava::agent::ToolResultPayload payload;
            payload.status = result->canceled ? ava::agent::ToolResultStatus::Canceled : ava::agent::ToolResultStatus::Success;
            return payload;
          }()};
}

}  // namespace ava::agent
