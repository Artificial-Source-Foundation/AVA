#include "ava/app/events.h"

#include <utility>

#include "ava/core/ids.h"
#include "ava/core/json.h"

namespace ava::app {
namespace {

void append_string_field(std::string& out, std::string_view key, std::string_view value) {
  if (value.empty()) return;
  out += ",\"";
  out += key;
  out += "\":\"";
  out += ava::core::json::escape(value);
  out += '"';
}

void append_number_field(std::string& out, std::string_view key, std::size_t value) {
  if (value == 0) return;
  out += ",\"";
  out += key;
  out += "\":";
  out += std::to_string(value);
}

void append_bool_field(std::string& out, std::string_view key, bool value) {
  if (!value) return;
  out += ",\"";
  out += key;
  out += "\":true";
}

void append_required_string_field(std::string& out, std::string_view key, std::string_view value) {
  out += ",\"";
  out += key;
  out += "\":\"";
  out += ava::core::json::escape(value);
  out += '"';
}

void append_optional_string_field(std::string& out, std::string_view key, const std::optional<std::string>& value) {
  if (!value || value->empty()) return;
  append_required_string_field(out, key, *value);
}

void append_payload_string_field(std::string& out, bool& has_field, std::string_view key, std::string_view value) {
  if (value.empty()) return;
  if (has_field) out += ',';
  out += '"';
  out += key;
  out += "\":\"";
  out += ava::core::json::escape(value);
  out += '"';
  has_field = true;
}

void append_payload_number_field(std::string& out, bool& has_field, std::string_view key, std::size_t value) {
  if (value == 0) return;
  if (has_field) out += ',';
  out += '"';
  out += key;
  out += "\":";
  out += std::to_string(value);
  has_field = true;
}

void append_payload_bool_field(std::string& out, bool& has_field, std::string_view key, bool value) {
  if (!value) return;
  if (has_field) out += ',';
  out += '"';
  out += key;
  out += "\":true";
  has_field = true;
}

std::string payload_json_for_runtime_event(const RuntimeEvent& event) {
  std::string out = "{";
  bool has_field = false;
  if (event.type == RuntimeEventType::SessionStart) {
    append_payload_string_field(out, has_field, "mode", ava::agent::to_string(event.mode));
    append_payload_string_field(out, has_field, "provider", event.provider_id);
    append_payload_string_field(out, has_field, "model", event.model_id);
  }
  append_payload_string_field(out, has_field, "text", event.text);
  append_payload_string_field(out, has_field, "call_id", event.call_id);
  append_payload_string_field(out, has_field, "tool", event.tool_name);
  append_payload_string_field(out, has_field, "status", event.status);
  append_payload_string_field(out, has_field, "category", event.error_category);
  append_payload_string_field(out, has_field, "message", event.error_message);
  append_payload_string_field(out, has_field, "details", event.error_details);
  append_payload_string_field(out, has_field, "stop_reason", event.stop_reason);
  append_payload_string_field(out, has_field, "reasoning_format", event.reasoning_format);
  append_payload_bool_field(out, has_field, "reasoning_redacted", event.reasoning_redacted);
  append_payload_bool_field(out, has_field, "reasoning_signature_present", event.reasoning_signature_present);
  append_payload_number_field(out, has_field, "provider_iterations", event.provider_iterations);
  append_payload_number_field(out, has_field, "tool_calls", event.tool_calls);
  out += '}';
  return out;
}

void append_payload_aliases(std::string& out, std::string_view payload_json) {
  for (std::string_view key : {"mode", "provider", "model", "text", "call_id", "tool", "status", "category", "message",
                               "details", "stop_reason", "reasoning_format"}) {
    if (auto value = ava::core::json::string_field(payload_json, key); value && !value->empty()) {
      append_required_string_field(out, key, *value);
    }
  }
  if (auto value = ava::core::json::integer_field(payload_json, "provider_iterations"); value && *value > 0) {
    out += ",\"provider_iterations\":";
    out += std::to_string(*value);
  }
  if (auto value = ava::core::json::integer_field(payload_json, "tool_calls"); value && *value > 0) {
    out += ",\"tool_calls\":";
    out += std::to_string(*value);
  }
}

}  // namespace

std::string to_string(RuntimeEventType type) {
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
    case RuntimeEventType::Error:
      return "error";
    case RuntimeEventType::Done:
      return "done";
  }
  return "error";
}

