#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ava::core::json::detail {

[[nodiscard]] int hex_value(char ch);
void append_utf8(std::string& out, int codepoint);
[[nodiscard]] std::optional<int> parse_hex_code_unit(std::string_view text, std::size_t hex_start);
[[nodiscard]] bool is_high_surrogate(int code_unit);
[[nodiscard]] bool is_low_surrogate(int code_unit);
[[nodiscard]] bool is_hex_digit(char ch);
[[nodiscard]] bool is_json_whitespace(char ch);

class JsonValidator {
 public:
  explicit JsonValidator(std::string_view value);

  [[nodiscard]] bool valid_object();

 private:
  void skip_ws();
  [[nodiscard]] bool consume(char expected);
  [[nodiscard]] bool consume_literal(std::string_view literal);
  [[nodiscard]] bool parse_value();
  [[nodiscard]] bool parse_object();
  [[nodiscard]] bool parse_array();
  [[nodiscard]] bool parse_string();
  [[nodiscard]] bool parse_number();

  std::string_view value_;
  std::size_t offset_ = 0;
};

[[nodiscard]] std::optional<std::string> parse_string_at(std::string_view text, std::size_t start);
[[nodiscard]] std::optional<std::size_t> string_literal_end(std::string_view text, std::size_t start);
[[nodiscard]] std::optional<std::string> parse_balanced(std::string_view text, std::size_t start, char open,
                                                        char close);

}  // namespace ava::core::json::detail
