#include <map>
#include <string>

#include "ava/tools/webfetch_support.h"
#include "tests/support/test_harness.h"

namespace {

void test_webfetch_url_validation()
{
  auto valid = ava::tools::detail::validate_webfetch_url("HTTPS://Example.COM:8443/path?q=1");
  expect(valid && valid->url == "HTTPS://Example.COM:8443/path?q=1" && valid->host == "example.com" &&
             valid->port == "8443",
         "webfetch support validates http URLs and lowercases the host");

  auto default_https = ava::tools::detail::validate_webfetch_url("https://example.com/path");
  auto default_http = ava::tools::detail::validate_webfetch_url("http://example.com/path");
  expect(default_https && default_https->port == "443" && default_http && default_http->port == "80",
         "webfetch support derives default ports from URL scheme");

  auto digit_domain = ava::tools::detail::validate_webfetch_url("https://1.be/");
  expect(digit_domain && digit_domain->host == "1.be",
         "webfetch support allows digit-leading DNS names that are not IP aliases");

  expect(!ava::tools::detail::validate_webfetch_url("file:///etc/passwd"),
         "webfetch support rejects unsupported URL schemes");
  expect(!ava::tools::detail::validate_webfetch_url("https://user@example.com/"),
         "webfetch support rejects URL userinfo");
  expect(!ava::tools::detail::validate_webfetch_url("https://[::1]/"),
         "webfetch support rejects bracketed IP literal hosts");
  expect(!ava::tools::detail::validate_webfetch_url("https://example.com:not-a-port/"),
         "webfetch support rejects invalid ports");
  expect(!ava::tools::detail::validate_webfetch_url(std::string("https://example.com/\n")),
         "webfetch support rejects control bytes in URLs");
  expect(!ava::tools::detail::validate_webfetch_url("https://localhost/") &&
             !ava::tools::detail::validate_webfetch_url("https://service.local/"),
         "webfetch support rejects local hostnames");
  expect(!ava::tools::detail::validate_webfetch_url("http://127.0.0.1:8080") &&
             !ava::tools::detail::validate_webfetch_url("http://127.1:8080") &&
             !ava::tools::detail::validate_webfetch_url("http://2130706433/") &&
             !ava::tools::detail::validate_webfetch_url("http://0x7f000001/"),
         "webfetch support rejects IPv4 literals and aliases before transport use");
}

void test_webfetch_ip_helpers_and_bounds()
{
  expect(ava::tools::detail::numeric_ipv4_literal_or_alias("127.1") &&
             ava::tools::detail::numeric_ipv4_literal_or_alias("2130706433") &&
             ava::tools::detail::numeric_ipv4_literal_or_alias("0x7f000001") &&
             !ava::tools::detail::numeric_ipv4_literal_or_alias("1.be"),
         "webfetch support detects numeric IPv4 aliases without rejecting digit-leading DNS");
  expect(ava::tools::detail::private_or_non_global_ipv4(0x0A000001UL) &&
             ava::tools::detail::private_or_non_global_ipv4(0x7F000001UL) &&
             ava::tools::detail::private_or_non_global_ipv4(0xC0000201UL) &&
             !ava::tools::detail::private_or_non_global_ipv4(0x08080808UL),
         "webfetch support classifies private and reserved IPv4 address ranges");

  expect(ava::tools::detail::normalized_webfetch_max_bytes(0) == 1024 * 1024 &&
             ava::tools::detail::normalized_webfetch_max_bytes(123) == 123 &&
             ava::tools::detail::normalized_webfetch_max_bytes(99 * 1024 * 1024) ==
                 ava::tools::detail::kMaxWebFetchBytes,
         "webfetch support clamps requested byte limits");
  expect(ava::tools::detail::normalized_webfetch_timeout_ms(0) == 30000 &&
             ava::tools::detail::normalized_webfetch_timeout_ms(50) == 1000 &&
             ava::tools::detail::normalized_webfetch_timeout_ms(999999) ==
                 ava::tools::detail::kMaxWebFetchTimeoutMs,
         "webfetch support clamps requested timeouts");
}

void test_webfetch_response_helpers()
{
  std::map<std::string, std::string> headers{{"Content-Type", "text/html; charset=utf-8"}, {"X-Test", "ok"}};
  expect(ava::tools::detail::webfetch_header_value(headers, "content-type") == "text/html; charset=utf-8" &&
             ava::tools::detail::webfetch_header_value(headers, "x-test") == "ok" &&
             ava::tools::detail::webfetch_header_value(headers, "missing").empty(),
         "webfetch support looks up response headers case-insensitively");

  expect(!ava::tools::detail::webfetch_looks_binary("{\"ok\":true}", "application/json") &&
             !ava::tools::detail::webfetch_looks_binary("<html></html>", "text/html") &&
             ava::tools::detail::webfetch_looks_binary("png", "image/png") &&
             ava::tools::detail::webfetch_looks_binary(std::string("abc\0def", 7), "text/plain"),
         "webfetch support detects binary response content conservatively");
}

}  // namespace

void run_webfetch_support_tests()
{
  test_webfetch_url_validation();
  test_webfetch_ip_helpers_and_bounds();
  test_webfetch_response_helpers();
}
