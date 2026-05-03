#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cwchar>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/print_mode.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/context/context_loader.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/permissions/permission.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/registry.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

namespace {

class StreamingFakeTransport final : public ava::provider::Transport {
 public:
  explicit StreamingFakeTransport(std::vector<ava::provider::HttpResponse> responses)
      : responses_(responses.begin(), responses.end()) {}

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(
      const ava::provider::HttpRequest& request) override {
    return send_streaming(request, nullptr);
  }

  [[nodiscard]] bool supports_streaming() const noexcept override { return true; }

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send_streaming(
      const ava::provider::HttpRequest& request, BodyChunkSink on_body_chunk,
      CancelCallback cancel_requested = nullptr) override {
    requests_.push_back(request);
    if (responses_.empty()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "fake transport has no response"));
    }
    auto response = responses_.front();
    responses_.pop_front();
    if (cancel_requested && cancel_requested()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
    }
    if (on_body_chunk && !response.body.empty()) {
      if (auto delivered = on_body_chunk(response.body); !delivered) return std::unexpected(std::move(delivered.error()));
    }
    return response;
  }

  [[nodiscard]] const std::vector<ava::provider::HttpRequest>& requests() const noexcept { return requests_; }

 private:
  std::deque<ava::provider::HttpResponse> responses_;
  std::vector<ava::provider::HttpRequest> requests_;
};

class FailingOnceTransport final : public ava::provider::Transport {
 public:
  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(
      const ava::provider::HttpRequest& request) override {
    requests_.push_back(request);
    if (requests_.size() == 1) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "temporary transport failure"));
    }
    return ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "ok"};
  }

  [[nodiscard]] const std::vector<ava::provider::HttpRequest>& requests() const noexcept { return requests_; }

 private:
  std::vector<ava::provider::HttpRequest> requests_;
};

