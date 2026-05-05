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
#include "ava/provider/openai_compatible_provider.h"
#include "ava/provider/openai_compatible_request.h"
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
      : responses_(responses.begin(), responses.end())
  {
  }

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override
  {
    return send_streaming(request, nullptr);
  }

  [[nodiscard]] bool supports_streaming() const noexcept override { return true; }

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send_streaming(
      ava::provider::HttpRequest const& request, BodyChunkSink on_body_chunk,
      CancelCallback cancel_requested = nullptr) override
  {
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
      if (auto delivered = on_body_chunk(response.body); !delivered)
        return std::unexpected(std::move(delivered.error()));
    }
    return response;
  }

  [[nodiscard]] std::vector<ava::provider::HttpRequest> const& requests() const noexcept { return requests_; }

 private:
  std::deque<ava::provider::HttpResponse> responses_;
  std::vector<ava::provider::HttpRequest> requests_;
};

class FailingOnceTransport final : public ava::provider::Transport {
 public:
  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override
  {
    requests_.push_back(request);
    if (requests_.size() == 1) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "temporary transport failure"));
    }
    return ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "ok"};
  }

  [[nodiscard]] std::vector<ava::provider::HttpRequest> const& requests() const noexcept { return requests_; }

 private:
  std::vector<ava::provider::HttpRequest> requests_;
};

