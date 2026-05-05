#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ava/config/openai_oauth_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

namespace {

std::vector<std::uint8_t> bytes(std::string_view text)
{
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

ava::provider::HttpResponse response(int status, std::string body)
{
  ava::provider::HttpResponse value;
  value.status_code = status;
  value.body = std::move(body);
  return value;
}

std::string token_with_payload(std::string payload)
{
  auto const payload_bytes = bytes(payload);
  return "header." + ava::config::detail::base64_url_encode(payload_bytes) + ".signature";
}

void test_encoding_helpers()
{
  auto const foo = bytes("foo");
  auto const fo = bytes("fo");
  auto const f = bytes("f");
  expect(ava::config::detail::base64_url_encode(foo) == "Zm9v", "OAuth support base64url encodes full triples");
  expect(ava::config::detail::base64_url_encode(fo) == "Zm8", "OAuth support base64url encodes two-byte tails");
  expect(ava::config::detail::base64_url_encode(f) == "Zg", "OAuth support base64url encodes one-byte tails");

  auto decoded = ava::config::detail::base64_url_decode("Zm9v");
  expect(decoded && std::string(decoded->begin(), decoded->end()) == "foo", "OAuth support decodes base64url text");
  expect(!ava::config::detail::base64_url_decode("bad*"),
         "OAuth support rejects base64url text with invalid characters");

  expect(ava::config::detail::url_encode("a b/+~._-") == "a%20b%2F%2B~._-",
         "OAuth support percent-encodes form values");
  expect(ava::config::detail::code_challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") ==
             "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
         "OAuth support computes RFC 7636 S256 PKCE challenge");
}

void test_json_and_token_parsing()
{
  expect(ava::config::detail::is_complete_json_object(R"( {"a":"}","b":{"c":true}} )"),
         "OAuth support accepts complete JSON object text");
  expect(!ava::config::detail::is_complete_json_object(R"({"a":true} trailing)"),
         "OAuth support rejects trailing content after JSON object");
  expect(!ava::config::detail::is_complete_json_object(R"({"a":true)"),
         "OAuth support rejects incomplete JSON object text");

  auto direct = ava::config::detail::parse_oauth_token_response(
      R"({"access_token":"access","expires_at":2000,"account_id":"acct_direct"})", 1000, "fallback_refresh",
      "fallback_account");
  expect(direct && direct->access_token == "access" && direct->refresh_token == "fallback_refresh" &&
             direct->expires_at == 2000 && direct->account_id == "acct_direct",
         "OAuth support parses direct token response fields and refresh fallback");

  auto const org_token = token_with_payload(R"({"organizations":[{"id":"org_123"}]})");
  auto fallback = ava::config::detail::parse_oauth_token_response(
      "{\"access_token\":\"access\",\"expires_in\":120,\"id_token\":\"" + org_token + "\"}", 1000, "", "");
  expect(fallback && fallback->expires_at == 1120 && fallback->account_id == "org_123",
         "OAuth support extracts account fallback from token payload organizations");

  auto malformed = ava::config::detail::parse_oauth_token_response("not json", 1000, "", "");
  expect(!malformed && malformed.error().message().find("malformed JSON") != std::string::npos,
         "OAuth support rejects malformed token JSON");
  auto missing_access = ava::config::detail::parse_oauth_token_response(R"({"refresh_token":"refresh"})", 1000, "", "");
  expect(!missing_access && missing_access.error().message().find("access token") != std::string::npos,
         "OAuth support rejects token responses without access tokens");
}

void test_token_post_and_random_helpers()
{
  ava::tests::FakeTransport transport({response(401, R"({"error":"denied"})")});
  auto posted = ava::config::detail::post_oauth_token_form("grant_type=test", transport,
                                                           "https://auth.example.test/token", "token post failed");
  expect(!posted && posted.error().format().find("status: 401") != std::string::npos,
         "OAuth support reports token-form HTTP failures with status context");
  auto const& requests = transport.requests();
  expect(requests.size() == 1 && requests.front().method == "POST" &&
             requests.front().url == "https://auth.example.test/token" &&
             requests.front().headers.at("Content-Type") == "application/x-www-form-urlencoded" &&
             requests.front().body == "grant_type=test",
         "OAuth support posts token forms with expected method, headers, URL, and body");

  auto random = ava::config::detail::random_token(32);
  expect(random && random->size() == 43, "OAuth support generates 32-byte base64url tokens without padding");
  if (random) {
    expect(std::all_of(random->begin(), random->end(),
                       [](char ch) {
                         return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                                ch == '-' || ch == '_';
                       }),
           "OAuth support generated tokens use only base64url characters");
  }
}

}  // namespace

void run_config_openai_oauth_support_tests()
{
  test_encoding_helpers();
  test_json_and_token_parsing();
  test_token_post_and_random_helpers();
}
