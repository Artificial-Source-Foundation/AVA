#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ava/provider/openai_compatible_response_support.h"
#include "ava/provider/provider.h"
#include "tests/support/test_harness.h"

namespace {

void test_reasoning_closure_helper()
{
  bool reasoning_open = true;
  auto events = ava::provider::detail::finish_reasoning_if_open(reasoning_open, "reasoning_content");
  expect(events.size() == 1 && events[0].type == ava::provider::StreamEventType::ReasoningEnd &&
             events[0].reasoning_format == "reasoning_content" && !reasoning_open,
         "OpenAI-compatible response support closes open reasoning blocks");

  auto none = ava::provider::detail::finish_reasoning_if_open(reasoning_open, "reasoning_content");
  expect(none.empty(), "OpenAI-compatible response support leaves closed reasoning blocks untouched");
}

void test_tool_call_delta_lifecycle_helpers()
{
  std::vector<ava::provider::StreamEvent> events;
  std::map<int, std::string> open_tool_call_ids;

  ava::provider::detail::append_openai_compatible_tool_call_delta_events(
      events, open_tool_call_ids,
      R"({"tool_calls":[{"index":2,"id":"call_a","function":{"name":"read_file","arguments":"{\"path\":\"a.txt\"}"}}]})");
  expect(events.size() == 2 && events[0].type == ava::provider::StreamEventType::ToolCallStart &&
             events[0].tool_call_id == "call_a" && events[0].tool_name == "read_file" &&
             events[1].type == ava::provider::StreamEventType::ToolCallDelta &&
             events[1].text == R"({"path":"a.txt"})" && open_tool_call_ids.at(2) == "call_a",
         "OpenAI-compatible response support emits tool call start and delta events");

  ava::provider::detail::append_openai_compatible_tool_call_delta_events(
      events, open_tool_call_ids, R"({"tool_calls":[{"index":2,"function":{"arguments":"tail"}}]})");
  expect(events.size() == 3 && events[2].type == ava::provider::StreamEventType::ToolCallDelta &&
             events[2].tool_call_id == "call_a" && events[2].text == "tail",
         "OpenAI-compatible response support reuses existing tool call ids for later deltas");

  ava::provider::detail::append_openai_compatible_tool_call_end_events(events, open_tool_call_ids);
  expect(events.size() == 4 && events[3].type == ava::provider::StreamEventType::ToolCallEnd &&
             events[3].tool_call_id == "call_a" && open_tool_call_ids.empty(),
         "OpenAI-compatible response support emits end events and clears open tool calls");
}

void test_data_event_done_cleanup()
{
  std::vector<ava::provider::StreamEvent> events;
  std::map<int, std::string> open_tool_call_ids{{0, "call_a"}};
  bool reasoning_open = true;
  ava::provider::TokenUsage done_usage;
  done_usage.input_tokens = 2;
  std::optional<ava::provider::TokenUsage> usage = done_usage;
  std::string stop_reason = "tool_calls";
  bool saw_data = false;
  bool done_seen = false;
  bool error_seen = false;

  ava::provider::detail::append_openai_compatible_event_for_data(events, open_tool_call_ids, reasoning_open, usage,
                                                                 stop_reason, saw_data, done_seen, error_seen, "[DONE]",
                                                                 "reasoning_content");
  expect(events.size() == 3 && events[0].type == ava::provider::StreamEventType::ReasoningEnd &&
             events[1].type == ava::provider::StreamEventType::ToolCallEnd &&
             events[2].type == ava::provider::StreamEventType::Done && events[2].stop_reason == "tool_calls" &&
             events[2].usage && events[2].usage->input_tokens == 2 && open_tool_call_ids.empty() && !reasoning_open &&
             done_seen && !usage && stop_reason.empty(),
         "OpenAI-compatible response support closes reasoning/tools and emits done for done marker");
}

void test_data_event_malformed_and_error_redaction()
{
  std::vector<ava::provider::StreamEvent> events;
  std::map<int, std::string> open_tool_call_ids;
  bool reasoning_open = false;
  std::optional<ava::provider::TokenUsage> usage = std::nullopt;
  std::string stop_reason;
  bool saw_data = false;
  bool done_seen = false;
  bool error_seen = false;

  ava::provider::detail::append_openai_compatible_event_for_data(events, open_tool_call_ids, reasoning_open, usage,
                                                                 stop_reason, saw_data, done_seen, error_seen,
                                                                 "not-json", "reasoning_content");
  expect(events.size() == 1 && events[0].type == ava::provider::StreamEventType::Error && saw_data && error_seen,
         "OpenAI-compatible response support emits semantic errors for malformed SSE data");

  events.clear();
  error_seen = false;
  done_seen = false;
  ava::provider::detail::append_openai_compatible_event_for_data(
      events, open_tool_call_ids, reasoning_open, usage, stop_reason, saw_data, done_seen, error_seen,
      R"({"error":{"message":"Bearer secret-token"}})", "reasoning_content");
  expect(events.size() == 1 && events[0].type == ava::provider::StreamEventType::Error &&
             events[0].error_message.find("secret-token") == std::string::npos &&
             events[0].error_message.find("Bearer [redacted]") != std::string::npos && done_seen && error_seen,
         "OpenAI-compatible response support redacts sensitive provider error messages");
}

void test_sse_line_accumulation()
{
  std::vector<ava::provider::StreamEvent> events;
  std::map<int, std::string> open_tool_call_ids;
  bool reasoning_open = false;
  std::optional<ava::provider::TokenUsage> usage = std::nullopt;
  std::string stop_reason;
  bool saw_data = false;
  bool done_seen = false;
  bool error_seen = false;
  std::string data;

  ava::provider::detail::append_openai_compatible_events_for_sse_line(
      events, open_tool_call_ids, reasoning_open, usage, stop_reason, saw_data, done_seen, error_seen, data,
      R"(data: {"choices":[{"delta":{"content":"hello"}}]})", "reasoning_content");
  expect(events.empty() && !data.empty(), "OpenAI-compatible response support buffers non-empty SSE data lines");

  ava::provider::detail::append_openai_compatible_events_for_sse_line(events, open_tool_call_ids, reasoning_open, usage,
                                                                      stop_reason, saw_data, done_seen, error_seen,
                                                                      data, "", "reasoning_content");
  expect(events.size() == 1 && events[0].type == ava::provider::StreamEventType::TextDelta &&
             events[0].text == "hello" && data.empty() && saw_data,
         "OpenAI-compatible response support flushes buffered SSE data on blank lines");
}

}  // namespace

void run_provider_openai_compatible_response_support_tests()
{
  test_reasoning_closure_helper();
  test_tool_call_delta_lifecycle_helpers();
  test_data_event_done_cleanup();
  test_data_event_malformed_and_error_redaction();
  test_sse_line_accumulation();
}