void test_openai_provider_contract() {
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  const auto request = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai",
          .model_id = "gpt-5.5",
          .system_prompt = "system",
          .messages = {ava::provider::ChatMessage{.role = "user", .content = "hello \"ava\""}},
          .tools_json = {"{\"type\":\"function\",\"name\":\"read_file\"}"},
          .max_output_tokens = 1234},
      "oauth-token");
  expect(request.has_value(), "OpenAI request builds with OAuth token");
  if (request) {
    expect(request->method == "POST" && request->url == "https://api.example.test/v1/responses",
           "OpenAI request targets responses endpoint");
    expect(request->headers.at("Authorization") == "Bearer oauth-token", "OpenAI request uses OAuth bearer header");
    expect(request->body.find("\"model\":\"gpt-5.5\"") != std::string::npos, "OpenAI request includes model id");
    expect(request->body.find("\"stream\":true") != std::string::npos, "OpenAI request defaults to streaming");
    expect(request->body.find("\"max_output_tokens\":1234") != std::string::npos,
           "OpenAI request includes configured max output tokens");
    expect(request->body.find("\"store\":false") == std::string::npos,
           "OpenAI API-key request does not force Codex store flag");
    expect(request->body.find("hello \\\"ava\\\"") != std::string::npos, "OpenAI request JSON escapes message content");
    expect(request->body.find("\"tools\":[{\"type\":\"function\",\"name\":\"read_file\"}]") != std::string::npos,
           "OpenAI request includes tools array");
    expect(request->timeout_ms == 60000, "OpenAI request carries default HTTP timeout");
  }

  const auto non_stream_request = provider.build_request(ava::provider::ProviderRequest{.provider_id = "openai",
                                                                                        .model_id = "gpt-5.5",
                                                                                        .system_prompt = "system",
                                                                                        .messages = {},
                                                                                        .tools_json = {},
                                                                                        .stream = false},
                                                         "oauth-token");
  expect(non_stream_request && non_stream_request->body.find("\"stream\":false") != std::string::npos,
         "OpenAI request preserves stream=false body field");

  const auto native_parts_request = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai",
          .model_id = "gpt-5.5",
          .system_prompt = "system",
          .messages = {ava::provider::ChatMessage{
              .role = "user",
              .content = "fallback content",
              .content_parts = {ava::provider::ContentPart{.type = ava::provider::ContentPartType::ToolResult,
                                                           .text = "native result",
                                                           .tool_call_id = "call_ignored",
                                                           .tool_name = "read_file",
                                                           .input_json = "",
                                                           .is_error = false}}}},
          .tools_json = {}},
      "oauth-token");
  expect(native_parts_request && native_parts_request->body.find("fallback content") != std::string::npos &&
             native_parts_request->body.find("content_parts") == std::string::npos &&
             native_parts_request->body.find("tool_result") == std::string::npos &&
             native_parts_request->body.find("call_ignored") == std::string::npos,
         "OpenAI request ignores native content parts and serializes fallback content only");

  const auto expired_credential_request = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai", .model_id = "gpt-5.5", .system_prompt = "system", .messages = {}, .tools_json = {}},
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "expired-token",
                                    .refresh_token = "",
                                    .expires_at = 10,
                                    .source_path = {}},
      11);
  expect(
      !expired_credential_request && expired_credential_request.error().message().find("expired") != std::string::npos,
      "OpenAI provider rejects expired OAuth before building request");

  const auto oauth_credential_request = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai", .model_id = "gpt-5.5", .system_prompt = "system", .messages = {}, .tools_json = {}},
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "codex-token",
                                    .refresh_token = "refresh",
                                    .expires_at = 120,
                                    .account_id = "acct_123",
                                    .source_path = {}},
      11);
  expect(oauth_credential_request && oauth_credential_request->url == "https://chatgpt.com/backend-api/codex/responses",
         "OpenAI OAuth request targets ChatGPT Codex responses endpoint");
  if (oauth_credential_request) {
    expect(oauth_credential_request->headers.at("ChatGPT-Account-Id") == "acct_123" &&
               oauth_credential_request->headers.at("OpenAI-Beta") == "responses=experimental" &&
               oauth_credential_request->headers.at("originator") == "ava",
           "OpenAI OAuth request carries Codex account and beta headers");
    expect(oauth_credential_request->body.find("\"store\":false") != std::string::npos,
           "OpenAI OAuth request disables Codex response storage");
  }

  const auto oauth_credential_request_without_now = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai", .model_id = "gpt-5.5", .system_prompt = "system", .messages = {}, .tools_json = {}},
      ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                    .access_token = "codex-token",
                                    .refresh_token = "refresh",
                                    .expires_at = 0,
                                    .account_id = "acct_456",
                                    .source_path = {}});
  expect(oauth_credential_request_without_now &&
             oauth_credential_request_without_now->url == "https://chatgpt.com/backend-api/codex/responses",
         "OpenAI OAuth request without explicit clock still uses Codex endpoint");
  if (oauth_credential_request_without_now) {
    expect(oauth_credential_request_without_now->headers.at("ChatGPT-Account-Id") == "acct_456" &&
               oauth_credential_request_without_now->body.find("\"store\":false") != std::string::npos,
           "OpenAI OAuth request without explicit clock applies Codex auth options");
  }

  const auto invalid_tool = provider.build_request(ava::provider::ProviderRequest{.provider_id = "openai",
                                                                                  .model_id = "gpt-5.5",
                                                                                  .system_prompt = "system",
                                                                                  .messages = {},
                                                                                  .tools_json = {"not an object"}},
                                                   "oauth-token");
  expect(!invalid_tool && invalid_tool.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI request rejects malformed tool JSON before embedding");

  const auto missing_model = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai", .model_id = "", .system_prompt = "system", .messages = {}, .tools_json = {}},
      "oauth-token");
  expect(!missing_model && missing_model.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI request rejects empty model");
  const auto missing_token = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai", .model_id = "gpt-5.5", .system_prompt = "system", .messages = {}, .tools_json = {}},
      "");
  expect(!missing_token && missing_token.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "OpenAI request rejects empty token");

  const std::string sse =
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"hi\"}\r\n\r\n"
      "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\r\n\r\n"
      "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{}\"}\n\n"
      "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_1\"}\n\n"
      "data: [DONE]\n\n";
  auto events = ava::provider::parse_openai_sse(sse);
  expect(events.has_value(), "OpenAI SSE parses");
  if (events) {
    expect(events->size() == 5, "OpenAI SSE produces expected events");
    expect((*events)[0].type == ava::provider::StreamEventType::TextDelta && (*events)[0].text == "hi",
           "OpenAI SSE text delta parses");
    expect((*events)[1].type == ava::provider::StreamEventType::ToolCallStart && (*events)[1].tool_name == "read_file",
           "OpenAI SSE tool start parses");
    expect((*events)[4].type == ava::provider::StreamEventType::Done, "OpenAI SSE done parses");
  }
  auto output_item_tool = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"call_live\","
      "\"type\":\"function_call\",\"name\":\"read_file\",\"call_id\":\"call_live_provider\"}}\n\n"
      "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_live\","
      "\"delta\":\"{\\\"path\\\":\\\"smoke.txt\\\"}\"}\n\n"
      "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_live\"}\n\n"
      "data: [DONE]\n\n");
  expect(output_item_tool && output_item_tool->size() == 4 &&
             (*output_item_tool)[0].type == ava::provider::StreamEventType::ToolCallStart &&
             (*output_item_tool)[0].tool_call_id == "call_live" &&
             (*output_item_tool)[0].tool_name == "read_file" &&
             (*output_item_tool)[1].type == ava::provider::StreamEventType::ToolCallDelta &&
             (*output_item_tool)[1].tool_call_id == "call_live" &&
             (*output_item_tool)[1].text.find("smoke.txt") != std::string::npos,
         "OpenAI output_item.added function calls preserve tool names for argument deltas");
  auto http_error = ava::provider::parse_openai_sse_response(ava::provider::HttpResponse{
      .status_code = 401, .headers = {}, .body = "{\"error\":\"bad auth\",\"Authorization\":\"Bearer secret\"}"});
  expect(!http_error && http_error.error().category() == ava::core::ErrorCategory::Provider &&
              http_error.error().message().find("401") != std::string::npos,
         "OpenAI auth response is normalized as a provider error with status context");
  if (!http_error) {
    const auto formatted = http_error.error().format();
    expect(formatted.find("provider_error_kind: authentication") != std::string::npos &&
                formatted.find("body_snippet") != std::string::npos && formatted.find("bad auth") != std::string::npos &&
               formatted.find("Bearer secret") == std::string::npos,
           "OpenAI non-200 response includes normalized kind and sanitized body snippet context");
  }
  auto rate_limit = ava::provider::parse_openai_sse_response(ava::provider::HttpResponse{
      .status_code = 429, .headers = {{"Retry-After", "2"}}, .body = "{\"error\":\"rate limited\"}"});
  expect(!rate_limit && rate_limit.error().format().find("provider_error_kind: rate_limited") != std::string::npos &&
             rate_limit.error().format().find("retry_after: 2") != std::string::npos,
         "OpenAI rate-limit errors carry normalized kind and Retry-After context");
  expect(
      ava::provider::is_auth_status(401) && ava::provider::is_auth_status(403) && !ava::provider::is_auth_status(429),
      "OpenAI auth status helper classifies auth failures");
  expect(ava::provider::is_retryable_status(429) && ava::provider::is_retryable_status(500) &&
              !ava::provider::is_retryable_status(401),
         "OpenAI retryable status helper classifies transient failures");

  ava::tests::FakeTransport retry_inner({ava::provider::HttpResponse{.status_code = 429,
                                                                      .headers = {{"Retry-After", "0"}},
                                                                      .body = "rate limited"},
                                        ava::provider::HttpResponse{.status_code = 200,
                                                                    .headers = {},
                                                                    .body = "ok"}});
  ava::provider::RetryTransport retry_transport(
      retry_inner, ava::provider::RetryOptions{.max_attempts = 2, .base_delay_ms = 0, .max_retry_after_ms = 0});
  const auto retry_request = ava::provider::HttpRequest{.method = "POST",
                                                        .url = "https://api.example.test",
                                                        .headers = {},
                                                        .body = {},
                                                        .timeout_ms = 60000,
                                                        .follow_redirects = true,
                                                        .include_response_headers = false,
                                                        .resolve_hosts = {}};
  auto retried = retry_transport.send(retry_request);
  expect(retried && retried->status_code == 200 && retry_inner.requests().size() == 2,
         "retry transport retries rate-limited non-streaming responses");

  FailingOnceTransport failing_once;
  ava::provider::RetryTransport retry_transport_error(
      failing_once, ava::provider::RetryOptions{.max_attempts = 2, .base_delay_ms = 0, .max_retry_after_ms = 0});
  auto retried_transport_error = retry_transport_error.send(retry_request);
  expect(retried_transport_error && retried_transport_error->status_code == 200 && failing_once.requests().size() == 2,
         "retry transport retries retryable transport errors");

  StreamingFakeTransport streaming_inner({ava::provider::HttpResponse{.status_code = 429,
                                                                       .headers = {{"Retry-After", "0"}},
                                                                       .body = ""},
                                         ava::provider::HttpResponse{.status_code = 200,
                                                                     .headers = {},
                                                                     .body = "data: [DONE]\n\n"}});
  ava::provider::RetryTransport streaming_retry_transport(
      streaming_inner, ava::provider::RetryOptions{.max_attempts = 2, .base_delay_ms = 0, .max_retry_after_ms = 0});
  std::string streamed_body;
  auto streaming_retry = streaming_retry_transport.send_streaming(
      retry_request,
      [&streamed_body](std::string_view chunk) -> ava::core::VoidResult {
        streamed_body.append(chunk);
        return {};
      });
  expect(streaming_retry && streaming_retry->status_code == 200 && streaming_inner.requests().size() == 2 &&
             streamed_body == "data: [DONE]\n\n",
         "retry transport retries rate-limited streaming responses and only delivers final chunks");

  auto completed = ava::provider::parse_openai_sse("data: {\"type\":\"response.completed\"}\n\n");
  expect(completed && completed->size() == 1 && (*completed)[0].type == ava::provider::StreamEventType::Done,
         "OpenAI response.completed event produces done event");
  auto completed_with_usage = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.completed\",\"response\":{\"usage\":{\"input_tokens\":11,"
      "\"output_tokens\":7,\"total_tokens\":18,\"input_tokens_details\":{\"cached_tokens\":3},"
      "\"output_tokens_details\":{\"reasoning_tokens\":2}}}}\n\n");
  expect(completed_with_usage && completed_with_usage->size() == 1 && (*completed_with_usage)[0].usage &&
             (*completed_with_usage)[0].usage->input_tokens == 11 &&
             (*completed_with_usage)[0].usage->output_tokens == 7 &&
             (*completed_with_usage)[0].usage->total_tokens == 18 &&
             (*completed_with_usage)[0].usage->cache_read_tokens == 3 &&
             (*completed_with_usage)[0].usage->reasoning_tokens == 2,
         "OpenAI response.completed event preserves Responses API usage details");
  auto lifecycle = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.created\"}\n\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"hi\"}\n\n"
      "data: {\"type\":\"response.output_text.done\"}\n\n"
      "data: {\"type\":\"response.function_call_arguments.done\",\"item_id\":\"call_1\"}\n\n"
      "data: [DONE]\n\n");
  expect(lifecycle && lifecycle->size() == 2 && (*lifecycle)[0].type == ava::provider::StreamEventType::TextDelta &&
             (*lifecycle)[0].text == "hi" && (*lifecycle)[1].type == ava::provider::StreamEventType::Done,
         "OpenAI SSE parser ignores non-content lifecycle events");
  auto completed_tool = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.function_call.completed\",\"call_id\":\"call_fallback\"}\n\n");
  expect(completed_tool && completed_tool->size() == 1 &&
             (*completed_tool)[0].type == ava::provider::StreamEventType::ToolCallEnd &&
             (*completed_tool)[0].tool_call_id == "call_fallback",
         "OpenAI function_call.completed uses call_id fallback");
  auto text_fallback =
      ava::provider::parse_openai_sse("data: {\"type\":\"response.text.delta\",\"text\":\"fallback\"}\n\n");
  expect(text_fallback && text_fallback->size() == 1 &&
             (*text_fallback)[0].type == ava::provider::StreamEventType::TextDelta &&
             (*text_fallback)[0].text == "fallback",
         "OpenAI response.text.delta uses text fallback");

  auto unknown = ava::provider::parse_openai_sse("data: {\"type\":\"response.unexpected\"}\n\n");
  expect(unknown && unknown->empty(), "OpenAI unknown SSE event is ignored as forward-compatible lifecycle data");
  auto malformed = ava::provider::parse_openai_sse("data: {not-json}\n\n");
  expect(malformed && malformed->size() == 1 && (*malformed)[0].type == ava::provider::StreamEventType::Error,
         "OpenAI malformed SSE data produces error event");
  auto api_error = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.error\",\"error\":{\"message\":\"bad request\"}}\n\n");
  expect(api_error && api_error->size() == 1 && (*api_error)[0].type == ava::provider::StreamEventType::Error &&
             (*api_error)[0].error_message == "bad request",
         "OpenAI SSE error event preserves error message");
  auto unknown_between_deltas = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"a\"}\n\n"
      "data: {\"type\":\"response.new_lifecycle_event\"}\n\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"b\"}\n\n"
      "data: [DONE]\n\n");
  expect(unknown_between_deltas && unknown_between_deltas->size() == 3 &&
             (*unknown_between_deltas)[0].type == ava::provider::StreamEventType::TextDelta &&
             (*unknown_between_deltas)[1].type == ava::provider::StreamEventType::TextDelta &&
             (*unknown_between_deltas)[2].type == ava::provider::StreamEventType::Done,
         "OpenAI unknown SSE events do not suppress final assistant content");
  auto text = ava::provider::parse_openai_response_text("{\"output_text\":\"done\"}");
  expect(text && *text == "done", "OpenAI non-stream response text parses");
  auto non_stream_usage = ava::provider::parse_openai_usage(
      "{\"output_text\":\"done\",\"usage\":{\"prompt_tokens\":5,\"completion_tokens\":6,"
      "\"total_tokens\":11,\"completion_tokens_details\":{\"reasoning_tokens\":4}}}");
  expect(non_stream_usage && non_stream_usage->input_tokens == 5 && non_stream_usage->output_tokens == 6 &&
             non_stream_usage->total_tokens == 11 && non_stream_usage->reasoning_tokens == 4,
         "OpenAI non-stream usage parser accepts prompt/completion aliases");
  auto missing_text = ava::provider::parse_openai_response_text("{\"id\":\"resp_1\"}");
  expect(!missing_text, "OpenAI non-stream response requires expected text field");

  if (request) {
    ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "ok"}});
    auto fake_response = transport.send(*request);
    expect(fake_response && fake_response->body == "ok" && transport.requests().size() == 1 &&
               transport.requests()[0].timeout_ms == request->timeout_ms,
           "fake transport records offline provider request and preserves timeout");
  }
}

