#include "ava/agent/tool_timeline.h"

#include <utility>

#include "ava/agent/tool_result.h"
#include "ava/core/json.h"

namespace ava::agent {
namespace {

std::string dispatch_error_result_json(ProviderToolCall const& call, ava::core::Error const& error)
{
  return "{\"tool\":\"" + ava::core::json::escape(call.name) + "\",\"ok\":false,\"error\":{\"category\":\"" +
         ava::core::json::escape(ava::core::to_string(error.category())) + "\",\"message\":\"" +
         ava::core::json::escape(error.message()) + "\",\"details\":\"" + ava::core::json::escape(error.format()) +
         "\"}}";
}

}  // namespace

std::string to_string(ToolTimelineStatus status)
{
  switch (status) {
    case ToolTimelineStatus::Running:
      return "running";
    case ToolTimelineStatus::Success:
      return "success";
    case ToolTimelineStatus::Error:
      return "error";
  }
  return "unknown";
}

ToolDispatchResult synthetic_failed_dispatch_result(ProviderToolCall const& call, ava::core::Error const& error)
{
  return with_tool_result_payload(ToolDispatchResult{
      .call_id = call.id, .name = call.name, .success = false, .result_text = dispatch_error_result_json(call, error)});
}

void populate_tool_timeline_metadata(ToolTimelineEntry& entry, ToolDispatchResult const& result)
{
  auto const& payload = result.payload;
  entry.result_json = result.result_text;
  entry.structured_result_json = serialize_tool_result_payload_json(result);
  entry.content_type = payload.content_type;
  entry.error_category = payload.error_category;
  entry.error_code = payload.error_code;
  entry.error_message = payload.error_message;
  entry.error_details = payload.error_details;
  entry.diff = payload.diff;
  entry.diff_truncated = payload.diff_truncated;
  entry.truncated = payload.truncated;
  entry.spill_truncated = payload.spill_truncated;
  entry.spill_path = payload.spill_path;
  entry.output_bytes = payload.output_bytes;
  entry.total_bytes = payload.total_bytes;
  entry.omitted_bytes = payload.omitted_bytes;
  entry.omitted_lines = payload.omitted_lines;
  entry.visible_matches = payload.visible_matches;
  entry.total_matches = payload.total_matches;
  entry.changed_paths = payload.changed_paths;
}

}  // namespace ava::agent
