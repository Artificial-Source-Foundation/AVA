#include "sys.h"
#include "ava/core/json.h"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace ava::core::json {
namespace {

int hex_value(char ch)
{
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'a' && ch <= 'f')
    return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F')
    return ch - 'A' + 10;
  return -1;
}

void append_utf8(std::string& out, int codepoint)
{
  if (codepoint <= 0x7F)
  {
    out.push_back(static_cast<char>(codepoint));
  }
  else if (codepoint <= 0x7FF)
  {
    out.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  else if (codepoint <= 0xFFFF)
  {
    out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  else
  {
    out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

std::optional<int> parse_hex_code_unit(std::string_view text, std::size_t hex_start)
{
  if (hex_start + 3 >= text.size())
    return std::nullopt;
  int const a = hex_value(text[hex_start]);
  int const b = hex_value(text[hex_start + 1]);
  int const c = hex_value(text[hex_start + 2]);
  int const d = hex_value(text[hex_start + 3]);
  if (a < 0 || b < 0 || c < 0 || d < 0)
    return std::nullopt;
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

class JsonValidator
{
 public:
  explicit JsonValidator(std::string_view value) : value_(value) { }

  [[nodiscard]] bool valid_object()
  {
    skip_ws();
    if (!parse_object())
      return false;
    skip_ws();
    return offset_ == value_.size();
  }

 private:
  void skip_ws()
  {
    while (offset_ < value_.size() && is_json_whitespace(value_[offset_])) ++offset_;
  }

  [[nodiscard]] bool consume(char expected)
  {
    if (offset_ >= value_.size() || value_[offset_] != expected)
      return false;
    ++offset_;
    return true;
  }

  [[nodiscard]] bool consume_literal(std::string_view literal)
  {
    if (value_.substr(offset_, literal.size()) != literal)
      return false;
    offset_ += literal.size();
    return true;
  }

  [[nodiscard]] bool parse_value()
  {
    skip_ws();
    if (offset_ >= value_.size())
      return false;
    char const ch = value_[offset_];
    if (ch == '"')
      return parse_string();
    if (ch == '{')
      return parse_object();
    if (ch == '[')
      return parse_array();
    if (ch == 't')
      return consume_literal("true");
    if (ch == 'f')
      return consume_literal("false");
    if (ch == 'n')
      return consume_literal("null");
    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0)
      return parse_number();
    return false;
  }

  [[nodiscard]] bool parse_object()
  {
    if (!consume('{'))
      return false;
    skip_ws();
    if (consume('}'))
      return true;
    while (true)
    {
      skip_ws();
      if (!parse_string())
        return false;
      skip_ws();
      if (!consume(':'))
        return false;
      if (!parse_value())
        return false;
      skip_ws();
      if (consume('}'))
        return true;
      if (!consume(','))
        return false;
    }
  }

  [[nodiscard]] bool parse_array()
  {
    if (!consume('['))
      return false;
    skip_ws();
    if (consume(']'))
      return true;
    while (true)
    {
      if (!parse_value())
        return false;
      skip_ws();
      if (consume(']'))
        return true;
      if (!consume(','))
        return false;
    }
  }

  [[nodiscard]] bool parse_string()
  {
    if (!consume('"'))
      return false;
    while (offset_ < value_.size())
    {
      char const ch = value_[offset_++];
      if (static_cast<unsigned char>(ch) < 0x20)
        return false;
      if (ch == '"')
        return true;
      if (ch != '\\')
        continue;
      if (offset_ >= value_.size())
        return false;
      char const escaped = value_[offset_++];
      if (escaped == '"' || escaped == '\\' || escaped == '/' || escaped == 'b' || escaped == 'f' || escaped == 'n' || escaped == 'r' || escaped == 't')
      {
        continue;
      }
      if (escaped != 'u')
        return false;
      for (int digit = 0; digit < 4; ++digit)
      {
        if (offset_ >= value_.size() || !is_hex_digit(value_[offset_]))
          return false;
        ++offset_;
      }
    }
    return false;
  }

  [[nodiscard]] bool parse_number()
  {
    if (consume('-') && offset_ >= value_.size())
      return false;
    if (consume('0'))
    {
      if (offset_ < value_.size() && std::isdigit(static_cast<unsigned char>(value_[offset_])) != 0)
        return false;
    }
    else
    {
      if (offset_ >= value_.size() || std::isdigit(static_cast<unsigned char>(value_[offset_])) == 0)
        return false;
      while (offset_ < value_.size() && std::isdigit(static_cast<unsigned char>(value_[offset_])) != 0) ++offset_;
    }
    if (consume('.'))
    {
      if (offset_ >= value_.size() || std::isdigit(static_cast<unsigned char>(value_[offset_])) == 0)
        return false;
      while (offset_ < value_.size() && std::isdigit(static_cast<unsigned char>(value_[offset_])) != 0) ++offset_;
    }
    if (offset_ < value_.size() && (value_[offset_] == 'e' || value_[offset_] == 'E'))
    {
      ++offset_;
      if (offset_ < value_.size() && (value_[offset_] == '+' || value_[offset_] == '-'))
        ++offset_;
      if (offset_ >= value_.size() || std::isdigit(static_cast<unsigned char>(value_[offset_])) == 0)
        return false;
      while (offset_ < value_.size() && std::isdigit(static_cast<unsigned char>(value_[offset_])) != 0) ++offset_;
    }
    return true;
  }

  std::string_view value_;
  std::size_t offset_ = 0;
};

std::optional<std::string> parse_string_at(std::string_view text, std::size_t start)
{
  if (start >= text.size() || text[start] != '"')
    return std::nullopt;
  std::string result;
  bool escaped = false;
  for (std::size_t index = start + 1; index < text.size(); ++index)
  {
    char const ch = text[index];
    if (escaped)
    {
      switch (ch)
      {
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
          if (auto const code_unit = parse_hex_code_unit(text, index + 1))
          {
            if (is_high_surrogate(*code_unit))
            {
              if (index + 10 < text.size() && text[index + 5] == '\\' && text[index + 6] == 'u')
              {
                auto const low = parse_hex_code_unit(text, index + 7);
                if (low && is_low_surrogate(*low))
                {
                  append_utf8(result, ((*code_unit - 0xD800) << 10) + (*low - 0xDC00) + 0x10000);
                  index += 10;
                }
                else
                {
                  append_utf8(result, 0xFFFD);
                  index += 4;
                }
              }
              else
              {
                append_utf8(result, 0xFFFD);
                index += 4;
              }
            }
            else if (is_low_surrogate(*code_unit))
            {
              append_utf8(result, 0xFFFD);
              index += 4;
            }
            else
            {
              append_utf8(result, *code_unit);
              index += 4;
            }
          }
          else
          {
            return std::nullopt;
          }
          break;
        default:
          return std::nullopt;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\')
    {
      escaped = true;
      continue;
    }
    if (ch == '"')
      return result;
    result.push_back(ch);
  }
  return std::nullopt;
}

std::optional<std::size_t> string_literal_end(std::string_view text, std::size_t start)
{
  if (start >= text.size() || text[start] != '"')
    return std::nullopt;
  bool escaped = false;
  for (std::size_t index = start + 1; index < text.size(); ++index)
  {
    char const ch = text[index];
    if (escaped)
    {
      escaped = false;
      continue;
    }
    if (ch == '\\')
    {
      escaped = true;
      continue;
    }
    if (ch == '"')
      return index;
  }
  return std::nullopt;
}

std::optional<std::string> parse_balanced(std::string_view text, std::size_t start, char open, char close)
{
  if (start >= text.size() || text[start] != open)
    return std::nullopt;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (std::size_t index = start; index < text.size(); ++index)
  {
    char const ch = text[index];
    if (escaped)
    {
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string)
    {
      escaped = true;
      continue;
    }
    if (ch == '"')
    {
      in_string = !in_string;
      continue;
    }
    if (in_string)
      continue;
    if (ch == open)
      ++depth;
    if (ch == close)
    {
      --depth;
      if (depth == 0)
        return std::string(text.substr(start, index - start + 1));
    }
  }
  return std::nullopt;
}

std::size_t valid_utf8_sequence_length(std::string_view value, std::size_t index) noexcept
{
  auto const lead = static_cast<unsigned char>(value[index]);
  if (lead <= 0x7FU)
    return 1;

  std::size_t length = 0;
  unsigned int code_point = 0;
  unsigned int minimum = 0;
  if (lead >= 0xC2U && lead <= 0xDFU)
  {
    length = 2;
    code_point = lead & 0x1FU;
    minimum = 0x80U;
  }
  else if (lead >= 0xE0U && lead <= 0xEFU)
  {
    length = 3;
    code_point = lead & 0x0FU;
    minimum = 0x800U;
  }
  else if (lead >= 0xF0U && lead <= 0xF4U)
  {
    length = 4;
    code_point = lead & 0x07U;
    minimum = 0x10000U;
  }
  else
  {
    return 0;
  }
  if (index + length > value.size())
    return 0;
  for (std::size_t offset = 1; offset < length; ++offset)
  {
    auto const continuation = static_cast<unsigned char>(value[index + offset]);
    if ((continuation & 0xC0U) != 0x80U)
      return 0;
    code_point = (code_point << 6U) | (continuation & 0x3FU);
  }
  if (code_point < minimum || code_point > 0x10FFFFU || (code_point >= 0xD800U && code_point <= 0xDFFFU))
    return 0;
  return length;
}

}  // namespace

bool is_valid_utf8(std::string_view value) noexcept
{
  for (std::size_t index = 0; index < value.size();)
  {
    auto const length = valid_utf8_sequence_length(value, index);
    if (length == 0)
      return false;
    index += length;
  }
  return true;
}

std::string replace_invalid_utf8(std::string_view value)
{
  std::string result;
  result.reserve(value.size());
  for (std::size_t index = 0; index < value.size();)
  {
    auto const length = valid_utf8_sequence_length(value, index);
    if (length == 0)
    {
      result += "\xEF\xBF\xBD";
      ++index;
      continue;
    }
    result.append(value.substr(index, length));
    index += length;
  }
  return result;
}

std::string escape(std::string_view value)
{
  std::string result;
  result.reserve(value.size());
  for (std::size_t index = 0; index < value.size();)
  {
    auto const length = valid_utf8_sequence_length(value, index);
    if (length == 0)
    {
      result += "\xEF\xBF\xBD";
      ++index;
      continue;
    }
    if (length != 1)
    {
      result.append(value.substr(index, length));
      index += length;
      continue;
    }

    char const ch = value[index++];
    switch (ch)
    {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      case '\b':
        result += "\\b";
        break;
      case '\f':
        result += "\\f";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20U)
        {
          std::ostringstream escaped;
          escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(ch));
          result += escaped.str();
        }
        else
        {
          result.push_back(ch);
        }
        break;
    }
  }
  return result;
}

std::optional<std::size_t> field_value_start(std::string_view object, std::string_view key)
{
  std::string const needle = "\"" + escape(key) + "\"";
  bool in_string = false;
  bool escaped = false;
  int object_depth = 0;
  int array_depth = 0;
  for (std::size_t index = 0; index < object.size(); ++index)
  {
    char const ch = object[index];
    if (in_string)
    {
      if (escaped)
      {
        escaped = false;
        continue;
      }
      if (ch == '\\')
      {
        escaped = true;
        continue;
      }
      if (ch == '"')
        in_string = false;
      continue;
    }

    if (ch == '"')
    {
      if (object_depth == 1 && array_depth == 0 && object.substr(index, needle.size()) == needle)
      {
        auto colon = index + needle.size();
        while (colon < object.size() && std::isspace(static_cast<unsigned char>(object[colon])) != 0) ++colon;
        if (colon < object.size() && object[colon] == ':')
        {
          ++colon;
          while (colon < object.size() && std::isspace(static_cast<unsigned char>(object[colon])) != 0) ++colon;
          return colon;
        }
      }
      in_string = true;
      continue;
    }

    if (ch == '{')
    {
      ++object_depth;
    }
    else if (ch == '}')
    {
      if (object_depth > 0)
        --object_depth;
    }
    else if (ch == '[')
    {
      ++array_depth;
    }
    else if (ch == ']')
    {
      if (array_depth > 0)
        --array_depth;
    }
  }
  return std::nullopt;
}

std::optional<std::string> string_field(std::string_view object, std::string_view key)
{
  auto const start = field_value_start(object, key);
  if (!start)
    return std::nullopt;
  return parse_string_at(object, *start);
}

std::optional<long long> integer_field(std::string_view object, std::string_view key)
{
  auto const start = field_value_start(object, key);
  if (!start)
    return std::nullopt;
  std::size_t end = *start;
  if (end < object.size() && object[end] == '-')
    ++end;
  while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end])) != 0) ++end;
  if (end == *start)
    return std::nullopt;
  try
  {
    return std::stoll(std::string(object.substr(*start, end - *start)));
  }
  catch (...)
  {
    return std::nullopt;
  }
}

