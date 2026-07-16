#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/app/EventEnvelope.h"
#include "ava/app/events.h"
#include "ava/app/interactive_run_queue.h"
#include "ava/app/runtime/CancellationPayload.h"
#include "ava/app/runtime/CompletionPayload.h"
#include "ava/app/runtime/ErrorPayload.h"
#include "ava/app/runtime/RetryPayload.h"
#include "ava/app/runtime/ToolPayload.h"
#include "ava/agent/mode.h"
#include "ava/core/json.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

void test_event_envelope_serialization_is_deterministic()
{
  ava::app::EventEnvelope const envelope{.schema_version = 1,
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

  auto const json = ava::app::serialize_event_envelope_json(envelope);
  expect(json ==
             "{\"schema_version\":1,\"event_id\":\"event_1\","
             "\"timestamp\":\"2026-04-30T00:00:00Z\",\"session_id\":\"session_1\","
             "\"run_id\":\"run_1\",\"turn_id\":\"turn_1\",\"message_id\":\"message_1\","
             "\"request_id\":\"request_1\",\"correlation_id\":\"correlation_1\","
             "\"name\":\"user_message\",\"type\":\"user_message\","
             "\"payload\":{\"text\":\"hello\\n\\\"ava\\\"\"},\"text\":\"hello\\n\\\"ava\\\"\"}",
         "event envelope JSON serialization uses deterministic field ordering");

  auto const jsonl = ava::app::serialize_event_envelope_jsonl(envelope);
  expect(jsonl == json + '\n', "event envelope JSONL appends one newline");
}

void test_runtime_event_conversion_preserves_legacy_payload_shape()
{
  ava::app::runtime::Event event;
  event.type = ava::app::runtime::EventType::ToolResult;
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
  auto const envelope = ava::app::to_event_envelope(event, context);
  expect(envelope.schema_version == 1, "runtime event conversion sets schema version");
  expect(envelope.event_id == "event_2", "runtime event conversion uses supplied event id");
  expect(envelope.timestamp == "2026-04-30T00:00:01Z", "runtime event conversion carries timestamp");
  expect(envelope.session_id == "session_1", "runtime event conversion carries session id");
  expect(envelope.run_id == "run_1" && envelope.turn_id == "turn_2", "runtime event conversion carries optional ids");
  expect(envelope.name == "tool_result", "runtime event conversion maps event name");
  expect(envelope.payload_type == "tool", "runtime event conversion classifies tool payloads");
  expect(envelope.payload_json == "{\"text\":\"read ok\",\"call_id\":\"call_1\",\"tool\":\"read\",\"status\":\"completed\"}",
         "runtime event conversion maps legacy event fields into payload object");
  auto const envelope_json = ava::app::serialize_event_envelope_json(envelope);
  expect(envelope_json.find("\"payload_type\":\"tool\"") != std::string::npos,
         "runtime event envelopes advertise payload family without changing payload shape");
}

void test_runtime_event_bus_adapter_publishes_and_forwards()
{
  ava::app::EventBus bus;
  std::vector<ava::app::EventEnvelope> published;
  std::vector<ava::app::runtime::Event> forwarded;
  bus.subscribe([&published](ava::app::EventEnvelope const& envelope) {
    published.push_back(envelope);
    return ava::core::VoidResult{};
  });

  ava::app::EventEnvelopeContext context;
  context.event_id = "event_3";
  context.correlation_id = "correlation_1";
  auto sink = ava::app::make_runtime_event_bus_adapter(bus, context, [&forwarded](ava::app::runtime::Event const& event) {
    forwarded.push_back(event);
    return ava::core::VoidResult{};
  });

  ava::app::runtime::Event event;
  event.type = ava::app::runtime::EventType::AssistantMessage;
  event.timestamp = "2026-04-30T00:00:02Z";
  event.session_id = "session_1";
  event.text = "done";

  auto const emitted = sink(event);
  expect(emitted.has_value(), "runtime event bus adapter succeeds when bus and legacy sink succeed");
  expect(published.size() == 1 && published.front().name == "assistant_message" && published.front().correlation_id == "correlation_1",
         "runtime event bus adapter publishes converted envelopes");
  expect(forwarded.size() == 1 && forwarded.front().text == "done", "runtime event bus adapter forwards runtime events to legacy sink");
}

void test_runtime_event_bus_adapter_allows_default_legacy_sink()
{
  ava::app::EventBus bus;
  std::vector<ava::app::EventEnvelope> published;
  bus.subscribe([&published](ava::app::EventEnvelope const& envelope) {
    published.push_back(envelope);
    return ava::core::VoidResult{};
  });

  auto sink = ava::app::make_runtime_event_bus_adapter(bus);
  ava::app::runtime::Event event;
  event.type = ava::app::runtime::EventType::Done;
  event.timestamp = "2026-04-30T00:00:03Z";
  event.session_id = "session_1";

  auto const emitted = sink(event);
  expect(emitted.has_value() && published.size() == 1 && published.front().name == "done", "runtime event bus adapter supports a null legacy sink");
}

void test_tool_progress_runtime_event_serialization_and_bus_adapter()
{
  ava::app::runtime::Event event;
  event.type = ava::app::runtime::EventType::ToolProgress;
  event.timestamp = "2026-04-30T00:00:04Z";
  event.session_id = "session_1";
  event.text = "reading";
  event.call_id = "call_2";
  event.tool_name = "read_file";
  event.status = "running";

  auto const json = ava::app::serialize_event_json(event);
  expect(json ==
             "{\"type\":\"tool_progress\",\"timestamp\":\"2026-04-30T00:00:04Z\","
             "\"session_id\":\"session_1\",\"text\":\"reading\",\"call_id\":\"call_2\","
             "\"tool\":\"read_file\",\"status\":\"running\"}",
         "tool progress serializes with existing runtime event payload fields");

  ava::app::EventBus bus;
  std::vector<ava::app::EventEnvelope> published;
  bus.subscribe([&published](ava::app::EventEnvelope const& envelope) {
    published.push_back(envelope);
    return ava::core::VoidResult{};
  });
  ava::app::EventEnvelopeContext context;
  context.event_id = "event_progress";
  auto sink = ava::app::make_runtime_event_bus_adapter(bus, context);
  auto const emitted = sink(event);
  expect(emitted.has_value() && published.size() == 1 && published.front().name == "tool_progress", "event bus adapter publishes tool progress envelopes");
  expect(published.front().payload_json == "{\"text\":\"reading\",\"call_id\":\"call_2\",\"tool\":\"read_file\",\"status\":\"running\"}",
         "tool progress envelope payload keeps existing tool event fields");
}

void test_tool_runtime_event_serializes_semantic_frontend_payloads()
{
  ava::app::runtime::Event event;
  event.type = ava::app::runtime::EventType::ToolResult;
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
  event.permission_request_ids = {"permreq_edit"};
  event.diff_truncated = true;
  event.truncated = true;
  event.byte_limited = true;
  event.line_limited = true;
  event.spill_path = "/tmp/ava-spill/tool.txt";
  event.spill_truncated = true;
  event.output_bytes = 128;
  event.total_bytes = 512;
  event.output_lines = 4;
  event.total_lines = 11;
  event.start_line = 2;
  event.end_line = 5;
  event.next_offset_line = 6;
  event.omitted_bytes = 384;
  event.omitted_lines = 7;

  auto const json = ava::app::serialize_event_json(event);
  expect(json.find("\"args\":{\"path\":\"src/main.cpp\"}") != std::string::npos &&
             json.find("\"result\":{\"ok\":true,\"path\":\"src/main.cpp\"}") != std::string::npos &&
             json.find("\"structured_result\":{\"schema_version\":1") != std::string::npos &&
             json.find("\"content_type\":\"application/json\"") != std::string::npos &&
             json.find("\"changed_paths\":[\"src/main.cpp\",\"include/ava/app/events.h\"]") != std::string::npos &&
             json.find("\"permission_request_ids\":[\"permreq_edit\"]") != std::string::npos && json.find("\"diff_truncated\":true") != std::string::npos &&
             json.find("\"byte_limited\":true") != std::string::npos && json.find("\"line_limited\":true") != std::string::npos &&
             json.find("\"output_lines\":4") != std::string::npos && json.find("\"next_offset_line\":6") != std::string::npos &&
             json.find("\"omitted_lines\":7") != std::string::npos,
         "tool runtime events serialize semantic args, result, permission ids, diffs, and truncation metadata");

  ava::app::EventEnvelopeContext context;
  context.event_id = "event_semantic_tool";
  auto const envelope = ava::app::to_event_envelope(event, context);
  auto const args = ava::core::json::object_field(envelope.payload_json, "args");
  auto const result = ava::core::json::object_field(envelope.payload_json, "result");
  auto const structured_result = ava::core::json::object_field(envelope.payload_json, "structured_result");
  auto const paths = ava::core::json::strings_in_array_field(envelope.payload_json, "changed_paths");
  auto const permission_ids = ava::core::json::strings_in_array_field(envelope.payload_json, "permission_request_ids");
  expect(envelope.name == "tool_result" && args && *args == "{\"path\":\"src/main.cpp\"}" && result && *result == "{\"ok\":true,\"path\":\"src/main.cpp\"}" &&
             structured_result && envelope.payload_type == "tool" && structured_result->find("\"status\":\"success\"") != std::string::npos &&
             paths.size() == 2 && paths[1] == "include/ava/app/events.h" && permission_ids.size() == 1 && permission_ids[0] == "permreq_edit" &&
             envelope.payload_json.find("\"output_lines\":4") != std::string::npos && envelope.payload_json.find("\"total_lines\":11") != std::string::npos &&
             envelope.payload_json.find("\"next_offset_line\":6") != std::string::npos &&
             envelope.payload_json.find("\"spill_path\":\"/tmp/ava-spill/tool.txt\"") != std::string::npos,
         "tool event envelopes preserve semantic payloads for frontend replay");
}

void test_high_risk_runtime_payload_builders_preserve_wire_shapes()
{
  ava::app::runtime::Event tool_event;
  tool_event.type = ava::app::runtime::EventType::ToolResult;
  tool_event.text = "read ok";
  tool_event.call_id = "call_read";
  tool_event.tool_name = "read_file";
  tool_event.tool_arguments_json = "{\"path\":\"README.md\"}";
  tool_event.tool_result_json = "{\"ok\":true}";
  tool_event.tool_structured_result_json = "{\"schema_version\":1,\"ok\":true}";
  tool_event.status = "success";
  tool_event.content_type = "application/json";
  tool_event.changed_paths = {"README.md"};
  tool_event.permission_request_ids = {"permreq_read"};
  tool_event.output_lines = 2;
  tool_event.total_lines = 3;
  auto const tool_payload = ava::app::serialize_payload_json(ava::app::tool_payload_from_event(tool_event));
  expect(tool_payload ==
             "{\"text\":\"read ok\",\"call_id\":\"call_read\",\"tool\":\"read_file\","
             "\"args\":{\"path\":\"README.md\"},\"result\":{\"ok\":true},"
             "\"structured_result\":{\"schema_version\":1,\"ok\":true},\"status\":\"success\","
             "\"content_type\":\"application/json\",\"changed_paths\":[\"README.md\"],"
             "\"permission_request_ids\":[\"permreq_read\"],\"output_lines\":2,\"total_lines\":3}",
         "typed tool payload builder preserves the existing JSON field contract");
  expect(ava::app::to_event_envelope(tool_event).payload_json == tool_payload, "tool event envelopes are serialized through the typed payload contract");

  ava::app::runtime::Event retry_event;
  retry_event.type = ava::app::runtime::EventType::RetryTick;
  retry_event.text = "HTTP status 429";
  retry_event.status = "request";
  retry_event.trigger = "provider_transport";
  retry_event.reason = "rate_limited";
  retry_event.attempt = 2;
  retry_event.max_attempts = 3;
  retry_event.delay_ms = 1000;
  retry_event.remaining_ms = 250;
  auto const retry_payload = ava::app::serialize_payload_json(ava::app::retry_payload_from_event(retry_event));
  expect(retry_payload ==
             "{\"text\":\"HTTP status 429\",\"status\":\"request\",\"trigger\":\"provider_transport\","
             "\"reason\":\"rate_limited\",\"attempt\":2,\"max_attempts\":3,\"delay_ms\":1000,"
             "\"remaining_ms\":250}",
         "typed retry payload builder preserves retry timing fields");
  expect(ava::app::to_event_envelope(retry_event).payload_json == retry_payload, "retry event envelopes are serialized through the typed payload contract");

  ava::app::runtime::Event canceled_event;
  canceled_event.type = ava::app::runtime::EventType::Canceled;
  canceled_event.text = "stopped by user";
  canceled_event.error_category = "canceled";
  canceled_event.error_message = "agent loop canceled";
  canceled_event.error_details = "canceled: agent loop canceled";
  canceled_event.reason = "agent loop canceled";
  auto const cancellation_payload = ava::app::serialize_payload_json(ava::app::cancellation_payload_from_event(canceled_event));
  expect(cancellation_payload ==
             "{\"text\":\"stopped by user\",\"category\":\"canceled\",\"message\":\"agent loop canceled\","
             "\"details\":\"canceled: agent loop canceled\",\"reason\":\"agent loop canceled\"}",
         "typed cancellation payload builder preserves cancellation reason fields");
  expect(ava::app::to_event_envelope(canceled_event).payload_json == cancellation_payload,
         "cancellation event envelopes are serialized through the typed payload contract");

  ava::app::runtime::Event error_event;
  error_event.type = ava::app::runtime::EventType::Error;
  error_event.error_category = "io";
  error_event.error_code = "write_failed";
  error_event.error_message = "failed to write RPC JSONL record";
  error_event.error_details = "io: failed to write RPC JSONL record";
  auto const error_payload = ava::app::serialize_payload_json(ava::app::error_payload_from_event(error_event));
  expect(error_payload ==
             "{\"category\":\"io\",\"error_code\":\"write_failed\","
             "\"message\":\"failed to write RPC JSONL record\","
             "\"details\":\"io: failed to write RPC JSONL record\"}",
         "typed error payload builder preserves error diagnostic fields");
  expect(ava::app::to_event_envelope(error_event).payload_json == error_payload, "error event envelopes are serialized through the typed payload contract");

  ava::app::runtime::Event done_event;
  done_event.type = ava::app::runtime::EventType::Done;
  done_event.stop_reason = "completed";
  done_event.provider_iterations = 2;
  done_event.tool_calls = 1;
  auto const completion_payload = ava::app::serialize_payload_json(ava::app::completion_payload_from_event(done_event));
  expect(completion_payload == "{\"stop_reason\":\"completed\",\"provider_iterations\":2,\"tool_calls\":1}",
         "typed completion payload builder preserves usage summary counters");
  expect(ava::app::to_event_envelope(done_event).payload_json == completion_payload,
         "completion event envelopes are serialized through the typed payload contract");
}

void test_tool_and_cancellation_event_envelopes_have_golden_wire_shapes()
{
  ava::app::runtime::Event success_event;
  success_event.type = ava::app::runtime::EventType::ToolResult;
  success_event.timestamp = "2026-05-07T00:00:00Z";
  success_event.session_id = "session_golden";
  success_event.text = "wrote note";
  success_event.call_id = "call_write";
  success_event.tool_name = "write_file";
  success_event.tool_arguments_json = "{\"path\":\"note.txt\"}";
  success_event.tool_result_json = "{\"ok\":true,\"path\":\"note.txt\"}";
  success_event.tool_structured_result_json = "{\"schema_version\":1,\"call_id\":\"call_write\",\"tool\":\"write_file\",\"status\":\"success\",";
  success_event.tool_structured_result_json += "\"ok\":true,\"content_type\":\"application/json\",\"content\":{\"ok\":true,\"path\":\"note.txt\"},";
  success_event.tool_structured_result_json += "\"changed_paths\":[\"note.txt\"],\"permission_request_ids\":[\"permreq_write\"]}";
  success_event.status = "success";
  success_event.content_type = "application/json";
  success_event.diff = "--- note.txt\n+++ note.txt\n-old\n+new";
  success_event.changed_paths = {"note.txt"};
  success_event.permission_request_ids = {"permreq_write"};
  success_event.spill_path = "spill/tool.txt";
  success_event.diff_truncated = true;
  success_event.truncated = true;
  success_event.byte_limited = true;
  success_event.line_limited = true;
  success_event.spill_truncated = true;
  success_event.output_bytes = 128;
  success_event.total_bytes = 512;
  success_event.output_lines = 2;
  success_event.total_lines = 5;
  success_event.start_line = 1;
  success_event.end_line = 2;
  success_event.next_offset_line = 3;
  success_event.omitted_bytes = 384;
  success_event.omitted_lines = 3;

  ava::app::EventEnvelopeContext success_context;
  success_context.event_id = "event_tool_success";
  auto const success_payload = std::string{"{\"text\":\"wrote note\",\"call_id\":\"call_write\",\"tool\":\"write_file\","} +
                               "\"args\":{\"path\":\"note.txt\"},\"result\":{\"ok\":true,\"path\":\"note.txt\"}," +
                               "\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_write\",\"tool\":\"write_file\"," +
                               "\"status\":\"success\",\"ok\":true,\"content_type\":\"application/json\"," +
                               "\"content\":{\"ok\":true,\"path\":\"note.txt\"},\"changed_paths\":[\"note.txt\"]," +
                               "\"permission_request_ids\":[\"permreq_write\"]},\"status\":\"success\"," +
                               "\"content_type\":\"application/json\",\"diff\":\"--- note.txt\\n+++ note.txt\\n-old\\n+new\"," +
                               "\"changed_paths\":[\"note.txt\"],\"permission_request_ids\":[\"permreq_write\"]," +
                               "\"spill_path\":\"spill/tool.txt\",\"diff_truncated\":true,\"truncated\":true," +
                               "\"byte_limited\":true,\"line_limited\":true,\"spill_truncated\":true,\"output_bytes\":128," +
                               "\"total_bytes\":512,\"output_lines\":2,\"total_lines\":5,\"start_line\":1,\"end_line\":2," +
                               "\"next_offset_line\":3,\"omitted_bytes\":384,\"omitted_lines\":3}";
  auto const success_envelope = ava::app::to_event_envelope(success_event, success_context);
  auto const success_json = ava::app::serialize_event_envelope_json(success_envelope);
  auto const expected_success_json = std::string{"{\"schema_version\":1,\"event_id\":\"event_tool_success\","} +
                                     "\"timestamp\":\"2026-05-07T00:00:00Z\",\"session_id\":\"session_golden\"," +
                                     "\"name\":\"tool_result\",\"type\":\"tool_result\",\"payload_type\":\"tool\",\"payload\":" + success_payload +
                                     ",\"text\":\"wrote note\",\"call_id\":\"call_write\",\"tool\":\"write_file\",\"status\":\"success\"," +
                                     "\"content_type\":\"application/json\",\"diff\":\"--- note.txt\\n+++ note.txt\\n-old\\n+new\"," +
                                     "\"spill_path\":\"spill/tool.txt\",\"output_bytes\":128,\"total_bytes\":512,\"output_lines\":2," +
                                     "\"total_lines\":5,\"start_line\":1,\"end_line\":2,\"next_offset_line\":3," + "\"omitted_bytes\":384,\"omitted_lines\":3}";
  expect(success_envelope.payload_type == "tool" && success_envelope.payload_json == success_payload && success_json == expected_success_json,
         "successful tool event envelope locks structured result, permission ids, diffs, truncation, and spill metadata");

  ava::app::runtime::Event denied_event;
  denied_event.type = ava::app::runtime::EventType::ToolResult;
  denied_event.timestamp = "2026-05-07T00:00:01Z";
  denied_event.session_id = "session_golden";
  denied_event.text = "permission denied";
  denied_event.call_id = "call_denied";
  denied_event.tool_name = "write_file";
  denied_event.tool_structured_result_json =
      "{\"schema_version\":1,\"call_id\":\"call_denied\",\"tool\":\"write_file\",\"status\":\"error\","
      "\"ok\":false,\"summary\":\"permission denied\",\"content_type\":\"text/plain\","
      "\"content\":\"permission denied\",\"error\":{\"category\":\"permission\",\"code\":\"permission_denied\","
      "\"message\":\"permission denied\",\"details\":\"resolution: deny\"}}";
  denied_event.status = "error";
  denied_event.error_category = "permission";
  denied_event.error_code = "permission_denied";
  denied_event.error_message = "permission denied";
  denied_event.error_details = "resolution: deny";
  denied_event.content_type = "text/plain";

  ava::app::EventEnvelopeContext denied_context;
  denied_context.event_id = "event_tool_denied";
  auto const denied_payload = std::string{"{\"text\":\"permission denied\",\"call_id\":\"call_denied\",\"tool\":\"write_file\","} +
                              "\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_denied\",\"tool\":\"write_file\"," +
                              "\"status\":\"error\",\"ok\":false,\"summary\":\"permission denied\",\"content_type\":\"text/plain\"," +
                              "\"content\":\"permission denied\",\"error\":{\"category\":\"permission\",\"code\":\"permission_denied\"," +
                              "\"message\":\"permission denied\",\"details\":\"resolution: deny\"}},\"status\":\"error\"," +
                              "\"category\":\"permission\",\"error_code\":\"permission_denied\",\"message\":\"permission denied\"," +
                              "\"details\":\"resolution: deny\",\"content_type\":\"text/plain\"}";
  auto const denied_envelope = ava::app::to_event_envelope(denied_event, denied_context);
  auto const denied_json = ava::app::serialize_event_envelope_json(denied_envelope);
  auto const expected_denied_json = std::string{"{\"schema_version\":1,\"event_id\":\"event_tool_denied\","} +
                                    "\"timestamp\":\"2026-05-07T00:00:01Z\",\"session_id\":\"session_golden\"," +
                                    "\"name\":\"tool_result\",\"type\":\"tool_result\",\"payload_type\":\"tool\",\"payload\":" + denied_payload +
                                    ",\"text\":\"permission denied\",\"call_id\":\"call_denied\",\"tool\":\"write_file\"," +
                                    "\"status\":\"error\",\"category\":\"permission\",\"error_code\":\"permission_denied\"," +
                                    "\"message\":\"permission denied\",\"details\":\"resolution: deny\",\"content_type\":\"text/plain\"}";
  auto const denied_structured = ava::core::json::object_field(denied_envelope.payload_json, "structured_result");
  expect(denied_envelope.payload_type == "tool" && denied_envelope.payload_json == denied_payload && denied_json == expected_denied_json && denied_structured &&
             denied_structured->find("\"status\":\"error\"") != std::string::npos && denied_structured->find("\"ok\":false") != std::string::npos,
         "denied tool event envelope locks structured error fields with status/ok consistency");

  ava::app::runtime::Event canceled_event;
  canceled_event.type = ava::app::runtime::EventType::Canceled;
  canceled_event.timestamp = "2026-05-07T00:00:02Z";
  canceled_event.session_id = "session_golden";
  canceled_event.text = "stopped by user";
  canceled_event.status = "canceled";
  canceled_event.error_category = "canceled";
  canceled_event.error_code = "user_canceled";
  canceled_event.error_message = "agent loop canceled";
  canceled_event.error_details = "canceled at permission wait";
  canceled_event.trigger = "rpc_cancel";
  canceled_event.reason = "user_requested";

  ava::app::EventEnvelopeContext canceled_context;
  canceled_context.event_id = "event_canceled";
  auto const canceled_payload =
      "{\"text\":\"stopped by user\",\"status\":\"canceled\",\"category\":\"canceled\","
      "\"error_code\":\"user_canceled\",\"message\":\"agent loop canceled\","
      "\"details\":\"canceled at permission wait\",\"trigger\":\"rpc_cancel\",\"reason\":\"user_requested\"}";
  auto const canceled_envelope = ava::app::to_event_envelope(canceled_event, canceled_context);
  auto const canceled_json = ava::app::serialize_event_envelope_json(canceled_envelope);
  auto const expected_canceled_json = std::string{"{\"schema_version\":1,\"event_id\":\"event_canceled\","} +
                                      "\"timestamp\":\"2026-05-07T00:00:02Z\",\"session_id\":\"session_golden\"," +
                                      "\"name\":\"canceled\",\"type\":\"canceled\",\"payload_type\":\"cancellation\",\"payload\":" + canceled_payload +
                                      ",\"text\":\"stopped by user\",\"status\":\"canceled\",\"category\":\"canceled\"," +
                                      "\"error_code\":\"user_canceled\",\"message\":\"agent loop canceled\"," +
                                      "\"details\":\"canceled at permission wait\",\"trigger\":\"rpc_cancel\",\"reason\":\"user_requested\"}";
  expect(canceled_envelope.payload_type == "cancellation" && canceled_envelope.payload_json == canceled_payload && canceled_json == expected_canceled_json,
         "canceled event envelope locks payload_type, status, trigger, and reason fields");
}

void test_reasoning_runtime_event_serialization_hides_provider_private_state()
{
  ava::app::runtime::Event event;
  event.type = ava::app::runtime::EventType::ReasoningDelta;
  event.timestamp = "2026-04-30T00:00:05Z";
  event.session_id = "session_1";
  event.text = "visible reasoning summary";
  event.reasoning_format = "anthropic_thinking";
  event.reasoning_redacted = true;
  event.reasoning_signature_present = true;

  auto const json = ava::app::serialize_event_json(event);
  expect(json ==
             "{\"type\":\"reasoning_delta\",\"timestamp\":\"2026-04-30T00:00:05Z\","
             "\"session_id\":\"session_1\",\"text\":\"visible reasoning summary\","
             "\"reasoning_format\":\"anthropic_thinking\",\"reasoning_redacted\":true,"
             "\"reasoning_signature_present\":true}",
         "reasoning runtime event serializes visible text and format only");

  ava::app::EventEnvelopeContext context;
  context.event_id = "event_reasoning";
  auto const envelope = ava::app::to_event_envelope(event, context);
  expect(envelope.name == "reasoning_delta" && envelope.payload_type == "reasoning" &&
             envelope.payload_json ==
                 "{\"text\":\"visible reasoning summary\",\"reasoning_format\":\"anthropic_thinking\","
                 "\"reasoning_redacted\":true,\"reasoning_signature_present\":true}",
         "reasoning envelope carries frontend-visible reasoning payload");
  auto const envelope_json = ava::app::serialize_event_envelope_json(envelope);
  expect(envelope_json.find("super-secret-signature") == std::string::npos && envelope_json.find("reasoning_signature\":") == std::string::npos &&
             envelope_json.find("\"signature\":") == std::string::npos,
         "reasoning frontend event envelope never exposes provider-private signatures");
}

void test_lifecycle_runtime_event_serialization_and_aliases()
{
  ava::app::runtime::Event event;
  event.type = ava::app::runtime::EventType::CompactionEnd;
  event.timestamp = "2026-04-30T00:00:06Z";
  event.session_id = "session_1";
  event.status = "completed";
  event.trigger = "context_overflow";
  event.attempt = 1;
  event.max_attempts = 2;
  event.estimated_tokens = 9000;
  event.threshold_tokens = 8000;
  event.summary_bytes = 512;

  auto const json = ava::app::serialize_event_json(event);
  expect(json ==
             "{\"type\":\"compaction_end\",\"timestamp\":\"2026-04-30T00:00:06Z\","
             "\"session_id\":\"session_1\",\"status\":\"completed\",\"trigger\":\"context_overflow\","
             "\"attempt\":1,\"max_attempts\":2,\"estimated_tokens\":9000,\"threshold_tokens\":8000,"
             "\"summary_bytes\":512}",
         "compaction lifecycle runtime events serialize backend-owned compaction metadata");

  ava::app::EventEnvelopeContext context;
  context.event_id = "event_compaction";
  auto const envelope = ava::app::to_event_envelope(event, context);
  auto const envelope_json = ava::app::serialize_event_envelope_json(envelope);
  expect(envelope.name == "compaction_end" && envelope.payload_type == "compaction" &&
             envelope.payload_json.find("\"trigger\":\"context_overflow\"") != std::string::npos &&
             envelope.payload_json.find("\"max_attempts\":2") != std::string::npos && envelope_json.find("\"summary_bytes\":512") != std::string::npos,
         "compaction lifecycle envelope preserves payload and top-level aliases for stream clients");

  event.type = ava::app::runtime::EventType::Canceled;
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

  event.type = ava::app::runtime::EventType::RetryTick;
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
  auto const retry_tick_json = ava::app::serialize_event_json(event);
  expect(retry_tick_json.find("\"type\":\"retry_tick\"") != std::string::npos && retry_tick_json.find("\"remaining_ms\":500") != std::string::npos,
         "retry countdown tick runtime events serialize explicit backend timing data");
  expect(ava::app::to_string(ava::app::payload_type_for_event(ava::app::runtime::EventType::SessionStart)) == "session" &&
             ava::app::to_string(ava::app::payload_type_for_event(ava::app::runtime::EventType::AssistantMessage)) == "message" &&
             ava::app::to_string(ava::app::payload_type_for_event(ava::app::runtime::EventType::RetryTick)) == "retry" &&
             ava::app::to_string(ava::app::payload_type_for_event(ava::app::runtime::EventType::Canceled)) == "cancellation" &&
             ava::app::to_string(ava::app::runtime::PayloadType::Permission) == "permission" &&
             ava::app::to_string(ava::app::runtime::PayloadType::Question) == "question" &&
             ava::app::to_string(ava::app::runtime::PayloadType::Queue) == "queue" &&
             ava::app::to_string(ava::app::payload_type_for_event(ava::app::runtime::EventType::Done)) == "completion",
         "runtime event types map to stable payload families");
}

void test_interactive_run_queue_emits_steer_queued_and_applied_events()
{
  std::vector<ava::app::EventEnvelope> events;
  ava::app::InteractiveRunQueue queue("session_queue", "request_active", [&events](ava::app::EventEnvelope const& envelope) {
    events.push_back(envelope);
    return ava::core::VoidResult{};
  });

  auto queued = queue.queue_steering("adjust this turn");
  expect(queued.has_value(), "interactive run queue accepts a bounded active-run steering message");
  expect(events.size() == 1 && events.back().name == "steer_queued" && events.back().correlation_id == "request_active" && events.back().request_id &&
             *events.back().request_id != "request_active" && events.back().payload_json.find("\"message\":\"adjust this turn\"") != std::string::npos,
         "interactive run queue emits a correlated steer queued event");

  auto taken = queue.take_steering_messages();
  expect(taken.has_value() && taken->size() == 1 && taken->front() == "adjust this turn",
         "interactive run queue returns queued steering at the backend safe point");
  expect(events.size() == 2 && events.back().name == "steer_applied" && events.back().correlation_id == "request_active",
         "interactive run queue emits a steer applied event when consumed by the backend");
}

void test_interactive_run_queue_runs_follow_up_lifecycle()
{
  std::vector<ava::app::EventEnvelope> events;
  ava::app::InteractiveRunQueue queue("session_queue", "request_active", [&events](ava::app::EventEnvelope const& envelope) {
    events.push_back(envelope);
    return ava::core::VoidResult{};
  });

  auto queued = queue.queue_follow_up("next turn");
  auto next = queue.take_next_follow_up();
  auto started = next ? queue.mark_follow_up_started(*next) : ava::core::VoidResult{};
  auto steer_after_start = queue.queue_steering("steer follow-up");

  expect(queued.has_value() && next && next->message == "next turn" && started.has_value() && steer_after_start.has_value(),
         "interactive run queue accepts and starts a queued follow-up");
  expect(events.size() == 3 && events[0].name == "follow_up_queued" && events[1].name == "follow_up_started" && events[1].request_id == next->request_id &&
             events[1].correlation_id == next->request_id && events[2].name == "steer_queued" && events[2].correlation_id == next->request_id,
         "interactive run queue retargets active correlation when a follow-up starts");
}

void test_interactive_run_queue_skips_pending_messages_on_finish()
{
  std::vector<ava::app::EventEnvelope> events;
  ava::app::InteractiveRunQueue queue("session_queue", "request_active", [&events](ava::app::EventEnvelope const& envelope) {
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

void test_interactive_run_queue_restores_latest_pending_message()
{
  std::vector<ava::app::EventEnvelope> events;
  ava::app::InteractiveRunQueue queue("session_queue", "request_active", [&events](ava::app::EventEnvelope const& envelope) {
    events.push_back(envelope);
    return ava::core::VoidResult{};
  });

  auto follow = queue.queue_follow_up("first follow-up");
  auto steer = queue.queue_steering("latest steer");
  auto restored = queue.restore_latest();
  auto taken = queue.take_steering_messages();
  auto next = queue.take_next_follow_up();

  expect(follow.has_value() && steer.has_value() && restored.has_value() && restored->steering && restored->message == "latest steer",
         "interactive run queue restores the latest queued message with its kind");
  expect(taken.has_value() && taken->empty() && next && next->message == "first follow-up",
         "interactive run queue removes restored steering without disturbing older follow-up messages");
  expect(events.size() == 3 && events.back().name == "steer_skipped" &&
             events.back().payload_json.find("\"reason\":\"restored_to_composer\"") != std::string::npos,
         "interactive run queue emits a skipped event when restoring a queued message to the composer");
}

void test_interactive_run_queue_bounds_and_truncates_event_payloads()
{
  std::vector<ava::app::EventEnvelope> events;
  ava::app::InteractiveRunQueue queue("session_queue", "request_active", [&events](ava::app::EventEnvelope const& envelope) {
    events.push_back(envelope);
    return ava::core::VoidResult{};
  });

  std::string const long_message(ava::app::kMaxInteractiveQueueEventMessageBytes + 16, 'x');
  auto queued = queue.queue_steering(long_message);
  std::string const too_large(ava::app::kMaxInteractiveQueuedMessageBytes + 1, 'y');
  auto rejected = queue.queue_steering(too_large);

  expect(queued.has_value(), "interactive run queue accepts messages within the aggregate byte limit");
  expect(!rejected.has_value(), "interactive run queue rejects oversized messages");
  expect(events.size() == 1 && events.back().name == "steer_queued" && events.back().payload_json.find("\"message_truncated\":true") != std::string::npos &&
             events.back().payload_json.find("\"message_bytes\":") != std::string::npos,
         "interactive run queue truncates event payloads without truncating queued content");
  auto taken = queue.take_steering_messages();
  expect(taken.has_value() && taken->size() == 1 && taken->front().size() == long_message.size(),
         "interactive run queue preserves full content for backend consumption");
}

}  // namespace

void run_app_event_bus_tests()
{
  test_event_envelope_serialization_is_deterministic();
  test_runtime_event_conversion_preserves_legacy_payload_shape();
  test_runtime_event_bus_adapter_publishes_and_forwards();
  test_runtime_event_bus_adapter_allows_default_legacy_sink();
  test_tool_progress_runtime_event_serialization_and_bus_adapter();
  test_tool_runtime_event_serializes_semantic_frontend_payloads();
  test_high_risk_runtime_payload_builders_preserve_wire_shapes();
  test_tool_and_cancellation_event_envelopes_have_golden_wire_shapes();
  test_reasoning_runtime_event_serialization_hides_provider_private_state();
  test_lifecycle_runtime_event_serialization_and_aliases();
  test_interactive_run_queue_emits_steer_queued_and_applied_events();
  test_interactive_run_queue_runs_follow_up_lifecycle();
  test_interactive_run_queue_skips_pending_messages_on_finish();
  test_interactive_run_queue_restores_latest_pending_message();
  test_interactive_run_queue_bounds_and_truncates_event_payloads();
}
