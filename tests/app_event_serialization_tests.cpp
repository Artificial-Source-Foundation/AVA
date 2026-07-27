#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/app/EventEnvelope.h"
#include "ava/app/events.h"
#include "ava/app/runtime/Event.h"
#include "ava/agent/mode.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef AVA_RUNTIME_EVENTS_V1_GOLDEN_DIR
#define AVA_RUNTIME_EVENTS_V1_GOLDEN_DIR ""
#endif

namespace ava::tests::app_runtime_tests {

using namespace ava::tests;

std::vector<ava::app::runtime::Event> runtime_event_v1_examples()
{
  using ava::app::runtime::Event;
  using ava::app::runtime::EventType;

  auto make_event = [](EventType type) {
    Event event;
    event.type = type;
    event.timestamp = "2026-08-01T12:34:56Z";
    event.session_id = "session_runtime_v1";
    return event;
  };

  std::vector<Event> events;
  events.reserve(19);

  auto session_start = make_event(EventType::SessionStart);
  session_start.mode = ava::agent::Mode::Plan;
  session_start.provider_id = "openai";
  session_start.model_id = "model-x";
  events.push_back(std::move(session_start));

  auto user_message = make_event(EventType::UserMessage);
  user_message.text = "Inspect \"events\".\n";
  events.push_back(std::move(user_message));

  auto assistant_message = make_event(EventType::AssistantMessage);
  assistant_message.text = "Finished the checkpoint.";
  events.push_back(std::move(assistant_message));

  auto message_update = make_event(EventType::MessageUpdate);
  message_update.text = "Streaming answer";
  events.push_back(std::move(message_update));

  auto message_end = make_event(EventType::MessageEnd);
  message_end.status = "done";
  message_end.stop_reason = "stop";
  events.push_back(std::move(message_end));

  auto reasoning_start = make_event(EventType::ReasoningStart);
  reasoning_start.status = "reasoning_start";
  reasoning_start.reasoning_format = "anthropic_thinking";
  events.push_back(std::move(reasoning_start));

  auto reasoning_delta = make_event(EventType::ReasoningDelta);
  reasoning_delta.text = "Visible reasoning summary";
  reasoning_delta.status = "reasoning_delta";
  reasoning_delta.reasoning_format = "anthropic_thinking";
  events.push_back(std::move(reasoning_delta));

  auto reasoning_end = make_event(EventType::ReasoningEnd);
  reasoning_end.status = "reasoning_end";
  reasoning_end.reasoning_format = "anthropic_thinking";
  reasoning_end.reasoning_signature_present = true;
  events.push_back(std::move(reasoning_end));

  auto provider_event = make_event(EventType::ProviderEvent);
  provider_event.text = "permission allowed for this session: bash";
  provider_event.tool_name = "bash";
  provider_event.status = "tui:permission_allow";
  provider_event.error_details = "reused tui session grant";
  provider_event.reason = "command requires approval";
  provider_event.permission_request_ids = {"permreq_provider_1"};
  events.push_back(std::move(provider_event));

  auto tool_start = make_event(EventType::ToolStart);
  tool_start.text = "path=src/main.cpp";
  tool_start.call_id = "call_tool_1";
  tool_start.tool_name = "edit_file";
  tool_start.tool_arguments_json = "{\"path\":\"src/main.cpp\"}";
  tool_start.status = "running";
  events.push_back(std::move(tool_start));

  auto tool_progress = make_event(EventType::ToolProgress);
  tool_progress.text = "applying edit";
  tool_progress.call_id = "call_tool_1";
  tool_progress.tool_name = "edit_file";
  tool_progress.status = "running";
  events.push_back(std::move(tool_progress));

  auto tool_result = make_event(EventType::ToolResult);
  tool_result.text = "edited src/main.cpp";
  tool_result.call_id = "call_tool_1";
  tool_result.tool_name = "edit_file";
  tool_result.tool_arguments_json = "{\"path\":\"src/main.cpp\",\"oldText\":\"old\",\"newText\":\"new\"}";
  tool_result.tool_result_json = "non-object tool result";
  tool_result.tool_structured_result_json =
      "{\"schema_version\":1,\"call_id\":\"call_tool_1\",\"tool\":\"edit_file\",\"status\":\"success\","
      "\"ok\":true,\"content_type\":\"application/json\",\"content\":{\"ok\":true},"
      "\"changed_paths\":[\"src/main.cpp\"],\"permission_request_ids\":[\"permreq_tool_1\"]}";
  tool_result.status = "success";
  tool_result.content_type = "application/json";
  tool_result.diff = "--- src/main.cpp\n+++ src/main.cpp\n-old\n+new";
  tool_result.changed_paths = {"src/main.cpp"};
  tool_result.permission_request_ids = {"permreq_tool_1"};
  tool_result.spill_path = "spill/tool-call-1.txt";
  tool_result.diff_truncated = true;
  tool_result.truncated = true;
  tool_result.byte_limited = true;
  tool_result.line_limited = true;
  tool_result.spill_truncated = true;
  tool_result.output_bytes = 128;
  tool_result.total_bytes = 512;
  tool_result.output_lines = 4;
  tool_result.total_lines = 12;
  tool_result.start_line = 2;
  tool_result.end_line = 5;
  tool_result.next_offset_line = 6;
  tool_result.omitted_bytes = 384;
  tool_result.omitted_lines = 8;
  tool_result.visible_matches = 3;
  tool_result.total_matches = 9;
  events.push_back(std::move(tool_result));

  auto compaction_start = make_event(EventType::CompactionStart);
  compaction_start.provider_id = "openai";
  compaction_start.model_id = "model-x";
  compaction_start.status = "started";
  compaction_start.trigger = "context_overflow";
  compaction_start.reason = "overflow";
  compaction_start.attempt = 1;
  compaction_start.max_attempts = 2;
  compaction_start.estimated_tokens = 9000;
  compaction_start.threshold_tokens = 8000;
  compaction_start.retained_tokens = 1200;
  events.push_back(std::move(compaction_start));

  auto compaction_end = make_event(EventType::CompactionEnd);
  compaction_end.provider_id = "openai";
  compaction_end.model_id = "model-x";
  compaction_end.status = "completed";
  compaction_end.trigger = "context_overflow";
  compaction_end.reason = "overflow";
  compaction_end.attempt = 1;
  compaction_end.max_attempts = 2;
  compaction_end.estimated_tokens = 9000;
  compaction_end.threshold_tokens = 8000;
  compaction_end.retained_tokens = 1200;
  compaction_end.post_compaction_tokens = 1800;
  compaction_end.summary_bytes = 640;
  events.push_back(std::move(compaction_end));

  auto retry = make_event(EventType::Retry);
  retry.text = "HTTP status 429";
  retry.status = "request";
  retry.trigger = "provider_transport";
  retry.reason = "rate_limited";
  retry.attempt = 2;
  retry.max_attempts = 4;
  retry.delay_ms = 1500;
  events.push_back(std::move(retry));

  auto retry_tick = make_event(EventType::RetryTick);
  retry_tick.text = "HTTP status 429";
  retry_tick.status = "request";
  retry_tick.trigger = "provider_transport";
  retry_tick.reason = "rate_limited";
  retry_tick.attempt = 2;
  retry_tick.max_attempts = 4;
  retry_tick.delay_ms = 1500;
  retry_tick.remaining_ms = 750;
  events.push_back(std::move(retry_tick));

  auto canceled = make_event(EventType::Canceled);
  canceled.text = "stopped by user";
  canceled.error_category = "canceled";
  canceled.error_message = "agent loop canceled";
  canceled.error_details = "canceled: agent loop canceled";
  canceled.reason = "agent loop canceled";
  events.push_back(std::move(canceled));

  auto error = make_event(EventType::Error);
  error.error_category = "provider";
  error.error_message = "provider stream failed";
  error.error_details = "provider: provider stream failed\n  status: 503";
  events.push_back(std::move(error));

  auto done = make_event(EventType::Done);
  done.stop_reason = "completed";
  done.provider_iterations = 2;
  done.tool_calls = 1;
  events.push_back(std::move(done));

  return events;
}

std::string read_runtime_events_v1_golden()
{
  auto const directory = std::filesystem::path(AVA_RUNTIME_EVENTS_V1_GOLDEN_DIR);
  expect(!directory.empty(), "runtime EventEnvelope v1 golden directory is configured");
  auto const path = directory / "envelopes.jsonl";
  std::ifstream file(path, std::ios::binary);
  expect(file.is_open(), "runtime EventEnvelope v1 golden fixture is readable: " + path.string());
  if (!file)
    return {};
  std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  expect(!file.bad(), "runtime EventEnvelope v1 golden fixture was read completely: " + path.string());
  return bytes;
}

void test_runtime_event_envelopes_match_v1_golden()
{
  constexpr std::array<std::string_view, 19> expected_names = {
      "session_start", "user_message",   "assistant_message", "message_update", "message_end", "reasoning_start",  "reasoning_delta",
      "reasoning_end", "provider_event", "tool_start",        "tool_progress",  "tool_result", "compaction_start", "compaction_end",
      "retry",         "retry_tick",     "canceled",          "error",          "done"};
  constexpr std::array<std::string_view, 19> expected_payload_types = {
      "session", "message", "message",    "message",    "message", "reasoning", "reasoning",    "reasoning", "provider",  "tool",
      "tool",    "tool",    "compaction", "compaction", "retry",   "retry",     "cancellation", "error",     "completion"};

  auto const events = runtime_event_v1_examples();
  expect(events.size() == expected_names.size(), "runtime EventEnvelope v1 golden covers every EventType exactly once");

  std::string actual;
  std::vector<std::string> actual_names;
  std::vector<std::string> actual_payload_types;
  for (std::size_t index = 0; index < events.size(); ++index)
  {
    ava::app::EventEnvelopeContext context;
    context.event_id = "event_runtime_v1_" + std::to_string(index + 1);
    context.run_id = "run_runtime_v1";
    context.turn_id = "turn_runtime_v1";
    context.message_id = "message_runtime_v1";
    context.request_id = "request_runtime_v1";
    context.correlation_id = "correlation_runtime_v1";
    auto const envelope = ava::app::to_event_envelope(events[index], context);
    actual_names.push_back(envelope.name);
    actual_payload_types.push_back(envelope.payload_type);
    actual += ava::app::serialize_event_envelope_jsonl(envelope);
  }

  expect(std::equal(actual_names.begin(), actual_names.end(), expected_names.begin(), expected_names.end()),
         "runtime EventEnvelope v1 names remain in EventType order");
  expect(std::equal(actual_payload_types.begin(), actual_payload_types.end(), expected_payload_types.begin(), expected_payload_types.end()),
         "runtime EventEnvelope v1 payload_type sequence remains stable");
  expect(actual == read_runtime_events_v1_golden(), "runtime EventEnvelope v1 JSONL bytes match the golden fixture exactly");
}

void test_compaction_retry_envelope_preserves_v1_counter_omissions()
{
  ava::app::runtime::Event event;
  event.type = ava::app::runtime::EventType::Retry;
  event.timestamp = "2026-08-01T12:35:00Z";
  event.session_id = "session_runtime_v1";
  event.status = "started";
  event.trigger = "context_overflow";
  event.reason = "stale_compaction_snapshot";
  event.attempt = 2;
  event.max_attempts = 3;
  event.estimated_tokens = 9000;
  event.threshold_tokens = 8000;
  event.snapshot_entries = 12;
  event.current_entries = 13;

  auto const flat_json = ava::app::serialize_event_json(event);
  expect(flat_json.find("\"estimated_tokens\":9000") != std::string::npos && flat_json.find("\"threshold_tokens\":8000") != std::string::npos &&
             flat_json.find("\"snapshot_entries\":12") != std::string::npos && flat_json.find("\"current_entries\":13") != std::string::npos,
         "legacy flat compaction retry events retain internal token and snapshot counters");

  ava::app::EventEnvelopeContext context;
  context.event_id = "event_compaction_retry_v1";
  auto const envelope = ava::app::to_event_envelope(event, context);
  auto const envelope_json = ava::app::serialize_event_envelope_json(envelope);
  for (std::string_view field : {"estimated_tokens", "threshold_tokens", "snapshot_entries", "current_entries"})
  {
    auto const field_token = "\"" + std::string(field) + "\"";
    expect(envelope.payload_json.find(field_token) == std::string::npos && envelope_json.find(field_token) == std::string::npos,
           "runtime EventEnvelope v1 intentionally omits compaction retry counter: " + std::string(field));
  }
}

void test_app_event_serialization()
{
  ava::app::runtime::Event session_event;
  session_event.type = ava::app::runtime::EventType::SessionStart;
  session_event.timestamp = "2026-04-29T00:00:00Z";
  session_event.session_id = "session_1";
  session_event.mode = ava::agent::Mode::Plan;
  session_event.provider_id = "openai";
  session_event.model_id = "gpt-5.5";
  auto const jsonl = ava::app::serialize_event_jsonl(session_event);
  expect(jsonl ==
             "{\"type\":\"session_start\",\"timestamp\":\"2026-04-29T00:00:00Z\","
             "\"session_id\":\"session_1\",\"mode\":\"plan\",\"provider\":\"openai\","
             "\"model\":\"gpt-5.5\"}\n",
         "runtime event JSONL serialization is deterministic");

  ava::app::runtime::Event message_event;
  message_event.type = ava::app::runtime::EventType::UserMessage;
  message_event.timestamp = "2026-04-29T00:00:01Z";
  message_event.session_id = "session_1";
  message_event.text = "hello\n\"ava\"";
  auto const message_jsonl = ava::app::serialize_event_jsonl(message_event);
  expect(message_jsonl.find("hello\\n\\\"ava\\\"") != std::string::npos, "runtime event JSONL escapes message text");
  expect(message_jsonl.ends_with('\n') && message_jsonl.substr(0, message_jsonl.size() - 1).find('\n') == std::string::npos,
         "runtime event JSONL contains one terminating newline only");

  test_runtime_event_envelopes_match_v1_golden();
  test_compaction_retry_envelope_preserves_v1_counter_omissions();
}

}  // namespace ava::tests::app_runtime_tests