void test_openai_incremental_sse_parser() {
  ava::provider::OpenAIStreamParser parser;
  std::vector<ava::provider::StreamEvent> events;
  auto append = [&](std::string_view chunk) {
    auto parsed = parser.append(chunk);
    expect(parsed.has_value(), "incremental OpenAI SSE chunk parses");
    if (parsed) events.insert(events.end(), parsed->begin(), parsed->end());
  };

  append("data: {\"type\":\"response.output_text.delta\",\"del");
  expect(events.empty(), "incremental OpenAI SSE waits for a complete split frame");
  append("ta\":\"he");
  append("llo\"}\n\n");
  append("data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"bash\"}\n\n");
  append("data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\"}\n");
  append("\n");
  append("data: {not-json}\n\n");
  append("data: {\"type\":\"response.error\",\"error\":{\"message\":\"bad stream\"}}\n\n");
  append("data: [DONE]");
  auto final_events = parser.finish();
  expect(final_events.has_value(), "incremental OpenAI SSE finish parses pending data");
  if (final_events) events.insert(events.end(), final_events->begin(), final_events->end());

  expect(events.size() == 6, "incremental OpenAI SSE emits text, tool, error, and done events");
  if (events.size() == 6) {
    expect(events[0].type == ava::provider::StreamEventType::TextDelta && events[0].text == "hello",
           "incremental OpenAI SSE preserves split text delta");
    expect(events[1].type == ava::provider::StreamEventType::ToolCallStart && events[1].tool_call_id == "call_1" &&
               events[1].tool_name == "bash",
           "incremental OpenAI SSE preserves tool call start");
    expect(events[2].type == ava::provider::StreamEventType::ToolCallDelta && events[2].text == "{",
           "incremental OpenAI SSE preserves tool argument deltas");
    expect(events[3].type == ava::provider::StreamEventType::Error,
           "incremental OpenAI SSE reports malformed data as an error event");
    expect(events[4].type == ava::provider::StreamEventType::Error && events[4].error_message == "bad stream",
           "incremental OpenAI SSE preserves provider error messages");
    expect(events[5].type == ava::provider::StreamEventType::Done, "incremental OpenAI SSE emits done on finish");
  }
}

void test_builtin_provider_registry() {
  auto registry = ava::provider::builtin_provider_registry();
  expect(registry.contains("openai"), "builtin provider registry contains OpenAI");
  auto provider = registry.create("openai");
  expect(provider.has_value() && *provider, "builtin provider registry creates OpenAI provider");

  auto missing = registry.create("missing-provider");
  expect(!missing && missing.error().category() == ava::core::ErrorCategory::NotFound,
         "provider registry rejects unknown providers");
}

}  // namespace

void run_provider_openai_tests() {
  test_openai_provider_contract();
  test_openai_incremental_sse_parser();
  test_builtin_provider_registry();
}
