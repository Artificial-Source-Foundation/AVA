#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "ava/core/string_utils.hpp"

TEST_CASE("lowercase_ascii_char lowercases ASCII uppercase only", "[ava_tools]") {
  REQUIRE(ava::core::lowercase_ascii_char(static_cast<unsigned char>('A')) == 'a');
  REQUIRE(ava::core::lowercase_ascii_char(static_cast<unsigned char>('Z')) == 'z');
  REQUIRE(ava::core::lowercase_ascii_char(static_cast<unsigned char>('a')) == 'a');
  REQUIRE(ava::core::lowercase_ascii_char(static_cast<unsigned char>('0')) == '0');

  const auto non_ascii = static_cast<unsigned char>(0xC4);
  REQUIRE(static_cast<unsigned char>(ava::core::lowercase_ascii_char(non_ascii)) == non_ascii);
}

TEST_CASE("lowercase_ascii std::string overload lowercases explicit ASCII letters only", "[ava_tools]") {
  const std::string input = "AZ az 09 -_[]`~";
  const auto lowered = ava::core::lowercase_ascii(input);
  REQUIRE(lowered == "az az 09 -_[]`~");
}

TEST_CASE("lowercase_ascii string_view overload lowercases ASCII input", "[ava_tools]") {
  const std::string_view input = "HeLLo, StrIng_VieW!";
  REQUIRE(ava::core::lowercase_ascii(input) == "hello, string_view!");
}

TEST_CASE("lowercase_ascii const char* overload supports literals and null pointers", "[ava_tools]") {
  const char* literal = "HeLLo, AVA!";
  REQUIRE(ava::core::lowercase_ascii(literal) == "hello, ava!");

  const char* null_value = nullptr;
  REQUIRE(ava::core::lowercase_ascii(null_value).empty());
}

TEST_CASE("lowercase_ascii keeps non-ASCII UTF-8 bytes unchanged", "[ava_tools]") {
  const std::string input = "MIX:\xC3\x84\xC3\x9F";
  const auto lowered = ava::core::lowercase_ascii(input);
  REQUIRE(lowered == std::string{"mix:\xC3\x84\xC3\x9F"});
}

TEST_CASE("trim_copy removes leading and trailing whitespace only", "[ava_tools]") {
  REQUIRE(ava::core::trim_copy(std::string{"\t  hello   world  \n"}) == "hello   world");
  REQUIRE(ava::core::trim_copy(std::string{"\r\n\t  "}).empty());
  REQUIRE(ava::core::trim_copy(std::string{"already-trimmed"}) == "already-trimmed");
}

TEST_CASE("split_words handles multi-word input and mixed spacing", "[ava_tools]") {
  const std::string input = "  alpha   beta\tgamma\n delta  ";
  const auto words = ava::core::split_words(input);
  REQUIRE(words.size() == 4);
  REQUIRE(words.at(0) == "alpha");
  REQUIRE(words.at(1) == "beta");
  REQUIRE(words.at(2) == "gamma");
  REQUIRE(words.at(3) == "delta");
}

TEST_CASE("split_words returns empty for empty or whitespace-only input", "[ava_tools]") {
  REQUIRE(ava::core::split_words(std::string_view{}).empty());
  REQUIRE(ava::core::split_words(" \t\n\r ").empty());
}

TEST_CASE("split_words preserves non-ASCII UTF-8 bytes in tokens", "[ava_tools]") {
  const std::string input = "\xC3\x84"
                            "rger  "
                            "\xC3\xBC"
                            "ber  caf"
                            "\xC3\xA9";
  const auto words = ava::core::split_words(input);
  REQUIRE(words.size() == 3);
  REQUIRE(words.at(0) == std::string_view{"\xC3\x84" "rger"});
  REQUIRE(words.at(1) == std::string_view{"\xC3\xBC" "ber"});
  REQUIRE(words.at(2) == std::string_view{"caf" "\xC3\xA9"});
}
