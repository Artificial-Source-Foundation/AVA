#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ava/provider/anthropic_response_support.h"
#include "tests/support/test_harness.h"

namespace {

struct StreamEventHarness {
  std::vector<ava::provider::StreamEvent> events;
  std::map<long long, ava::provider::AnthropicStreamParser::ToolBlock> tools;
  std::map<long long, ava::provider::AnthropicStreamParser::ReasoningBlock> reasoning_blocks;
  std::optional<ava::provider::TokenUsage> usage;
  std::string stop_reason;
  std::string data;
  bool saw_data = false;
  bool message_stop_seen = false;
  bool error_seen = false;

  void append_data(std::string_view event_data)
  {
    ava::provider::detail::append_anthropic_event_for_data(events, tools, reasoning_blocks, usage, stop_reason,
                                                           saw_data, message_stop_seen, error_seen, event_data);
  }

  void append_line(std::string line)
  {
    ava::provider::detail::append_anthropic_events_for_sse_line(events, tools, reasoning_blocks, usage, stop_reason,
                                                                data, saw_data, message_stop_seen, error_seen,
                                                                std::move(line));
  }
};

void test_anthropic_stream_support_projects_tool_lifecycle_events()
{
  StreamEventHarness harness;
  harness.append_data(
      R"({"type":"content_block_start","index":0,"content_block":{"type":"tool_use","id":"toolu_1","name":"read_file"}})");
  harness.append_data(
      R"({"type":"content_block_delta","index":0,"delta":{"type":"input_json_delta","partial_json":"{\"path\""}})");
  harness.append_data(R"({"type":"content_block_stop","index":0})");

  expect(harness.events.size() == 3 && harness.events[0].type == ava::provider::StreamEventType::ToolCallStart &&
             harness.events[0].tool_call_id == "toolu_1" && harness.events[0].tool_name == "read_file" &&
             harness.events[1].type == ava::provider::StreamEventType::ToolCallDelta &&
             harness.events[1].tool_call_id == "toolu_1" &&
             harness.events[2].type == ava::provider::StreamEventType::ToolCallEnd &&
             harness.events[2].tool_call_id == "toolu_1",
         "Anthropic stream support projects tool start, argument delta, and end events");
}

void test_anthropic_stream_support_projects_reasoning_events()
{
  StreamEventHarness harness;
  harness.append_data(
      R"({"type":"content_block_start","index":1,"content_block":{"type":"thinking","signature":"sig-a"}})");
  harness.append_data(
      R"({"type":"content_block_delta","index":1,"delta":{"type":"thinking_delta","thinking":"visible"}})");
  harness.append_data(
      R"({"type":"content_block_delta","index":1,"delta":{"type":"signature_delta","signature":"-b"}})");
  harness.append_data(R"({"type":"content_block_stop","index":1})");

  expect(harness.events.size() == 3 && harness.events[0].type == ava::provider::StreamEventType::ReasoningStart &&
             harness.events[1].type == ava::provider::StreamEventType::ReasoningDelta &&
             harness.events[1].text == "visible" &&
             harness.events[2].type == ava::provider::StreamEventType::ReasoningEnd &&
             harness.events[2].reasoning_signature == "sig-a-b" && harness.reasoning_blocks.empty(),
         "Anthropic stream support projects thinking start, delta, signature, and end events");
}

void test_anthropic_stream_support_projects_refusal_and_done()
{
  StreamEventHarness harness;
  harness.append_data(
      R"({"type":"message_delta","delta":{"stop_reason":"refusal","stop_details":{"explanation":"blocked"}},"usage":{"output_tokens":2}})");
  harness.append_data(R"({"type":"message_stop"})");

  expect(harness.events.size() == 2 && harness.events[0].type == ava::provider::StreamEventType::TextDelta &&
             harness.events[0].text == "blocked" && harness.events[1].type == ava::provider::StreamEventType::Done &&
             harness.events[1].stop_reason == "refusal" && harness.events[1].usage &&
             harness.events[1].usage->output_tokens && *harness.events[1].usage->output_tokens == 2 &&
             harness.message_stop_seen,
         "Anthropic stream support projects refusal details and final usage");
}

void test_anthropic_stream_support_reports_malformed_events()
{
  StreamEventHarness harness;
  harness.append_data("not-json");

  expect(harness.events.size() == 1 && harness.events[0].type == ava::provider::StreamEventType::Error &&
             harness.events[0].error_message == "malformed Anthropic stream event" && harness.error_seen,
         "Anthropic stream support reports malformed data events");
}

void test_anthropic_stream_support_aggregates_sse_lines()
{
  StreamEventHarness harness;
  harness.append_line(R"(data: {"type":"content_block_delta","delta":{"type":"text_delta","text":"hello"}})"
                      "\r");
  harness.append_line("");

  expect(harness.events.size() == 1 && harness.events[0].type == ava::provider::StreamEventType::TextDelta &&
             harness.events[0].text == "hello" && harness.data.empty() && harness.saw_data,
         "Anthropic stream support strips SSE framing and flushes complete data events");
}

}  // namespace

void run_provider_anthropic_stream_events_tests()
{
  test_anthropic_stream_support_projects_tool_lifecycle_events();
  test_anthropic_stream_support_projects_reasoning_events();
  test_anthropic_stream_support_projects_refusal_and_done();
  test_anthropic_stream_support_reports_malformed_events();
  test_anthropic_stream_support_aggregates_sse_lines();
}