void test_openai_provider_contract()
{
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  auto const request = provider.build_request(
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

  auto const non_stream_request = provider.build_request(ava::provider::ProviderRequest{.provider_id = "openai",
                                                                                        .model_id = "gpt-5.5",
                                                                                        .system_prompt = "system",
                                                                                        .messages = {},
                                                                                        .tools_json = {},
                                                                                        .stream = false},
                                                         "oauth-token");
  expect(non_stream_request && non_stream_request->body.find("\"stream\":false") != std::string::npos,
         "OpenAI request preserves stream=false body field");

  auto const reasoning_request = provider.build_request(
      ava::provider::ProviderRequest{.provider_id = "openai",
                                     .model_id = "gpt-5.5",
                                     .system_prompt = "system",
                                     .messages = {},
                                     .tools_json = {},
                                     .reasoning = ava::provider::ProviderReasoningOptions{.type = "low"}},
      "oauth-token");
  expect(reasoning_request && reasoning_request->body.find("\"reasoning\":{\"effort\":\"low\"") != std::string::npos &&
             reasoning_request->body.find("\"summary\":\"auto\"") != std::string::npos,
         "OpenAI request serializes reasoning effort with visible summary request");

  auto const invalid_reasoning_budget = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai",
          .model_id = "gpt-5.5",
          .system_prompt = "system",
          .messages = {},
          .tools_json = {},
          .reasoning = ava::provider::ProviderReasoningOptions{.type = "low", .budget_tokens = 1024}},
      "oauth-token");
  expect(!invalid_reasoning_budget &&
             invalid_reasoning_budget.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI request rejects budgeted reasoning options");

  auto const invalid_reasoning_level = provider.build_request(
      ava::provider::ProviderRequest{.provider_id = "openai",
                                     .model_id = "gpt-5.5",
                                     .system_prompt = "system",
                                     .messages = {},
                                     .tools_json = {},
                                     .reasoning = ava::provider::ProviderReasoningOptions{.type = "ultra"}},
      "oauth-token");
  expect(!invalid_reasoning_level &&
             invalid_reasoning_level.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI request rejects unsupported reasoning effort");

  auto const native_parts_request = provider.build_request(
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

  auto const expired_credential_request = provider.build_request(
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

  auto const oauth_credential_request = provider.build_request(
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

  auto const oauth_credential_request_without_now = provider.build_request(
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

  auto const invalid_tool = provider.build_request(ava::provider::ProviderRequest{.provider_id = "openai",
                                                                                  .model_id = "gpt-5.5",
                                                                                  .system_prompt = "system",
                                                                                  .messages = {},
                                                                                  .tools_json = {"not an object"}},
                                                   "oauth-token");
  expect(!invalid_tool && invalid_tool.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI request rejects malformed tool JSON before embedding");

  auto const missing_model = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai", .model_id = "", .system_prompt = "system", .messages = {}, .tools_json = {}},
      "oauth-token");
  expect(!missing_model && missing_model.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI request rejects empty model");
  auto const missing_token = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "openai", .model_id = "gpt-5.5", .system_prompt = "system", .messages = {}, .tools_json = {}},
      "");
  expect(!missing_token && missing_token.error().category() == ava::core::ErrorCategory::PermissionDenied,
         "OpenAI request rejects empty token");

  std::string const sse =
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
             (*output_item_tool)[0].tool_call_id == "call_live" && (*output_item_tool)[0].tool_name == "read_file" &&
             (*output_item_tool)[1].type == ava::provider::StreamEventType::ToolCallDelta &&
             (*output_item_tool)[1].tool_call_id == "call_live" &&
             (*output_item_tool)[1].text.find("smoke.txt") != std::string::npos,
         "OpenAI output_item.added function calls preserve tool names for argument deltas");

  auto reasoning_summary = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"rs_1\",\"type\":\"reasoning\"}}\n\n"
      "data: {\"type\":\"response.reasoning_summary_part.added\",\"item_id\":\"rs_1\"}\n\n"
      "data: {\"type\":\"response.reasoning_summary_text.delta\",\"delta\":\"plan\"}\n\n"
      "data: {\"type\":\"response.reasoning_summary_text.done\",\"text\":\"plan\"}\n\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"answer\"}\n\n"
      "data: [DONE]\n\n");
  expect(reasoning_summary && reasoning_summary->size() == 5 &&
             (*reasoning_summary)[0].type == ava::provider::StreamEventType::ReasoningStart &&
             (*reasoning_summary)[0].reasoning_format == "openai_responses" &&
             (*reasoning_summary)[1].type == ava::provider::StreamEventType::ReasoningDelta &&
             (*reasoning_summary)[1].text == "plan" &&
             (*reasoning_summary)[2].type == ava::provider::StreamEventType::ReasoningEnd &&
             (*reasoning_summary)[3].type == ava::provider::StreamEventType::TextDelta &&
             (*reasoning_summary)[3].text == "answer" &&
             (*reasoning_summary)[4].type == ava::provider::StreamEventType::Done,
         "OpenAI Responses SSE emits reasoning summary before answer text");
  auto reasoning_duplicate_done = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"rs_1\",\"type\":\"reasoning\"}}\n\n"
      "data: {\"type\":\"response.reasoning_summary_text.delta\",\"item_id\":\"rs_1\",\"delta\":\"plan\"}\n\n"
      "data: {\"type\":\"response.reasoning_summary_text.done\",\"item_id\":\"rs_1\",\"text\":\"plan\"}\n\n"
      "data: {\"type\":\"response.reasoning_summary_part.done\",\"item_id\":\"rs_1\",\"text\":\"plan\"}\n\n"
      "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"rs_1\",\"type\":\"reasoning\","
      "\"summary\":[{\"type\":\"summary_text\",\"text\":\"plan\"}]}}\n\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"answer\"}\n\n"
      "data: [DONE]\n\n");
  expect(reasoning_duplicate_done && reasoning_duplicate_done->size() == 5 &&
             (*reasoning_duplicate_done)[0].type == ava::provider::StreamEventType::ReasoningStart &&
             (*reasoning_duplicate_done)[1].type == ava::provider::StreamEventType::ReasoningDelta &&
             (*reasoning_duplicate_done)[1].text == "plan" &&
             (*reasoning_duplicate_done)[2].type == ava::provider::StreamEventType::ReasoningEnd &&
             (*reasoning_duplicate_done)[3].type == ava::provider::StreamEventType::TextDelta &&
             (*reasoning_duplicate_done)[4].type == ava::provider::StreamEventType::Done,
         "OpenAI Responses SSE ignores duplicate reasoning summary done events for one item");
  auto reasoning_done_only = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"rs_2\",\"type\":\"reasoning\"}}\n\n"
      "data: {\"type\":\"response.reasoning_summary_text.done\",\"text\":\"done-only plan\"}\n\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"answer\"}\n\n"
      "data: [DONE]\n\n");
  expect(reasoning_done_only && reasoning_done_only->size() == 5 &&
             (*reasoning_done_only)[0].type == ava::provider::StreamEventType::ReasoningStart &&
             (*reasoning_done_only)[1].type == ava::provider::StreamEventType::ReasoningDelta &&
             (*reasoning_done_only)[1].text == "done-only plan" &&
             (*reasoning_done_only)[2].type == ava::provider::StreamEventType::ReasoningEnd &&
             (*reasoning_done_only)[3].type == ava::provider::StreamEventType::TextDelta &&
             (*reasoning_done_only)[4].type == ava::provider::StreamEventType::Done,
         "OpenAI Responses SSE preserves done-only reasoning summary text");
  auto http_error = ava::provider::parse_openai_sse_response(ava::provider::HttpResponse{
      .status_code = 401, .headers = {}, .body = "{\"error\":\"bad auth\",\"Authorization\":\"Bearer secret\"}"});
  expect(!http_error && http_error.error().category() == ava::core::ErrorCategory::Provider &&
             http_error.error().message().find("401") != std::string::npos,
         "OpenAI auth response is normalized as a provider error with status context");
  if (!http_error) {
    auto const formatted = http_error.error().format();
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
  expect(ava::provider::classify_provider_error(
             ava::provider::HttpResponse{.status_code = 400, .headers = {}, .body = "Input token length too long"}) ==
             ava::provider::ProviderErrorKind::ContextOverflow,
         "provider error classifier recognizes Kimi input token overflow wording");
  expect(ava::provider::classify_provider_error(ava::provider::HttpResponse{
             .status_code = 400, .headers = {}, .body = "Your request exceeded model token limit : 131072"}) ==
             ava::provider::ProviderErrorKind::ContextOverflow,
         "provider error classifier recognizes Kimi combined model token overflow wording");
  expect(ava::provider::classify_provider_error(
             ava::provider::HttpResponse{.status_code = 400, .headers = {}, .body = "exceeded model token limit"}) ==
             ava::provider::ProviderErrorKind::ContextOverflow,
         "provider error classifier recognizes short Kimi model token overflow wording");

  ava::tests::FakeTransport retry_inner(
      {ava::provider::HttpResponse{.status_code = 429, .headers = {{"Retry-After", "0"}}, .body = "rate limited"},
       ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "ok"}});
  std::vector<ava::provider::RetryOptions::Event> retry_events;
  ava::provider::RetryTransport retry_transport(
      retry_inner,
      ava::provider::RetryOptions{.max_attempts = 2,
                                  .base_delay_ms = 0,
                                  .max_retry_after_ms = 0,
                                  .on_retry = [&retry_events](ava::provider::RetryOptions::Event const& event) {
                                    retry_events.push_back(event);
                                    return ava::core::VoidResult{};
                                  }});
  auto const retry_request = ava::provider::HttpRequest{.method = "POST",
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
  expect(retry_events.size() == 1 && retry_events[0].attempt == 2 && retry_events[0].max_attempts == 2 &&
             retry_events[0].reason == "rate_limited" && retry_events[0].status_code == 429 &&
             !retry_events[0].streaming && !retry_events[0].countdown_tick,
         "retry transport reports backend-owned retry metadata before sleeping");

  ava::tests::FakeTransport countdown_inner(
      {ava::provider::HttpResponse{.status_code = 503, .headers = {}, .body = "try again"},
       ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "ok"}});
  std::vector<ava::provider::RetryOptions::Event> countdown_events;
  ava::provider::RetryTransport countdown_transport(
      countdown_inner,
      ava::provider::RetryOptions{.max_attempts = 2,
                                  .base_delay_ms = 1,
                                  .max_retry_after_ms = 1,
                                  .countdown_tick_ms = 1,
                                  .on_retry = [&countdown_events](ava::provider::RetryOptions::Event const& event) {
                                    countdown_events.push_back(event);
                                    return ava::core::VoidResult{};
                                  }});
  auto countdown_retry = countdown_transport.send(retry_request);
  expect(countdown_retry && countdown_retry->status_code == 200 && countdown_inner.requests().size() == 2,
         "retry transport completes after a countdown-backed retry");
  expect(countdown_events.size() == 2 && !countdown_events[0].countdown_tick && countdown_events[0].delay_ms == 1 &&
             countdown_events[0].remaining_ms == 1 && countdown_events[1].countdown_tick &&
             countdown_events[1].remaining_ms == 0 && countdown_events[1].reason == "transient",
         "retry transport emits explicit backend countdown ticks while waiting to retry");

  ava::tests::FakeTransport cancel_retry_inner(
      {ava::provider::HttpResponse{.status_code = 503, .headers = {}, .body = "try again"},
       ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "ok"}});
  std::vector<ava::provider::RetryOptions::Event> cancel_retry_events;
  ava::provider::RetryTransport cancel_retry_transport(
      cancel_retry_inner, ava::provider::RetryOptions{
                              .max_attempts = 2,
                              .base_delay_ms = 10,
                              .max_retry_after_ms = 0,
                              .countdown_tick_ms = 10,
                              .on_retry =
                                  [&cancel_retry_events](ava::provider::RetryOptions::Event const& event) {
                                    cancel_retry_events.push_back(event);
                                    return ava::core::VoidResult{};
                                  },
                              .cancel_requested = [&cancel_retry_events] { return !cancel_retry_events.empty(); },
                          });
  auto canceled_retry = cancel_retry_transport.send(retry_request);
  expect(!canceled_retry && canceled_retry.error().message().find("retry canceled") != std::string::npos &&
             cancel_retry_inner.requests().size() == 1 && cancel_retry_events.size() == 1,
         "retry transport observes cancellation before sleeping for a retry");

  ava::tests::FakeTransport direct_cancel_inner(
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "ok"}});
  auto direct_canceled =
      static_cast<ava::provider::Transport&>(direct_cancel_inner).send(retry_request, [] { return true; });
  expect(!direct_canceled && direct_canceled.error().message().find("canceled") != std::string::npos &&
             direct_cancel_inner.requests().empty(),
         "transport default cancellable send checks cancellation before dispatch");

  ava::tests::FakeTransport retry_call_cancel_inner(
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "ok"}});
  ava::provider::RetryTransport retry_call_cancel_transport(
      retry_call_cancel_inner, ava::provider::RetryOptions{.max_attempts = 2, .base_delay_ms = 0});
  auto retry_call_canceled = retry_call_cancel_transport.send(retry_request, [] { return true; });
  expect(!retry_call_canceled && retry_call_canceled.error().message().find("retry canceled") != std::string::npos &&
             retry_call_cancel_inner.requests().empty(),
         "retry transport cancellable send checks cancellation before dispatch");

  FailingOnceTransport failing_once;
  ava::provider::RetryTransport retry_transport_error(
      failing_once, ava::provider::RetryOptions{.max_attempts = 2, .base_delay_ms = 0, .max_retry_after_ms = 0});
  auto retried_transport_error = retry_transport_error.send(retry_request);
  expect(retried_transport_error && retried_transport_error->status_code == 200 && failing_once.requests().size() == 2,
         "retry transport retries retryable transport errors");

  StreamingFakeTransport streaming_inner(
      {ava::provider::HttpResponse{.status_code = 429, .headers = {{"Retry-After", "0"}}, .body = ""},
       ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "data: [DONE]\n\n"}});
  ava::provider::RetryTransport streaming_retry_transport(
      streaming_inner, ava::provider::RetryOptions{.max_attempts = 2, .base_delay_ms = 0, .max_retry_after_ms = 0});
  std::string streamed_body;
  auto streaming_retry = streaming_retry_transport.send_streaming(
      retry_request, [&streamed_body](std::string_view chunk) -> ava::core::VoidResult {
        streamed_body.append(chunk);
        return {};
      });
  expect(streaming_retry && streaming_retry->status_code == 200 && streaming_inner.requests().size() == 2 &&
             streamed_body == "data: [DONE]\n\n",
         "retry transport retries rate-limited streaming responses and only delivers final chunks");

  auto completed = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n");
  expect(completed && completed->size() == 1 && (*completed)[0].type == ava::provider::StreamEventType::Done &&
             (*completed)[0].stop_reason == "completed",
         "OpenAI response.completed event produces done event");
  auto completed_then_done = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n"
      "data: [DONE]\n\n");
  expect(completed_then_done && completed_then_done->size() == 1 &&
             (*completed_then_done)[0].type == ava::provider::StreamEventType::Done &&
             (*completed_then_done)[0].stop_reason == "completed",
         "OpenAI SSE parser suppresses duplicate done marker after response.completed");
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
  auto incomplete = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.incomplete\",\"response\":{\"status\":\"incomplete\","
      "\"incomplete_details\":{\"reason\":\"max_output_tokens\"}}}\n\n");
  expect(incomplete && incomplete->size() == 1 && (*incomplete)[0].type == ava::provider::StreamEventType::Done &&
             (*incomplete)[0].stop_reason == "max_tokens",
         "OpenAI response.incomplete preserves normalized incomplete reason");
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
      "data: {\"type\":\"response.function_call.completed\",\"call_id\":\"call_fallback\"}\n\n"
      "data: {\"type\":\"response.completed\"}\n\n");
  expect(completed_tool && completed_tool->size() == 2 &&
             (*completed_tool)[0].type == ava::provider::StreamEventType::ToolCallEnd &&
             (*completed_tool)[0].tool_call_id == "call_fallback",
         "OpenAI function_call.completed uses call_id fallback");
  auto text_fallback = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.text.delta\",\"text\":\"fallback\"}\n\n"
      "data: [DONE]\n\n");
  expect(text_fallback && text_fallback->size() == 2 &&
             (*text_fallback)[0].type == ava::provider::StreamEventType::TextDelta &&
             (*text_fallback)[0].text == "fallback",
         "OpenAI response.text.delta uses text fallback");
  auto truncated =
      ava::provider::parse_openai_sse("data: {\"type\":\"response.output_text.delta\",\"delta\":\"partial\"}\n\n");
  expect(truncated && truncated->size() == 2 && (*truncated)[0].type == ava::provider::StreamEventType::TextDelta &&
             (*truncated)[1].type == ava::provider::StreamEventType::Error &&
             (*truncated)[1].error_message.find("done marker") != std::string::npos,
         "OpenAI SSE parser reports truncated streams after content");

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

  ava::provider::OpenAIProvider const non_stream_provider("https://api.example.test");
  auto non_stream_tool = non_stream_provider.parse_response(
      ava::provider::HttpResponse{.status_code = 200,
                                  .headers = {},
                                  .body = "{\"output_text\":\"Let me read that file.\","
                                          "\"output\":[{\"type\":\"function_call\",\"call_id\":\"call_1\","
                                          "\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\\\"README.md\\\"}\"}],"
                                          "\"usage\":{\"input_tokens\":2,\"output_tokens\":3}}"},
      false);
  expect(non_stream_tool && non_stream_tool->size() == 5 &&
             (*non_stream_tool)[0].type == ava::provider::StreamEventType::TextDelta &&
             (*non_stream_tool)[0].text == "Let me read that file." &&
             (*non_stream_tool)[1].type == ava::provider::StreamEventType::ToolCallStart &&
             (*non_stream_tool)[1].tool_call_id == "call_1" && (*non_stream_tool)[1].tool_name == "read_file" &&
             (*non_stream_tool)[2].type == ava::provider::StreamEventType::ToolCallDelta &&
             (*non_stream_tool)[3].type == ava::provider::StreamEventType::ToolCallEnd &&
             (*non_stream_tool)[4].type == ava::provider::StreamEventType::Done && (*non_stream_tool)[4].usage,
         "OpenAI non-stream Responses API parses mixed text and tool calls");
  auto non_stream_reasoning = non_stream_provider.parse_response(
      ava::provider::HttpResponse{.status_code = 200,
                                  .headers = {},
                                  .body = "{\"output\":[{\"type\":\"reasoning\",\"summary\":[{\"type\":"
                                          "\"summary_text\",\"text\":\"think\"}]},{\"type\":\"message\","
                                          "\"content\":[{\"type\":\"output_text\",\"text\":\"done\"}]}],"
                                          "\"usage\":{\"input_tokens\":2,\"output_tokens\":3}}"},
      false);
  expect(non_stream_reasoning && non_stream_reasoning->size() == 5 &&
             (*non_stream_reasoning)[0].type == ava::provider::StreamEventType::ReasoningStart &&
             (*non_stream_reasoning)[1].type == ava::provider::StreamEventType::ReasoningDelta &&
             (*non_stream_reasoning)[1].text == "think" &&
             (*non_stream_reasoning)[2].type == ava::provider::StreamEventType::ReasoningEnd &&
             (*non_stream_reasoning)[3].type == ava::provider::StreamEventType::TextDelta &&
             (*non_stream_reasoning)[3].text == "done" &&
             (*non_stream_reasoning)[4].type == ava::provider::StreamEventType::Done,
         "OpenAI non-stream Responses API parses reasoning summary before answer text");
  auto nested_text = ava::provider::parse_openai_response_text(
      "{\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"nested\"}]}]}");
  expect(nested_text && *nested_text == "nested", "OpenAI non-stream response text parses nested message content");
  auto non_stream_incomplete = non_stream_provider.parse_response(
      ava::provider::HttpResponse{.status_code = 200,
                                  .headers = {},
                                  .body = "{\"output_text\":\"partial\",\"status\":\"incomplete\","
                                          "\"incomplete_details\":{\"reason\":\"content_filter\"}}"},
      false);
  expect(non_stream_incomplete && non_stream_incomplete->size() == 2 &&
             (*non_stream_incomplete)[1].type == ava::provider::StreamEventType::Done &&
             (*non_stream_incomplete)[1].stop_reason == "content_filter",
         "OpenAI non-stream Responses API preserves incomplete stop reason");
  auto non_stream_empty_incomplete = non_stream_provider.parse_response(
      ava::provider::HttpResponse{.status_code = 200,
                                  .headers = {},
                                  .body = "{\"status\":\"incomplete\","
                                          "\"incomplete_details\":{\"reason\":\"max_output_tokens\"}}"},
      false);
  expect(non_stream_empty_incomplete && non_stream_empty_incomplete->size() == 1 &&
             (*non_stream_empty_incomplete)[0].type == ava::provider::StreamEventType::Done &&
             (*non_stream_empty_incomplete)[0].stop_reason == "max_tokens",
         "OpenAI non-stream Responses API accepts empty incomplete terminal response");

  if (request) {
    ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "ok"}});
    auto fake_response = transport.send(*request);
    expect(fake_response && fake_response->body == "ok" && transport.requests().size() == 1 &&
               transport.requests()[0].timeout_ms == request->timeout_ms,
           "fake transport records offline provider request and preserves timeout");
  }
}

