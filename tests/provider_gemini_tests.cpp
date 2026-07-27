#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/http/transport.h"
#include "ava/config/model_config.h"
#include "ava/config/provider_profiles.h"
#include "ava/provider/gemini_provider.h"
#include "ava/provider/registry.h"

#include <algorithm>
#include <string>

namespace {

void test_gemini_profiles_and_registry()
{
  auto const profile = ava::config::find_provider_profile("gemini");
  expect(profile && profile->display_name == "Google Gemini" && profile->default_base_url_env == "GEMINI_BASE_URL" && profile->runtime_selectable,
         "Gemini provider profile is runtime selectable with base URL override metadata");

  auto const registry = ava::config::builtin_model_registry();
  auto const model = ava::config::find_model(registry, "gemini", "gemini-2.5-pro");
  expect(model && model->provider_id == "gemini" && model->api_family == "gemini_generate_content" &&
             std::ranges::find(model->input_modalities, "image") != model->input_modalities.end() && model->supports_tools.value_or(false) &&
             model->supports_streaming.value_or(false),
         "Gemini model profile is available with generateContent capabilities");

  auto providers = ava::provider::builtin_provider_registry();
  auto gemini = providers.create("gemini");
  expect(gemini.has_value() && *gemini, "builtin provider registry creates Gemini provider");
}

void test_gemini_provider_contract()
{
  ava::provider::GeminiProvider const provider("https://gemini.example.test/");
  auto const request = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "gemini",
          .model_id = "gemini-2.5-pro",
          .system_prompt = "system",
          .messages = {ava::provider::ChatMessage{.role = "user", .content = "hello \"ava\""},
                       ava::provider::ChatMessage{.role = "assistant", .content = "hi"}},
          .tools_json =
              {R"({"type":"function","name":"read_file","description":"Read","parameters":{"type":"object","properties":{"path":{"type":"string"}},"required":["path"]}})"},
          .stream = true,
          .max_output_tokens = 2048},
      "gemini-key");
  expect(request.has_value(), "Gemini request builds with API key");
  if (request)
  {
    expect(request->method == "POST" && request->url == "https://gemini.example.test/v1beta/models/gemini-2.5-pro:streamGenerateContent?alt=sse",
           "Gemini streaming request targets streamGenerateContent SSE endpoint with trimmed base URL");
    expect(request->headers.at("x-goog-api-key") == "gemini-key", "Gemini request uses x-goog-api-key header");
    expect(request->headers.at("Accept") == "text/event-stream", "Gemini streaming request asks for SSE");
    expect(request->body.find(R"("systemInstruction":{"parts":[{"text":"system"}]})") != std::string::npos, "Gemini request serializes systemInstruction");
    expect(
        request->body.find(R"("contents":[{"role":"user","parts":[{"text":"hello \"ava\""}]},{"role":"model","parts":[{"text":"hi"}]})") != std::string::npos,
        "Gemini request maps assistant role to model and escapes content");
    expect(
        request->body.find(R"("tools":[{"functionDeclarations":[{"name":"read_file","description":"Read","parameters":{"type":"object")") != std::string::npos,
        "Gemini request converts OpenAI-style tools to functionDeclarations");
    expect(request->body.find(R"("generationConfig":{"maxOutputTokens":2048})") != std::string::npos,
           "Gemini request serializes max output tokens in generationConfig");
  }

  auto const non_stream = provider.build_request(ava::provider::ProviderRequest{.provider_id = "gemini",
                                                                                .model_id = "models/gemini-2.5-pro",
                                                                                .system_prompt = "",
                                                                                .messages = {ava::provider::ChatMessage{.role = "user", .content = "hello"}},
                                                                                .tools_json = {},
                                                                                .stream = false},
                                                 "gemini-key");
  expect(non_stream && non_stream->url == "https://gemini.example.test/v1beta/models/gemini-2.5-pro:generateContent" &&
             non_stream->headers.at("Accept") == "application/json",
         "Gemini non-streaming request targets generateContent and accepts JSON");

  auto const native_parts = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "gemini",
          .model_id = "gemini-2.5-pro",
          .system_prompt = "system",
          .messages = {ava::provider::ChatMessage{.role = "assistant",
                                                  .content = "fallback call",
                                                  .content_parts = {ava::provider::ContentPart{.type = ava::provider::ContentPartType::ToolUse,
                                                                                               .tool_call_id = "call_1",
                                                                                               .tool_name = "read_file",
                                                                                               .input_json = R"({"path":"README.md"})"}}},
                       ava::provider::ChatMessage{.role = "user",
                                                  .content = "fallback result",
                                                  .content_parts = {ava::provider::ContentPart{.type = ava::provider::ContentPartType::ToolResult,
                                                                                               .text = "file contents",
                                                                                               .tool_call_id = "call_1",
                                                                                               .tool_name = "read_file",
                                                                                               .is_error = false}}}},
          .tools_json = {},
          .stream = false},
      "gemini-key");
  expect(native_parts && native_parts->body.find(R"({"functionCall":{"id":"call_1","name":"read_file","args":{"path":"README.md"}}})") != std::string::npos &&
             native_parts->body.find(R"({"functionResponse":{"id":"call_1","name":"read_file","response":{"content":"file contents"}}})") != std::string::npos,
         "Gemini request serializes native functionCall and functionResponse parts");

  auto const image_request = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "gemini",
          .model_id = "gemini-2.5-pro",
          .system_prompt = "system",
          .messages = {ava::provider::ChatMessage{
              .role = "user",
              .content = "fallback image metadata",
              .content_parts = {ava::provider::ContentPart{.type = ava::provider::ContentPartType::Text, .text = "describe this"},
                                ava::provider::ContentPart{.type = ava::provider::ContentPartType::Image,
                                                           .attachment_id = "img_1",
                                                           .mime_type = "image/png",
                                                           .storage_path = "attachments/img_1.png",
                                                           .sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                                                           .byte_size = 3,
                                                           .data_base64 = "aGk="}}}},
          .tools_json = {},
          .stream = false},
      "gemini-key");
  expect(image_request && image_request->body.find(R"({"text":"describe this"})") != std::string::npos &&
             image_request->body.find(R"({"inlineData":{"mimeType":"image/png","data":"aGk="}})") != std::string::npos,
         "Gemini request serializes verified image content parts");

  auto const missing_image_bytes = provider.build_request(
      ava::provider::ProviderRequest{.provider_id = "gemini",
                                     .model_id = "gemini-2.5-pro",
                                     .system_prompt = "system",
                                     .messages = {ava::provider::ChatMessage{.role = "user",
                                                                             .content = "fallback image metadata",
                                                                             .content_parts = {ava::provider::ContentPart{
                                                                                 .type = ava::provider::ContentPartType::Image,
                                                                                 .attachment_id = "img_1",
                                                                                 .mime_type = "image/png",
                                                                                 .storage_path = "attachments/img_1.png",
                                                                                 .sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                                                                                 .byte_size = 3}}}},
                                     .tools_json = {},
                                     .stream = false},
      "gemini-key");
  expect(!missing_image_bytes && missing_image_bytes.error().message().find("verified attachment bytes") != std::string::npos,
         "Gemini request rejects image content without verified attachment bytes");

  auto const missing_token = provider.build_request(
      ava::provider::ProviderRequest{.provider_id = "gemini", .model_id = "gemini-2.5-pro", .system_prompt = "", .messages = {}, .tools_json = {}}, "");
  expect(!missing_token && missing_token.error().category() == ava::core::ErrorCategory::PermissionDenied, "Gemini request rejects empty API key");

  auto const invalid_model = provider.build_request(
      ava::provider::ProviderRequest{.provider_id = "gemini", .model_id = "bad/model", .system_prompt = "", .messages = {}, .tools_json = {}}, "gemini-key");
  expect(!invalid_model && invalid_model.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "Gemini request rejects unsafe model IDs before URL construction");

  auto const unsupported_reasoning = provider.build_request(ava::provider::ProviderRequest{.provider_id = "gemini",
                                                                                           .model_id = "gemini-2.5-pro",
                                                                                           .system_prompt = "",
                                                                                           .messages = {},
                                                                                           .tools_json = {},
                                                                                           .reasoning = ava::provider::ProviderReasoningOptions{.type = "low"}},
                                                            "gemini-key");
  expect(!unsupported_reasoning && unsupported_reasoning.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "Gemini request fails closed for unsupported reasoning options");
}

