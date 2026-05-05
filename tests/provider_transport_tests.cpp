#include <string>

#include "ava/provider/curl_transport_protocol.h"
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

}  // namespace

void run_provider_transport_tests()
{
  test_curl_config_escape();
  test_build_curl_config();
  test_parse_curl_output_without_headers();
  test_parse_curl_output_with_headers();
  test_parse_curl_output_missing_status();
}
