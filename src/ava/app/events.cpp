#include "ava/app/events.h"

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

}  // namespace

std::string to_string(RuntimeEventType type) {
  switch (type) {
    case RuntimeEventType::SessionStart:
      return "session_start";
    case RuntimeEventType::UserMessage:
      return "user_message";
    case RuntimeEventType::AssistantMessage:
      return "assistant_message";
    case RuntimeEventType::ToolStart:
      return "tool_start";
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

}  // namespace ava::app
