#include <string>
#include <utility>
#include <vector>

#include "ava/provider/openai_stream_events.h"
#include "ava/provider/provider.h"
#include "tests/support/test_harness.h"

namespace {

void append_line(std::vector<ava::provider::StreamEvent>& events, ava::provider::detail::OpenAIStreamEventState& state,
                 std::string line)
{
  ava::provider::detail::append_openai_events_for_sse_line(events, state, std::move(line));
}

void test_openai_stream_lifecycle_filtering_and_line_aggregation()
{
  expect(ava::provider::detail::is_ignored_openai_lifecycle_event("response.created") &&
             ava::provider::detail::is_ignored_openai_lifecycle_event("response.output_text.done") &&
             !ava::provider::detail::is_ignored_openai_lifecycle_event("response.output_text.delta"),
         "OpenAI stream events classify lifecycle-only events");

  ava::provider::detail::OpenAIStreamEventState state;
  std::vector<ava::provider::StreamEvent> events;
  append_line(events, state, "data: {\"type\":\"response.created\"}\r");
  append_line(events, state, "");
  append_line(events, state, "data: {\"type\":\"response.output_text.delta\",\"delta\":\"hi\"}\r");
  append_line(events, state, "");
  append_line(events, state, "data: [DONE]");
  append_line(events, state, "");

  expect(events.size() == 2 && events[0].type == ava::provider::StreamEventType::TextDelta &&
             events[0].text == "hi" && events[1].type == ava::provider::StreamEventType::Done,
         "OpenAI stream events aggregate CRLF data frames and ignore lifecycle-only frames");
}

void test_openai_stream_reasoning_deduplication()
{
  ava::provider::detail::OpenAIStreamEventState state;
  std::vector<ava::provider::StreamEvent> events;
  append_line(events, state, "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"rs_1\","
                             "\"type\":\"reasoning\"}}");
  append_line(events, state, "");
  append_line(events, state, "data: {\"type\":\"response.reasoning_summary_text.delta\",\"item_id\":\"rs_1\","
                             "\"delta\":\"plan\"}");
  append_line(events, state, "");
  append_line(events, state, "data: {\"type\":\"response.reasoning_summary_text.done\",\"item_id\":\"rs_1\","
                             "\"text\":\"plan\"}");
  append_line(events, state, "");
  append_line(events, state, "data: {\"type\":\"response.reasoning_summary_part.done\",\"item_id\":\"rs_1\","
                             "\"text\":\"plan\"}");
  append_line(events, state, "");
  append_line(events, state, "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"rs_1\","
                             "\"type\":\"reasoning\",\"summary\":[{\"type\":\"summary_text\",\"text\":\"plan\"}]}}");
  append_line(events, state, "");
  append_line(events, state, "data: {\"type\":\"response.output_text.delta\",\"delta\":\"answer\"}");
  append_line(events, state, "");
  append_line(events, state, "data: [DONE]");
  append_line(events, state, "");

  expect(events.size() == 5 && events[0].type == ava::provider::StreamEventType::ReasoningStart &&
             events[1].type == ava::provider::StreamEventType::ReasoningDelta && events[1].text == "plan" &&
             events[2].type == ava::provider::StreamEventType::ReasoningEnd &&
             events[3].type == ava::provider::StreamEventType::TextDelta && events[3].text == "answer" &&
             events[4].type == ava::provider::StreamEventType::Done,
         "OpenAI stream events suppress duplicate reasoning completion frames");
}

void test_openai_stream_tool_and_completion_projection()
{
  ava::provider::detail::OpenAIStreamEventState state;
  std::vector<ava::provider::StreamEvent> events;
  ava::provider::detail::append_openai_event_for_data(
      events, state,
      "{\"type\":\"response.output_item.added\",\"item\":{\"id\":\"call_1\",\"type\":\"function_call\","
      "\"name\":\"read_file\"}}");
  ava::provider::detail::append_openai_event_for_data(
      events, state,
      "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
      "\\\"README.md\\\"}\"}");
  ava::provider::detail::append_openai_event_for_data(
      events, state,
      "{\"type\":\"response.function_call.completed\",\"call_id\":\"call_1\"}");
  ava::provider::detail::append_openai_event_for_data(
      events, state,
      "{\"type\":\"response.incomplete\",\"response\":{\"status\":\"incomplete\","
      "\"incomplete_details\":{\"reason\":\"max_output_tokens\"},\"usage\":{\"input_tokens\":11,"
      "\"output_tokens\":7,\"total_tokens\":18}}}");

  expect(events.size() == 4 && events[0].type == ava::provider::StreamEventType::ToolCallStart &&
             events[0].tool_call_id == "call_1" && events[0].tool_name == "read_file" &&
             events[1].type == ava::provider::StreamEventType::ToolCallDelta &&
             events[1].text.find("README.md") != std::string::npos &&
             events[2].type == ava::provider::StreamEventType::ToolCallEnd &&
             events[3].type == ava::provider::StreamEventType::Done && events[3].stop_reason == "max_tokens" &&
             events[3].usage && events[3].usage->input_tokens == 11 && events[3].usage->output_tokens == 7 &&
             events[3].usage->total_tokens == 18,
         "OpenAI stream events project tool lifecycle and completion metadata");
}

void test_openai_stream_errors_and_finish_reset()
{
  {
    ava::provider::detail::OpenAIStreamEventState state;
    std::vector<ava::provider::StreamEvent> events;
    ava::provider::detail::append_openai_event_for_data(events, state, "{not-json}");
    ava::provider::detail::finish_openai_stream_events(events, state);
    expect(events.size() == 1 && events[0].type == ava::provider::StreamEventType::Error &&
               events[0].error_message.find("malformed") != std::string::npos && !state.error_seen &&
               !state.reasoning_open,
           "OpenAI stream events report malformed data and reset stream state");
  }
  {
    ava::provider::detail::OpenAIStreamEventState state;
    std::vector<ava::provider::StreamEvent> events;
    ava::provider::detail::append_openai_event_for_data(
        events, state, "{\"type\":\"response.output_text.delta\",\"delta\":\"partial\"}");
    ava::provider::detail::finish_openai_stream_events(events, state);
    expect(events.size() == 2 && events[0].type == ava::provider::StreamEventType::TextDelta &&
               events[1].type == ava::provider::StreamEventType::Error &&
               events[1].error_message.find("done marker") != std::string::npos && !state.saw_content,
           "OpenAI stream events report truncated streams and reset stream state");
  }
}

}  // namespace

void run_provider_openai_stream_events_tests()
{
  test_openai_stream_lifecycle_filtering_and_line_aggregation();
  test_openai_stream_reasoning_deduplication();
  test_openai_stream_tool_and_completion_projection();
  test_openai_stream_errors_and_finish_reset();
}
