#include <iostream>
#include <string>
#include <vector>

#include "ava/agent/mode.h"
#include "ava/app/events.h"
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

}  // namespace

void run_app_event_bus_tests() {
  test_event_envelope_serialization_is_deterministic();
  test_runtime_event_conversion_preserves_legacy_payload_shape();
  test_runtime_event_bus_adapter_publishes_and_forwards();
  test_runtime_event_bus_adapter_allows_default_legacy_sink();
}