void test_gemini_response_parsing()
{
  ava::provider::GeminiProvider const provider("https://gemini.example.test");
  auto const response = provider.parse_response(
      ava::http::HttpResponse{
          .status_code = 200,
          .headers = {},
          .body =
              R"({"candidates":[{"content":{"role":"model","parts":[{"text":"I will read it."},{"functionCall":{"id":"call_1","name":"read_file","args":{"path":"README.md"}}}]},"finishReason":"STOP"}],"usageMetadata":{"promptTokenCount":2,"candidatesTokenCount":3,"totalTokenCount":5,"thoughtsTokenCount":1,"cachedContentTokenCount":1}})"},
      false);
  expect(response && response->size() == 5 && (*response)[0].type == ava::provider::StreamEventType::TextDelta && (*response)[0].text == "I will read it." &&
             (*response)[1].type == ava::provider::StreamEventType::ToolCallStart && (*response)[1].tool_call_id == "call_1" &&
             (*response)[1].tool_name == "read_file" && (*response)[2].type == ava::provider::StreamEventType::ToolCallDelta &&
             (*response)[2].text.find("README.md") != std::string::npos && (*response)[3].type == ava::provider::StreamEventType::ToolCallEnd &&
             (*response)[4].type == ava::provider::StreamEventType::Done && (*response)[4].finish_reason == ava::provider::ProviderFinishReason::Completed &&
             (*response)[4].usage && (*response)[4].usage->input_tokens == 2 && (*response)[4].usage->output_tokens == 3 &&
             (*response)[4].usage->total_tokens == 5 && (*response)[4].usage->reasoning_tokens == 1 && (*response)[4].usage->cache_read_tokens == 1,
         "Gemini non-stream response parses text, functionCall, finish reason, and usage");

  auto const missing_id_body = ava::http::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body =
          R"({"candidates":[{"content":{"role":"model","parts":[{"functionCall":{"name":"read_file","args":{"path":"README.md"}}}]},"finishReason":"STOP"}]})"};
  auto const first_missing_id = provider.parse_response(missing_id_body, false);
  auto const second_missing_id = provider.parse_response(missing_id_body, false);
  auto const first_fallback = first_missing_id && first_missing_id->size() >= 3 ? (*first_missing_id)[0].tool_call_id : std::string{};
  auto const second_fallback = second_missing_id && second_missing_id->size() >= 3 ? (*second_missing_id)[0].tool_call_id : std::string{};
  expect(!first_fallback.empty() && !second_fallback.empty() && first_fallback != second_fallback && (*first_missing_id)[1].tool_call_id == first_fallback &&
             (*first_missing_id)[2].tool_call_id == first_fallback,
         "Gemini missing function-call IDs use globally distinct per-response fallbacks while same-turn fragments merge");

  auto const missing_content =
      provider.parse_response(ava::http::HttpResponse{.status_code = 200, .headers = {}, .body = R"({"candidates":[{"finishReason":"STOP"}]})"}, false);
  expect(!missing_content && missing_content.error().message().find("content is missing") != std::string::npos,
         "Gemini non-stream response requires content events");

  auto const http_error = provider.parse_response(
      ava::http::HttpResponse{
          .status_code = 403,
          .headers = {},
          .body =
              R"({"error":{"message":"bad key","x-goog-api-key":"GEMINI_HTTP_KEY_CANARY","unknown":{"private":"GEMINI_HTTP_NESTED_CANARY"}},"unknown":"GEMINI_HTTP_OUTER_CANARY"})"},
      false);
  auto const error_text = http_error ? std::string{} : http_error.error().format();
  expect(!http_error && error_text.find("provider_error_kind: authentication") != std::string::npos &&
             error_text.find("provider_message") == std::string::npos && error_text.find("body_snippet") == std::string::npos &&
             error_text.find("GEMINI_HTTP_KEY_CANARY") == std::string::npos && error_text.find("GEMINI_HTTP_NESTED_CANARY") == std::string::npos &&
             error_text.find("GEMINI_HTTP_OUTER_CANARY") == std::string::npos,
         "Gemini HTTP errors expose only fixed local status and classification diagnostics");
}

