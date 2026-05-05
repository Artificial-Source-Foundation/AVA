#include <string>

#include "ava/provider/openai_compatible_response.h"
#include "ava/provider/provider.h"
#include "tests/support/test_harness.h"

namespace {

void test_openai_compatible_response_helpers()
{
  expect(ava::provider::detail::normalized_openai_compatible_finish_reason("stop") == "completed" &&
             ava::provider::detail::normalized_openai_compatible_finish_reason("length") == "max_tokens" &&
             ava::provider::detail::normalized_openai_compatible_finish_reason("function_call") == "tool_calls" &&
             ava::provider::detail::normalized_openai_compatible_finish_reason("custom") == "custom",
         "OpenAI-compatible response helpers normalize known finish reasons and preserve unknown reasons");

  auto const sanitized = ava::provider::detail::sanitized_openai_compatible_snippet(
      R"({"api_key":"secret-key","reasoning_content":"private reasoning","message":"safe"})");
  expect(sanitized.find("secret-key") == std::string::npos &&
             sanitized.find("private reasoning") == std::string::npos &&
             sanitized.find("[redacted]") != std::string::npos,
         "OpenAI-compatible response helpers redact secrets and private reasoning snippets");
}

void test_openai_compatible_chat_response_parsing()
{
  auto const events = ava::provider::detail::parse_openai_compatible_chat_response(
      R"({"choices":[{"finish_reason":"tool_calls","message":{"content":"hello","reasoning_content":"thinking","tool_calls":[{"id":"call_1","function":{"name":"read_file","arguments":"{\"path\":\"README.md\"}"}}]}}],"usage":{"prompt_tokens":2,"completion_tokens":3,"total_tokens":5}})",
      "reasoning_content");

  expect(events.has_value(), "OpenAI-compatible response helper parses chat completion messages");
  expect(events->size() == 8, "OpenAI-compatible response helper emits reasoning, text, tool, and done events");
  expect((*events)[0].type == ava::provider::StreamEventType::ReasoningStart &&
             (*events)[1].type == ava::provider::StreamEventType::ReasoningDelta && (*events)[1].text == "thinking" &&
             (*events)[2].type == ava::provider::StreamEventType::ReasoningEnd,
         "OpenAI-compatible response helper emits reasoning events");
  expect((*events)[3].type == ava::provider::StreamEventType::TextDelta && (*events)[3].text == "hello",
         "OpenAI-compatible response helper emits assistant text");
  expect((*events)[4].type == ava::provider::StreamEventType::ToolCallStart && (*events)[4].tool_call_id == "call_1" &&
             (*events)[4].tool_name == "read_file" &&
             (*events)[5].type == ava::provider::StreamEventType::ToolCallDelta &&
             (*events)[6].type == ava::provider::StreamEventType::ToolCallEnd,
         "OpenAI-compatible response helper emits complete tool lifecycle events");
  expect((*events)[7].type == ava::provider::StreamEventType::Done && (*events)[7].stop_reason == "tool_calls" &&
             (*events)[7].usage && (*events)[7].usage->input_tokens == 2 && (*events)[7].usage->output_tokens == 3 &&
             (*events)[7].usage->total_tokens == 5,
         "OpenAI-compatible response helper emits stop reason and usage metadata");
}

void test_openai_compatible_chat_response_content_filter_without_message()
{
  auto const events = ava::provider::detail::parse_openai_compatible_chat_response(
      R"({"choices":[{"finish_reason":"content_filter"}]})", "reasoning_content");

  expect(events.has_value() && events->size() == 1 && (*events)[0].type == ava::provider::StreamEventType::Done &&
             (*events)[0].stop_reason == "content_filter",
         "OpenAI-compatible response helper accepts terminal content filter responses without message content");
}

}  // namespace

void run_provider_openai_compatible_response_tests()
{
  test_openai_compatible_response_helpers();
  test_openai_compatible_chat_response_parsing();
  test_openai_compatible_chat_response_content_filter_without_message();
}
