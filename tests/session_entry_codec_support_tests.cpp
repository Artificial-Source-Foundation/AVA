#include <string>

#include "ava/session/session_entry_codec_support.h"
#include "tests/support/test_harness.h"

namespace {

void test_hex_and_utf8_helpers()
{
  expect(ava::session::detail::hex_value('0') == 0 && ava::session::detail::hex_value('f') == 15 &&
             ava::session::detail::hex_value('A') == 10 && ava::session::detail::hex_value('x') == -1,
         "session entry codec support parses hex nibbles");

  std::string utf8;
  ava::session::detail::append_utf8(utf8, 0x00A9);
  ava::session::detail::append_utf8_codepoint(utf8, 0x1F600);
  expect(utf8 == std::string("\xC2\xA9\xF0\x9F\x98\x80", 6), "session entry codec support encodes UTF-8 codepoints");
}

void test_version_extraction()
{
  auto missing = ava::session::detail::extract_entry_version("{\"id\":\"entry\"}");
  expect(missing && !*missing, "session entry codec support treats missing version as legacy");

  auto current = ava::session::detail::extract_entry_version("{\"version\":2,\"id\":\"entry\"}");
  expect(current && *current && **current == 2, "session entry codec support parses integer versions");

  auto spaced = ava::session::detail::extract_entry_version("{\"version\":-1 }");
  expect(spaced && *spaced && **spaced == -1, "session entry codec support accepts signed integer versions");

  auto malformed = ava::session::detail::extract_entry_version("{\"version\":2x}");
  expect(!malformed && malformed.error().message() == "invalid session entry version",
         "session entry codec support rejects version suffixes");

  auto overflow = ava::session::detail::extract_entry_version("{\"version\":92233720368547758070}");
  expect(!overflow && overflow.error().message() == "invalid session entry version",
         "session entry codec support rejects version overflow");
}

void test_string_extraction()
{
  auto const line = "{\"text\":\"quote: \\\" slash: \\\\ newline: \\n tab: \\t solidus: \\/\",\"other\":\"ignored\"}";
  expect(ava::session::detail::extract_json_string(line, "text") ==
             std::string("quote: \" slash: \\ newline: \n tab: \t solidus: /"),
         "session entry codec support decodes JSON string escapes");

  auto const surrogate = ava::session::detail::extract_json_string("{\"text\":\"\\uD83D\\uDE00\"}", "text");
  expect(surrogate == std::string("\xF0\x9F\x98\x80", 4), "session entry codec support decodes valid surrogate pairs");

  auto const replacement = ava::session::detail::extract_json_string("{\"text\":\"\\uD83D\"}", "text");
  expect(replacement == std::string("\xEF\xBF\xBD", 3),
         "session entry codec support replaces dangling surrogate escapes");

  expect(ava::session::detail::extract_json_string("{\"text\":\"\\u12zz\"}", "text").empty(),
         "session entry codec support rejects malformed unicode escapes");
  expect(ava::session::detail::extract_json_string("{\"text\":\"unterminated}", "text").empty(),
         "session entry codec support rejects unterminated strings");
}

void test_object_extraction()
{
  auto const line =
      "{\"id\":\"entry\",\"data\":{\"nested\":{\"text\":\"brace } and quote \\\" ok\"},\"value\":1},\"next\":true}";
  expect(ava::session::detail::json_object_field_has_object_value(line, "data"),
         "session entry codec support detects object-valued fields");
  expect(ava::session::detail::extract_json_object(line, "data") ==
             "{\"nested\":{\"text\":\"brace } and quote \\\" ok\"},\"value\":1}",
         "session entry codec support extracts nested objects with quoted braces");

  expect(!ava::session::detail::json_object_field_has_object_value("{\"data\":\"not object\"}", "data"),
         "session entry codec support rejects non-object values");
  expect(ava::session::detail::extract_json_object("{\"data\":{\"open\":true", "data") == "{}",
         "session entry codec support falls back for unterminated objects");
  expect(ava::session::detail::extract_json_object("{\"id\":\"entry\"}", "data") == "{}",
         "session entry codec support returns an empty object for missing objects");
}

void test_json_value_delimiters()
{
  expect(ava::session::detail::is_json_value_delimiter(',') && ava::session::detail::is_json_value_delimiter('}') &&
             ava::session::detail::is_json_value_delimiter(' ') && !ava::session::detail::is_json_value_delimiter('x'),
         "session entry codec support identifies JSON value delimiters");
}

}  // namespace

void run_session_entry_codec_support_tests()
{
  test_hex_and_utf8_helpers();
  test_version_extraction();
  test_string_extraction();
  test_object_extraction();
  test_json_value_delimiters();
}
