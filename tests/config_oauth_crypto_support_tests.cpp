#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "ava/config/oauth_crypto_support.h"
#include "tests/support/test_harness.h"

namespace {

std::vector<std::uint8_t> bytes(std::string_view text)
{
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

void test_base64_url_round_trip()
{
  expect(ava::config::detail::base64_url_encode(bytes("foo")) == "Zm9v",
         "OAuth crypto support base64url encodes full triples");
  expect(ava::config::detail::base64_url_encode(bytes("fo")) == "Zm8",
         "OAuth crypto support base64url encodes two-byte tails");
  expect(ava::config::detail::base64_url_encode(bytes("f")) == "Zg",
         "OAuth crypto support base64url encodes one-byte tails");

  auto decoded = ava::config::detail::base64_url_decode("Zm9v");
  expect(decoded && std::string(decoded->begin(), decoded->end()) == "foo",
         "OAuth crypto support decodes base64url text");
  expect(!ava::config::detail::base64_url_decode("bad*"),
         "OAuth crypto support rejects base64url text with invalid characters");
}

void test_pkce_and_url_encoding()
{
  expect(ava::config::detail::code_challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") ==
             "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
         "OAuth crypto support computes RFC 7636 S256 PKCE challenge");
  expect(ava::config::detail::url_encode("a b/+~._-") == "a%20b%2F%2B~._-",
         "OAuth crypto support percent-encodes form values");
}

void test_random_token_shape()
{
  auto random = ava::config::detail::random_token(32);
  expect(random && random->size() == 43, "OAuth crypto support generates 32-byte base64url tokens without padding");
  if (random) {
    expect(std::all_of(random->begin(), random->end(),
                       [](char ch) {
                         return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                                ch == '-' || ch == '_';
                       }),
           "OAuth crypto support generated tokens use only base64url characters");
  }
}

}  // namespace

void run_config_oauth_crypto_support_tests()
{
  test_base64_url_round_trip();
  test_pkce_and_url_encoding();
  test_random_token_shape();
}