void test_openai_incremental_sse_parser()
{
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

void test_openai_compatible_request_helpers()
{
  expect(ava::provider::openai_compatible_join_url("https://compat.example.test///", "/v1/chat") ==
             "https://compat.example.test/v1/chat",
         "OpenAI-compatible URL join trims duplicate slashes");
  expect(ava::provider::openai_compatible_join_url("https://compat.example.test/root", "v1/chat") ==
             "https://compat.example.test/root/v1/chat",
         "OpenAI-compatible URL join inserts a missing separator");
  expect(ava::provider::openai_compatible_join_url("https://compat.example.test/root///", "") ==
             "https://compat.example.test/root",
         "OpenAI-compatible URL join handles empty paths");
  expect(ava::provider::openai_compatible_temperature_json(1.25) == "1.25",
         "OpenAI-compatible temperature serialization is locale-stable");

  auto const converted = ava::provider::chat_completion_tool_json(
      "{\"type\":\"function\",\"name\":\"strict_tool\",\"description\":\"Strict tool\","
      "\"strict\":true,\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}}}}");
  expect(converted &&
             converted->find("\"type\":\"function\",\"function\":{\"name\":\"strict_tool\"") != std::string::npos &&
             converted->find("\"strict\":true") != std::string::npos,
         "OpenAI-compatible request helper converts bare tool schemas and preserves strict mode");

  auto const wrapped = ava::provider::chat_completion_tool_json(
      "{\"type\":\"function\",\"function\":{\"name\":\"wrapped_tool\",\"parameters\":{\"type\":\"object\"}}}");
  expect(wrapped && *wrapped ==
                        "{\"type\":\"function\",\"function\":{\"name\":\"wrapped_tool\","
                        "\"parameters\":{\"type\":\"object\"}}}",
         "OpenAI-compatible request helper preserves already wrapped chat tools");

  auto const malformed_parameters = ava::provider::chat_completion_tool_json(
      "{\"type\":\"function\",\"name\":\"bad_params\",\"parameters\":{\"type\":\"object\"");
  expect(!malformed_parameters && malformed_parameters.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI-compatible request helper rejects malformed parameters");

  auto const missing_name = ava::provider::chat_completion_tool_json("{\"type\":\"function\"}");
  expect(!missing_name && missing_name.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI-compatible request helper rejects tools without names");

  auto const invalid_tools = ava::provider::validate_openai_compatible_tools_json(ava::provider::ProviderRequest{
      .provider_id = "", .model_id = "", .system_prompt = "", .messages = {}, .tools_json = {"not-json"}});
  expect(!invalid_tools && invalid_tools.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI-compatible request helper rejects invalid tool JSON before request construction");

  ava::provider::ChatMessage const assistant{
      .role = "assistant",
      .content = "fallback",
      .content_parts = {ava::provider::ContentPart{.type = ava::provider::ContentPartType::Reasoning,
                                                   .text = "visible reasoning",
                                                   .reasoning_format = "reasoning_content"},
                        ava::provider::ContentPart{.type = ava::provider::ContentPartType::Reasoning,
                                                   .text = "redacted reasoning",
                                                   .reasoning_format = "reasoning_content",
                                                   .redacted = true},
                        ava::provider::ContentPart{.type = ava::provider::ContentPartType::Text, .text = "answer text"},
                        ava::provider::ContentPart{.type = ava::provider::ContentPartType::ToolUse,
                                                   .tool_call_id = "call_read",
                                                   .tool_name = "read_file",
                                                   .input_json = "{\"path\":\"README.md\"}"}}};
  auto const preserved =
      ava::provider::openai_compatible_chat_messages_for_message(assistant, "reasoning_content", true);
  expect(preserved.size() == 1 &&
             preserved[0].find("\"reasoning_content\":\"visible reasoning\"") != std::string::npos &&
             preserved[0].find("redacted reasoning") == std::string::npos &&
             preserved[0].find("\"tool_calls\":[{\"id\":\"call_read\"") != std::string::npos,
         "OpenAI-compatible message helper projects replayable reasoning and tool calls semantically");

  auto const not_preserved =
      ava::provider::openai_compatible_chat_messages_for_message(assistant, "reasoning_content", false);
  expect(not_preserved.size() == 1 && not_preserved[0].find("visible reasoning") == std::string::npos,
         "OpenAI-compatible message helper omits reasoning replay unless enabled");

  ava::provider::ChatMessage const tool_result{
      .role = "user",
      .content = "fallback tool output",
      .content_parts = {ava::provider::ContentPart{
          .type = ava::provider::ContentPartType::ToolResult, .text = "tool output", .tool_call_id = "call_read"}}};
  auto const tool_messages =
      ava::provider::openai_compatible_chat_messages_for_message(tool_result, "reasoning_content", true);
  expect(tool_messages.size() == 1 &&
             tool_messages[0].find("\"role\":\"tool\",\"tool_call_id\":\"call_read\"") != std::string::npos &&
             tool_messages[0].find("fallback tool output") == std::string::npos,
         "OpenAI-compatible message helper serializes native tool results without fallback text");

  ava::provider::OpenAICompatibleProviderOptions const options{
      .default_temperature = 0.5, .preserve_reasoning_content = true, .include_stream_usage = true};
  auto const body = ava::provider::openai_compatible_request_body_json(
      ava::provider::ProviderRequest{
          .provider_id = "compatible",
          .model_id = "model-compatible",
          .system_prompt = "system prompt",
          .messages = {assistant, tool_result},
          .tools_json = {"{\"type\":\"function\",\"name\":\"read_file\",\"parameters\":{\"type\":\"object\"}}"},
          .stream = true,
          .max_output_tokens = 512,
          .reasoning = ava::provider::ProviderReasoningOptions{.type = "enabled", .budget_tokens = 128}},
      options);
  expect(
      body.find("\"model\":\"model-compatible\"") != std::string::npos &&
          body.find("\"role\":\"system\",\"content\":\"system prompt\"") != std::string::npos &&
          body.find("\"max_tokens\":512") != std::string::npos &&
          body.find("\"temperature\":0.5") != std::string::npos &&
          body.find("\"stream_options\":{\"include_usage\":true}") != std::string::npos &&
          body.find("\"thinking\":{\"type\":\"enabled\",\"budget_tokens\":128,\"keep\":\"all\"}") != std::string::npos,
      "OpenAI-compatible request body helper serializes session, generation, and reasoning options");
}

void test_openai_compatible_provider_contract()
{
  ava::provider::OpenAICompatibleProvider const provider(
      ava::provider::OpenAICompatibleProviderOptions{.base_url = "https://compat.example.test/api",
                                                     .chat_completions_path = "/v1/chat/completions",
                                                     .provider_name = "Compat",
                                                     .reasoning_format = "reasoning_content",
                                                     .user_agent = "CompatAgent/1.0",
                                                     .default_temperature = 1.0,
                                                     .preserve_reasoning_content = true,
                                                     .include_stream_usage = true});
  auto const request = provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "kimi",
          .model_id = "kimi-k2-thinking",
          .system_prompt = "system",
          .messages = {ava::provider::ChatMessage{.role = "user", .content = "hello"},
                       ava::provider::ChatMessage{
                           .role = "assistant",
                           .content = "fallback answer",
                           .content_parts = {ava::provider::ContentPart{
                                                 .type = ava::provider::ContentPartType::Reasoning,
                                                 .text = "prior reasoning",
                                                 .reasoning_format = "reasoning_content"},
                                             ava::provider::ContentPart{.type = ava::provider::ContentPartType::Text,
                                                                        .text = "answer text"},
                                             ava::provider::ContentPart{.type = ava::provider::ContentPartType::ToolUse,
                                                                        .text = "",
                                                                        .tool_call_id = "call_1",
                                                                        .tool_name = "read_file",
                                                                        .input_json = "{\"path\":\"README.md\"}"}}},
                       ava::provider::ChatMessage{.role = "user",
                                                  .content = "fallback tool output",
                                                  .content_parts = {ava::provider::ContentPart{
                                                      .type = ava::provider::ContentPartType::ToolResult,
                                                      .text = "tool output",
                                                      .tool_call_id = "call_1"}}}},
          .tools_json = {"{\"type\":\"function\",\"name\":\"read_file\",\"description\":\"Read\","
                         "\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},"
                         "\"required\":[\"path\"]}}"},
          .stream = true,
          .max_output_tokens = 16000,
          .reasoning = ava::provider::ProviderReasoningOptions{.type = "enabled",
                                                               .budget_tokens = 4096,
                                                               .display = "summarized"}},
      "compat-token");
  expect(request.has_value(), "OpenAI-compatible request builds");
  if (request) {
    expect(request->url == "https://compat.example.test/api/v1/chat/completions",
           "OpenAI-compatible request targets chat completions endpoint");
    expect(request->headers.at("Authorization") == "Bearer compat-token" &&
               request->headers.at("User-Agent") == "CompatAgent/1.0",
           "OpenAI-compatible request includes bearer auth and provider user agent");
    expect(request->body.find("\"model\":\"kimi-k2-thinking\"") != std::string::npos &&
               request->body.find("\"temperature\":1") != std::string::npos &&
               request->body.find("\"stream_options\":{\"include_usage\":true}") != std::string::npos &&
               request->body.find("\"thinking\":{\"type\":\"enabled\",\"budget_tokens\":4096") != std::string::npos &&
               request->body.find("\"display\":\"summarized\"") != std::string::npos &&
               request->body.find("\"keep\":\"all\"") != std::string::npos,
           "OpenAI-compatible request includes model, fixed temperature, stream usage, and Kimi thinking option");
    expect(request->body.find("\"reasoning_content\":\"prior reasoning\"") != std::string::npos,
           "OpenAI-compatible request preserves visible reasoning_content for compatible replay");
    expect(request->body.find("\"tool_calls\":[{\"id\":\"call_1\",\"type\":\"function\"") != std::string::npos &&
               request->body.find("\"role\":\"tool\",\"tool_call_id\":\"call_1\",\"content\":\"tool output\"") !=
                   std::string::npos,
           "OpenAI-compatible request serializes native tool_use/tool_result history");
    expect(request->body.find("fallback tool output") == std::string::npos,
           "OpenAI-compatible request does not insert fallback user text before native tool results");
    expect(request->body.find("\"tools\":[{\"type\":\"function\",\"function\":{\"name\":\"read_file\"") !=
               std::string::npos,
           "OpenAI-compatible request converts Responses-style tool schemas to chat-completions tools");
  }

  auto const invalid_tool =
      provider.build_request(ava::provider::ProviderRequest{.provider_id = "moonshot",
                                                            .model_id = "kimi-k2.6",
                                                            .system_prompt = "",
                                                            .messages = {},
                                                            .tools_json = {"{\"type\":\"function\"}"}},
                             "compat-token");
  expect(!invalid_tool && invalid_tool.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI-compatible request rejects function tools without names");

  auto const invalid_wrapped_tool =
      provider.build_request(ava::provider::ProviderRequest{.provider_id = "moonshot",
                                                            .model_id = "kimi-k2.6",
                                                            .system_prompt = "",
                                                            .messages = {},
                                                            .tools_json = {"{\"type\":\"function\",\"function\":{}}"}},
                             "compat-token");
  expect(!invalid_wrapped_tool && invalid_wrapped_tool.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI-compatible request rejects wrapped tools without function names");

  auto const strict_tool = provider.build_request(
      ava::provider::ProviderRequest{.provider_id = "moonshot",
                                     .model_id = "kimi-k2.6",
                                     .system_prompt = "",
                                     .messages = {},
                                     .tools_json = {"{\"type\":\"function\",\"name\":\"strict_tool\","
                                                    "\"description\":\"uses strict mode\",\"strict\":true,"
                                                    "\"parameters\":{\"type\":\"object\"}}"}},
      "compat-token");
  expect(strict_tool && strict_tool->body.find("\"strict\":true") != std::string::npos,
         "OpenAI-compatible request preserves strict tool schemas");

  auto const invalid_parameters = provider.build_request(
      ava::provider::ProviderRequest{.provider_id = "moonshot",
                                     .model_id = "kimi-k2.6",
                                     .system_prompt = "",
                                     .messages = {},
                                     .tools_json = {"{\"type\":\"function\",\"name\":\"bad_params\","
                                                    "\"parameters\":{\"type\":\"object\""}},
      "compat-token");
  expect(!invalid_parameters && invalid_parameters.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "OpenAI-compatible request rejects malformed tool parameter JSON");

  ava::provider::OpenAICompatibleProvider const kimi_default_reasoning_provider(
      ava::provider::OpenAICompatibleProviderOptions{.base_url = "https://compat.example.test",
                                                     .provider_name = "Kimi",
                                                     .reasoning_format = "reasoning_content",
                                                     .preserve_reasoning_content = true});
  auto const default_reasoning_request = kimi_default_reasoning_provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "kimi",
          .model_id = "kimi-k2-thinking",
          .system_prompt = "",
          .messages = {ava::provider::ChatMessage{
              .role = "assistant",
              .content = "answer",
              .content_parts = {ava::provider::ContentPart{.type = ava::provider::ContentPartType::Reasoning,
                                                           .text = "preserved thinking",
                                                           .reasoning_format = "reasoning_content"}}}},
          .tools_json = {}},
      "compat-token");
  expect(
      default_reasoning_request &&
          default_reasoning_request->body.find("\"reasoning_content\":\"preserved thinking\"") != std::string::npos &&
          default_reasoning_request->body.find("\"thinking\"") == std::string::npos,
      "OpenAI-compatible preserved reasoning replay does not request reasoning after clear/default state");

  ava::provider::OpenAICompatibleProvider const no_preserve_provider(ava::provider::OpenAICompatibleProviderOptions{
      .base_url = "https://compat.example.test", .provider_name = "Compat", .reasoning_format = "reasoning_content"});
  auto const no_preserve_request = no_preserve_provider.build_request(
      ava::provider::ProviderRequest{
          .provider_id = "moonshot",
          .model_id = "kimi-k2.6",
          .system_prompt = "",
          .messages = {ava::provider::ChatMessage{
              .role = "assistant",
              .content = "answer",
              .content_parts = {ava::provider::ContentPart{.type = ava::provider::ContentPartType::Reasoning,
                                                           .text = "private compatible reasoning",
                                                           .reasoning_format = "reasoning_content"}}}},
          .tools_json = {}},
      "compat-token");
  expect(no_preserve_request && no_preserve_request->body.find("private compatible reasoning") == std::string::npos,
         "OpenAI-compatible request only replays reasoning_content when explicitly enabled");
}

void test_openai_compatible_parsing()
{
  auto stream = ava::provider::parse_openai_compatible_sse(
      "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"plan\"}}]}\n\n"
      "data: {\"choices\":[{\"delta\":{\"content\":\"answer\"}}]}\n\n"
      "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\","
      "\"function\":{\"name\":\"read_file\",\"arguments\":\"{}\"}}]}}]}\n\n"
      "data: {\"choices\":[{\"finish_reason\":\"tool_calls\"}],\"usage\":{\"prompt_tokens\":11,"
      "\"completion_tokens\":7,\"total_tokens\":18,\"prompt_tokens_details\":{\"cached_tokens\":3},"
      "\"completion_tokens_details\":{\"reasoning_tokens\":2}}}\n\n"
      "data: [DONE]\n\n");
  expect(stream.has_value(), "OpenAI-compatible SSE parses");
  if (stream) {
    expect(stream->size() == 8, "OpenAI-compatible SSE emits reasoning, text, tool, and done events");
    expect((*stream)[0].type == ava::provider::StreamEventType::ReasoningStart &&
               (*stream)[0].reasoning_format == "reasoning_content",
           "OpenAI-compatible SSE starts reasoning_content block");
    expect((*stream)[1].type == ava::provider::StreamEventType::ReasoningDelta && (*stream)[1].text == "plan",
           "OpenAI-compatible SSE emits reasoning_content delta");
    expect((*stream)[2].type == ava::provider::StreamEventType::ReasoningEnd,
           "OpenAI-compatible SSE closes reasoning before answer text");
    expect((*stream)[3].type == ava::provider::StreamEventType::TextDelta && (*stream)[3].text == "answer",
           "OpenAI-compatible SSE emits answer text");
    expect((*stream)[4].type == ava::provider::StreamEventType::ToolCallStart &&
               (*stream)[4].tool_call_id == "call_1" && (*stream)[4].tool_name == "read_file",
           "OpenAI-compatible SSE emits tool call start");
    expect((*stream)[7].type == ava::provider::StreamEventType::Done && (*stream)[7].usage &&
               (*stream)[7].usage->input_tokens == 11 && (*stream)[7].usage->output_tokens == 7 &&
               (*stream)[7].usage->reasoning_tokens == 2 && (*stream)[7].usage->cache_read_tokens == 3 &&
               (*stream)[7].stop_reason == "tool_calls",
           "OpenAI-compatible SSE done carries usage and normalized tool stop reason");
  }

  ava::provider::OpenAICompatibleProvider const moonshot(ava::provider::OpenAICompatibleProviderOptions{
      .base_url = "https://moonshot.example.test", .provider_name = "Moonshot"});
  auto const non_stream = moonshot.parse_response(
      ava::provider::HttpResponse{.status_code = 200,
                                  .headers = {},
                                  .body = "{\"choices\":[{\"message\":{\"reasoning_content\":\"think\","
                                          "\"content\":\"done\"},\"finish_reason\":\"stop\"}],"
                                          "\"usage\":{\"prompt_tokens\":5,\"completion_tokens\":6,"
                                          "\"total_tokens\":11,\"cached_tokens\":1}}"},
      false);
  expect(non_stream && non_stream->size() == 5 &&
             (*non_stream)[0].type == ava::provider::StreamEventType::ReasoningStart &&
             (*non_stream)[1].type == ava::provider::StreamEventType::ReasoningDelta &&
             (*non_stream)[1].text == "think" && (*non_stream)[3].type == ava::provider::StreamEventType::TextDelta &&
             (*non_stream)[3].text == "done" && (*non_stream)[4].type == ava::provider::StreamEventType::Done &&
             (*non_stream)[4].usage && (*non_stream)[4].usage->cache_read_tokens == 1 &&
             (*non_stream)[4].stop_reason == "completed",
         "OpenAI-compatible non-stream response parses reasoning_content, text, usage, and stop reason");

  auto const filtered = moonshot.parse_response(
      ava::provider::HttpResponse{.status_code = 200,
                                  .headers = {},
                                  .body = "{\"choices\":[{\"finish_reason\":\"content_filter\"}],"
                                          "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":0}}"},
      false);
  expect(filtered && filtered->size() == 1 && (*filtered)[0].type == ava::provider::StreamEventType::Done &&
             (*filtered)[0].stop_reason == "content_filter",
         "OpenAI-compatible non-stream parser treats filtered empty responses as completed provider turns");
  auto const unknown_finish =
      moonshot.parse_response(ava::provider::HttpResponse{.status_code = 200,
                                                          .headers = {},
                                                          .body = "{\"choices\":[{\"message\":{\"content\":\"done\"},"
                                                                  "\"finish_reason\":\"provider_custom\"}]}"},
                              false);
  expect(unknown_finish && unknown_finish->size() == 2 &&
             (*unknown_finish)[1].type == ava::provider::StreamEventType::Done &&
             (*unknown_finish)[1].stop_reason == "provider_custom",
         "OpenAI-compatible non-stream parser preserves unknown finish reasons");

  auto const non_stream_tool = moonshot.parse_response(
      ava::provider::HttpResponse{.status_code = 200,
                                  .headers = {},
                                  .body = "{\"choices\":[{\"message\":{\"tool_calls\":[{"
                                          "\"id\":\"call_9\",\"function\":{\"name\":\"read_file\","
                                          "\"arguments\":\"{}\"}}]},\"finish_reason\":\"tool_calls\"}]}"},
      false);
  expect(non_stream_tool && non_stream_tool->size() == 4 &&
             (*non_stream_tool)[0].type == ava::provider::StreamEventType::ToolCallStart &&
             (*non_stream_tool)[0].tool_call_id == "call_9" && (*non_stream_tool)[0].tool_name == "read_file" &&
             (*non_stream_tool)[1].type == ava::provider::StreamEventType::ToolCallDelta &&
             (*non_stream_tool)[2].type == ava::provider::StreamEventType::ToolCallEnd &&
             (*non_stream_tool)[3].type == ava::provider::StreamEventType::Done &&
             (*non_stream_tool)[3].stop_reason == "tool_calls",
         "OpenAI-compatible non-stream parser emits tool call events");

  auto const malformed = moonshot.parse_response(
      ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"choices\":[]}"}, false);
  expect(!malformed && malformed.error().category() == ava::core::ErrorCategory::Provider,
         "OpenAI-compatible non-stream parser rejects missing messages");

  auto const http_error = moonshot.parse_response(
      ava::provider::HttpResponse{.status_code = 500,
                                  .headers = {},
                                  .body = "{\"error\":{\"message\":\"bad\",\"reasoning_content\":"
                                          "\"secret reasoning\",\"thinking\":\"secret thinking\","
                                          "\"api_key\":\"secret-key\"}}"},
      false);
  expect(!http_error && http_error.error().format().find("[redacted]") != std::string::npos &&
             http_error.error().format().find("secret reasoning") == std::string::npos &&
             http_error.error().format().find("secret thinking") == std::string::npos &&
             http_error.error().format().find("secret-key") == std::string::npos,
         "OpenAI-compatible HTTP errors redact reasoning and credential fields");

  auto sse_error = ava::provider::parse_openai_compatible_sse(
      "data: {\"error\":{\"message\":\"{\\\"reasoning_content\\\":\\\"secret stream reasoning\\\","
      "\\\"api_key\\\":\\\"secret-stream-key\\\"}\"}}\n\n");
  expect(sse_error && sse_error->size() == 1 && (*sse_error)[0].type == ava::provider::StreamEventType::Error &&
             (*sse_error)[0].error_message.find("[redacted]") != std::string::npos &&
             (*sse_error)[0].error_message.find("secret stream reasoning") == std::string::npos &&
             (*sse_error)[0].error_message.find("secret-stream-key") == std::string::npos,
         "OpenAI-compatible SSE errors redact reasoning and credential fields");
  auto malformed_compatible = ava::provider::parse_openai_compatible_sse("data: {not-json}\n\n");
  expect(malformed_compatible && malformed_compatible->size() == 1 &&
             (*malformed_compatible)[0].type == ava::provider::StreamEventType::Error,
         "OpenAI-compatible parser does not add truncation error after malformed SSE data");

  ava::provider::OpenAICompatibleProvider const parser_provider(ava::provider::OpenAICompatibleProviderOptions{
      .base_url = "https://compat.example.test", .reasoning_format = "custom_reasoning"});
  auto parser = parser_provider.create_stream_parser();
  auto part_one = parser->append("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"pl");
  expect(part_one && part_one->empty(), "OpenAI-compatible parser buffers partial SSE lines");
  auto part_two = parser->append(
      "an\"}}]}\n\n"
      "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,"
      "\"id\":\"call_2\",\"function\":{\"name\":\"grep\","
      "\"arguments\":\"{}\"}}]}}]}\n\n");
  expect(part_two && part_two->size() == 4 && (*part_two)[0].type == ava::provider::StreamEventType::ReasoningStart &&
             (*part_two)[0].reasoning_format == "custom_reasoning" &&
             (*part_two)[1].type == ava::provider::StreamEventType::ReasoningDelta &&
             (*part_two)[2].type == ava::provider::StreamEventType::ToolCallStart &&
             (*part_two)[2].tool_call_id == "call_2" &&
             (*part_two)[3].type == ava::provider::StreamEventType::ToolCallDelta,
         "OpenAI-compatible incremental parser preserves reasoning and tool state across chunks");
  auto flushed = parser->finish();
  expect(flushed && flushed->size() == 3 && (*flushed)[0].type == ava::provider::StreamEventType::ToolCallEnd &&
             (*flushed)[1].type == ava::provider::StreamEventType::ReasoningEnd &&
             (*flushed)[2].type == ava::provider::StreamEventType::Error,
         "OpenAI-compatible parser flushes open tool calls and reasoning before reporting truncated streams");
  auto second_finish = parser->finish();
  expect(second_finish && second_finish->empty(), "OpenAI-compatible parser finish resets terminal state");

  auto const error = moonshot.parse_response(
      ava::provider::HttpResponse{.status_code = 400, .headers = {}, .body = "Your request exceeded model token limit"},
      true);
  expect(!error && error.error().format().find("provider_error_kind: context_overflow") != std::string::npos,
         "OpenAI-compatible HTTP errors reuse normalized context-overflow classification");
}

void test_builtin_provider_registry()
{
  auto registry = ava::provider::builtin_provider_registry();
  expect(registry.contains("openai"), "builtin provider registry contains OpenAI");
  expect(registry.contains("kimi") && registry.contains("moonshot") && registry.contains("openrouter"),
         "builtin provider registry contains OpenAI-compatible provider shims");
  auto provider = registry.create("openai");
  expect(provider.has_value() && *provider, "builtin provider registry creates OpenAI provider");
  auto kimi = registry.create("kimi");
  expect(kimi.has_value() && *kimi, "builtin provider registry creates Kimi provider shim");

  auto missing = registry.create("missing-provider");
  expect(!missing && missing.error().category() == ava::core::ErrorCategory::NotFound,
         "provider registry rejects unknown providers");
}

}  // namespace

void run_provider_openai_tests()
{
  test_openai_provider_contract();
  test_openai_incremental_sse_parser();
  test_openai_compatible_request_helpers();
  test_openai_compatible_provider_contract();
  test_openai_compatible_parsing();
  test_builtin_provider_registry();
}
