#include "sys.h"
#include "events.h"
#include "ava/core/json.h"
#include "ava/core/mode.h"

#include <utility>

namespace ava::app {
namespace {

void append_string_field(std::string& out, std::string_view key, std::string_view value)
{
  if (value.empty())
    return;
  out += ",\"";
  out += key;
  out += "\":\"";
  out += ava::core::json::escape(value);
  out += '"';
}

void append_number_field(std::string& out, std::string_view key, std::size_t value)
{
  if (value == 0)
    return;
  out += ",\"";
  out += key;
  out += "\":";
  out += std::to_string(value);
}

void append_bool_field(std::string& out, std::string_view key, bool value)
{
  if (!value)
    return;
  out += ",\"";
  out += key;
  out += "\":true";
}

void append_string_array_field(std::string& out, std::string_view key, std::vector<std::string> const& values)
{
  if (values.empty())
    return;
  out += ",\"";
  out += key;
  out += "\":[";
  for (std::size_t index = 0; index < values.size(); ++index)
  {
    if (index > 0)
      out += ',';
    out += '"';
    out += ava::core::json::escape(values[index]);
    out += '"';
  }
  out += ']';
}

void append_json_object_field(std::string& out, std::string_view key, std::string_view value)
{
  if (value.empty())
    return;
  out += ",\"";
  out += key;
  if (ava::core::json::is_valid_object(value))
  {
    out += "\":";
    out += value;
    return;
  }
  out += "_json\":\"";
  out += ava::core::json::escape(value);
  out += '"';
}

}  // namespace

runtime::SessionPayload session_payload_from_event(runtime::Event const& event)
{
  return runtime::SessionPayload{.mode = event.mode, .provider = event.provider_id, .model = event.model_id};
}

runtime::MessagePayload message_payload_from_event(runtime::Event const& event)
{
  return runtime::MessagePayload{.text = event.text, .status = event.status, .error_message = event.error_message, .stop_reason = event.stop_reason};
}

runtime::ReasoningPayload reasoning_payload_from_event(runtime::Event const& event)
{
  return runtime::ReasoningPayload{.text = event.text,
                                   .status = event.status,
                                   .error_message = event.error_message,
                                   .stop_reason = event.stop_reason,
                                   .reasoning_format = event.reasoning_format,
                                   .reasoning_redacted = event.reasoning_redacted,
                                   .reasoning_signature_present = event.reasoning_signature_present};
}

runtime::ProviderPayload provider_payload_from_event(runtime::Event const& event)
{
  return runtime::ProviderPayload{.text = event.text,
                                  .call_id = event.call_id,
                                  .tool = event.tool_name,
                                  .status = event.status,
                                  .error_message = event.error_message,
                                  .error_details = event.error_details,
                                  .stop_reason = event.stop_reason,
                                  .reason = event.reason,
                                  .reasoning_format = event.reasoning_format,
                                  .permission_request_ids = event.permission_request_ids,
                                  .reasoning_redacted = event.reasoning_redacted,
                                  .reasoning_signature_present = event.reasoning_signature_present};
}

runtime::ToolPayload tool_payload_from_event(runtime::Event const& event)
{
  return runtime::ToolPayload{.text = event.text,
                              .call_id = event.call_id,
                              .tool = event.tool_name,
                              .args_json = event.tool_arguments_json,
                              .result_json = event.tool_result_json,
                              .structured_result_json = event.tool_structured_result_json,
                              .status = event.status,
                              .error_category = event.error_category,
                              .error_code = event.error_code,
                              .error_message = event.error_message,
                              .error_details = event.error_details,
                              .content_type = event.content_type,
                              .diff = event.diff,
                              .changed_paths = event.changed_paths,
                              .permission_request_ids = event.permission_request_ids,
                              .spill_path = event.spill_path,
                              .diff_truncated = event.diff_truncated,
                              .truncated = event.truncated,
                              .byte_limited = event.byte_limited,
                              .line_limited = event.line_limited,
                              .spill_truncated = event.spill_truncated,
                              .output_bytes = event.output_bytes,
                              .total_bytes = event.total_bytes,
                              .output_lines = event.output_lines,
                              .total_lines = event.total_lines,
                              .start_line = event.start_line,
                              .end_line = event.end_line,
                              .next_offset_line = event.next_offset_line,
                              .omitted_bytes = event.omitted_bytes,
                              .omitted_lines = event.omitted_lines,
                              .visible_matches = event.visible_matches,
                              .total_matches = event.total_matches};
}

runtime::CompactionPayload compaction_payload_from_event(runtime::Event const& event)
{
  return runtime::CompactionPayload{.provider = event.provider_id,
                                    .model = event.model_id,
                                    .status = event.status,
                                    .trigger = event.trigger,
                                    .reason = event.reason,
                                    .attempt = event.attempt,
                                    .max_attempts = event.max_attempts,
                                    .estimated_tokens = event.estimated_tokens,
                                    .threshold_tokens = event.threshold_tokens,
                                    .retained_tokens = event.retained_tokens,
                                    .post_compaction_tokens = event.post_compaction_tokens,
                                    .summary_bytes = event.summary_bytes};
}

runtime::RetryPayload retry_payload_from_event(runtime::Event const& event)
{
  return runtime::RetryPayload{.text = event.text,
                               .status = event.status,
                               .error_category = event.error_category,
                               .error_code = event.error_code,
                               .error_message = event.error_message,
                               .error_details = event.error_details,
                               .trigger = event.trigger,
                               .reason = event.reason,
                               .attempt = event.attempt,
                               .max_attempts = event.max_attempts,
                               .delay_ms = event.delay_ms,
                               .remaining_ms = event.remaining_ms};
}

runtime::CancellationPayload cancellation_payload_from_event(runtime::Event const& event)
{
  return runtime::CancellationPayload{.text = event.text,
                                      .status = event.status,
                                      .error_category = event.error_category,
                                      .error_code = event.error_code,
                                      .error_message = event.error_message,
                                      .error_details = event.error_details,
                                      .trigger = event.trigger,
                                      .reason = event.reason};
}

runtime::ErrorPayload error_payload_from_event(runtime::Event const& event)
{
  return runtime::ErrorPayload{.text = event.text,
                               .status = event.status,
                               .error_category = event.error_category,
                               .error_code = event.error_code,
                               .error_message = event.error_message,
                               .error_details = event.error_details,
                               .content_type = event.content_type,
                               .trigger = event.trigger,
                               .reason = event.reason};
}

runtime::CompletionPayload completion_payload_from_event(runtime::Event const& event)
{
  return runtime::CompletionPayload{.status = event.status,
                                    .stop_reason = event.stop_reason,
                                    .reason = event.reason,
                                    .provider_iterations = event.provider_iterations,
                                    .tool_calls = event.tool_calls};
}

ava::event::RuntimeEvent to_runtime_event(runtime::Event const& event)
{
  using runtime::EventType;
  ava::event::RuntimeEventMetadata metadata{.timestamp = event.timestamp, .session_id = event.session_id};

  switch (event.type)
  {
    case EventType::SessionStart:
      return {std::move(metadata), ava::event::SessionStartEvent{.payload = session_payload_from_event(event)}};
    case EventType::UserMessage:
      return {std::move(metadata), ava::event::UserMessageEvent{.payload = message_payload_from_event(event)}};
    case EventType::AssistantMessage:
      return {std::move(metadata), ava::event::AssistantMessageEvent{.payload = message_payload_from_event(event)}};
    case EventType::MessageUpdate:
      return {std::move(metadata), ava::event::MessageUpdateEvent{.payload = message_payload_from_event(event)}};
    case EventType::MessageEnd:
      return {std::move(metadata), ava::event::MessageEndEvent{.payload = message_payload_from_event(event)}};
    case EventType::ReasoningStart:
      return {std::move(metadata), ava::event::ReasoningStartEvent{.payload = reasoning_payload_from_event(event)}};
    case EventType::ReasoningDelta:
      return {std::move(metadata), ava::event::ReasoningDeltaEvent{.payload = reasoning_payload_from_event(event)}};
    case EventType::ReasoningEnd:
      return {std::move(metadata), ava::event::ReasoningEndEvent{.payload = reasoning_payload_from_event(event)}};
    case EventType::ProviderEvent:
      return {std::move(metadata), ava::event::ProviderEvent{.payload = provider_payload_from_event(event)}};
    case EventType::ToolStart:
      return {std::move(metadata), ava::event::ToolStartEvent{.payload = tool_payload_from_event(event)}};
    case EventType::ToolProgress:
      return {std::move(metadata), ava::event::ToolProgressEvent{.payload = tool_payload_from_event(event)}};
    case EventType::ToolResult:
      return {std::move(metadata), ava::event::ToolResultEvent{.payload = tool_payload_from_event(event)}};
    case EventType::CompactionStart:
      return {std::move(metadata), ava::event::CompactionStartEvent{.payload = compaction_payload_from_event(event)}};
    case EventType::CompactionEnd:
      return {std::move(metadata), ava::event::CompactionEndEvent{.payload = compaction_payload_from_event(event)}};
    case EventType::Retry:
      return {std::move(metadata), ava::event::RetryEvent{.payload = retry_payload_from_event(event),
                                                          .diagnostics = {.estimated_tokens = event.estimated_tokens,
                                                                          .threshold_tokens = event.threshold_tokens,
                                                                          .snapshot_entries = event.snapshot_entries,
                                                                          .current_entries = event.current_entries,
                                                                          .summary_bytes = event.summary_bytes}}};
    case EventType::RetryTick:
      return {std::move(metadata), ava::event::RetryTickEvent{.payload = retry_payload_from_event(event),
                                                              .diagnostics = {.estimated_tokens = event.estimated_tokens,
                                                                              .threshold_tokens = event.threshold_tokens,
                                                                              .snapshot_entries = event.snapshot_entries,
                                                                              .current_entries = event.current_entries,
                                                                              .summary_bytes = event.summary_bytes}}};
    case EventType::Canceled:
      return {std::move(metadata), ava::event::CancellationEvent{.payload = cancellation_payload_from_event(event)}};
    case EventType::Error:
      return {std::move(metadata), ava::event::ErrorEvent{.payload = error_payload_from_event(event)}};
    case EventType::Done:
      return {std::move(metadata), ava::event::CompletionEvent{.payload = completion_payload_from_event(event)}};
  }
  // Preserve the legacy envelope fallback for an out-of-range bag discriminator.
  return {std::move(metadata), ava::event::ErrorEvent{.payload = {}}};
}

std::string serialize_event_json(runtime::Event const& event)
{
  std::string out = "{\"type\":\"" + to_string(event.type) + "\"";
  append_string_field(out, "timestamp", event.timestamp);
  append_string_field(out, "session_id", event.session_id);
  if (event.type == runtime::EventType::SessionStart)
  {
    append_string_field(out, "mode", ava::core::to_string(event.mode));
    append_string_field(out, "provider", event.provider_id);
    append_string_field(out, "model", event.model_id);
  }
  append_string_field(out, "text", event.text);
  append_string_field(out, "call_id", event.call_id);
  append_string_field(out, "tool", event.tool_name);
  append_json_object_field(out, "args", event.tool_arguments_json);
  append_json_object_field(out, "result", event.tool_result_json);
  append_json_object_field(out, "structured_result", event.tool_structured_result_json);
  append_string_field(out, "status", event.status);
  append_string_field(out, "category", event.error_category);
  append_string_field(out, "error_code", event.error_code);
  append_string_field(out, "message", event.error_message);
  append_string_field(out, "details", event.error_details);
  append_string_field(out, "content_type", event.content_type);
  append_string_field(out, "stop_reason", event.stop_reason);
  append_string_field(out, "trigger", event.trigger);
  append_string_field(out, "reason", event.reason);
  append_string_field(out, "reasoning_format", event.reasoning_format);
  append_string_field(out, "diff", event.diff);
  append_string_array_field(out, "changed_paths", event.changed_paths);
  append_string_array_field(out, "permission_request_ids", event.permission_request_ids);
  append_string_field(out, "spill_path", event.spill_path);
  append_bool_field(out, "reasoning_redacted", event.reasoning_redacted);
  append_bool_field(out, "reasoning_signature_present", event.reasoning_signature_present);
  append_bool_field(out, "diff_truncated", event.diff_truncated);
  append_bool_field(out, "truncated", event.truncated);
  append_bool_field(out, "byte_limited", event.byte_limited);
  append_bool_field(out, "line_limited", event.line_limited);
  append_bool_field(out, "spill_truncated", event.spill_truncated);
  append_number_field(out, "provider_iterations", event.provider_iterations);
  append_number_field(out, "tool_calls", event.tool_calls);
  append_number_field(out, "attempt", event.attempt);
  append_number_field(out, "max_attempts", event.max_attempts);
  append_number_field(out, "delay_ms", event.delay_ms);
  append_number_field(out, "remaining_ms", event.remaining_ms);
  append_number_field(out, "estimated_tokens", event.estimated_tokens);
  append_number_field(out, "threshold_tokens", event.threshold_tokens);
  append_number_field(out, "retained_tokens", event.retained_tokens);
  append_number_field(out, "post_compaction_tokens", event.post_compaction_tokens);
  append_number_field(out, "summary_bytes", event.summary_bytes);
  append_number_field(out, "snapshot_entries", event.snapshot_entries);
  append_number_field(out, "current_entries", event.current_entries);
  append_number_field(out, "output_bytes", event.output_bytes);
  append_number_field(out, "total_bytes", event.total_bytes);
  append_number_field(out, "output_lines", event.output_lines);
  append_number_field(out, "total_lines", event.total_lines);
  append_number_field(out, "start_line", event.start_line);
  append_number_field(out, "end_line", event.end_line);
  append_number_field(out, "next_offset_line", event.next_offset_line);
  append_number_field(out, "omitted_bytes", event.omitted_bytes);
  append_number_field(out, "omitted_lines", event.omitted_lines);
  append_number_field(out, "visible_matches", event.visible_matches);
  append_number_field(out, "total_matches", event.total_matches);
  out += '}';
  return out;
}

std::string serialize_event_jsonl(runtime::Event const& event)
{
  return serialize_event_json(event) + '\n';
}

ava::core::VoidResult emit_event(ava::event::RuntimeEventSink const& sink, runtime::Event const& event)
{
  if (!sink)
    return {};
  auto typed_event = to_runtime_event(event);
  return ava::event::emit_event(sink, typed_event);
}

EventEnvelope to_event_envelope(runtime::Event const& event, EventEnvelopeContext const& context)
{
  auto typed_event = to_runtime_event(event);
  return ava::event::to_event_envelope(typed_event, context);
}

}  // namespace ava::app
