#include "ava/agent/tool_file_dispatch.h"

#include <string>

#include "ava/agent/tool_arguments.h"
#include "ava/agent/tool_dispatch_support.h"
#include "ava/agent/tool_result_json.h"
#include "ava/core/json.h"

namespace ava::agent::detail {

ToolDispatchResult read_file_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path) return tool_error_result(call, path.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::read_file(
      tool_context, workspace_path(context, *path),
      ava::tools::ReadOptions{.max_bytes = optional_size_arg(call.arguments_json, "max_bytes", 50 * 1024, 512 * 1024)});
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{
      .call_id = call.id,
      .name = call.name,
      .success = true,
      .result_text = "{\"tool\":\"read_file\",\"ok\":true,\"path\":\"" + ava::core::json::escape(*path) +
                     "\",\"content\":\"" + ava::core::json::escape(result->content) + "\",\"truncated\":" +
                     json_bool_literal(result->truncated) + ",\"total_bytes\":" + std::to_string(result->total_bytes) +
                     ",\"output_bytes\":" + std::to_string(result->output_bytes) + "}"};
}

ToolDispatchResult write_file_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path) return tool_error_result(call, path.error());
  auto content = required_text_arg(call.arguments_json, "content", call.name);
  if (!content) return tool_error_result(call, content.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::write_file(tool_context, workspace_path(context, *path), *content);
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = "{\"tool\":\"write_file\",\"ok\":true,\"path\":\"" +
                                           ava::core::json::escape(result->path.generic_string()) +
                                           "\",\"bytes_written\":" + std::to_string(result->bytes_written) + "}"};
}

ToolDispatchResult edit_file_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path) return tool_error_result(call, path.error());
  auto old_text = required_text_arg(call.arguments_json, "old_text", call.name);
  if (!old_text) return tool_error_result(call, old_text.error());
  auto new_text = required_text_arg(call.arguments_json, "new_text", call.name);
  if (!new_text) return tool_error_result(call, new_text.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::edit_file(tool_context, workspace_path(context, *path), *old_text, *new_text);
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = "{\"tool\":\"edit_file\",\"ok\":true,\"path\":\"" +
                                           ava::core::json::escape(result->path.generic_string()) +
                                           "\",\"bytes_written\":" + std::to_string(result->bytes_written) +
                                           ",\"diff\":\"" + ava::core::json::escape(result->diff) +
                                           "\",\"diff_truncated\":" + json_bool_literal(result->diff_truncated) +
                                           ",\"line_endings\":\"" + ava::core::json::escape(result->line_endings) +
                                           "\",\"utf8_bom\":" + json_bool_literal(result->had_utf8_bom) + "}"};
}

}  // namespace ava::agent::detail
