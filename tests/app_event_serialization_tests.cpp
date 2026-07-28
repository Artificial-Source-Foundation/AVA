#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/event/events.h"
#include "ava/core/mode.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#ifndef AVA_RUNTIME_EVENTS_V1_GOLDEN_DIR
#define AVA_RUNTIME_EVENTS_V1_GOLDEN_DIR ""
#endif

namespace ava::tests::app_runtime_tests {

using namespace ava::tests;

namespace {

constexpr std::array expected_runtime_event_types = {
    ava::event::RuntimeEventType::SessionStart,
    ava::event::RuntimeEventType::UserMessage,
    ava::event::RuntimeEventType::AssistantMessage,
    ava::event::RuntimeEventType::MessageUpdate,
    ava::event::RuntimeEventType::MessageEnd,
    ava::event::RuntimeEventType::ReasoningStart,
    ava::event::RuntimeEventType::ReasoningDelta,
    ava::event::RuntimeEventType::ReasoningEnd,
    ava::event::RuntimeEventType::ProviderEvent,
    ava::event::RuntimeEventType::ToolStart,
    ava::event::RuntimeEventType::ToolProgress,
    ava::event::RuntimeEventType::ToolResult,
    ava::event::RuntimeEventType::CompactionStart,
    ava::event::RuntimeEventType::CompactionEnd,
    ava::event::RuntimeEventType::Retry,
    ava::event::RuntimeEventType::RetryTick,
    ava::event::RuntimeEventType::Canceled,
    ava::event::RuntimeEventType::Error,
    ava::event::RuntimeEventType::Done,
};

constexpr std::array expected_payload_types = {
    ava::event::PayloadType::Session,      ava::event::PayloadType::Message,    ava::event::PayloadType::Message,    ava::event::PayloadType::Message,
    ava::event::PayloadType::Message,      ava::event::PayloadType::Reasoning,  ava::event::PayloadType::Reasoning,  ava::event::PayloadType::Reasoning,
    ava::event::PayloadType::Provider,     ava::event::PayloadType::Tool,       ava::event::PayloadType::Tool,       ava::event::PayloadType::Tool,
    ava::event::PayloadType::Compaction,   ava::event::PayloadType::Compaction, ava::event::PayloadType::Retry,      ava::event::PayloadType::Retry,
    ava::event::PayloadType::Cancellation, ava::event::PayloadType::Error,      ava::event::PayloadType::Completion,
};

template <ava::event::RuntimeEventAlternative Alternative>
void append_checked_event(std::vector<ava::event::RuntimeEvent>& events, ava::event::RuntimeEventMetadata const& metadata, Alternative alternative,
                          ava::event::RuntimeEventType expected_type, ava::event::PayloadType expected_payload_type)
{
  ava::event::RuntimeEvent event(metadata, std::move(alternative));
  expect(std::holds_alternative<Alternative>(event.payload()), "typed RuntimeEvent stores its concrete alternative without a discriminator bridge");
  expect(event.type() == expected_type, "typed RuntimeEvent derives its type from its concrete alternative");
  expect(ava::event::payload_type_for_event(event.type()) == expected_payload_type, "typed RuntimeEvent maps to the expected payload family");
  events.push_back(std::move(event));
}

std::vector<ava::event::RuntimeEvent> runtime_event_v1_examples()
{
  using namespace ava::event;

  RuntimeEventMetadata const metadata{.timestamp = "2026-08-01T12:34:56Z", .session_id = "session_runtime_v1"};
  std::vector<RuntimeEvent> events;
  events.reserve(expected_runtime_event_types.size());

  SessionPayload session_start{};
  session_start.mode = ava::core::Mode::Plan;
  session_start.provider = "openai";
  session_start.model = "model-x";
  append_checked_event(events, metadata, SessionStartEvent{std::move(session_start)}, expected_runtime_event_types[0], expected_payload_types[0]);

  MessagePayload user_message{};
  user_message.text = "Inspect \"events\".\n";
  append_checked_event(events, metadata, UserMessageEvent{std::move(user_message)}, expected_runtime_event_types[1], expected_payload_types[1]);
  MessagePayload assistant_message{};
  assistant_message.text = "Finished the checkpoint.";
  append_checked_event(events, metadata, AssistantMessageEvent{std::move(assistant_message)}, expected_runtime_event_types[2], expected_payload_types[2]);
  MessagePayload message_update{};
  message_update.text = "Streaming answer";
  append_checked_event(events, metadata, MessageUpdateEvent{std::move(message_update)}, expected_runtime_event_types[3], expected_payload_types[3]);
  MessagePayload message_end{};
  message_end.status = "done";
  message_end.stop_reason = "stop";
  append_checked_event(events, metadata, MessageEndEvent{std::move(message_end)}, expected_runtime_event_types[4], expected_payload_types[4]);

  ReasoningPayload reasoning_start{};
  reasoning_start.status = "reasoning_start";
  reasoning_start.reasoning_format = "anthropic_thinking";
  append_checked_event(events, metadata, ReasoningStartEvent{std::move(reasoning_start)}, expected_runtime_event_types[5], expected_payload_types[5]);
  ReasoningPayload reasoning_delta{};
  reasoning_delta.text = "Visible reasoning summary";
  reasoning_delta.status = "reasoning_delta";
  reasoning_delta.reasoning_format = "anthropic_thinking";
  append_checked_event(events, metadata, ReasoningDeltaEvent{std::move(reasoning_delta)}, expected_runtime_event_types[6], expected_payload_types[6]);
  ReasoningPayload reasoning_end{};
  reasoning_end.status = "reasoning_end";
  reasoning_end.reasoning_format = "anthropic_thinking";
  reasoning_end.reasoning_signature_present = true;
  append_checked_event(events, metadata, ReasoningEndEvent{std::move(reasoning_end)}, expected_runtime_event_types[7], expected_payload_types[7]);

  ProviderPayload provider{};
  provider.text = "permission allowed for this session: bash";
  provider.tool = "bash";
  provider.status = "tui:permission_allow";
  provider.error_details = "reused tui session grant";
  provider.reason = "command requires approval";
  provider.permission_request_ids = {"permreq_provider_1"};
  append_checked_event(events, metadata, ProviderEvent{std::move(provider)}, expected_runtime_event_types[8], expected_payload_types[8]);

  ToolPayload tool_start{};
  tool_start.text = "path=src/main.cpp";
  tool_start.call_id = "call_tool_1";
  tool_start.tool = "edit_file";
  tool_start.args_json = "{\"path\":\"src/main.cpp\"}";
  tool_start.status = "running";
  append_checked_event(events, metadata, ToolStartEvent{std::move(tool_start)}, expected_runtime_event_types[9], expected_payload_types[9]);
  ToolPayload tool_progress{};
  tool_progress.text = "applying edit";
  tool_progress.call_id = "call_tool_1";
  tool_progress.tool = "edit_file";
  tool_progress.status = "running";
  append_checked_event(events, metadata, ToolProgressEvent{std::move(tool_progress)}, expected_runtime_event_types[10], expected_payload_types[10]);
  ToolPayload tool_result{};
  tool_result.text = "edited src/main.cpp";
  tool_result.call_id = "call_tool_1";
  tool_result.tool = "edit_file";
  tool_result.args_json = "{\"path\":\"src/main.cpp\",\"oldText\":\"old\",\"newText\":\"new\"}";
  tool_result.result_json = "non-object tool result";
  tool_result.structured_result_json =
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
  append_checked_event(events, metadata, ToolResultEvent{std::move(tool_result)}, expected_runtime_event_types[11], expected_payload_types[11]);

  CompactionPayload compaction_start{};
  compaction_start.provider = "openai";
  compaction_start.model = "model-x";
  compaction_start.status = "started";
  compaction_start.trigger = "context_overflow";
  compaction_start.reason = "overflow";
  compaction_start.attempt = 1;
  compaction_start.max_attempts = 2;
  compaction_start.estimated_tokens = 9000;
  compaction_start.threshold_tokens = 8000;
  compaction_start.retained_tokens = 1200;
  append_checked_event(events, metadata, CompactionStartEvent{std::move(compaction_start)}, expected_runtime_event_types[12], expected_payload_types[12]);
  CompactionPayload compaction_end{};
  compaction_end.provider = "openai";
  compaction_end.model = "model-x";
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
  append_checked_event(events, metadata, CompactionEndEvent{std::move(compaction_end)}, expected_runtime_event_types[13], expected_payload_types[13]);

  RetryPayload retry{};
  retry.text = "HTTP status 429";
  retry.status = "request";
  retry.trigger = "provider_transport";
  retry.reason = "rate_limited";
  retry.attempt = 2;
  retry.max_attempts = 4;
  retry.delay_ms = 1500;
  append_checked_event(events, metadata, RetryEvent{std::move(retry), {}}, expected_runtime_event_types[14], expected_payload_types[14]);
  RetryPayload retry_tick{};
  retry_tick.text = "HTTP status 429";
  retry_tick.status = "request";
  retry_tick.trigger = "provider_transport";
  retry_tick.reason = "rate_limited";
  retry_tick.attempt = 2;
  retry_tick.max_attempts = 4;
  retry_tick.delay_ms = 1500;
  retry_tick.remaining_ms = 750;
  append_checked_event(events, metadata, RetryTickEvent{std::move(retry_tick), {}}, expected_runtime_event_types[15], expected_payload_types[15]);

  CancellationPayload canceled{};
  canceled.text = "stopped by user";
  canceled.error_category = "canceled";
  canceled.error_message = "agent loop canceled";
  canceled.error_details = "canceled: agent loop canceled";
  canceled.reason = "agent loop canceled";
  append_checked_event(events, metadata, CancellationEvent{std::move(canceled)}, expected_runtime_event_types[16], expected_payload_types[16]);
  ErrorPayload error{};
  error.error_category = "provider";
  error.error_message = "provider stream failed";
  error.error_details = "provider: provider stream failed\n  status: 503";
  append_checked_event(events, metadata, ErrorEvent{std::move(error)}, expected_runtime_event_types[17], expected_payload_types[17]);
  CompletionPayload done{};
  done.stop_reason = "completed";
  done.provider_iterations = 2;
  done.tool_calls = 1;
  append_checked_event(events, metadata, CompletionEvent{std::move(done)}, expected_runtime_event_types[18], expected_payload_types[18]);

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
  expect(events.size() == expected_names.size(), "runtime EventEnvelope v1 golden covers all 19 concrete RuntimeEvent alternatives exactly once");

  std::string actual;
  std::vector<std::string> actual_names;
  std::vector<std::string> actual_payload_types;
  for (std::size_t index = 0; index < events.size(); ++index)
  {
    ava::event::EventEnvelopeContext context;
    context.event_id = "event_runtime_v1_" + std::to_string(index + 1);
    context.run_id = "run_runtime_v1";
    context.turn_id = "turn_runtime_v1";
    context.message_id = "message_runtime_v1";
    context.request_id = "request_runtime_v1";
    context.correlation_id = "correlation_runtime_v1";
    auto const envelope = ava::event::to_event_envelope(events[index], context);
    actual_names.push_back(envelope.name);
    actual_payload_types.push_back(envelope.payload_type);
    actual += ava::event::serialize_event_envelope_jsonl(envelope);
  }

  expect(std::equal(actual_names.begin(), actual_names.end(), expected_names.begin(), expected_names.end()),
         "runtime EventEnvelope v1 names remain in concrete-alternative order");
  expect(std::equal(actual_payload_types.begin(), actual_payload_types.end(), expected_payload_types.begin(), expected_payload_types.end()),
         "runtime EventEnvelope v1 payload_type sequence remains stable");
  expect(actual.find("\"result_json\":\"non-object tool result\"") != std::string::npos,
         "invalid nested tool JSON falls back to the bounded escaped *_json field");
  expect(actual == read_runtime_events_v1_golden(), "all 19 runtime EventEnvelope v1 JSONL bytes match the golden fixture exactly");
}

void test_compaction_retry_envelope_preserves_internal_diagnostics_and_public_omissions()
{
  using namespace ava::event;
  RetryPayload payload{};
  payload.status = "started";
  payload.trigger = "context_overflow";
  payload.reason = "stale_compaction_snapshot";
  payload.attempt = 2;
  payload.max_attempts = 3;
  RetryDiagnostics diagnostics{};
  diagnostics.estimated_tokens = 9000;
  diagnostics.threshold_tokens = 8000;
  diagnostics.snapshot_entries = 12;
  diagnostics.current_entries = 13;
  RuntimeEvent event(RuntimeEventMetadata{.timestamp = "2026-08-01T12:35:00Z", .session_id = "session_runtime_v1"},
                     RetryEvent{std::move(payload), diagnostics});

  auto const* retry_event = std::get_if<RetryEvent>(&event.payload());
  expect(retry_event && retry_event->diagnostics.estimated_tokens == 9000 && retry_event->diagnostics.threshold_tokens == 8000 &&
             retry_event->diagnostics.snapshot_entries == 12 && retry_event->diagnostics.current_entries == 13,
         "typed retry events retain internal token and snapshot diagnostics");

  EventEnvelopeContext context;
  context.event_id = "event_compaction_retry_v1";
  auto const envelope = to_event_envelope(event, context);
  auto const envelope_json = serialize_event_envelope_json(envelope);
  expect(envelope.payload_json.find("\"attempt\":2") != std::string::npos && envelope.payload_json.find("\"max_attempts\":3") != std::string::npos,
         "retry envelope preserves public retry accounting");
  for (std::string_view field : {"estimated_tokens", "threshold_tokens", "snapshot_entries", "current_entries"})
  {
    auto const field_token = "\"" + std::string(field) + "\"";
    expect(envelope.payload_json.find(field_token) == std::string::npos && envelope_json.find(field_token) == std::string::npos,
           "runtime EventEnvelope v1 intentionally omits retry internal diagnostic: " + std::string(field));
  }
}

void test_direct_typed_runtime_event_contract()
{
  using namespace ava::event;

  static_assert(!std::is_default_constructible_v<RuntimeEvent>);
  static_assert(std::is_copy_constructible_v<RuntimeEvent>);
  static_assert(std::is_move_constructible_v<RuntimeEvent>);
  static_assert(!std::is_copy_assignable_v<RuntimeEvent>);
  static_assert(!std::is_move_assignable_v<RuntimeEvent>);
  static_assert(!std::is_constructible_v<RuntimeEvent, RuntimeEventMetadata, RuntimeEventPayload>);
  static_assert(std::variant_size_v<RuntimeEventPayload> == 19);

  auto const events = runtime_event_v1_examples();
  for (std::size_t index = 0; index < events.size(); ++index)
  {
    expect(events[index].type() == expected_runtime_event_types[index], "typed RuntimeEvent has the expected concrete type");
    expect(payload_type_for_event(events[index].type()) == expected_payload_types[index], "typed RuntimeEvent has the expected payload family");
    expect(events[index].metadata().timestamp == "2026-08-01T12:34:56Z" && events[index].metadata().session_id == "session_runtime_v1",
           "typed RuntimeEvent retains required immutable metadata");
  }
}

void test_authoritative_envelope_jsonl_serialization()
{
  using namespace ava::event;
  RuntimeEvent session(RuntimeEventMetadata{.timestamp = "2026-04-29T00:00:00Z", .session_id = "session_1"},
                       SessionStartEvent{SessionPayload{.mode = ava::core::Mode::Plan, .provider = "openai", .model = "gpt-5.5"}});
  EventEnvelopeContext session_context;
  session_context.event_id = "event_session";
  auto const session_envelope = to_event_envelope(session, session_context);
  expect(serialize_event_envelope_jsonl(session_envelope) ==
             "{\"schema_version\":1,\"event_id\":\"event_session\",\"timestamp\":\"2026-04-29T00:00:00Z\","
             "\"session_id\":\"session_1\",\"name\":\"session_start\",\"type\":\"session_start\","
             "\"payload_type\":\"session\",\"payload\":{\"mode\":\"plan\",\"provider\":\"openai\",\"model\":\"gpt-5.5\"},"
             "\"mode\":\"plan\",\"provider\":\"openai\",\"model\":\"gpt-5.5\"}\n",
         "typed session EventEnvelope JSONL serialization is deterministic");

  MessagePayload message_payload{};
  message_payload.text = "hello\n\"ava\"";
  RuntimeEvent message(RuntimeEventMetadata{.timestamp = "2026-04-29T00:00:01Z", .session_id = "session_1"}, UserMessageEvent{std::move(message_payload)});
  EventEnvelopeContext message_context;
  message_context.event_id = "event_message";
  auto const message_jsonl = serialize_event_envelope_jsonl(to_event_envelope(message, message_context));
  expect(message_jsonl.find("hello\\n\\\"ava\\\"") != std::string::npos, "typed EventEnvelope JSONL escapes message text");
  expect(message_jsonl.ends_with('\n') && message_jsonl.substr(0, message_jsonl.size() - 1).find('\n') == std::string::npos,
         "typed EventEnvelope JSONL contains one terminating newline only");
}

}  // namespace

void test_app_event_serialization()
{
  test_authoritative_envelope_jsonl_serialization();
  test_runtime_event_envelopes_match_v1_golden();
  test_direct_typed_runtime_event_contract();
  test_compaction_retry_envelope_preserves_internal_diagnostics_and_public_omissions();
}

}  // namespace ava::tests::app_runtime_tests