void test_gemini_sse_parsing_and_fake_transport()
{
  std::string const sse =
      "data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"hel\"}]}}]}\n\n"
      "data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"lo\"}]},\"finishReason\":\"STOP\"}],"
      "\"usageMetadata\":{\"promptTokenCount\":4,\"candidatesTokenCount\":5,\"totalTokenCount\":9}}\n\n";
  auto events = ava::provider::parse_gemini_sse(sse);
  expect(events && events->size() == 3 && (*events)[0].type == ava::provider::StreamEventType::TextDelta && (*events)[0].text == "hel" &&
             (*events)[1].type == ava::provider::StreamEventType::TextDelta && (*events)[1].text == "lo" &&
             (*events)[2].type == ava::provider::StreamEventType::Done && (*events)[2].usage && (*events)[2].usage->input_tokens == 4 &&
             (*events)[2].usage->output_tokens == 5 && (*events)[2].usage->total_tokens == 9,
         "Gemini SSE parser streams text deltas and emits terminal usage on finish");

  ava::provider::GeminiStreamParser parser;
  auto first = parser.append("data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"par");
  expect(first && first->empty(), "Gemini incremental parser buffers partial SSE data");
  auto second = parser.append("tial\"}]}}]}\n\n");
  auto final = parser.finish();
  expect(second && second->size() == 1 && (*second)[0].text == "partial" && final && final->size() == 1 &&
             (*final)[0].type == ava::provider::StreamEventType::Done,
         "Gemini incremental parser handles split chunks");

  ava::provider::GeminiProvider const provider("https://gemini.example.test");
  auto request = provider.build_request(ava::provider::ProviderRequest{.provider_id = "gemini",
                                                                       .model_id = "gemini-2.5-pro",
                                                                       .system_prompt = "system",
                                                                       .messages = {ava::provider::ChatMessage{.role = "user", .content = "hello"}},
                                                                       .tools_json = {},
                                                                       .stream = false},
                                        "gemini-key");
  expect(request.has_value(), "Gemini fake transport request builds");
  if (request)
  {
    ava::tests::FakeTransport transport({ava::http::HttpResponse{
        .status_code = 200, .headers = {}, .body = R"({"candidates":[{"content":{"parts":[{"text":"ok"}]},"finishReason":"STOP"}]})"}});
    auto fake_response = transport.send(*request);
    auto parsed = fake_response ? provider.parse_response(*fake_response, false) : ava::core::Result<std::vector<ava::provider::StreamEvent>>{};
    expect(fake_response && parsed && parsed->size() == 2 && (*parsed)[0].text == "ok" && transport.requests().size() == 1 &&
               transport.requests()[0].headers.at("x-goog-api-key") == "gemini-key",
           "Gemini provider works through fake transport without live credentials");
  }
}

}  // namespace

void run_provider_gemini_tests()
{
  test_gemini_profiles_and_registry();
  test_gemini_provider_contract();
  test_gemini_response_parsing();
  test_gemini_sse_parsing_and_fake_transport();
}