std::optional<std::string> object_field(std::string_view object, std::string_view key)
{
  auto const start = field_value_start(object, key);
  if (!start)
    return std::nullopt;
  return parse_balanced(object, *start, '{', '}');
}

std::vector<std::string> objects_in_array_field(std::string_view object, std::string_view key)
{
  std::vector<std::string> result;
  auto const start = field_value_start(object, key);
  if (!start || *start >= object.size() || object[*start] != '[')
    return result;
  auto const array = parse_balanced(object, *start, '[', ']');
  if (!array)
    return result;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = 1; index + 1 < array->size(); ++index)
  {
    char const ch = (*array)[index];
    if (escaped)
    {
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string)
    {
      escaped = true;
      continue;
    }
    if (ch == '"')
    {
      in_string = !in_string;
      continue;
    }
    if (!in_string && ch == '{')
    {
      if (auto parsed = parse_balanced(*array, index, '{', '}'))
      {
        result.push_back(*parsed);
        index += parsed->size() - 1;
      }
    }
  }
  return result;
}

std::vector<std::string> strings_in_array_field(std::string_view object, std::string_view key)
{
  std::vector<std::string> result;
  auto const start = field_value_start(object, key);
  if (!start || *start >= object.size() || object[*start] != '[')
    return result;
  auto const array = parse_balanced(object, *start, '[', ']');
  if (!array)
    return result;

  for (std::size_t index = 1; index + 1 < array->size();)
  {
    while (index + 1 < array->size() && (std::isspace(static_cast<unsigned char>((*array)[index])) != 0 || (*array)[index] == ','))
    {
      ++index;
    }
    if (index + 1 >= array->size())
      break;

    if ((*array)[index] == '"')
    {
      if (auto parsed = parse_string_at(*array, index))
        result.push_back(*parsed);
      if (auto end = string_literal_end(*array, index))
      {
        index = *end + 1;
      }
      else
      {
        break;
      }
      continue;
    }
    if ((*array)[index] == '{')
    {
      if (auto parsed = parse_balanced(*array, index, '{', '}'))
      {
        index += parsed->size();
        continue;
      }
    }
    if ((*array)[index] == '[')
    {
      if (auto parsed = parse_balanced(*array, index, '[', ']'))
      {
        index += parsed->size();
        continue;
      }
    }
    ++index;
  }
  return result;
}

bool is_valid_object(std::string_view value)
{
  return JsonValidator(value).valid_object();
}

}  // namespace ava::core::json
