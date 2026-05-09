#include "ava/agent/tool_timeline.h"

#include "ava/agent/tool_result.h"

namespace ava::agent {

void publish_tool_event(AgentLoopOptions const& options, ToolTimelineEntry const& event)
{
  if (options.on_tool_event) options.on_tool_event(event);
}

ava::core::VoidResult publish_tool_progress(AgentLoopOptions const& options, ToolProgressEntry const& event)
{
  if (!options.on_tool_progress) return {};
  return options.on_tool_progress(event);
}

void populate_tool_timeline_metadata(ToolTimelineEntry& entry, ToolDispatchResult const& result)
{
  auto const materialized = with_tool_result_payload(result);
  auto const& payload = materialized.payload;
  entry.result_json = materialized.result_text;
  entry.structured_result_json = serialize_tool_result_payload_json(materialized);
  entry.content_type = payload.content_type;
  entry.error_category = payload.error_category;
  entry.error_code = payload.error_code;
  entry.error_message = payload.error_message;
  entry.error_details = payload.error_details;
  entry.diff = payload.diff;
  entry.diff_truncated = payload.diff_truncated;
  entry.truncated = payload.truncated;
  entry.byte_limited = payload.byte_limited;
  entry.line_limited = payload.line_limited;
  entry.spill_truncated = payload.spill_truncated;
  entry.spill_path = payload.spill_path;
  entry.output_bytes = payload.output_bytes;
  entry.total_bytes = payload.total_bytes;
  entry.output_lines = payload.output_lines;
  entry.total_lines = payload.total_lines;
  entry.start_line = payload.start_line;
  entry.end_line = payload.end_line;
  entry.next_offset_line = payload.next_offset_line;
  entry.omitted_bytes = payload.omitted_bytes;
  entry.omitted_lines = payload.omitted_lines;
  entry.visible_matches = payload.visible_matches;
  entry.total_matches = payload.total_matches;
  entry.changed_paths = payload.changed_paths;
  entry.permission_request_ids = payload.permission_request_ids;
}

}  // namespace ava::agent