std::string serialize_event_json(const RuntimeEvent& event) {
  std::string out = "{\"type\":\"" + to_string(event.type) + "\"";
  append_string_field(out, "timestamp", event.timestamp);
  append_string_field(out, "session_id", event.session_id);
  if (event.type == RuntimeEventType::SessionStart) {
    append_string_field(out, "mode", ava::agent::to_string(event.mode));
    append_string_field(out, "provider", event.provider_id);
    append_string_field(out, "model", event.model_id);
  }
  append_string_field(out, "text", event.text);
  append_string_field(out, "call_id", event.call_id);
  append_string_field(out, "tool", event.tool_name);
  append_string_field(out, "status", event.status);
  append_string_field(out, "category", event.error_category);
  append_string_field(out, "message", event.error_message);
  append_string_field(out, "details", event.error_details);
  append_string_field(out, "stop_reason", event.stop_reason);
  append_string_field(out, "reasoning_format", event.reasoning_format);
  append_bool_field(out, "reasoning_redacted", event.reasoning_redacted);
  append_bool_field(out, "reasoning_signature_present", event.reasoning_signature_present);
  append_number_field(out, "provider_iterations", event.provider_iterations);
  append_number_field(out, "tool_calls", event.tool_calls);
  out += '}';
  return out;
}

std::string serialize_event_jsonl(const RuntimeEvent& event) { return serialize_event_json(event) + '\n'; }

ava::core::VoidResult emit_event(const RuntimeEventSink& sink, const RuntimeEvent& event) {
  if (!sink) return {};
  return sink(event);
}

void EventBus::subscribe(EventEnvelopeSink sink) {
  if (sink) sinks_.push_back(std::move(sink));
}

ava::core::VoidResult EventBus::publish(const EventEnvelope& envelope) const {
  for (const auto& sink : sinks_) {
    if (auto published = sink(envelope); !published) return std::unexpected(std::move(published.error()));
  }
  return {};
}

EventEnvelope to_event_envelope(const RuntimeEvent& event, const EventEnvelopeContext& context) {
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
                       .payload_json = payload_json_for_runtime_event(event)};
}

std::string serialize_event_envelope_json(const EventEnvelope& envelope) {
  std::string out = "{\"schema_version\":" + std::to_string(envelope.schema_version);
  append_required_string_field(out, "event_id", envelope.event_id);
  append_required_string_field(out, "timestamp", envelope.timestamp);
  append_required_string_field(out, "session_id", envelope.session_id);
  append_optional_string_field(out, "run_id", envelope.run_id);
  append_optional_string_field(out, "turn_id", envelope.turn_id);
  append_optional_string_field(out, "message_id", envelope.message_id);
  append_optional_string_field(out, "request_id", envelope.request_id);
  append_optional_string_field(out, "correlation_id", envelope.correlation_id);
  append_required_string_field(out, "name", envelope.name);
  append_required_string_field(out, "type", envelope.name);
  out += ",\"payload\":";
  out += envelope.payload_json.empty() ? "{}" : envelope.payload_json;
  append_payload_aliases(out, envelope.payload_json.empty() ? std::string_view("{}") : envelope.payload_json);
  out += '}';
  return out;
}

std::string serialize_event_envelope_jsonl(const EventEnvelope& envelope) {
  return serialize_event_envelope_json(envelope) + '\n';
}

RuntimeEventSink make_runtime_event_bus_adapter(EventBus& bus, EventEnvelopeContext context,
                                                RuntimeEventSink legacy_sink) {
  return [&bus, context = std::move(context),
          legacy_sink = std::move(legacy_sink)](const RuntimeEvent& event) -> ava::core::VoidResult {
    auto envelope = to_event_envelope(event, context);
    if (auto published = bus.publish(envelope); !published) return std::unexpected(std::move(published.error()));
    return emit_event(legacy_sink, event);
  };
}

}  // namespace ava::app
