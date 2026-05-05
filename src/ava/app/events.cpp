#include "ava/app/events.h"

#include <utility>

#include "ava/app/event_json_support.h"
#include "ava/app/event_payload.h"
#include "ava/core/ids.h"

namespace ava::app {

std::string to_string(RuntimeEventType type)
{
  switch (type) {
    case RuntimeEventType::SessionStart:
      return "session_start";
    case RuntimeEventType::UserMessage:
      return "user_message";
    case RuntimeEventType::AssistantMessage:
      return "assistant_message";
    case RuntimeEventType::MessageUpdate:
      return "message_update";
    case RuntimeEventType::MessageEnd:
      return "message_end";
    case RuntimeEventType::ReasoningStart:
      return "reasoning_start";
    case RuntimeEventType::ReasoningDelta:
      return "reasoning_delta";
    case RuntimeEventType::ReasoningEnd:
      return "reasoning_end";
    case RuntimeEventType::ProviderEvent:
      return "provider_event";
    case RuntimeEventType::ToolStart:
      return "tool_start";
    case RuntimeEventType::ToolProgress:
      return "tool_progress";
    case RuntimeEventType::ToolResult:
      return "tool_result";
    case RuntimeEventType::CompactionStart:
      return "compaction_start";
    case RuntimeEventType::CompactionEnd:
      return "compaction_end";
    case RuntimeEventType::Retry:
      return "retry";
    case RuntimeEventType::RetryTick:
      return "retry_tick";
    case RuntimeEventType::Canceled:
      return "canceled";
    case RuntimeEventType::Error:
      return "error";
    case RuntimeEventType::Done:
      return "done";
  }
  return "error";
}

std::string serialize_event_json(RuntimeEvent const& event)
{
  std::string out = "{\"type\":\"" + to_string(event.type) + "\"";
  detail::append_event_string_field(out, "timestamp", event.timestamp);
  detail::append_event_string_field(out, "session_id", event.session_id);
  if (event.type == RuntimeEventType::SessionStart) {
    detail::append_event_string_field(out, "mode", ava::agent::to_string(event.mode));
    detail::append_event_string_field(out, "provider", event.provider_id);
    detail::append_event_string_field(out, "model", event.model_id);
  }
  detail::append_event_string_field(out, "text", event.text);
  detail::append_event_string_field(out, "call_id", event.call_id);
  detail::append_event_string_field(out, "tool", event.tool_name);
  detail::append_event_json_object_field(out, "args", event.tool_arguments_json);
  detail::append_event_json_object_field(out, "result", event.tool_result_json);
  detail::append_event_json_object_field(out, "structured_result", event.tool_structured_result_json);
  detail::append_event_string_field(out, "status", event.status);
  detail::append_event_string_field(out, "category", event.error_category);
  detail::append_event_string_field(out, "error_code", event.error_code);
  detail::append_event_string_field(out, "message", event.error_message);
  detail::append_event_string_field(out, "details", event.error_details);
  detail::append_event_string_field(out, "content_type", event.content_type);
  detail::append_event_string_field(out, "stop_reason", event.stop_reason);
  detail::append_event_string_field(out, "trigger", event.trigger);
  detail::append_event_string_field(out, "reason", event.reason);
  detail::append_event_string_field(out, "reasoning_format", event.reasoning_format);
  detail::append_event_string_field(out, "diff", event.diff);
  detail::append_event_string_array_field(out, "changed_paths", event.changed_paths);
  detail::append_event_string_field(out, "spill_path", event.spill_path);
  detail::append_event_bool_field(out, "reasoning_redacted", event.reasoning_redacted);
  detail::append_event_bool_field(out, "reasoning_signature_present", event.reasoning_signature_present);
  detail::append_event_bool_field(out, "diff_truncated", event.diff_truncated);
  detail::append_event_bool_field(out, "truncated", event.truncated);
  detail::append_event_bool_field(out, "spill_truncated", event.spill_truncated);
  detail::append_event_number_field(out, "provider_iterations", event.provider_iterations);
  detail::append_event_number_field(out, "tool_calls", event.tool_calls);
  detail::append_event_number_field(out, "attempt", event.attempt);
  detail::append_event_number_field(out, "max_attempts", event.max_attempts);
  detail::append_event_number_field(out, "delay_ms", event.delay_ms);
  detail::append_event_number_field(out, "remaining_ms", event.remaining_ms);
  detail::append_event_number_field(out, "estimated_tokens", event.estimated_tokens);
  detail::append_event_number_field(out, "threshold_tokens", event.threshold_tokens);
  detail::append_event_number_field(out, "summary_bytes", event.summary_bytes);
  detail::append_event_number_field(out, "snapshot_entries", event.snapshot_entries);
  detail::append_event_number_field(out, "current_entries", event.current_entries);
  detail::append_event_number_field(out, "output_bytes", event.output_bytes);
  detail::append_event_number_field(out, "total_bytes", event.total_bytes);
  detail::append_event_number_field(out, "omitted_bytes", event.omitted_bytes);
  detail::append_event_number_field(out, "omitted_lines", event.omitted_lines);
  detail::append_event_number_field(out, "visible_matches", event.visible_matches);
  detail::append_event_number_field(out, "total_matches", event.total_matches);
  out += '}';
  return out;
}

std::string serialize_event_jsonl(RuntimeEvent const& event)
{
  return serialize_event_json(event) + '\n';
}

ava::core::VoidResult emit_event(RuntimeEventSink const& sink, RuntimeEvent const& event)
{
  if (!sink) return {};
  return sink(event);
}

void EventBus::subscribe(EventEnvelopeSink sink)
{
  if (sink) sinks_.push_back(std::move(sink));
}

ava::core::VoidResult EventBus::publish(EventEnvelope const& envelope) const
{
  for (auto const& sink : sinks_) {
    if (auto published = sink(envelope); !published) return std::unexpected(std::move(published.error()));
  }
  return {};
}

EventEnvelope to_event_envelope(RuntimeEvent const& event, EventEnvelopeContext const& context)
{
  return EventEnvelope{.schema_version = 1,
                       .event_id = context.event_id ? *context.event_id : ava::core::make_id("event"),
                       .timestamp = event.timestamp,
                       .session_id = event.session_id,
                       .run_id = context.run_id,
                       .turn_id = context.turn_id,
                       .message_id = context.message_id,
                       .request_id = context.request_id,
                       .correlation_id = context.correlation_id,
                       .name = to_string(event.type),
                       .payload_json = runtime_event_payload_json(event)};
}

std::string serialize_event_envelope_json(EventEnvelope const& envelope)
{
  std::string out = "{\"schema_version\":" + std::to_string(envelope.schema_version);
  detail::append_event_required_string_field(out, "event_id", envelope.event_id);
  detail::append_event_required_string_field(out, "timestamp", envelope.timestamp);
  detail::append_event_required_string_field(out, "session_id", envelope.session_id);
  detail::append_event_optional_string_field(out, "run_id", envelope.run_id);
  detail::append_event_optional_string_field(out, "turn_id", envelope.turn_id);
  detail::append_event_optional_string_field(out, "message_id", envelope.message_id);
  detail::append_event_optional_string_field(out, "request_id", envelope.request_id);
  detail::append_event_optional_string_field(out, "correlation_id", envelope.correlation_id);
  detail::append_event_required_string_field(out, "name", envelope.name);
  detail::append_event_required_string_field(out, "type", envelope.name);
  out += ",\"payload\":";
  out += envelope.payload_json.empty() ? "{}" : envelope.payload_json;
  append_runtime_event_payload_aliases(out,
                                       envelope.payload_json.empty() ? std::string_view("{}") : envelope.payload_json);
  out += '}';
  return out;
}

std::string serialize_event_envelope_jsonl(EventEnvelope const& envelope)
{
  return serialize_event_envelope_json(envelope) + '\n';
}

RuntimeEventSink make_runtime_event_bus_adapter(EventBus& bus, EventEnvelopeContext context,
                                                RuntimeEventSink legacy_sink)
{
  return [&bus, context = std::move(context),
          legacy_sink = std::move(legacy_sink)](RuntimeEvent const& event) -> ava::core::VoidResult {
    auto envelope = to_event_envelope(event, context);
    if (auto published = bus.publish(envelope); !published) return std::unexpected(std::move(published.error()));
    return emit_event(legacy_sink, event);
  };
}

}  // namespace ava::app
