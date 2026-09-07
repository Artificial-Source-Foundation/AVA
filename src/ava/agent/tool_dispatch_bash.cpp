#include "sys.h"
#include "ava/agent/tool_dispatch_bash.h"
#include "ava/agent/tool_dispatch_common.h"
#include "ava/tools/bash_tool.h"
#include "ava/core/json.h"

#include <chrono>

namespace ava::agent {
namespace {

auto bash_error_result(ProviderToolCall const& call, ava::core::Error const& error, bool malformed = false) -> ToolDispatchResult
{
  auto result = tool_dispatch::tool_error_result(call, error);
  auto const context_value = [&](std::string_view key) -> std::string_view {
    for (auto const& field : error.context())
    {
      if (field.key == key)
      {
        return field.value;
      }
    }
    return {};
  };
  auto status = context_value("command_status");
  if (status.empty())
  {
    status = malformed ? "malformed_invocation" : "command_error";
  }
  result.payload.error_code = status;
  result.result_text = R"({"tool":")" + ava::core::json::escape(call.name) + R"(","ok":false,"command_status":")" + std::string(status) + "\"";
  if (malformed || context_value("executed") == "false")
  {
    result.result_text += ",\"executed\":false";
  }
  for (auto const* const key : {"containment", "approval_scope", "platform", "next_action"})
  {
    if (auto const value = context_value(key); !value.empty())
    {
      result.result_text += ",\"" + std::string(key) + "\":\"" + ava::core::json::escape(value) + "\"";
    }
  }
  result.result_text += R"(,"error":{"category":")" + ava::core::to_string(error.category()) + R"(","code":")" + std::string(status) + R"(","message":")" +
                        ava::core::json::escape(error.message()) + R"(","details":")" + ava::core::json::escape(error.format()) + "\"}}";
  return result;
}

auto bash_completion_status(ava::tools::BashResult const& result) -> std::string_view
{
  if (result.canceled)
  {
    return "canceled";
  }
  if (result.timed_out)
  {
    return "timed_out";
  }
  return result.exit_code == 0 ? "completed" : "execution_failed";
}

}  // namespace

ToolDispatchResult bash_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  if (!ava::core::json::is_valid_object(call.arguments_json))
  {
    return bash_error_result(call, ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "bash arguments must be one valid JSON object"), true);
  }
  auto command = tool_dispatch::required_safe_string_arg(call.arguments_json, "command", call.name);
  if (!command)
    return bash_error_result(call, command.error(), true);
  if (command->empty())
  {
    return bash_error_result(call, ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "command must not be empty"), true);
  }
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
    return bash_error_result(call, result.error());
  return ToolDispatchResult{
      .call_id = call.id,
      .name = call.name,
      .success = result->exit_code == 0 && !result->timed_out && !result->canceled,
      .result_text =
          [&] {
            std::string text = R"({"tool":"bash","ok":)" + tool_dispatch::json_bool(result->exit_code == 0 && !result->timed_out && !result->canceled) +
                               R"(,"command_status":")" + std::string(bash_completion_status(*result)) + "\"" +
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
