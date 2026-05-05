#include <string>

#include "ava/core/json.h"
#include "ava/core/json_support.h"
#include "tests/support/test_harness.h"

namespace {

void test_json_support_hex_and_utf8_helpers()
{
  expect(ava::core::json::detail::hex_value('a') == 10 && ava::core::json::detail::hex_value('F') == 15 &&
             ava::core::json::detail::hex_value('x') == -1,
         "json support decodes hex digit values");

  auto code_unit = ava::core::json::detail::parse_hex_code_unit("0041", 0);
  auto bad_unit = ava::core::json::detail::parse_hex_code_unit("00xz", 0);
  expect(code_unit && *code_unit == 0x41 && !bad_unit, "json support parses bounded unicode code units");

  std::string utf8;
  ava::core::json::detail::append_utf8(utf8, 0x1F642);
  expect(utf8 == std::string("\xF0\x9F\x99\x82"), "json support encodes non-BMP codepoints as UTF-8");
}

void test_json_support_parse_string_at()
{
  auto parsed = ava::core::json::detail::parse_string_at(R"("line\n\u0041\uD83D\uDE42")", 0);
  expect(parsed && *parsed == std::string("line\nA\xF0\x9F\x99\x82"),
         "json support decodes escapes and surrogate pairs in strings");

  auto replacement = ava::core::json::detail::parse_string_at(R"("\uD83D")", 0);
  expect(replacement && *replacement == std::string("\xEF\xBF\xBD"), "json support replaces unmatched high surrogates");

  expect(!ava::core::json::detail::parse_string_at(R"("\q")", 0), "json support rejects unsupported string escapes");
}

void test_json_support_string_end_and_balanced_scanning()
{
  auto end = ava::core::json::detail::string_literal_end(R"("a\"b" tail)", 0);
  expect(end && *end == 5, "json support finds string literal ends while skipping escaped quotes");

  auto balanced = ava::core::json::detail::parse_balanced(R"({"a":"}","b":[{"c":1}]} tail)", 0, '{', '}');
  expect(balanced && *balanced == R"({"a":"}","b":[{"c":1}]})",
         "json support scans balanced objects while ignoring delimiters inside strings");

  expect(!ava::core::json::detail::parse_balanced(R"({"a":[1,2])", 0, '{', '}'),
         "json support rejects unbalanced object text");
}

void test_json_support_validator()
{
  expect(
      ava::core::json::detail::JsonValidator(R"({"a":[true,false,null,-12.3e+4],"b":{"c":"\u0041"}})").valid_object(),
      "json support validates nested JSON objects");
  expect(!ava::core::json::detail::JsonValidator("[1,2,3]").valid_object(),
         "json support validator requires an object root");
  expect(!ava::core::json::detail::JsonValidator(R"({"n":01})").valid_object(),
         "json support rejects leading-zero numbers");
  expect(!ava::core::json::detail::JsonValidator(std::string("{\"x\":\"bad\n\"}")).valid_object(),
         "json support rejects raw control characters in strings");
  expect(ava::core::json::is_valid_object(R"({"ok":true})"),
         "json public object validation delegates to support validator");
}

}  // namespace

void run_json_support_tests()
{
  test_json_support_hex_and_utf8_helpers();
  test_json_support_parse_string_at();
  test_json_support_string_end_and_balanced_scanning();
  test_json_support_validator();
}
