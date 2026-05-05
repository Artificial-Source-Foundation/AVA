#include "ava/app/event_payload.h"

#include <vector>

#include "ava/core/json.h"

namespace ava::app {
namespace {

void append_required_string_field(std::string& out, std::string_view key, std::string_view value)
{
  out += ",\"";
  out += key;
  out += "\":\"";
  out += ava::core::json::escape(value);
  out += '"';
}

void append_payload_string_field(std::string& out, bool& has_field, std::string_view key, std::string_view value)
{
  if (value.empty()) return;
  if (has_field) out += ',';
  out += '"';
  out += key;
  out += "\":\"";
  out += ava::core::json::escape(value);
  out += '"';
  has_field = true;
}

void append_payload_number_field(std::string& out, bool& has_field, std::string_view key, std::size_t value)
{
  if (value == 0) return;
  if (has_field) out += ',';
  out += '"';
  out += key;
  out += "\":";
  out += std::to_string(value);
  has_field = true;
}

void append_payload_bool_field(std::string& out, bool& has_field, std::string_view key, bool value)
{
  if (!value) return;
  if (has_field) out += ',';
  out += '"';
  out += key;
  out += "\":true";
  has_field = true;
}

void append_payload_json_object_field(std::string& out, bool& has_field, std::string_view key, std::string_view value)
{
  if (value.empty()) return;
  if (has_field) out += ',';
  out += '"';
  out += key;
  if (ava::core::json::is_valid_object(value)) {
    out += "\":";
    out += value;
  } else {
    out += "_json\":\"";
    out += ava::core::json::escape(value);
    out += '"';
  }
  has_field = true;
}

void append_payload_string_array_field(std::string& out, bool& has_field, std::string_view key,
                                       std::vector<std::string> const& values)
{
  if (values.empty()) return;
  if (has_field) out += ',';
  out += '"';
  out += key;
  out += "\":[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) out += ',';
    out += '"';
    out += ava::core::json::escape(values[index]);
    out += '"';
  }
  out += ']';
  has_field = true;
}

}  // namespace

std::string runtime_event_payload_json(RuntimeEvent const& event)
{
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
  append_payload_json_object_field(out, has_field, "args", event.tool_arguments_json);
  append_payload_json_object_field(out, has_field, "result", event.tool_result_json);
  append_payload_json_object_field(out, has_field, "structured_result", event.tool_structured_result_json);
  append_payload_string_field(out, has_field, "status", event.status);
  append_payload_string_field(out, has_field, "category", event.error_category);
  append_payload_string_field(out, has_field, "error_code", event.error_code);
  append_payload_string_field(out, has_field, "message", event.error_message);
  append_payload_string_field(out, has_field, "details", event.error_details);
  append_payload_string_field(out, has_field, "content_type", event.content_type);
  append_payload_string_field(out, has_field, "stop_reason", event.stop_reason);
  append_payload_string_field(out, has_field, "trigger", event.trigger);
  append_payload_string_field(out, has_field, "reason", event.reason);
  append_payload_string_field(out, has_field, "reasoning_format", event.reasoning_format);
  append_payload_string_field(out, has_field, "diff", event.diff);
  append_payload_string_array_field(out, has_field, "changed_paths", event.changed_paths);
  append_payload_string_field(out, has_field, "spill_path", event.spill_path);
  append_payload_bool_field(out, has_field, "reasoning_redacted", event.reasoning_redacted);
  append_payload_bool_field(out, has_field, "reasoning_signature_present", event.reasoning_signature_present);
  append_payload_bool_field(out, has_field, "diff_truncated", event.diff_truncated);
  append_payload_bool_field(out, has_field, "truncated", event.truncated);
  append_payload_bool_field(out, has_field, "spill_truncated", event.spill_truncated);
  append_payload_number_field(out, has_field, "provider_iterations", event.provider_iterations);
  append_payload_number_field(out, has_field, "tool_calls", event.tool_calls);
  append_payload_number_field(out, has_field, "attempt", event.attempt);
  append_payload_number_field(out, has_field, "max_attempts", event.max_attempts);
  append_payload_number_field(out, has_field, "delay_ms", event.delay_ms);
  append_payload_number_field(out, has_field, "remaining_ms", event.remaining_ms);
  append_payload_number_field(out, has_field, "estimated_tokens", event.estimated_tokens);
  append_payload_number_field(out, has_field, "threshold_tokens", event.threshold_tokens);
  append_payload_number_field(out, has_field, "summary_bytes", event.summary_bytes);
  append_payload_number_field(out, has_field, "snapshot_entries", event.snapshot_entries);
  append_payload_number_field(out, has_field, "current_entries", event.current_entries);
  append_payload_number_field(out, has_field, "output_bytes", event.output_bytes);
  append_payload_number_field(out, has_field, "total_bytes", event.total_bytes);
  append_payload_number_field(out, has_field, "omitted_bytes", event.omitted_bytes);
  append_payload_number_field(out, has_field, "omitted_lines", event.omitted_lines);
  append_payload_number_field(out, has_field, "visible_matches", event.visible_matches);
  append_payload_number_field(out, has_field, "total_matches", event.total_matches);
  out += '}';
  return out;
}

void append_runtime_event_payload_aliases(std::string& out, std::string_view payload_json)
{
  for (std::string_view key :
       {"mode", "provider", "model", "text", "call_id", "tool", "status", "category", "error_code", "message",
        "details", "content_type", "stop_reason", "trigger", "reason", "reasoning_format", "diff", "spill_path"}) {
    if (auto value = ava::core::json::string_field(payload_json, key); value && !value->empty()) {
      append_required_string_field(out, key, *value);
    }
  }
  for (std::string_view key :
       {"provider_iterations", "tool_calls", "attempt", "max_attempts", "delay_ms", "remaining_ms", "estimated_tokens",
        "threshold_tokens", "summary_bytes", "snapshot_entries", "current_entries", "output_bytes", "total_bytes",
        "omitted_bytes", "omitted_lines", "visible_matches", "total_matches"}) {
    if (auto value = ava::core::json::integer_field(payload_json, key); value && *value > 0) {
      out += ",\"";
      out += key;
      out += "\":";
      out += std::to_string(*value);
    }
  }
}

}  // namespace ava::app
