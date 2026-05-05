#include "ava/core/json_support.h"

#include <cctype>

namespace ava::core::json::detail {

int hex_value(char ch)
{
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

void append_utf8(std::string& out, int codepoint)
{
  if (codepoint <= 0x7F) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

std::optional<int> parse_hex_code_unit(std::string_view text, std::size_t hex_start)
{
  if (hex_start + 3 >= text.size()) return std::nullopt;
  int const a = hex_value(text[hex_start]);
  int const b = hex_value(text[hex_start + 1]);
  int const c = hex_value(text[hex_start + 2]);
  int const d = hex_value(text[hex_start + 3]);
  if (a < 0 || b < 0 || c < 0 || d < 0) return std::nullopt;
  return (a << 12) | (b << 8) | (c << 4) | d;
}

bool is_high_surrogate(int code_unit)
{
  return code_unit >= 0xD800 && code_unit <= 0xDBFF;
}

bool is_low_surrogate(int code_unit)
{
  return code_unit >= 0xDC00 && code_unit <= 0xDFFF;
}

bool is_hex_digit(char ch)
{
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

bool is_json_whitespace(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

JsonValidator::JsonValidator(std::string_view value) : value_(value)
{
}

bool JsonValidator::valid_object()
{
  skip_ws();
  if (!parse_object()) return false;
  skip_ws();
  return offset_ == value_.size();
}

void JsonValidator::skip_ws()
{
  while (offset_ < value_.size() && is_json_whitespace(value_[offset_])) ++offset_;
}

bool JsonValidator::consume(char expected)
{
  if (offset_ >= value_.size() || value_[offset_] != expected) return false;
  ++offset_;
  return true;
}

bool JsonValidator::consume_literal(std::string_view literal)
{
  if (value_.substr(offset_, literal.size()) != literal) return false;
  offset_ += literal.size();
  return true;
}

bool JsonValidator::parse_value()
{
  skip_ws();
  if (offset_ >= value_.size()) return false;
  char const ch = value_[offset_];
  if (ch == '"') return parse_string();
  if (ch == '{') return parse_object();
  if (ch == '[') return parse_array();
  if (ch == 't') return consume_literal("true");
  if (ch == 'f') return consume_literal("false");
  if (ch == 'n') return consume_literal("null");
  if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0) return parse_number();
  return false;
}

bool JsonValidator::parse_object()
{
  if (!consume('{')) return false;
  skip_ws();
  if (consume('}')) return true;
  while (true) {
    skip_ws();
    if (!parse_string()) return false;
    skip_ws();
    if (!consume(':')) return false;
    if (!parse_value()) return false;
    skip_ws();
    if (consume('}')) return true;
    if (!consume(',')) return false;
  }
}

bool JsonValidator::parse_array()
{
  if (!consume('[')) return false;
  skip_ws();
  if (consume(']')) return true;
  while (true) {
    if (!parse_value()) return false;
    skip_ws();
    if (consume(']')) return true;
    if (!consume(',')) return false;
  }
}

bool JsonValidator::parse_string()
{
  if (!consume('"')) return false;
  while (offset_ < value_.size()) {
    char const ch = value_[offset_++];
    if (static_cast<unsigned char>(ch) < 0x20) return false;
    if (ch == '"') return true;
    if (ch != '\\') continue;
    if (offset_ >= value_.size()) return false;
    char const escaped = value_[offset_++];
    if (escaped == '"' || escaped == '\\' || escaped == '/' || escaped == 'b' || escaped == 'f' || escaped == 'n' ||
        escaped == 'r' || escaped == 't') {
      continue;
    }
    if (escaped != 'u') return false;
    for (int digit = 0; digit < 4; ++digit) {
      if (offset_ >= value_.size() || !is_hex_digit(value_[offset_])) return false;
      ++offset_;
    }
  }
  return false;
}

bool JsonValidator::parse_number()
{
  if (consume('-') && offset_ >= value_.size()) return false;
  if (consume('0')) {
    if (offset_ < value_.size() && std::isdigit(static_cast<unsigned char>(value_[offset_])) != 0) return false;
  } else {
    if (offset_ >= value_.size() || std::isdigit(static_cast<unsigned char>(value_[offset_])) == 0) return false;
    while (offset_ < value_.size() && std::isdigit(static_cast<unsigned char>(value_[offset_])) != 0) ++offset_;
  }
  if (consume('.')) {
    if (offset_ >= value_.size() || std::isdigit(static_cast<unsigned char>(value_[offset_])) == 0) return false;
    while (offset_ < value_.size() && std::isdigit(static_cast<unsigned char>(value_[offset_])) != 0) ++offset_;
  }
  if (offset_ < value_.size() && (value_[offset_] == 'e' || value_[offset_] == 'E')) {
    ++offset_;
    if (offset_ < value_.size() && (value_[offset_] == '+' || value_[offset_] == '-')) ++offset_;
    if (offset_ >= value_.size() || std::isdigit(static_cast<unsigned char>(value_[offset_])) == 0) return false;
    while (offset_ < value_.size() && std::isdigit(static_cast<unsigned char>(value_[offset_])) != 0) ++offset_;
  }
  return true;
}

std::optional<std::string> parse_string_at(std::string_view text, std::size_t start)
{
  if (start >= text.size() || text[start] != '"') return std::nullopt;
  std::string result;
  bool escaped = false;
  for (std::size_t index = start + 1; index < text.size(); ++index) {
    char const ch = text[index];
    if (escaped) {
      switch (ch) {
        case '"':
          result.push_back('"');
          break;
        case '\\':
          result.push_back('\\');
          break;
        case '/':
          result.push_back('/');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'u':
          if (auto const code_unit = parse_hex_code_unit(text, index + 1)) {
            if (is_high_surrogate(*code_unit)) {
              if (index + 10 < text.size() && text[index + 5] == '\\' && text[index + 6] == 'u') {
                auto const low = parse_hex_code_unit(text, index + 7);
                if (low && is_low_surrogate(*low)) {
                  append_utf8(result, ((*code_unit - 0xD800) << 10) + (*low - 0xDC00) + 0x10000);
                  index += 10;
                } else {
                  append_utf8(result, 0xFFFD);
                  index += 4;
                }
              } else {
                append_utf8(result, 0xFFFD);
                index += 4;
              }
            } else if (is_low_surrogate(*code_unit)) {
              append_utf8(result, 0xFFFD);
              index += 4;
            } else {
              append_utf8(result, *code_unit);
              index += 4;
            }
          } else {
            return std::nullopt;
          }
          break;
        default:
          return std::nullopt;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') return result;
    result.push_back(ch);
  }
  return std::nullopt;
}

std::optional<std::size_t> string_literal_end(std::string_view text, std::size_t start)
{
  if (start >= text.size() || text[start] != '"') return std::nullopt;
  bool escaped = false;
  for (std::size_t index = start + 1; index < text.size(); ++index) {
    char const ch = text[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') return index;
  }
  return std::nullopt;
}

std::optional<std::string> parse_balanced(std::string_view text, std::size_t start, char open, char close)
{
  if (start >= text.size() || text[start] != open) return std::nullopt;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (std::size_t index = start; index < text.size(); ++index) {
    char const ch = text[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string) {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) continue;
    if (ch == open) ++depth;
    if (ch == close) {
      --depth;
      if (depth == 0) return std::string(text.substr(start, index - start + 1));
    }
  }
  return std::nullopt;
}

}  // namespace ava::core::json::detail
