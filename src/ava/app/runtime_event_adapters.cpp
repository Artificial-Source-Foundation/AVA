#include "sys.h"
#include "runtime_event_adapters.h"

#include <string>
#include <utility>

namespace ava::app {
namespace {

ava::event::ToolPayload tool_payload_from_timeline_entry(ava::agent::ToolTimelineEntry const& entry)
{
  ava::event::ToolPayload payload{
      .text = entry.status == ava::agent::ToolTimelineStatus::Running ? entry.argument_summary : entry.result_summary,
      .call_id = entry.call_id,
      .tool = entry.name,
      .args_json = entry.arguments_json,
      .result_json = entry.result_json,
      .structured_result_json = entry.structured_result_json,
      .status = ava::agent::to_string(entry.status),
      .error_category = entry.error_category,
      .error_code = entry.error_code,
      .error_message = entry.error_message,
      .error_details = entry.error_details,
      .content_type = entry.content_type,
      .diff = entry.diff,
      .changed_paths = entry.changed_paths,
      .permission_request_ids = entry.permission_request_ids,
      .spill_path = entry.spill_path,
      .diff_truncated = entry.diff_truncated,
      .truncated = entry.truncated,
      .byte_limited = entry.byte_limited,
      .line_limited = entry.line_limited,
      .spill_truncated = entry.spill_truncated,
  };
  // Unset optional counters remain 0, matching the legacy bag defaults.
  if (entry.output_bytes)
    payload.output_bytes = *entry.output_bytes;
  if (entry.total_bytes)
    payload.total_bytes = *entry.total_bytes;
  if (entry.output_lines)
    payload.output_lines = *entry.output_lines;
  if (entry.total_lines)
    payload.total_lines = *entry.total_lines;
  if (entry.start_line)
    payload.start_line = *entry.start_line;
  if (entry.end_line)
    payload.end_line = *entry.end_line;
  if (entry.next_offset_line)
    payload.next_offset_line = *entry.next_offset_line;
  if (entry.omitted_bytes)
    payload.omitted_bytes = *entry.omitted_bytes;
  if (entry.omitted_lines)
    payload.omitted_lines = *entry.omitted_lines;
  if (entry.visible_matches)
    payload.visible_matches = *entry.visible_matches;
  if (entry.total_matches)
    payload.total_matches = *entry.total_matches;
  return payload;
}

std::string stream_stop_reason(ava::provider::StreamEvent const& stream_event)
{
  if (!stream_event.finish_reason)
    return {};
  return std::string(ava::provider::to_string(*stream_event.finish_reason));
}

bool stream_reasoning_signature_present(ava::provider::StreamEvent const& stream_event)
{
  return stream_event.reasoning_signature_present || !stream_event.reasoning_signature.empty();
}

ava::event::MessagePayload message_payload_from_stream_event(ava::provider::StreamEvent const& stream_event)
{
  ava::event::MessagePayload payload;
  payload.text = stream_event.text;
  payload.status = ava::provider::to_string(stream_event.type);
  payload.error_message = stream_event.error_message;
  payload.stop_reason = stream_stop_reason(stream_event);
  return payload;
}

ava::event::ReasoningPayload reasoning_payload_from_stream_event(ava::provider::StreamEvent const& stream_event)
{
  ava::event::ReasoningPayload payload;
  payload.text = stream_event.text;
  payload.status = ava::provider::to_string(stream_event.type);
  payload.error_message = stream_event.error_message;
  payload.stop_reason = stream_stop_reason(stream_event);
  payload.reasoning_format = stream_event.reasoning_format;
  payload.reasoning_redacted = stream_event.redacted;
  payload.reasoning_signature_present = stream_reasoning_signature_present(stream_event);
  return payload;
}

ava::event::ProviderPayload provider_payload_from_stream_event(ava::provider::StreamEvent const& stream_event)
{
  ava::event::ProviderPayload payload;
  payload.text = stream_event.text;
  payload.call_id = stream_event.tool_call_id;
  payload.tool = stream_event.tool_name;
  payload.status = ava::provider::to_string(stream_event.type);
  payload.error_message = stream_event.error_message;
  payload.stop_reason = stream_stop_reason(stream_event);
  payload.reasoning_format = stream_event.reasoning_format;
  payload.reasoning_redacted = stream_event.redacted;
  payload.reasoning_signature_present = stream_reasoning_signature_present(stream_event);
  return payload;
}

}  // namespace

ava::event::RuntimeEvent runtime_event_from_tool_timeline_entry(ava::event::RuntimeEventMetadata metadata, ava::agent::ToolTimelineEntry const& entry)
{
  auto payload = tool_payload_from_timeline_entry(entry);
  if (entry.status == ava::agent::ToolTimelineStatus::Running)
  {
    return ava::event::RuntimeEvent{std::move(metadata), ava::event::ToolStartEvent{.payload = std::move(payload)}};
  }
  return ava::event::RuntimeEvent{std::move(metadata), ava::event::ToolResultEvent{.payload = std::move(payload)}};
}

ava::event::RuntimeEvent runtime_event_from_tool_progress_entry(ava::event::RuntimeEventMetadata metadata, ava::agent::ToolProgressEntry const& entry)
{
  ava::event::ToolPayload payload;
  payload.text = entry.text;
  payload.call_id = entry.call_id;
  payload.tool = entry.name;
  payload.status = entry.status;
  return ava::event::RuntimeEvent{std::move(metadata), ava::event::ToolProgressEvent{.payload = std::move(payload)}};
}

ava::event::RuntimeEvent runtime_event_from_provider_stream_event(ava::event::RuntimeEventMetadata metadata, ava::provider::StreamEvent const& stream_event)
{
  switch (stream_event.type)
  {
    case ava::provider::StreamEventType::TextDelta:
      return ava::event::RuntimeEvent{std::move(metadata), ava::event::MessageUpdateEvent{.payload = message_payload_from_stream_event(stream_event)}};
    case ava::provider::StreamEventType::ReasoningStart:
      return ava::event::RuntimeEvent{std::move(metadata), ava::event::ReasoningStartEvent{.payload = reasoning_payload_from_stream_event(stream_event)}};
    case ava::provider::StreamEventType::ReasoningDelta:
      return ava::event::RuntimeEvent{std::move(metadata), ava::event::ReasoningDeltaEvent{.payload = reasoning_payload_from_stream_event(stream_event)}};
    case ava::provider::StreamEventType::ReasoningEnd:
      return ava::event::RuntimeEvent{std::move(metadata), ava::event::ReasoningEndEvent{.payload = reasoning_payload_from_stream_event(stream_event)}};
    case ava::provider::StreamEventType::Done:
      return ava::event::RuntimeEvent{std::move(metadata), ava::event::MessageEndEvent{.payload = message_payload_from_stream_event(stream_event)}};
    case ava::provider::StreamEventType::TextStart:
    case ava::provider::StreamEventType::TextEnd:
    case ava::provider::StreamEventType::ToolCallStart:
    case ava::provider::StreamEventType::ToolCallDelta:
    case ava::provider::StreamEventType::ToolCallEnd:
    case ava::provider::StreamEventType::Error:
      break;
  }
  return ava::event::RuntimeEvent{std::move(metadata), ava::event::ProviderEvent{.payload = provider_payload_from_stream_event(stream_event)}};
}

}  // namespace ava::app
