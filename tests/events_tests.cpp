#include <iostream>
#include <string>
#include <vector>

#include "ava/agent/mode.h"
#include "ava/app/events.h"
#include "ava/app/interactive_run_queue.h"
#include "ava/core/json.h"
#include "tests/support/test_harness.h"

namespace {

void test_event_envelope_serialization_is_deterministic() {
  const ava::app::EventEnvelope envelope{.schema_version = 1,
                                         .event_id = "event_1",
                                         .timestamp = "2026-04-30T00:00:00Z",
                                         .session_id = "session_1",
                                         .run_id = "run_1",
                                         .turn_id = "turn_1",
                                         .message_id = "message_1",
                                         .request_id = "request_1",
                                         .correlation_id = "correlation_1",
                                         .name = "user_message",
                                         .payload_json = "{\"text\":\"hello\\n\\\"ava\\\"\"}"};

  const auto json = ava::app::serialize_event_envelope_json(envelope);
  expect(json ==
             "{\"schema_version\":1,\"event_id\":\"event_1\","
             "\"timestamp\":\"2026-04-30T00:00:00Z\",\"session_id\":\"session_1\","
             "\"run_id\":\"run_1\",\"turn_id\":\"turn_1\",\"message_id\":\"message_1\","
             "\"request_id\":\"request_1\",\"correlation_id\":\"correlation_1\","
             "\"name\":\"user_message\",\"type\":\"user_message\","
             "\"payload\":{\"text\":\"hello\\n\\\"ava\\\"\"},\"text\":\"hello\\n\\\"ava\\\"\"}",
         "event envelope JSON serialization uses deterministic field ordering");

  const auto jsonl = ava::app::serialize_event_envelope_jsonl(envelope);
  expect(jsonl == json + '\n', "event envelope JSONL appends one newline");
}

void test_runtime_event_conversion_preserves_legacy_payload_shape() {
  ava::app::RuntimeEvent event;
  event.type = ava::app::RuntimeEventType::ToolResult;
  event.timestamp = "2026-04-30T00:00:01Z";
  event.session_id = "session_1";
  event.text = "read ok";
  event.call_id = "call_1";
  event.tool_name = "read";
  event.status = "completed";

  ava::app::EventEnvelopeContext context;
  context.event_id = "event_2";
  context.run_id = "run_1";
  context.turn_id = "turn_2";
  const auto envelope = ava::app::to_event_envelope(event, context);
  expect(envelope.schema_version == 1, "runtime event conversion sets schema version");
  expect(envelope.event_id == "event_2", "runtime event conversion uses supplied event id");
  expect(envelope.timestamp == "2026-04-30T00:00:01Z", "runtime event conversion carries timestamp");
  expect(envelope.session_id == "session_1", "runtime event conversion carries session id");
  expect(envelope.run_id == "run_1" && envelope.turn_id == "turn_2", "runtime event conversion carries optional ids");
  expect(envelope.name == "tool_result", "runtime event conversion maps event name");
  expect(envelope.payload_json ==
             "{\"text\":\"read ok\",\"call_id\":\"call_1\",\"tool\":\"read\",\"status\":\"completed\"}",
         "runtime event conversion maps legacy event fields into payload object");
}

void test_runtime_event_bus_adapter_publishes_and_forwards() {
  ava::app::EventBus bus;
  std::vector<ava::app::EventEnvelope> published;
  std::vector<ava::app::RuntimeEvent> forwarded;
  bus.subscribe([&published](const ava::app::EventEnvelope& envelope) {
    published.push_back(envelope);
    return ava::core::VoidResult{};
  });

  ava::app::EventEnvelopeContext context;
  context.event_id = "event_3";
  context.correlation_id = "correlation_1";
  auto sink = ava::app::make_runtime_event_bus_adapter(bus, context, [&forwarded](const ava::app::RuntimeEvent& event) {
    forwarded.push_back(event);
    return ava::core::VoidResult{};
  });

  ava::app::RuntimeEvent event;
  event.type = ava::app::RuntimeEventType::AssistantMessage;
  event.timestamp = "2026-04-30T00:00:02Z";
  event.session_id = "session_1";
  event.text = "done";

  const auto emitted = sink(event);
  expect(emitted.has_value(), "runtime event bus adapter succeeds when bus and legacy sink succeed");
  expect(published.size() == 1 && published.front().name == "assistant_message" &&
             published.front().correlation_id == "correlation_1",
         "runtime event bus adapter publishes converted envelopes");
  expect(forwarded.size() == 1 && forwarded.front().text == "done",
         "runtime event bus adapter forwards runtime events to legacy sink");
}

void test_runtime_event_bus_adapter_allows_default_legacy_sink() {
  ava::app::EventBus bus;
  std::vector<ava::app::EventEnvelope> published;
  bus.subscribe([&published](const ava::app::EventEnvelope& envelope) {
    published.push_back(envelope);
    return ava::core::VoidResult{};
  });

  auto sink = ava::app::make_runtime_event_bus_adapter(bus);
  ava::app::RuntimeEvent event;
  event.type = ava::app::RuntimeEventType::Done;
  event.timestamp = "2026-04-30T00:00:03Z";
  event.session_id = "session_1";

  const auto emitted = sink(event);
  expect(emitted.has_value() && published.size() == 1 && published.front().name == "done",
         "runtime event bus adapter supports a null legacy sink");
}

void test_tool_progress_runtime_event_serialization_and_bus_adapter() {
  ava::app::RuntimeEvent event;
  event.type = ava::app::RuntimeEventType::ToolProgress;
  event.timestamp = "2026-04-30T00:00:04Z";
  event.session_id = "session_1";
  event.text = "reading";
  event.call_id = "call_2";
  event.tool_name = "read_file";
  event.status = "running";

  const auto json = ava::app::serialize_event_json(event);
  expect(json ==
             "{\"type\":\"tool_progress\",\"timestamp\":\"2026-04-30T00:00:04Z\","
             "\"session_id\":\"session_1\",\"text\":\"reading\",\"call_id\":\"call_2\","
             "\"tool\":\"read_file\",\"status\":\"running\"}",
         "tool progress serializes with existing runtime event payload fields");

  ava::app::EventBus bus;
  std::vector<ava::app::EventEnvelope> published;
  bus.subscribe([&published](const ava::app::EventEnvelope& envelope) {
    published.push_back(envelope);
    return ava::core::VoidResult{};
  });
  ava::app::EventEnvelopeContext context;
  context.event_id = "event_progress";
  auto sink = ava::app::make_runtime_event_bus_adapter(bus, context);
  const auto emitted = sink(event);
  expect(emitted.has_value() && published.size() == 1 && published.front().name == "tool_progress",
         "event bus adapter publishes tool progress envelopes");
  expect(published.front().payload_json ==
             "{\"text\":\"reading\",\"call_id\":\"call_2\",\"tool\":\"read_file\",\"status\":\"running\"}",
         "tool progress envelope payload keeps existing tool event fields");
}

void test_tool_runtime_event_serializes_semantic_frontend_payloads() {
  ava::app::RuntimeEvent event;
  event.type = ava::app::RuntimeEventType::ToolResult;
  event.timestamp = "2026-04-30T00:00:04Z";
  event.session_id = "session_1";
  event.text = "edited src/main.cpp";
  event.call_id = "call_edit";
  event.tool_name = "edit_file";
  event.tool_arguments_json = "{\"path\":\"src/main.cpp\"}";
  event.tool_result_json = "{\"ok\":true,\"path\":\"src/main.cpp\"}";
  event.tool_structured_result_json =
      "{\"schema_version\":1,\"call_id\":\"call_edit\",\"tool\":\"edit_file\",\"status\":\"success\","
      "\"ok\":true,\"content_type\":\"application/json\",\"content\":{\"ok\":true,\"path\":\"src/main.cpp\"},"
      "\"changed_paths\":[\"src/main.cpp\"]}";
  event.status = "success";
  event.content_type = "application/json";
  event.diff = "--- src/main.cpp\n+++ src/main.cpp\n-old\n+new";
  event.changed_paths = {"src/main.cpp", "include/ava/app/events.h"};
  event.diff_truncated = true;
  event.truncated = true;
  event.spill_path = "/tmp/ava-spill/tool.txt";
  event.spill_truncated = true;
  event.output_bytes = 128;
  event.total_bytes = 512;
  event.omitted_bytes = 384;
  event.omitted_lines = 7;

  const auto json = ava::app::serialize_event_json(event);
  expect(json.find("\"args\":{\"path\":\"src/main.cpp\"}") != std::string::npos &&
             json.find("\"result\":{\"ok\":true,\"path\":\"src/main.cpp\"}") != std::string::npos &&
             json.find("\"structured_result\":{\"schema_version\":1") != std::string::npos &&
             json.find("\"content_type\":\"application/json\"") != std::string::npos &&
             json.find("\"changed_paths\":[\"src/main.cpp\",\"include/ava/app/events.h\"]") != std::string::npos &&
             json.find("\"diff_truncated\":true") != std::string::npos &&
             json.find("\"omitted_lines\":7") != std::string::npos,
         "tool runtime events serialize semantic args, result, changed paths, diffs, and truncation metadata");

  ava::app::EventEnvelopeContext context;
  context.event_id = "event_semantic_tool";
  const auto envelope = ava::app::to_event_envelope(event, context);
  const auto args = ava::core::json::object_field(envelope.payload_json, "args");
  const auto result = ava::core::json::object_field(envelope.payload_json, "result");
  const auto structured_result = ava::core::json::object_field(envelope.payload_json, "structured_result");
  const auto paths = ava::core::json::strings_in_array_field(envelope.payload_json, "changed_paths");
  expect(envelope.name == "tool_result" && args && *args == "{\"path\":\"src/main.cpp\"}" && result &&
             *result == "{\"ok\":true,\"path\":\"src/main.cpp\"}" && structured_result &&
             structured_result->find("\"status\":\"success\"") != std::string::npos && paths.size() == 2 &&
             paths[1] == "include/ava/app/events.h" &&
             envelope.payload_json.find("\"spill_path\":\"/tmp/ava-spill/tool.txt\"") != std::string::npos,
         "tool event envelopes preserve semantic payloads for frontend replay");
}

void test_reasoning_runtime_event_serialization_hides_provider_private_state() {
  ava::app::RuntimeEvent event;
  event.type = ava::app::RuntimeEventType::ReasoningDelta;
  event.timestamp = "2026-04-30T00:00:05Z";
  event.session_id = "session_1";
  event.text = "visible reasoning summary";
  event.reasoning_format = "anthropic_thinking";
  event.reasoning_redacted = true;
  event.reasoning_signature_present = true;

  const auto json = ava::app::serialize_event_json(event);
  expect(json ==
             "{\"type\":\"reasoning_delta\",\"timestamp\":\"2026-04-30T00:00:05Z\","
             "\"session_id\":\"session_1\",\"text\":\"visible reasoning summary\","
             "\"reasoning_format\":\"anthropic_thinking\",\"reasoning_redacted\":true,"
             "\"reasoning_signature_present\":true}",
         "reasoning runtime event serializes visible text and format only");

  ava::app::EventEnvelopeContext context;
  context.event_id = "event_reasoning";
  const auto envelope = ava::app::to_event_envelope(event, context);
  expect(envelope.name == "reasoning_delta" &&
             envelope.payload_json ==
                 "{\"text\":\"visible reasoning summary\",\"reasoning_format\":\"anthropic_thinking\","
                 "\"reasoning_redacted\":true,\"reasoning_signature_present\":true}",
         "reasoning envelope carries frontend-visible reasoning payload");
  const auto envelope_json = ava::app::serialize_event_envelope_json(envelope);
  expect(envelope_json.find("super-secret-signature") == std::string::npos &&
             envelope_json.find("reasoning_signature\":") == std::string::npos &&
             envelope_json.find("\"signature\":") == std::string::npos,
         "reasoning frontend event envelope never exposes provider-private signatures");
}

void test_lifecycle_runtime_event_serialization_and_aliases() {
  ava::app::RuntimeEvent event;
  event.type = ava::app::RuntimeEventType::CompactionEnd;
  event.timestamp = "2026-04-30T00:00:06Z";
  event.session_id = "session_1";
  event.status = "completed";
  event.trigger = "context_overflow";
  event.attempt = 1;
  event.max_attempts = 2;
  event.estimated_tokens = 9000;
  event.threshold_tokens = 8000;
  event.summary_bytes = 512;

  const auto json = ava::app::serialize_event_json(event);
  expect(json ==
             "{\"type\":\"compaction_end\",\"timestamp\":\"2026-04-30T00:00:06Z\","
             "\"session_id\":\"session_1\",\"status\":\"completed\",\"trigger\":\"context_overflow\","
             "\"attempt\":1,\"max_attempts\":2,\"estimated_tokens\":9000,\"threshold_tokens\":8000,"
             "\"summary_bytes\":512}",
         "compaction lifecycle runtime events serialize backend-owned compaction metadata");

  ava::app::EventEnvelopeContext context;
  context.event_id = "event_compaction";
  const auto envelope = ava::app::to_event_envelope(event, context);
  const auto envelope_json = ava::app::serialize_event_envelope_json(envelope);
  expect(envelope.name == "compaction_end" &&
             envelope.payload_json.find("\"trigger\":\"context_overflow\"") != std::string::npos &&
             envelope.payload_json.find("\"max_attempts\":2") != std::string::npos &&
             envelope_json.find("\"summary_bytes\":512") != std::string::npos,
         "compaction lifecycle envelope preserves payload and top-level aliases for stream clients");

  event.type = ava::app::RuntimeEventType::Canceled;
  event.status.clear();
  event.trigger.clear();
  event.reason = "cancel_requested";
  event.attempt = 0;
  event.max_attempts = 0;
  event.estimated_tokens = 0;
  event.threshold_tokens = 0;
  event.summary_bytes = 0;
  expect(ava::app::serialize_event_json(event).find("\"type\":\"canceled\"") != std::string::npos,
         "explicit canceled runtime events have a stable shared stream name");

  event.type = ava::app::RuntimeEventType::RetryTick;
  event.reason = "rate_limited";
  event.trigger = "provider_transport";
  event.attempt = 2;
  event.max_attempts = 3;
  event.delay_ms = 1000;
  event.remaining_ms = 500;
  event.estimated_tokens = 0;
  event.threshold_tokens = 0;
  event.summary_bytes = 0;
  event.snapshot_entries = 0;
  event.current_entries = 0;
  const auto retry_tick_json = ava::app::serialize_event_json(event);
  expect(retry_tick_json.find("\"type\":\"retry_tick\"") != std::string::npos &&
             retry_tick_json.find("\"remaining_ms\":500") != std::string::npos,
         "retry countdown tick runtime events serialize explicit backend timing data");
}

void test_interactive_run_queue_emits_steer_queued_and_applied_events() {
  std::vector<ava::app::EventEnvelope> events;
  ava::app::InteractiveRunQueue queue("session_queue", "request_active",
                                      [&events](const ava::app::EventEnvelope& envelope) {
                                        events.push_back(envelope);
                                        return ava::core::VoidResult{};
                                      });

  auto queued = queue.queue_steering("adjust this turn");
  expect(queued.has_value(), "interactive run queue accepts a bounded active-run steering message");
  expect(events.size() == 1 && events.back().name == "steer_queued" &&
             events.back().correlation_id == "request_active" && events.back().request_id &&
             *events.back().request_id != "request_active" &&
             events.back().payload_json.find("\"message\":\"adjust this turn\"") != std::string::npos,
         "interactive run queue emits a correlated steer queued event");

  auto taken = queue.take_steering_messages();
  expect(taken.has_value() && taken->size() == 1 && taken->front() == "adjust this turn",
         "interactive run queue returns queued steering at the backend safe point");
  expect(
      events.size() == 2 && events.back().name == "steer_applied" && events.back().correlation_id == "request_active",
      "interactive run queue emits a steer applied event when consumed by the backend");
}

void test_interactive_run_queue_runs_follow_up_lifecycle() {
  std::vector<ava::app::EventEnvelope> events;
  ava::app::InteractiveRunQueue queue("session_queue", "request_active",
                                      [&events](const ava::app::EventEnvelope& envelope) {
                                        events.push_back(envelope);
                                        return ava::core::VoidResult{};
                                      });

  auto queued = queue.queue_follow_up("next turn");
  auto next = queue.take_next_follow_up();
  auto started = next ? queue.mark_follow_up_started(*next) : ava::core::VoidResult{};
  auto steer_after_start = queue.queue_steering("steer follow-up");

  expect(queued.has_value() && next && next->message == "next turn" && started.has_value() &&
             steer_after_start.has_value(),
         "interactive run queue accepts and starts a queued follow-up");
  expect(events.size() == 3 && events[0].name == "follow_up_queued" && events[1].name == "follow_up_started" &&
             events[1].request_id == next->request_id && events[1].correlation_id == next->request_id &&
             events[2].name == "steer_queued" && events[2].correlation_id == next->request_id,
         "interactive run queue retargets active correlation when a follow-up starts");
}

void test_interactive_run_queue_skips_pending_messages_on_finish() {
  std::vector<ava::app::EventEnvelope> events;
  ava::app::InteractiveRunQueue queue("session_queue", "request_active",
                                      [&events](const ava::app::EventEnvelope& envelope) {
                                        events.push_back(envelope);
                                        return ava::core::VoidResult{};
                                      });

  auto steer = queue.queue_steering("too late");
  auto follow = queue.queue_follow_up("also too late");
  auto finished = queue.finish(false);
  auto after_finish = queue.queue_follow_up("must not queue");

  expect(steer.has_value() && follow.has_value() && finished.has_value(), "interactive run queue finishes cleanly");
  expect(!after_finish.has_value(), "interactive run queue rejects messages after active run finish");
  expect(events.size() == 4 && events[2].name == "steer_skipped" && events[3].name == "follow_up_skipped" &&
             events.back().payload_json.find("\"reason\":\"run_completed_before_safe_point\"") != std::string::npos,
         "interactive run queue emits skipped events for unconsumed messages");
}

void test_interactive_run_queue_restores_latest_pending_message() {
  std::vector<ava::app::EventEnvelope> events;
  ava::app::InteractiveRunQueue queue("session_queue", "request_active",
                                      [&events](const ava::app::EventEnvelope& envelope) {
                                        events.push_back(envelope);
                                        return ava::core::VoidResult{};
                                      });

  auto follow = queue.queue_follow_up("first follow-up");
  auto steer = queue.queue_steering("latest steer");
  auto restored = queue.restore_latest();
  auto taken = queue.take_steering_messages();
  auto next = queue.take_next_follow_up();

  expect(follow.has_value() && steer.has_value() && restored.has_value() && restored->steering &&
             restored->message == "latest steer",
         "interactive run queue restores the latest queued message with its kind");
  expect(taken.has_value() && taken->empty() && next && next->message == "first follow-up",
         "interactive run queue removes restored steering without disturbing older follow-up messages");
  expect(events.size() == 3 && events.back().name == "steer_skipped" &&
             events.back().payload_json.find("\"reason\":\"restored_to_composer\"") != std::string::npos,
         "interactive run queue emits a skipped event when restoring a queued message to the composer");
}

void test_interactive_run_queue_bounds_and_truncates_event_payloads() {
  std::vector<ava::app::EventEnvelope> events;
  ava::app::InteractiveRunQueue queue("session_queue", "request_active",
                                      [&events](const ava::app::EventEnvelope& envelope) {
                                        events.push_back(envelope);
                                        return ava::core::VoidResult{};
                                      });

  const std::string long_message(ava::app::kMaxInteractiveQueueEventMessageBytes + 16, 'x');
  auto queued = queue.queue_steering(long_message);
  const std::string too_large(ava::app::kMaxInteractiveQueuedMessageBytes + 1, 'y');
  auto rejected = queue.queue_steering(too_large);

  expect(queued.has_value(), "interactive run queue accepts messages within the aggregate byte limit");
  expect(!rejected.has_value(), "interactive run queue rejects oversized messages");
  expect(events.size() == 1 && events.back().name == "steer_queued" &&
             events.back().payload_json.find("\"message_truncated\":true") != std::string::npos &&
             events.back().payload_json.find("\"message_bytes\":") != std::string::npos,
         "interactive run queue truncates event payloads without truncating queued content");
  auto taken = queue.take_steering_messages();
  expect(taken.has_value() && taken->size() == 1 && taken->front().size() == long_message.size(),
         "interactive run queue preserves full content for backend consumption");
}

}  // namespace

void run_app_event_bus_tests() {
  test_event_envelope_serialization_is_deterministic();
  test_runtime_event_conversion_preserves_legacy_payload_shape();
  test_runtime_event_bus_adapter_publishes_and_forwards();
  test_runtime_event_bus_adapter_allows_default_legacy_sink();
  test_tool_progress_runtime_event_serialization_and_bus_adapter();
  test_tool_runtime_event_serializes_semantic_frontend_payloads();
  test_reasoning_runtime_event_serialization_hides_provider_private_state();
  test_lifecycle_runtime_event_serialization_and_aliases();
  test_interactive_run_queue_emits_steer_queued_and_applied_events();
  test_interactive_run_queue_runs_follow_up_lifecycle();
  test_interactive_run_queue_skips_pending_messages_on_finish();
  test_interactive_run_queue_restores_latest_pending_message();
  test_interactive_run_queue_bounds_and_truncates_event_payloads();
}
