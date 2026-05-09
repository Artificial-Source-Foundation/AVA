#include "ava/app/events.h"

#include "ava/core/ids.h"
#include "ava/core/json.h"

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

void append_required_string_field(std::string& out, std::string_view key, std::string_view value)
{
  out += ",\"";
  out += key;
  out += "\":\"";
  out += ava::core::json::escape(value);
  out += '"';
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

void append_optional_string_field(std::string& out, std::string_view key, std::optional<std::string> const& value)
{
  if (!value || value->empty())
    return;
  append_required_string_field(out, key, *value);
}

void append_payload_string_field(std::string& out, bool& has_field, std::string_view key, std::string_view value)
{
  if (value.empty())
    return;
  if (has_field)
    out += ',';
  out += '"';
  out += key;
  out += "\":\"";
  out += ava::core::json::escape(value);
  out += '"';
  has_field = true;
}

void append_payload_number_field(std::string& out, bool& has_field, std::string_view key, std::size_t value)
{
  if (value == 0)
    return;
  if (has_field)
    out += ',';
  out += '"';
  out += key;
  out += "\":";
  out += std::to_string(value);
  has_field = true;
}

void append_payload_bool_field(std::string& out, bool& has_field, std::string_view key, bool value)
{
  if (!value)
    return;
  if (has_field)
    out += ',';
  out += '"';
  out += key;
  out += "\":true";
  has_field = true;
}

void append_payload_json_object_field(std::string& out, bool& has_field, std::string_view key, std::string_view value)
{
  if (value.empty())
    return;
  if (has_field)
    out += ',';
  out += '"';
  out += key;
  if (ava::core::json::is_valid_object(value))
  {
    out += "\":";
    out += value;
  }
  else
  {
    out += "_json\":\"";
    out += ava::core::json::escape(value);
    out += '"';
  }
  has_field = true;
}

void append_payload_string_array_field(std::string& out, bool& has_field, std::string_view key, std::vector<std::string> const& values)
{
  if (values.empty())
    return;
  if (has_field)
    out += ',';
  out += '"';
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
  has_field = true;
}

std::string generic_payload_json_for_runtime_event(RuntimeEvent const& event)
{
  std::string out = "{";
  bool has_field = false;
  if (event.type == RuntimeEventType::SessionStart)
  {
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
  append_payload_string_array_field(out, has_field, "permission_request_ids", event.permission_request_ids);
  append_payload_string_field(out, has_field, "spill_path", event.spill_path);
  append_payload_bool_field(out, has_field, "reasoning_redacted", event.reasoning_redacted);
  append_payload_bool_field(out, has_field, "reasoning_signature_present", event.reasoning_signature_present);
  append_payload_bool_field(out, has_field, "diff_truncated", event.diff_truncated);
  append_payload_bool_field(out, has_field, "truncated", event.truncated);
  append_payload_bool_field(out, has_field, "byte_limited", event.byte_limited);
  append_payload_bool_field(out, has_field, "line_limited", event.line_limited);
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
  append_payload_number_field(out, has_field, "output_lines", event.output_lines);
  append_payload_number_field(out, has_field, "total_lines", event.total_lines);
  append_payload_number_field(out, has_field, "start_line", event.start_line);
  append_payload_number_field(out, has_field, "end_line", event.end_line);
  append_payload_number_field(out, has_field, "next_offset_line", event.next_offset_line);
  append_payload_number_field(out, has_field, "omitted_bytes", event.omitted_bytes);
  append_payload_number_field(out, has_field, "omitted_lines", event.omitted_lines);
  append_payload_number_field(out, has_field, "visible_matches", event.visible_matches);
  append_payload_number_field(out, has_field, "total_matches", event.total_matches);
  out += '}';
  return out;
}

std::string payload_json_for_runtime_event(RuntimeEvent const& event)
{
  switch (event.type)
  {
    case RuntimeEventType::ToolStart:
    case RuntimeEventType::ToolProgress:
    case RuntimeEventType::ToolResult:
      return serialize_payload_json(tool_payload_from_event(event));
    case RuntimeEventType::Retry:
    case RuntimeEventType::RetryTick:
      return serialize_payload_json(retry_payload_from_event(event));
    case RuntimeEventType::Canceled:
      return serialize_payload_json(cancellation_payload_from_event(event));
    case RuntimeEventType::Error:
      return serialize_payload_json(error_payload_from_event(event));
    case RuntimeEventType::Done:
      return serialize_payload_json(completion_payload_from_event(event));
    case RuntimeEventType::SessionStart:
    case RuntimeEventType::UserMessage:
    case RuntimeEventType::AssistantMessage:
    case RuntimeEventType::MessageUpdate:
    case RuntimeEventType::MessageEnd:
    case RuntimeEventType::ReasoningStart:
    case RuntimeEventType::ReasoningDelta:
    case RuntimeEventType::ReasoningEnd:
    case RuntimeEventType::ProviderEvent:
    case RuntimeEventType::CompactionStart:
    case RuntimeEventType::CompactionEnd:
      return generic_payload_json_for_runtime_event(event);
  }
  return generic_payload_json_for_runtime_event(event);
}

void append_payload_aliases(std::string& out, std::string_view payload_json)
{
  for (std::string_view key : {"mode", "provider", "model", "text", "call_id", "tool", "status", "category", "error_code", "message", "details", "content_type",
                               "stop_reason", "trigger", "reason", "reasoning_format", "diff", "spill_path"})
  {
    if (auto value = ava::core::json::string_field(payload_json, key); value && !value->empty())
    {
      append_required_string_field(out, key, *value);
    }
  }
  for (std::string_view key : {"provider_iterations", "tool_calls",       "attempt",         "max_attempts",     "delay_ms",        "remaining_ms",
                               "estimated_tokens",    "threshold_tokens", "summary_bytes",   "snapshot_entries", "current_entries", "output_bytes",
                               "total_bytes",         "output_lines",     "total_lines",     "start_line",       "end_line",        "next_offset_line",
                               "omitted_bytes",       "omitted_lines",    "visible_matches", "total_matches"})
  {
    if (auto value = ava::core::json::integer_field(payload_json, key); value && *value > 0)
    {
      out += ",\"";
      out += key;
      out += "\":";
      out += std::to_string(*value);
    }
  }
}

}  // namespace

std::string to_string(RuntimeEventType type)
{
  switch (type)
  {
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

std::string_view to_string(RuntimePayloadType type) noexcept
{
  switch (type)
  {
    case RuntimePayloadType::Session:
      return "session";
    case RuntimePayloadType::Message:
      return "message";
    case RuntimePayloadType::Reasoning:
      return "reasoning";
    case RuntimePayloadType::Provider:
      return "provider";
    case RuntimePayloadType::Tool:
      return "tool";
    case RuntimePayloadType::Compaction:
      return "compaction";
    case RuntimePayloadType::Retry:
      return "retry";
    case RuntimePayloadType::Cancellation:
      return "cancellation";
    case RuntimePayloadType::Error:
      return "error";
    case RuntimePayloadType::Completion:
      return "completion";
    case RuntimePayloadType::Permission:
      return "permission";
    case RuntimePayloadType::Question:
      return "question";
    case RuntimePayloadType::Queue:
      return "queue";
  }
  return "error";
}

RuntimePayloadType payload_type_for_event(RuntimeEventType type) noexcept
{
  switch (type)
  {
    case RuntimeEventType::SessionStart:
      return RuntimePayloadType::Session;
    case RuntimeEventType::UserMessage:
    case RuntimeEventType::AssistantMessage:
    case RuntimeEventType::MessageUpdate:
    case RuntimeEventType::MessageEnd:
      return RuntimePayloadType::Message;
    case RuntimeEventType::ReasoningStart:
    case RuntimeEventType::ReasoningDelta:
    case RuntimeEventType::ReasoningEnd:
      return RuntimePayloadType::Reasoning;
    case RuntimeEventType::ProviderEvent:
      return RuntimePayloadType::Provider;
    case RuntimeEventType::ToolStart:
    case RuntimeEventType::ToolProgress:
    case RuntimeEventType::ToolResult:
      return RuntimePayloadType::Tool;
    case RuntimeEventType::CompactionStart:
    case RuntimeEventType::CompactionEnd:
      return RuntimePayloadType::Compaction;
    case RuntimeEventType::Retry:
    case RuntimeEventType::RetryTick:
      return RuntimePayloadType::Retry;
    case RuntimeEventType::Canceled:
      return RuntimePayloadType::Cancellation;
    case RuntimeEventType::Error:
      return RuntimePayloadType::Error;
    case RuntimeEventType::Done:
      return RuntimePayloadType::Completion;
  }
  return RuntimePayloadType::Error;
}

RuntimeToolPayload tool_payload_from_event(RuntimeEvent const& event)
{
  return RuntimeToolPayload{.text = event.text,
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

RuntimeRetryPayload retry_payload_from_event(RuntimeEvent const& event)
{
  return RuntimeRetryPayload{.text = event.text,
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

RuntimeCancellationPayload cancellation_payload_from_event(RuntimeEvent const& event)
{
  return RuntimeCancellationPayload{.text = event.text,
                                    .status = event.status,
                                    .error_category = event.error_category,
                                    .error_code = event.error_code,
                                    .error_message = event.error_message,
                                    .error_details = event.error_details,
                                    .trigger = event.trigger,
                                    .reason = event.reason};
}

RuntimeErrorPayload error_payload_from_event(RuntimeEvent const& event)
{
  return RuntimeErrorPayload{.text = event.text,
                             .status = event.status,
                             .error_category = event.error_category,
                             .error_code = event.error_code,
                             .error_message = event.error_message,
                             .error_details = event.error_details,
                             .content_type = event.content_type,
                             .trigger = event.trigger,
                             .reason = event.reason};
}

RuntimeCompletionPayload completion_payload_from_event(RuntimeEvent const& event)
{
  return RuntimeCompletionPayload{.status = event.status,
                                  .stop_reason = event.stop_reason,
                                  .reason = event.reason,
                                  .provider_iterations = event.provider_iterations,
                                  .tool_calls = event.tool_calls};
}

std::string serialize_payload_json(RuntimeToolPayload const& payload)
{
  std::string out = "{";
  bool has_field = false;
  append_payload_string_field(out, has_field, "text", payload.text);
  append_payload_string_field(out, has_field, "call_id", payload.call_id);
  append_payload_string_field(out, has_field, "tool", payload.tool);
  append_payload_json_object_field(out, has_field, "args", payload.args_json);
  append_payload_json_object_field(out, has_field, "result", payload.result_json);
  append_payload_json_object_field(out, has_field, "structured_result", payload.structured_result_json);
  append_payload_string_field(out, has_field, "status", payload.status);
  append_payload_string_field(out, has_field, "category", payload.error_category);
  append_payload_string_field(out, has_field, "error_code", payload.error_code);
  append_payload_string_field(out, has_field, "message", payload.error_message);
  append_payload_string_field(out, has_field, "details", payload.error_details);
  append_payload_string_field(out, has_field, "content_type", payload.content_type);
  append_payload_string_field(out, has_field, "diff", payload.diff);
  append_payload_string_array_field(out, has_field, "changed_paths", payload.changed_paths);
  append_payload_string_array_field(out, has_field, "permission_request_ids", payload.permission_request_ids);
  append_payload_string_field(out, has_field, "spill_path", payload.spill_path);
  append_payload_bool_field(out, has_field, "diff_truncated", payload.diff_truncated);
  append_payload_bool_field(out, has_field, "truncated", payload.truncated);
  append_payload_bool_field(out, has_field, "byte_limited", payload.byte_limited);
  append_payload_bool_field(out, has_field, "line_limited", payload.line_limited);
  append_payload_bool_field(out, has_field, "spill_truncated", payload.spill_truncated);
  append_payload_number_field(out, has_field, "output_bytes", payload.output_bytes);
  append_payload_number_field(out, has_field, "total_bytes", payload.total_bytes);
  append_payload_number_field(out, has_field, "output_lines", payload.output_lines);
  append_payload_number_field(out, has_field, "total_lines", payload.total_lines);
  append_payload_number_field(out, has_field, "start_line", payload.start_line);
  append_payload_number_field(out, has_field, "end_line", payload.end_line);
  append_payload_number_field(out, has_field, "next_offset_line", payload.next_offset_line);
  append_payload_number_field(out, has_field, "omitted_bytes", payload.omitted_bytes);
  append_payload_number_field(out, has_field, "omitted_lines", payload.omitted_lines);
  append_payload_number_field(out, has_field, "visible_matches", payload.visible_matches);
  append_payload_number_field(out, has_field, "total_matches", payload.total_matches);
  out += '}';
  return out;
}

std::string serialize_payload_json(RuntimeRetryPayload const& payload)
{
  std::string out = "{";
  bool has_field = false;
  append_payload_string_field(out, has_field, "text", payload.text);
  append_payload_string_field(out, has_field, "status", payload.status);
  append_payload_string_field(out, has_field, "category", payload.error_category);
  append_payload_string_field(out, has_field, "error_code", payload.error_code);
  append_payload_string_field(out, has_field, "message", payload.error_message);
  append_payload_string_field(out, has_field, "details", payload.error_details);
  append_payload_string_field(out, has_field, "trigger", payload.trigger);
  append_payload_string_field(out, has_field, "reason", payload.reason);
  append_payload_number_field(out, has_field, "attempt", payload.attempt);
  append_payload_number_field(out, has_field, "max_attempts", payload.max_attempts);
  append_payload_number_field(out, has_field, "delay_ms", payload.delay_ms);
  append_payload_number_field(out, has_field, "remaining_ms", payload.remaining_ms);
  out += '}';
  return out;
}

std::string serialize_payload_json(RuntimeCancellationPayload const& payload)
{
  std::string out = "{";
  bool has_field = false;
  append_payload_string_field(out, has_field, "text", payload.text);
  append_payload_string_field(out, has_field, "status", payload.status);
  append_payload_string_field(out, has_field, "category", payload.error_category);
  append_payload_string_field(out, has_field, "error_code", payload.error_code);
  append_payload_string_field(out, has_field, "message", payload.error_message);
  append_payload_string_field(out, has_field, "details", payload.error_details);
  append_payload_string_field(out, has_field, "trigger", payload.trigger);
  append_payload_string_field(out, has_field, "reason", payload.reason);
  out += '}';
  return out;
}

std::string serialize_payload_json(RuntimeErrorPayload const& payload)
{
  std::string out = "{";
  bool has_field = false;
  append_payload_string_field(out, has_field, "text", payload.text);
  append_payload_string_field(out, has_field, "status", payload.status);
  append_payload_string_field(out, has_field, "category", payload.error_category);
  append_payload_string_field(out, has_field, "error_code", payload.error_code);
  append_payload_string_field(out, has_field, "message", payload.error_message);
  append_payload_string_field(out, has_field, "details", payload.error_details);
  append_payload_string_field(out, has_field, "content_type", payload.content_type);
  append_payload_string_field(out, has_field, "trigger", payload.trigger);
  append_payload_string_field(out, has_field, "reason", payload.reason);
  out += '}';
  return out;
}

std::string serialize_payload_json(RuntimeCompletionPayload const& payload)
{
  std::string out = "{";
  bool has_field = false;
  append_payload_string_field(out, has_field, "status", payload.status);
  append_payload_string_field(out, has_field, "stop_reason", payload.stop_reason);
  append_payload_string_field(out, has_field, "reason", payload.reason);
  append_payload_number_field(out, has_field, "provider_iterations", payload.provider_iterations);
  append_payload_number_field(out, has_field, "tool_calls", payload.tool_calls);
  out += '}';
  return out;
}

std::string serialize_event_json(RuntimeEvent const& event)
{
  std::string out = "{\"type\":\"" + to_string(event.type) + "\"";
  append_string_field(out, "timestamp", event.timestamp);
  append_string_field(out, "session_id", event.session_id);
  if (event.type == RuntimeEventType::SessionStart)
  {
    append_string_field(out, "mode", ava::agent::to_string(event.mode));
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

std::string serialize_event_jsonl(RuntimeEvent const& event)
{
  return serialize_event_json(event) + '\n';
}

ava::core::VoidResult emit_event(RuntimeEventSink const& sink, RuntimeEvent const& event)
{
  if (!sink)
    return {};
  return sink(event);
}

void EventBus::subscribe(EventEnvelopeSink sink)
{
  if (sink)
    sinks_.push_back(std::move(sink));
}

ava::core::VoidResult EventBus::publish(EventEnvelope const& envelope) const
{
  for (auto const& sink : sinks_)
  {
    if (auto published = sink(envelope); !published)
      return std::unexpected(std::move(published.error()));
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
                       .payload_json = payload_json_for_runtime_event(event),
                       .payload_type = std::string(to_string(payload_type_for_event(event.type)))};
}

std::string serialize_event_envelope_json(EventEnvelope const& envelope)
{
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
  append_string_field(out, "payload_type", envelope.payload_type);
  out += ",\"payload\":";
  out += envelope.payload_json.empty() ? "{}" : envelope.payload_json;
  append_payload_aliases(out, envelope.payload_json.empty() ? std::string_view("{}") : envelope.payload_json);
  out += '}';
  return out;
}

std::string serialize_event_envelope_jsonl(EventEnvelope const& envelope)
{
  return serialize_event_envelope_json(envelope) + '\n';
}

RuntimeEventSink make_runtime_event_bus_adapter(EventBus& bus, EventEnvelopeContext context, RuntimeEventSink legacy_sink)
{
  return [&bus, context = std::move(context), legacy_sink = std::move(legacy_sink)](RuntimeEvent const& event) -> ava::core::VoidResult {
    auto envelope = to_event_envelope(event, context);
    if (auto published = bus.publish(envelope); !published)
      return std::unexpected(std::move(published.error()));
    return emit_event(legacy_sink, event);
  };
}

}  // namespace ava::app
