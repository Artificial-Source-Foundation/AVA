#include "sys.h"
#include "tests/provider_openai_test_suite.h"
#include "tests/support/test_harness.h"
#include "ava/http/transport.h"
#include "ava/agent/assistant_turn.h"
#include "ava/provider/finish_reason.h"
#include "ava/provider/openai_response_parser.h"
#include "ava/provider/provider.h"
#include "ava/provider/registry.h"
#include "ava/core/error.h"
#include "ava/core/result.h"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tests::provider_openai_suite {

void test_openai_responses_refusal_and_unsupported_output()
{
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  auto const non_stream = provider.parse_response(
      ava::http::HttpResponse{
          .status_code = 200,
          .headers = {},
          .body =
              R"({"status":"completed","output":[{"id":"msg_refusal_nonstream","type":"message","output_index":0,"phase":"final_answer","content":[{"type":"output_text","text":"Visible preface."},{"type":"refusal","refusal":"I cannot help with that."}]}]})"},
      false);
  auto const parsed_text = ava::provider::parse_openai_response_text(
      R"({"output":[{"type":"message","content":[{"type":"output_text","text":"Visible preface."},{"type":"refusal","refusal":"I cannot help with that."}]}]})");
  auto const non_stream_turn = non_stream ? ava::agent::parse_assistant_turn(*non_stream, {}) : ava::core::Result<ava::agent::ParsedAssistantTurn>{};
  expect(non_stream && non_stream_turn && non_stream_turn->finish_reason == ava::provider::ProviderFinishReason::Refusal &&
             non_stream_turn->text == "Visible preface.I cannot help with that." && parsed_text &&
             *parsed_text == "Visible preface.\n\nI cannot help with that.",
         "OpenAI non-stream Responses preserves visible refusal content and marks the terminal outcome as refusal");

  auto const stream = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.added\",\"output_index\":0,\"item\":{\"id\":\"msg_refusal_stream\",\"type\":\"message\",\"phase\":\"final_answer\"}}"
      "\n\n"
      "data: {\"type\":\"response.refusal.delta\",\"item_id\":\"msg_refusal_stream\",\"output_index\":0,\"content_index\":0,\"delta\":\"I cannot \"}\n\n"
      "data: {\"type\":\"response.refusal.done\",\"item_id\":\"msg_refusal_stream\",\"output_index\":0,\"content_index\":0,\"refusal\":\"I cannot help.\"}\n\n"
      "data: "
      "{\"type\":\"response.output_item.done\",\"output_index\":0,\"item\":{\"id\":\"msg_refusal_stream\",\"type\":\"message\",\"phase\":\"final_answer\","
      "\"content\":[{\"type\":\"refusal\",\"refusal\":\"I cannot help.\"}]}}\n\n"
      "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n");
  auto const stream_turn = stream ? ava::agent::parse_assistant_turn(*stream, {}) : ava::core::Result<ava::agent::ParsedAssistantTurn>{};
  expect(stream && stream_turn && stream_turn->finish_reason == ava::provider::ProviderFinishReason::Refusal && stream_turn->text == "I cannot help.",
         "OpenAI streaming Responses reconciles refusal deltas/done text and marks the terminal outcome as refusal");

  auto has_content_free_error = [](auto const& events, std::string_view discriminator, std::string_view canary) {
    if (!events)
      return false;
    auto const error =
        std::ranges::find_if(*events, [](ava::provider::StreamEvent const& event) { return event.type == ava::provider::StreamEventType::Error; });
    return error != events->end() && error->error_message.find("unsupported") != std::string::npos &&
           error->error_message.find(discriminator) == std::string::npos && error->error_message.find(canary) == std::string::npos;
  };
  auto const non_stream_unsupported_item = provider.parse_response(
      ava::http::HttpResponse{
          .status_code = 200,
          .headers = {},
          .body = R"({"status":"completed","output":[{"id":"unknown","type":"image_generation_call","output_index":0,"private_canary":"NO_LEAK"}]})"},
      false);
  auto const non_stream_unsupported_content = provider.parse_response(
      ava::http::HttpResponse{
          .status_code = 200,
          .headers = {},
          .body =
              R"({"status":"completed","output":[{"id":"message_unknown_content","type":"message","output_index":0,"content":[{"type":"input_image","private_canary":"NO_LEAK"}]}]})"},
      false);
  auto const stream_unsupported_added = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.added\",\"output_index\":5,\"item\":{\"id\":\"unknown_stream\",\"type\":\"image_generation_call\",\"private_canary\":"
      "\"NO_LEAK\"}}\n\n");
  auto const stream_unsupported_done = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.done\",\"output_index\":6,\"item\":{\"id\":\"unknown_stream_done\",\"type\":\"image_generation_call\",\"private_"
      "canary\":\"NO_LEAK\"}}\n\n");
  auto const stream_unsupported_content = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.added\",\"output_index\":7,\"item\":{\"id\":\"message_unknown_content_stream\",\"type\":\"message\",\"content\":[{"
      "\"type\":\"input_image\",\"private_canary\":\"NO_LEAK\"}]}}\n\n");
  expect(has_content_free_error(non_stream_unsupported_item, "image_generation_call", "NO_LEAK") &&
             has_content_free_error(non_stream_unsupported_content, "input_image", "NO_LEAK") &&
             has_content_free_error(stream_unsupported_added, "image_generation_call", "NO_LEAK") &&
             has_content_free_error(stream_unsupported_done, "image_generation_call", "NO_LEAK") &&
             has_content_free_error(stream_unsupported_content, "input_image", "NO_LEAK"),
         "OpenAI Responses rejects unsupported discriminators without exposing their values or payloads");
}

