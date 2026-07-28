#include "sys.h"
#include "tests/provider_openai_test_suite.h"
#include "tests/support/test_harness.h"
#include "ava/http/transport.h"
#include "ava/provider/openai_response_parser.h"
#include "ava/provider/provider.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ava::tests::provider_openai_suite {

void exercise_contract_terminal_parsing(ava::provider::OpenAIProvider const& provider)
{
  auto completed = ava::provider::parse_openai_sse("data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n");
  expect(completed && completed->size() == 1 && (*completed)[0].type == ava::provider::StreamEventType::Done &&
             (*completed)[0].finish_reason == ava::provider::ProviderFinishReason::Completed,
         "OpenAI response.completed event produces done event");
  auto completed_then_done = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n"
      "data: [DONE]\n\n");
  expect(completed_then_done && completed_then_done->size() == 1 && (*completed_then_done)[0].type == ava::provider::StreamEventType::Done &&
             (*completed_then_done)[0].finish_reason == ava::provider::ProviderFinishReason::Completed,
         "OpenAI SSE parser suppresses duplicate done marker after response.completed");
  auto completed_with_usage = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.completed\",\"response\":{\"usage\":{\"input_tokens\":11,"
      "\"output_tokens\":7,\"total_tokens\":18,\"input_tokens_details\":{\"cached_tokens\":3},"
      "\"output_tokens_details\":{\"reasoning_tokens\":2}}}}\n\n");
  expect(completed_with_usage && completed_with_usage->size() == 1 && (*completed_with_usage)[0].usage &&
             (*completed_with_usage)[0].usage->input_tokens == 11 && (*completed_with_usage)[0].usage->output_tokens == 7 &&
             (*completed_with_usage)[0].usage->total_tokens == 18 && (*completed_with_usage)[0].usage->cache_read_tokens == 3 &&
             (*completed_with_usage)[0].usage->reasoning_tokens == 2,
         "OpenAI response.completed event preserves Responses API usage details");
  auto incomplete = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.incomplete\",\"response\":{\"status\":\"incomplete\","
      "\"incomplete_details\":{\"reason\":\"max_output_tokens\"}}}\n\n");
  expect(incomplete && incomplete->size() == 1 && (*incomplete)[0].type == ava::provider::StreamEventType::Done &&
             (*incomplete)[0].finish_reason == ava::provider::ProviderFinishReason::MaxTokens,
         "OpenAI response.incomplete preserves normalized incomplete reason");
  auto lifecycle = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.created\"}\n\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"hi\"}\n\n"
      "data: {\"type\":\"response.output_text.done\"}\n\n"
      "data: {\"type\":\"response.function_call_arguments.done\",\"call_id\":\"call_1\"}\n\n"
      "data: [DONE]\n\n");
  expect(lifecycle && lifecycle->size() == 2 && (*lifecycle)[0].type == ava::provider::StreamEventType::TextDelta && (*lifecycle)[0].text == "hi" &&
             (*lifecycle)[1].type == ava::provider::StreamEventType::Done,
         "OpenAI SSE parser ignores non-content lifecycle events");
  auto completed_tool = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.function_call.completed\",\"call_id\":\"call_fallback\"}\n\n"
      "data: {\"type\":\"response.completed\"}\n\n");
  expect(completed_tool && completed_tool->size() == 2 && (*completed_tool)[0].type == ava::provider::StreamEventType::ToolCallEnd &&
             (*completed_tool)[0].tool_call_id == "call_fallback",
         "OpenAI function_call.completed uses call_id fallback");
  auto text_fallback = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.text.delta\",\"text\":\"fallback\"}\n\n"
      "data: [DONE]\n\n");
  expect(text_fallback && text_fallback->size() == 2 && (*text_fallback)[0].type == ava::provider::StreamEventType::TextDelta &&
             (*text_fallback)[0].text == "fallback",
         "OpenAI response.text.delta uses text fallback");
  auto truncated = ava::provider::parse_openai_sse("data: {\"type\":\"response.output_text.delta\",\"delta\":\"partial\"}\n\n");
  expect(truncated && truncated->size() == 2 && (*truncated)[0].type == ava::provider::StreamEventType::TextDelta &&
             (*truncated)[1].type == ava::provider::StreamEventType::Error && (*truncated)[1].error_message.find("done marker") != std::string::npos,
         "OpenAI SSE parser reports truncated streams after content");

  auto unknown = ava::provider::parse_openai_sse("data: {\"type\":\"response.unexpected\"}\n\n");
  expect(unknown && unknown->empty(), "OpenAI unknown SSE event is ignored as forward-compatible lifecycle data");
  auto malformed = ava::provider::parse_openai_sse("data: {not-json}\n\n");
  expect(malformed && malformed->size() == 1 && (*malformed)[0].type == ava::provider::StreamEventType::Error,
         "OpenAI malformed SSE data produces error event");
  auto api_error = ava::provider::parse_openai_sse("data: {\"type\":\"response.error\",\"error\":{\"message\":\"bad request\"}}\n\n");
  expect(api_error && api_error->size() == 1 && (*api_error)[0].type == ava::provider::StreamEventType::Error &&
             (*api_error)[0].error_message == "OpenAI provider reported a streaming error",
         "OpenAI SSE error event uses fixed local diagnostics");
  auto documented_top_level_error =
      ava::provider::parse_openai_sse("data: {\"type\":\"error\",\"message\":\"top-level provider failure\",\"untrusted_payload\":\"do-not-log\"}\n\n");
  expect(documented_top_level_error && documented_top_level_error->size() == 1 &&
             (*documented_top_level_error)[0].type == ava::provider::StreamEventType::Error &&
             (*documented_top_level_error)[0].error_message == "OpenAI provider reported a streaming error" &&
             (*documented_top_level_error)[0].error_message.find("top-level provider failure") == std::string::npos &&
             (*documented_top_level_error)[0].error_message.find("do-not-log") == std::string::npos,
         "OpenAI top-level SSE error diagnostics omit provider messages");
  auto documented_failed_error = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.failed\",\"response\":{\"error\":{\"message\":\"documented failure\",\"code\":\"bad_request\"},"
      "\"untrusted_payload\":\"do-not-log\"}}\n\n");
  expect(documented_failed_error && documented_failed_error->size() == 1 && (*documented_failed_error)[0].type == ava::provider::StreamEventType::Error &&
             (*documented_failed_error)[0].error_message == "OpenAI provider reported a streaming error" &&
             (*documented_failed_error)[0].error_message.find("documented failure") == std::string::npos &&
             (*documented_failed_error)[0].error_message.find("do-not-log") == std::string::npos,
         "OpenAI response.failed SSE errors omit response.error.message");
  auto private_sse_error = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.error\",\"error\":{\"message\":\"event: error\\ndata: OPENAI_SSE_FRAGMENT_CANARY\","
      "\"native\":{\"reasoning\":\"OPENAI_SSE_REASONING_CANARY\"}},\"unknown\":\"OPENAI_SSE_OUTER_CANARY\"}\n\n");
  auto bearer_sse_error = ava::provider::parse_openai_sse("data: {\"type\":\"error\",\"message\":\"authorization Bearer OPENAI_SSE_BEARER_CANARY\"}\n\n");
  expect(private_sse_error && private_sse_error->size() == 1 && (*private_sse_error)[0].error_message == "OpenAI provider reported a streaming error" &&
             (*private_sse_error)[0].error_message.find("OPENAI_SSE_FRAGMENT_CANARY") == std::string::npos &&
             (*private_sse_error)[0].error_message.find("OPENAI_SSE_REASONING_CANARY") == std::string::npos &&
             (*private_sse_error)[0].error_message.find("OPENAI_SSE_OUTER_CANARY") == std::string::npos && bearer_sse_error && bearer_sse_error->size() == 1 &&
             (*bearer_sse_error)[0].error_message == "OpenAI provider reported a streaming error" &&
             (*bearer_sse_error)[0].error_message.find("OPENAI_SSE_BEARER_CANARY") == std::string::npos,
         "OpenAI SSE diagnostics use fixed local text and omit provider payloads");
  auto unknown_between_deltas = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"a\"}\n\n"
      "data: {\"type\":\"response.new_lifecycle_event\"}\n\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"b\"}\n\n"
      "data: [DONE]\n\n");
  expect(unknown_between_deltas && unknown_between_deltas->size() == 3 && (*unknown_between_deltas)[0].type == ava::provider::StreamEventType::TextDelta &&
             (*unknown_between_deltas)[1].type == ava::provider::StreamEventType::TextDelta &&
             (*unknown_between_deltas)[2].type == ava::provider::StreamEventType::Done,
         "OpenAI unknown SSE events do not suppress final assistant content");
  auto text = ava::provider::parse_openai_response_text("{\"output_text\":\"done\"}");
  expect(text && *text == "done", "OpenAI non-stream response text parses");
  auto non_stream_usage = ava::provider::parse_openai_usage(
      "{\"output_text\":\"done\",\"usage\":{\"prompt_tokens\":5,\"completion_tokens\":6,"
      "\"total_tokens\":11,\"completion_tokens_details\":{\"reasoning_tokens\":4}}}");
  expect(non_stream_usage && non_stream_usage->input_tokens == 5 && non_stream_usage->output_tokens == 6 && non_stream_usage->total_tokens == 11 &&
             non_stream_usage->reasoning_tokens == 4,
         "OpenAI non-stream usage parser accepts prompt/completion aliases");
  auto missing_text = ava::provider::parse_openai_response_text("{\"id\":\"resp_1\"}");
  expect(!missing_text, "OpenAI non-stream response requires expected text field");

  auto non_stream_tool =
      provider.parse_response(ava::http::HttpResponse{.status_code = 200,
                                                      .headers = {},
                                                      .body = "{\"output_text\":\"Let me read that file.\","
                                                              "\"output\":[{\"type\":\"function_call\",\"id\":\"fc_1\",\"call_id\":\"call_1\","
                                                              "\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\\\"README.md\\\"}\"}],"
                                                              "\"usage\":{\"input_tokens\":2,\"output_tokens\":3}}"},
                              false);
  expect(non_stream_tool && non_stream_tool->size() == 5 && (*non_stream_tool)[0].type == ava::provider::StreamEventType::TextDelta &&
             (*non_stream_tool)[0].text == "Let me read that file." && (*non_stream_tool)[1].type == ava::provider::StreamEventType::ToolCallStart &&
             (*non_stream_tool)[1].tool_call_id == "call_1" && (*non_stream_tool)[1].tool_name == "read_file" &&
             (*non_stream_tool)[2].type == ava::provider::StreamEventType::ToolCallDelta &&
             (*non_stream_tool)[3].type == ava::provider::StreamEventType::ToolCallEnd && (*non_stream_tool)[4].type == ava::provider::StreamEventType::Done &&
             (*non_stream_tool)[4].usage,
         "OpenAI non-stream Responses API parses mixed text and tool calls");
  auto non_stream_missing_call_id =
      provider.parse_response(ava::http::HttpResponse{.status_code = 200,
                                                      .headers = {},
                                                      .body = "{\"output\":[{\"id\":\"fc_nonstream_missing\",\"type\":\"function_call\",\"name\":\"read_file\","
                                                              "\"arguments\":\"{}\"}]}"},
                              false);
  expect(non_stream_missing_call_id &&
             std::none_of(non_stream_missing_call_id->begin(), non_stream_missing_call_id->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) &&
             std::any_of(non_stream_missing_call_id->begin(), non_stream_missing_call_id->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI non-stream function-call items reject a missing logical call_id instead of item.id fallback");
  auto non_stream_missing_arguments =
      provider.parse_response(ava::http::HttpResponse{.status_code = 200,
                                                      .headers = {},
                                                      .body = "{\"output\":[{\"id\":\"fc_nonstream_missing_arguments\",\"type\":\"function_call\","
                                                              "\"call_id\":\"opaque-nonstream-missing-arguments\",\"name\":\"read_file\"}]}"},
                              false);
  auto non_stream_wrong_type_arguments =
      provider.parse_response(ava::http::HttpResponse{.status_code = 200,
                                                      .headers = {},
                                                      .body = "{\"output\":[{\"id\":\"fc_nonstream_wrong_arguments\",\"type\":\"function_call\","
                                                              "\"call_id\":\"opaque-nonstream-wrong-arguments\",\"name\":\"read_file\",\"arguments\":[]}]}"},
                              false);
  expect(non_stream_missing_arguments && non_stream_wrong_type_arguments &&
             std::none_of(non_stream_missing_arguments->begin(), non_stream_missing_arguments->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) &&
             std::none_of(non_stream_wrong_type_arguments->begin(), non_stream_wrong_type_arguments->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) &&
             std::any_of(non_stream_missing_arguments->begin(), non_stream_missing_arguments->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }) &&
             std::any_of(non_stream_wrong_type_arguments->begin(), non_stream_wrong_type_arguments->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI non-stream function-call items require a present string arguments field before dispatch");
  auto non_stream_missing_item_id =
      provider.parse_response(ava::http::HttpResponse{.status_code = 200,
                                                      .headers = {},
                                                      .body = "{\"output\":[{\"type\":\"function_call\",\"call_id\":\"opaque-nonstream-missing-item\","
                                                              "\"name\":\"read_file\",\"arguments\":\"{}\"}]}"},
                              false);
  auto non_stream_malformed_arguments =
      provider.parse_response(ava::http::HttpResponse{.status_code = 200,
                                                      .headers = {},
                                                      .body = "{\"output\":[{\"id\":\"fc_nonstream_bad_arguments\",\"type\":\"function_call\","
                                                              "\"call_id\":\"opaque-nonstream-bad-arguments\",\"name\":\"read_file\",\"arguments\":\"[\"}]}"},
                              false);
  auto non_stream_duplicate_item = provider.parse_response(
      ava::http::HttpResponse{.status_code = 200,
                              .headers = {},
                              .body = "{\"output\":[{\"id\":\"fc_duplicate\",\"type\":\"function_call\",\"call_id\":\"call_one\","
                                      "\"name\":\"read_file\",\"arguments\":\"{}\"},{\"id\":\"fc_duplicate\",\"type\":\"function_call\","
                                      "\"call_id\":\"call_two\",\"name\":\"read_file\",\"arguments\":\"{}\"}]}"},
      false);
  auto non_stream_duplicate_call =
      provider.parse_response(ava::http::HttpResponse{.status_code = 200,
                                                      .headers = {},
                                                      .body = "{\"output\":[{\"id\":\"fc_call_one\",\"type\":\"function_call\",\"call_id\":\"call_duplicate\","
                                                              "\"name\":\"read_file\",\"arguments\":\"{}\"},{\"id\":\"fc_call_two\",\"type\":\"function_call\","
                                                              "\"call_id\":\"call_duplicate\",\"name\":\"read_file\",\"arguments\":\"{}\"}]}"},
                              false);
  expect(non_stream_missing_item_id && non_stream_malformed_arguments && non_stream_duplicate_item && non_stream_duplicate_call &&
             std::any_of(non_stream_missing_item_id->begin(), non_stream_missing_item_id->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }) &&
             std::any_of(non_stream_malformed_arguments->begin(), non_stream_malformed_arguments->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }) &&
             std::any_of(non_stream_duplicate_item->begin(), non_stream_duplicate_item->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }) &&
             std::any_of(non_stream_duplicate_call->begin(), non_stream_duplicate_call->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI non-stream function calls require JSON-object arguments and unique item/call identities");
  auto non_stream_reasoning = provider.parse_response(
      ava::http::HttpResponse{
          .status_code = 200,
          .headers = {},
          .body = "{\"output\":[{\"id\":\"rs_non_stream\",\"type\":\"reasoning\",\"summary\":[{\"type\":"
                  "\"summary_text\",\"text\":\"think\"}],\"status\":\"completed\",\"encrypted_content\":\"cipher-non-stream\"},{\"type\":\"message\","
                  "\"content\":[{\"type\":\"output_text\",\"text\":\"done\"}]}],"
                  "\"usage\":{\"input_tokens\":2,\"output_tokens\":3}}"},
      false);
  expect(non_stream_reasoning && non_stream_reasoning->size() == 5 && (*non_stream_reasoning)[0].type == ava::provider::StreamEventType::ReasoningStart &&
             (*non_stream_reasoning)[1].type == ava::provider::StreamEventType::ReasoningDelta && (*non_stream_reasoning)[1].text == "think" &&
             (*non_stream_reasoning)[2].type == ava::provider::StreamEventType::ReasoningEnd &&
             (*non_stream_reasoning)[2].reasoning_native_item_json.find("cipher-non-stream") != std::string::npos &&
             (*non_stream_reasoning)[3].type == ava::provider::StreamEventType::TextDelta && (*non_stream_reasoning)[3].text == "done" &&
             (*non_stream_reasoning)[4].type == ava::provider::StreamEventType::Done,
         "OpenAI non-stream Responses API parses reasoning summary before answer text");
  auto non_stream_malformed_reasoning =
      provider.parse_response(ava::http::HttpResponse{.status_code = 200,
                                                      .headers = {},
                                                      .body = "{\"output\":[{\"id\":\"rs_missing_summary\",\"type\":\"reasoning\",\"text\":\"safe summary\"},"
                                                              "{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"done\"}]}]}"},
                              false);
  auto const malformed_reasoning_end = non_stream_malformed_reasoning
                                           ? std::find_if(non_stream_malformed_reasoning->begin(), non_stream_malformed_reasoning->end(),
                                                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ReasoningEnd; })
                                           : std::vector<ava::provider::StreamEvent>::const_iterator{};
  expect(non_stream_malformed_reasoning && malformed_reasoning_end != non_stream_malformed_reasoning->end() &&
             malformed_reasoning_end->reasoning_native_item_json.empty(),
         "OpenAI non-stream parser keeps readable reasoning while dropping malformed native replay metadata");
  auto nested_text = ava::provider::parse_openai_response_text(
      "{\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"nested one\"},"
      "{\"type\":\"output_text\",\"text\":\"nested two\"}]}]}");
  expect(nested_text && *nested_text == "nested one\n\nnested two", "OpenAI non-stream response retains all nested output_text parts in message order");
  auto non_stream_incomplete = provider.parse_response(ava::http::HttpResponse{.status_code = 200,
                                                                               .headers = {},
                                                                               .body = "{\"output_text\":\"partial\",\"status\":\"incomplete\","
                                                                                       "\"incomplete_details\":{\"reason\":\"content_filter\"}}"},
                                                       false);
  expect(non_stream_incomplete && non_stream_incomplete->size() == 2 && (*non_stream_incomplete)[1].type == ava::provider::StreamEventType::Done &&
             (*non_stream_incomplete)[1].finish_reason == ava::provider::ProviderFinishReason::Refusal,
         "OpenAI non-stream Responses API preserves incomplete stop reason");
  auto non_stream_empty_incomplete = provider.parse_response(ava::http::HttpResponse{.status_code = 200,
                                                                                     .headers = {},
                                                                                     .body = "{\"status\":\"incomplete\","
                                                                                             "\"incomplete_details\":{\"reason\":\"max_output_tokens\"}}"},
                                                             false);
  expect(non_stream_empty_incomplete && non_stream_empty_incomplete->size() == 1 &&
             (*non_stream_empty_incomplete)[0].type == ava::provider::StreamEventType::Done &&
             (*non_stream_empty_incomplete)[0].finish_reason == ava::provider::ProviderFinishReason::MaxTokens,
         "OpenAI non-stream Responses API accepts empty incomplete terminal response");
}

}  // namespace ava::tests::provider_openai_suite
