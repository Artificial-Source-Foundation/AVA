#include <string>
#include <utility>

#include "ava/core/error.h"
#include "ava/provider/provider_error_support.h"
#include "tests/support/test_harness.h"

namespace {

ava::provider::HttpResponse response(int status_code, std::string body = {})
{
  return ava::provider::HttpResponse{.status_code = status_code, .headers = {}, .body = std::move(body)};
}

void test_text_normalization_helpers()
{
  expect(ava::provider::detail::lower_copy("MiXeD 123") == "mixed 123", "provider error helper lowercases text");
  expect(ava::provider::detail::has_any("provider context limit", {"quota", "limit"}),
         "provider error helper finds any matching needle");
  expect(!ava::provider::detail::has_any("provider context limit", {"quota", "billing"}),
         "provider error helper rejects absent needles");
}

void test_error_heuristics()
{
  expect(ava::provider::detail::looks_like_context_overflow("Prompt token length exceeded the maximum context window"),
         "provider error helper detects context overflow messages");
  expect(!ava::provider::detail::looks_like_context_overflow("prompt was rejected by a safety policy"),
         "provider error helper requires both context and overflow terms");
  expect(ava::provider::detail::looks_like_quota("Insufficient_Quota for account billing"),
         "provider error helper detects quota messages");
  expect(ava::provider::detail::looks_like_content_filter("Request was BLOCKED by a content filter"),
         "provider error helper detects content-filter messages");
  expect(ava::provider::detail::looks_like_refusal("The model cannot comply with that request"),
         "provider error helper detects refusal messages");
}

void test_enum_string_conversion()
{
  expect(ava::provider::to_string(ava::provider::StreamEventType::TextDelta) == "text_delta" &&
             ava::provider::to_string(ava::provider::StreamEventType::ReasoningStart) == "reasoning_start" &&
             ava::provider::to_string(ava::provider::StreamEventType::ToolCallEnd) == "tool_call_end" &&
             ava::provider::to_string(ava::provider::StreamEventType::Done) == "done",
         "provider error support keeps stream event string conversions");
  expect(ava::provider::to_string(ava::provider::ProviderErrorKind::Authentication) == "authentication" &&
             ava::provider::to_string(ava::provider::ProviderErrorKind::RateLimited) == "rate_limited" &&
             ava::provider::to_string(ava::provider::ProviderErrorKind::ContextOverflow) == "context_overflow" &&
             ava::provider::to_string(ava::provider::ProviderErrorKind::ContentFilter) == "content_filter" &&
             ava::provider::to_string(ava::provider::ProviderErrorKind::Unknown) == "unknown",
         "provider error support keeps provider error string conversions");
}

void test_classification_precedence()
{
  expect(ava::provider::classify_provider_error(response(200)) == ava::provider::ProviderErrorKind::Unknown,
         "provider error classifier ignores successful HTTP responses");
  expect(ava::provider::classify_provider_error(response(401)) == ava::provider::ProviderErrorKind::Authentication,
         "provider error classifier detects authentication failures");
  expect(ava::provider::classify_provider_error(
             response(400, "prompt has too many tokens for the maximum context length")) ==
             ava::provider::ProviderErrorKind::ContextOverflow,
         "provider error classifier prefers context overflow body semantics over status");
  expect(ava::provider::classify_provider_error(response(429, "billing quota exhausted")) ==
             ava::provider::ProviderErrorKind::Quota,
         "provider error classifier prefers quota body semantics over rate-limit status");
  expect(ava::provider::classify_provider_error(response(429, "slow down")) ==
             ava::provider::ProviderErrorKind::RateLimited,
         "provider error classifier detects ordinary rate limits");
  expect(ava::provider::classify_provider_error(response(400, "blocked by content_filter")) ==
             ava::provider::ProviderErrorKind::ContentFilter,
         "provider error classifier detects content filters");
  expect(ava::provider::classify_provider_error(response(400, "refusal: cannot comply")) ==
             ava::provider::ProviderErrorKind::Refusal,
         "provider error classifier detects refusals");
  expect(ava::provider::classify_provider_error(response(422)) == ava::provider::ProviderErrorKind::InvalidRequest,
         "provider error classifier detects invalid requests");
  expect(ava::provider::classify_provider_error(response(503)) == ava::provider::ProviderErrorKind::Transient,
         "provider error classifier detects transient failures");
}

void test_retry_after_lookup()
{
  auto retry_after = ava::provider::retry_after_header(
      ava::provider::HttpResponse{.status_code = 429, .headers = {{"rEtRy-AfTeR", "12"}}, .body = ""});
  expect(retry_after && *retry_after == "12", "provider error support finds retry-after headers case-insensitively");

  auto missing =
      ava::provider::retry_after_header(ava::provider::HttpResponse{.status_code = 429, .headers = {}, .body = ""});
  expect(!missing, "provider error support returns empty retry-after for missing headers");
}

void test_context_overflow_error_detection()
{
  ava::core::Error direct(ava::core::ErrorCategory::Provider, "Maximum context length exceeded");
  expect(ava::provider::is_context_overflow_error(direct),
         "provider error support detects context overflow in provider error message");

  ava::core::Error contextual(ava::core::ErrorCategory::Provider, "provider failed");
  contextual.with_context("body", "Prompt has too many tokens");
  expect(ava::provider::is_context_overflow_error(contextual),
         "provider error support detects context overflow in provider error context");

  ava::core::Error non_provider(ava::core::ErrorCategory::Io, "Maximum context length exceeded");
  expect(!ava::provider::is_context_overflow_error(non_provider),
         "provider error support ignores non-provider context overflow text");
}

}  // namespace

void run_provider_error_support_tests()
{
  test_text_normalization_helpers();
  test_error_heuristics();
  test_enum_string_conversion();
  test_classification_precedence();
  test_retry_after_lookup();
  test_context_overflow_error_detection();
}
