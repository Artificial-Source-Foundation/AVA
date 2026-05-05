#include <string>
#include <vector>

#include "ava/provider/curl_transport_protocol.h"
#include "ava/provider/retry_policy.h"
#include "tests/support/test_harness.h"

namespace {

void test_curl_config_escape()
{
  expect(ava::provider::detail::curl_config_escape("a\\b\"c\nd\re") == "a\\\\b\\\"c d e",
         "curl config escape protects config syntax and flattens newlines");
}

void test_build_curl_config()
{
  auto const config = ava::provider::detail::build_curl_config(
      ava::provider::HttpRequest{.method = "PATCH",
                                 .url = "https://example.test/a\"b\nc",
                                 .headers = {{"Authorization", "Bearer \"token\""}, {"X-Test", "two\nlines"}},
                                 .body = "{}",
                                 .timeout_ms = 2500,
                                 .follow_redirects = false,
                                 .include_response_headers = true,
                                 .resolve_hosts = {"example.test:443:127.0.0.1"}},
      "/tmp/body\"file");

  expect(config.find("url = \"https://example.test/a\\\"b c\"") != std::string::npos, "curl config escapes URL values");
  expect(config.find("request = \"PATCH\"") != std::string::npos, "curl config records request method");
  expect(config.find("location") == std::string::npos, "curl config omits redirects when disabled");
  expect(config.find("include") != std::string::npos, "curl config can request response headers");
  expect(config.find("resolve = \"example.test:443:127.0.0.1\"") != std::string::npos &&
             config.find("noproxy = \"*\"") != std::string::npos,
         "curl config records host resolution overrides");
  expect(config.find("max-time = \"2.500000\"") != std::string::npos, "curl config converts timeouts to seconds");
  expect(config.find("header = \"Authorization: Bearer \\\"token\\\"\"") != std::string::npos &&
             config.find("header = \"X-Test: two lines\"") != std::string::npos,
         "curl config escapes headers and flattens header newlines");
  expect(config.find("data-binary = \"@/tmp/body\\\"file\"") != std::string::npos,
         "curl config references the temporary request body path");
}

void test_parse_curl_output_without_headers()
{
  auto parsed = ava::provider::detail::parse_curl_output("response body\nAVA_HTTP_STATUS:201", false);
  expect(parsed.has_value(), "curl output parser accepts body plus status marker");
  if (!parsed) return;
  expect(parsed->status_code == 201, "curl output parser preserves HTTP status code");
  expect(parsed->headers.empty(), "curl output parser omits headers when not requested");
  expect(parsed->body == "response body", "curl output parser strips final status marker from body");
}

void test_parse_curl_output_with_headers()
{
  auto parsed = ava::provider::detail::parse_curl_output(
      "HTTP/1.1 100 Continue\r\nTemp: ignored\r\n\r\n"
      "HTTP/2 200\r\nContent-Type: application/json\r\nX-Test:   value\r\n\r\n"
      "{\"ok\":true}\nAVA_HTTP_STATUS:200",
      true);
  expect(parsed.has_value(), "curl output parser accepts response headers");
  if (!parsed) return;
  expect(parsed->status_code == 200, "curl output parser keeps final HTTP status");
  expect(parsed->headers.size() == 2 && parsed->headers.at("Content-Type") == "application/json" &&
             parsed->headers.at("X-Test") == "value",
         "curl output parser keeps headers from the final response block");
  expect(parsed->body == "{\"ok\":true}", "curl output parser returns the final response body");
}

void test_parse_curl_output_missing_status()
{
  auto parsed = ava::provider::detail::parse_curl_output("response body", false);
  expect(!parsed && parsed.error().category() == ava::core::ErrorCategory::Provider,
         "curl output parser rejects output without a status marker");
}

void test_retry_policy_helpers()
{
  expect(ava::provider::detail::is_retryable_kind(ava::provider::ProviderErrorKind::RateLimited) &&
             ava::provider::detail::is_retryable_kind(ava::provider::ProviderErrorKind::Transient) &&
             !ava::provider::detail::is_retryable_kind(ava::provider::ProviderErrorKind::InvalidRequest),
         "retry policy helper classifies retryable provider errors");
  expect(ava::provider::detail::is_retryable_transport_error(ava::core::Error(ava::core::ErrorCategory::Io, "io")) &&
             !ava::provider::detail::is_retryable_transport_error(
                 ava::core::Error(ava::core::ErrorCategory::Provider, "provider")),
         "retry policy helper retries transport IO errors only");

  ava::provider::RetryOptions options;
  options.base_delay_ms = 125;
  expect(ava::provider::detail::exponential_delay_ms(options, 1) == 125 &&
             ava::provider::detail::exponential_delay_ms(options, 4) == 1000,
         "retry policy helper computes exponential delays");
  options.base_delay_ms = 0;
  expect(ava::provider::detail::exponential_delay_ms(options, 3) == 0, "retry policy helper honors disabled delay");

  auto capped_retry_after = ava::provider::detail::retry_after_ms(
      ava::provider::HttpResponse{.status_code = 429, .headers = {{"Retry-After", " 120 "}}, .body = ""}, 60'000);
  auto missing_retry_after = ava::provider::detail::retry_after_ms(
      ava::provider::HttpResponse{.status_code = 429, .headers = {{"Retry-After", "soon"}}, .body = ""}, 60'000);
  expect(capped_retry_after && *capped_retry_after == 60'000 && !missing_retry_after,
         "retry policy helper parses and caps Retry-After seconds");

  bool option_canceled = false;
  bool transport_canceled = false;
  ava::provider::RetryOptions cancel_options;
  cancel_options.cancel_requested = [&] { return option_canceled; };
  expect(!ava::provider::detail::retry_cancel_requested(cancel_options, [&] { return transport_canceled; }),
         "retry policy helper reports no cancellation when both callbacks are false");
  transport_canceled = true;
  expect(ava::provider::detail::retry_cancel_requested(cancel_options, [&] { return transport_canceled; }),
         "retry policy helper honors per-call cancellation");
  transport_canceled = false;
  option_canceled = true;
  expect(ava::provider::detail::retry_cancel_requested(cancel_options, nullptr),
         "retry policy helper honors retry option cancellation");

  std::vector<ava::provider::RetryOptions::Event> events;
  ava::provider::RetryOptions event_options;
  event_options.on_retry = [&](ava::provider::RetryOptions::Event const& event) -> ava::core::VoidResult {
    events.push_back(event);
    return {};
  };
  auto published =
      ava::provider::detail::publish_retry_event(event_options, 2, 4, -5, 0, "rate_limited", 429, true, true);
  expect(published.has_value() && events.size() == 1 && events[0].attempt == 2 && events[0].max_attempts == 4 &&
             events[0].delay_ms == 0 && events[0].reason == "rate_limited" && events[0].status_code == 429 &&
             events[0].streaming && events[0].countdown_tick,
         "retry policy helper publishes normalized retry events");

  auto no_sleep = ava::provider::detail::sleep_before_retry(event_options, 2, 4, 0, "rate_limited", 429, false);
  expect(no_sleep.has_value(), "retry policy helper returns immediately for zero delay");
}

}  // namespace

void run_provider_transport_tests()
{
  test_curl_config_escape();
  test_build_curl_config();
  test_parse_curl_output_without_headers();
  test_parse_curl_output_with_headers();
  test_parse_curl_output_missing_status();
  test_retry_policy_helpers();
}
