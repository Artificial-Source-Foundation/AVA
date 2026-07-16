#include "sys.h"
#include "EventEnvelope.h"
#include "runtime/RuntimeCancellationPayload.h"
#include "runtime/RuntimeCompletionPayload.h"
#include "runtime/RuntimeErrorPayload.h"
#include "runtime/RuntimeRetryPayload.h"
#include "runtime/RuntimeToolPayload.h"
#include "events.h"
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

std::string generic_payload_json_for_runtime_event(runtime::RuntimeEvent const& event)
{
  std::string out = "{";
  bool has_field = false;
  if (event.type == runtime::RuntimeEventType::SessionStart)
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

std::string payload_json_for_runtime_event(runtime::RuntimeEvent const& event)
{
  using runtime::RuntimeEventType;

  switch (event.type)
  {
    case runtime::RuntimeEventType::ToolStart:
    case runtime::RuntimeEventType::ToolProgress:
    case runtime::RuntimeEventType::ToolResult:
      return serialize_payload_json(tool_payload_from_event(event));
    case runtime::RuntimeEventType::Retry:
    case runtime::RuntimeEventType::RetryTick:
      return serialize_payload_json(retry_payload_from_event(event));
    case runtime::RuntimeEventType::Canceled:
      return serialize_payload_json(cancellation_payload_from_event(event));
    case runtime::RuntimeEventType::Error:
      return serialize_payload_json(error_payload_from_event(event));
    case runtime::RuntimeEventType::Done:
      return serialize_payload_json(completion_payload_from_event(event));
    case runtime::RuntimeEventType::SessionStart:
    case runtime::RuntimeEventType::UserMessage:
    case runtime::RuntimeEventType::AssistantMessage:
    case runtime::RuntimeEventType::MessageUpdate:
    case runtime::RuntimeEventType::MessageEnd:
    case runtime::RuntimeEventType::ReasoningStart:
    case runtime::RuntimeEventType::ReasoningDelta:
    case runtime::RuntimeEventType::ReasoningEnd:
    case runtime::RuntimeEventType::ProviderEvent:
    case runtime::RuntimeEventType::CompactionStart:
    case runtime::RuntimeEventType::CompactionEnd:
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

std::string to_string(runtime::RuntimeEventType type)
{
  using runtime::RuntimeEventType;

  switch (type)
  {
    case runtime::RuntimeEventType::SessionStart:
      return "session_start";
    case runtime::RuntimeEventType::UserMessage:
      return "user_message";
    case runtime::RuntimeEventType::AssistantMessage:
      return "assistant_message";
    case runtime::RuntimeEventType::MessageUpdate:
      return "message_update";
    case runtime::RuntimeEventType::MessageEnd:
      return "message_end";
    case runtime::RuntimeEventType::ReasoningStart:
      return "reasoning_start";
    case runtime::RuntimeEventType::ReasoningDelta:
      return "reasoning_delta";
    case runtime::RuntimeEventType::ReasoningEnd:
      return "reasoning_end";
    case runtime::RuntimeEventType::ProviderEvent:
      return "provider_event";
    case runtime::RuntimeEventType::ToolStart:
      return "tool_start";
    case runtime::RuntimeEventType::ToolProgress:
      return "tool_progress";
    case runtime::RuntimeEventType::ToolResult:
      return "tool_result";
    case runtime::RuntimeEventType::CompactionStart:
      return "compaction_start";
    case runtime::RuntimeEventType::CompactionEnd:
      return "compaction_end";
    case runtime::RuntimeEventType::Retry:
      return "retry";
    case runtime::RuntimeEventType::RetryTick:
      return "retry_tick";
    case runtime::RuntimeEventType::Canceled:
      return "canceled";
    case runtime::RuntimeEventType::Error:
      return "error";
    case runtime::RuntimeEventType::Done:
      return "done";
  }
  return "error";
}

std::string_view to_string(runtime::RuntimePayloadType type) noexcept
{
  switch (type)
  {
    case runtime::RuntimePayloadType::Session:
      return "session";
    case runtime::RuntimePayloadType::Message:
      return "message";
    case runtime::RuntimePayloadType::Reasoning:
      return "reasoning";
    case runtime::RuntimePayloadType::Provider:
      return "provider";
    case runtime::RuntimePayloadType::Tool:
      return "tool";
    case runtime::RuntimePayloadType::Compaction:
      return "compaction";
    case runtime::RuntimePayloadType::Retry:
      return "retry";
    case runtime::RuntimePayloadType::Cancellation:
      return "cancellation";
    case runtime::RuntimePayloadType::Error:
      return "error";
    case runtime::RuntimePayloadType::Completion:
      return "completion";
    case runtime::RuntimePayloadType::Permission:
      return "permission";
    case runtime::RuntimePayloadType::Question:
      return "question";
    case runtime::RuntimePayloadType::Queue:
      return "queue";
  }
  return "error";
}

runtime::RuntimePayloadType payload_type_for_event(runtime::RuntimeEventType type) noexcept
{
  using runtime::RuntimeEventType;

  switch (type)
  {
    case runtime::RuntimeEventType::SessionStart:
      return runtime::RuntimePayloadType::Session;
    case runtime::RuntimeEventType::UserMessage:
    case runtime::RuntimeEventType::AssistantMessage:
    case runtime::RuntimeEventType::MessageUpdate:
    case runtime::RuntimeEventType::MessageEnd:
      return runtime::RuntimePayloadType::Message;
    case runtime::RuntimeEventType::ReasoningStart:
    case runtime::RuntimeEventType::ReasoningDelta:
    case runtime::RuntimeEventType::ReasoningEnd:
      return runtime::RuntimePayloadType::Reasoning;
    case runtime::RuntimeEventType::ProviderEvent:
      return runtime::RuntimePayloadType::Provider;
    case runtime::RuntimeEventType::ToolStart:
    case runtime::RuntimeEventType::ToolProgress:
    case runtime::RuntimeEventType::ToolResult:
      return runtime::RuntimePayloadType::Tool;
    case runtime::RuntimeEventType::CompactionStart:
    case runtime::RuntimeEventType::CompactionEnd:
      return runtime::RuntimePayloadType::Compaction;
    case runtime::RuntimeEventType::Retry:
    case runtime::RuntimeEventType::RetryTick:
      return runtime::RuntimePayloadType::Retry;
    case runtime::RuntimeEventType::Canceled:
      return runtime::RuntimePayloadType::Cancellation;
    case runtime::RuntimeEventType::Error:
      return runtime::RuntimePayloadType::Error;
    case runtime::RuntimeEventType::Done:
      return runtime::RuntimePayloadType::Completion;
  }
  return runtime::RuntimePayloadType::Error;
}

runtime::RuntimeToolPayload tool_payload_from_event(runtime::RuntimeEvent const& event)
{
  return runtime::RuntimeToolPayload{.text = event.text,
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

runtime::RuntimeRetryPayload retry_payload_from_event(runtime::RuntimeEvent const& event)
{
  return runtime::RuntimeRetryPayload{.text = event.text,
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

runtime::RuntimeCancellationPayload cancellation_payload_from_event(runtime::RuntimeEvent const& event)
{
  return runtime::RuntimeCancellationPayload{.text = event.text,
                                    .status = event.status,
                                    .error_category = event.error_category,
                                    .error_code = event.error_code,
                                    .error_message = event.error_message,
                                    .error_details = event.error_details,
                                    .trigger = event.trigger,
                                    .reason = event.reason};
}

runtime::RuntimeErrorPayload error_payload_from_event(runtime::RuntimeEvent const& event)
{
  return runtime::RuntimeErrorPayload{.text = event.text,
                             .status = event.status,
                             .error_category = event.error_category,
                             .error_code = event.error_code,
                             .error_message = event.error_message,
                             .error_details = event.error_details,
                             .content_type = event.content_type,
                             .trigger = event.trigger,
                             .reason = event.reason};
}

runtime::RuntimeCompletionPayload completion_payload_from_event(runtime::RuntimeEvent const& event)
{
  return runtime::RuntimeCompletionPayload{.status = event.status,
                                  .stop_reason = event.stop_reason,
                                  .reason = event.reason,
                                  .provider_iterations = event.provider_iterations,
                                  .tool_calls = event.tool_calls};
}

std::string serialize_payload_json(runtime::RuntimeToolPayload const& payload)
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

std::string serialize_payload_json(runtime::RuntimeRetryPayload const& payload)
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

std::string serialize_payload_json(runtime::RuntimeCancellationPayload const& payload)
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

std::string serialize_payload_json(runtime::RuntimeErrorPayload const& payload)
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

std::string serialize_payload_json(runtime::RuntimeCompletionPayload const& payload)
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

std::string serialize_event_json(runtime::RuntimeEvent const& event)
{
  std::string out = "{\"type\":\"" + to_string(event.type) + "\"";
  append_string_field(out, "timestamp", event.timestamp);
  append_string_field(out, "session_id", event.session_id);
  if (event.type == runtime::RuntimeEventType::SessionStart)
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

std::string serialize_event_jsonl(runtime::RuntimeEvent const& event)
{
  return serialize_event_json(event) + '\n';
}

ava::core::VoidResult emit_event(runtime::RuntimeEventSink const& sink, runtime::RuntimeEvent const& event)
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

EventEnvelope to_event_envelope(runtime::RuntimeEvent const& event, EventEnvelopeContext const& context)
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

runtime::RuntimeEventSink make_runtime_event_bus_adapter(EventBus& bus, EventEnvelopeContext context, runtime::RuntimeEventSink legacy_sink)
{
  return [&bus, context = std::move(context), legacy_sink = std::move(legacy_sink)](runtime::RuntimeEvent const& event) -> ava::core::VoidResult {
    auto envelope = to_event_envelope(event, context);
    if (auto published = bus.publish(envelope); !published)
      return std::unexpected(std::move(published.error()));
    return emit_event(legacy_sink, event);
  };
}

}  // namespace ava::app