void test_closed_provider_finish_reason_catalog()
{
  bool exhaustive = true;
  for (auto const& mapping : ava::provider::kProviderFinishReasonCatalog)
  {
    exhaustive = exhaustive && ava::provider::normalize_provider_finish_reason(mapping.protocol, mapping.raw_reason) == mapping.reason;
  }
  exhaustive =
      exhaustive &&
      ava::provider::normalize_provider_finish_reason(ava::provider::ProviderProtocol::OpenAIChat, "provider_custom") ==
          ava::provider::ProviderFinishReason::Error &&
      ava::provider::normalize_provider_finish_reason(ava::provider::ProviderProtocol::Gemini, "OTHER") == ava::provider::ProviderFinishReason::Error &&
      ava::provider::normalize_provider_finish_reason(ava::provider::ProviderProtocol::Gemini, "SAFETY") == ava::provider::ProviderFinishReason::Refusal &&
      ava::provider::normalize_provider_finish_reason(ava::provider::ProviderProtocol::OpenAIResponses, "incomplete") ==
          ava::provider::ProviderFinishReason::Error;
  expect(exhaustive, "provider finish reasons use one closed catalog and unknown/failed terminals cannot become completed");
}

void test_openai_provider_parser_budgets()
{
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  std::string output = "{\"status\":\"completed\",\"output\":[";
  for (std::size_t index = 0; index <= ava::provider::kMaxProviderParserArrayItems; ++index)
  {
    if (index > 0)
      output += ',';
    output += "{\"id\":\"msg_" + std::to_string(index) + "\",\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"x\"}]}";
  }
  output += "]}";
  auto non_stream = provider.parse_response(ava::http::HttpResponse{.status_code = 200, .headers = {}, .body = output}, false);
  expect(non_stream && non_stream->size() == 1 && (*non_stream)[0].type == ava::provider::StreamEventType::Error &&
             (*non_stream)[0].error_message == "OpenAI response parser limit exceeded" && (*non_stream)[0].error_message.find("msg_") == std::string::npos,
         "OpenAI non-stream parser rejects oversized output arrays with one fixed terminal error before materializing provider events");

  std::string oversized_content_event = "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"msg_nested\",\"type\":\"message\",\"content\":[";
  std::string oversized_summary_event = "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"rs_nested\",\"type\":\"reasoning\",\"summary\":[";
  for (std::size_t index = 0; index <= ava::provider::kMaxProviderParserArrayItems; ++index)
  {
    if (index > 0)
    {
      oversized_content_event += ',';
      oversized_summary_event += ',';
    }
    oversized_content_event += "{\"type\":\"output_text\",\"text\":\"x\"}";
    oversized_summary_event += "{\"text\":\"x\"}";
  }
  oversized_content_event += "]}}\n\n";
  oversized_summary_event += "]}}\n\n";
  auto oversized_content = ava::provider::parse_openai_sse(oversized_content_event);
  auto oversized_summary = ava::provider::parse_openai_sse(oversized_summary_event);
  auto is_one_limit_error = [](auto const& events) {
    return events && events->size() == 1 && (*events)[0].type == ava::provider::StreamEventType::Error &&
           (*events)[0].error_message == "OpenAI response parser limit exceeded";
  };
  expect(is_one_limit_error(oversized_content) && is_one_limit_error(oversized_summary),
         "OpenAI SSE parser rejects oversized nested content and reasoning-summary arrays before event expansion");

  ava::provider::OpenAIStreamParser parser;
  std::string single_append;
  for (std::size_t index = 0; index <= ava::provider::kMaxProviderParserEvents; ++index)
    single_append += "data: {\"type\":\"response.output_text.delta\",\"delta\":\"x\"}\n\n";
  auto streamed = parser.append(single_append);
  auto after_limit = parser.append("data: {\"type\":\"response.output_text.delta\",\"delta\":\"STREAM_AFTER_LIMIT_CANARY\"}\n\n");
  auto finished = parser.finish();
  expect(streamed && streamed->size() == ava::provider::kMaxProviderParserEvents && !streamed->empty() &&
             streamed->back().type == ava::provider::StreamEventType::Error && streamed->back().error_message == "OpenAI response parser limit exceeded" &&
             streamed->back().error_message.find("STREAM_AFTER_LIMIT_CANARY") == std::string::npos && after_limit && after_limit->empty() && finished &&
             finished->empty(),
         "one large OpenAI SSE append is bounded to 4096 events, emits one fixed error, and terminally ignores later provider records");

  ava::provider::OpenAIStreamParser fragmented_parser;
  std::string budget_prefix;
  for (std::size_t index = 1; index < ava::provider::kMaxProviderParserEvents; ++index)
    budget_prefix += "data: {\"type\":\"response.output_text.delta\",\"delta\":\"x\"}\n\n";
  auto fragmented_prefix = fragmented_parser.append(budget_prefix);
  auto fragmented_terminal = fragmented_parser.append("data: {\"type\":\"response.output_text.delta\",\"delta\":\"FRAGMENT_AFTER_LIMIT_CANARY\"}\n\n");
  expect(fragmented_prefix && fragmented_prefix->size() == ava::provider::kMaxProviderParserEvents - 1 && fragmented_terminal &&
             fragmented_terminal->size() == 1 && (*fragmented_terminal)[0].type == ava::provider::StreamEventType::Error &&
             (*fragmented_terminal)[0].error_message == "OpenAI response parser limit exceeded" &&
             (*fragmented_terminal)[0].error_message.find("FRAGMENT_AFTER_LIMIT_CANARY") == std::string::npos,
         "OpenAI SSE parser applies its event budget across fragmented provider chunks");

  ava::provider::OpenAIStreamParser oversized_record_parser;
  auto oversized_record = oversized_record_parser.append("data: STREAM_RECORD_CANARY" + std::string(ava::provider::kMaxProviderSseBufferedBytes, 'x'));
  expect(oversized_record && oversized_record->size() == 1 && (*oversized_record)[0].type == ava::provider::StreamEventType::Error &&
             (*oversized_record)[0].error_message == "OpenAI response parser limit exceeded" &&
             (*oversized_record)[0].error_message.find("STREAM_RECORD_CANARY") == std::string::npos,
         "one oversized OpenAI SSE record is rejected before buffering or event expansion");
}

void test_builtin_provider_registry()
{
  auto registry = ava::provider::builtin_provider_registry();
  expect(registry.contains("openai"), "builtin provider registry contains OpenAI");
  expect(registry.contains("deepseek") && registry.contains("kimi") && registry.contains("moonshot") && registry.contains("openrouter"),
         "builtin provider registry contains OpenAI-compatible provider shims");
  auto provider = registry.create("openai");
  expect(provider.has_value() && *provider, "builtin provider registry creates OpenAI provider");
  auto kimi = registry.create("kimi");
  expect(kimi.has_value() && *kimi, "builtin provider registry creates Kimi provider shim");
  auto deepseek = registry.create("deepseek");
  expect(deepseek.has_value() && *deepseek, "builtin provider registry creates DeepSeek provider shim");

  auto missing = registry.create("missing-provider");
  expect(!missing && missing.error().category() == ava::core::ErrorCategory::NotFound, "provider registry rejects unknown providers");
}

}  // namespace ava::tests::provider_openai_suite
