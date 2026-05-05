#include "ava/app/event_payload.h"

#include "ava/app/event_json_support.h"
#include "ava/core/json.h"

namespace ava::app {

std::string runtime_event_payload_json(RuntimeEvent const& event)
{
  std::string out = "{";
  bool has_field = false;
  if (event.type == RuntimeEventType::SessionStart) {
    detail::append_payload_string_field(out, has_field, "mode", ava::agent::to_string(event.mode));
    detail::append_payload_string_field(out, has_field, "provider", event.provider_id);
    detail::append_payload_string_field(out, has_field, "model", event.model_id);
  }
  detail::append_payload_string_field(out, has_field, "text", event.text);
  detail::append_payload_string_field(out, has_field, "call_id", event.call_id);
  detail::append_payload_string_field(out, has_field, "tool", event.tool_name);
  detail::append_payload_json_object_field(out, has_field, "args", event.tool_arguments_json);
  detail::append_payload_json_object_field(out, has_field, "result", event.tool_result_json);
  detail::append_payload_json_object_field(out, has_field, "structured_result", event.tool_structured_result_json);
  detail::append_payload_string_field(out, has_field, "status", event.status);
  detail::append_payload_string_field(out, has_field, "category", event.error_category);
  detail::append_payload_string_field(out, has_field, "error_code", event.error_code);
  detail::append_payload_string_field(out, has_field, "message", event.error_message);
  detail::append_payload_string_field(out, has_field, "details", event.error_details);
  detail::append_payload_string_field(out, has_field, "content_type", event.content_type);
  detail::append_payload_string_field(out, has_field, "stop_reason", event.stop_reason);
  detail::append_payload_string_field(out, has_field, "trigger", event.trigger);
  detail::append_payload_string_field(out, has_field, "reason", event.reason);
  detail::append_payload_string_field(out, has_field, "reasoning_format", event.reasoning_format);
  detail::append_payload_string_field(out, has_field, "diff", event.diff);
  detail::append_payload_string_array_field(out, has_field, "changed_paths", event.changed_paths);
  detail::append_payload_string_field(out, has_field, "spill_path", event.spill_path);
  detail::append_payload_bool_field(out, has_field, "reasoning_redacted", event.reasoning_redacted);
  detail::append_payload_bool_field(out, has_field, "reasoning_signature_present", event.reasoning_signature_present);
  detail::append_payload_bool_field(out, has_field, "diff_truncated", event.diff_truncated);
  detail::append_payload_bool_field(out, has_field, "truncated", event.truncated);
  detail::append_payload_bool_field(out, has_field, "spill_truncated", event.spill_truncated);
  detail::append_payload_number_field(out, has_field, "provider_iterations", event.provider_iterations);
  detail::append_payload_number_field(out, has_field, "tool_calls", event.tool_calls);
  detail::append_payload_number_field(out, has_field, "attempt", event.attempt);
  detail::append_payload_number_field(out, has_field, "max_attempts", event.max_attempts);
  detail::append_payload_number_field(out, has_field, "delay_ms", event.delay_ms);
  detail::append_payload_number_field(out, has_field, "remaining_ms", event.remaining_ms);
  detail::append_payload_number_field(out, has_field, "estimated_tokens", event.estimated_tokens);
  detail::append_payload_number_field(out, has_field, "threshold_tokens", event.threshold_tokens);
  detail::append_payload_number_field(out, has_field, "summary_bytes", event.summary_bytes);
  detail::append_payload_number_field(out, has_field, "snapshot_entries", event.snapshot_entries);
  detail::append_payload_number_field(out, has_field, "current_entries", event.current_entries);
  detail::append_payload_number_field(out, has_field, "output_bytes", event.output_bytes);
  detail::append_payload_number_field(out, has_field, "total_bytes", event.total_bytes);
  detail::append_payload_number_field(out, has_field, "omitted_bytes", event.omitted_bytes);
  detail::append_payload_number_field(out, has_field, "omitted_lines", event.omitted_lines);
  detail::append_payload_number_field(out, has_field, "visible_matches", event.visible_matches);
  detail::append_payload_number_field(out, has_field, "total_matches", event.total_matches);
  out += '}';
  return out;
}

void append_runtime_event_payload_aliases(std::string& out, std::string_view payload_json)
{
  for (std::string_view key :
       {"mode", "provider", "model", "text", "call_id", "tool", "status", "category", "error_code", "message",
        "details", "content_type", "stop_reason", "trigger", "reason", "reasoning_format", "diff", "spill_path"}) {
    if (auto value = ava::core::json::string_field(payload_json, key); value && !value->empty()) {
      detail::append_event_required_string_field(out, key, *value);
    }
  }
  for (std::string_view key :
       {"provider_iterations", "tool_calls", "attempt", "max_attempts", "delay_ms", "remaining_ms", "estimated_tokens",
        "threshold_tokens", "summary_bytes", "snapshot_entries", "current_entries", "output_bytes", "total_bytes",
        "omitted_bytes", "omitted_lines", "visible_matches", "total_matches"}) {
    if (auto value = ava::core::json::integer_field(payload_json, key); value && *value > 0) {
      detail::append_event_number_field(out, key, static_cast<std::size_t>(*value));
    }
  }
}

}  // namespace ava::app
