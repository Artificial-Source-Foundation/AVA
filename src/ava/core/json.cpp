#include "ava/core/json.h"

#include <cctype>
#include <iomanip>
#include <sstream>

#include "ava/core/json_support.h"

namespace ava::core::json {

std::string escape(std::string_view value)
{
  std::string result;
  result.reserve(value.size());
  for (char const ch : value) {
    switch (ch) {
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
        if (static_cast<unsigned char>(ch) < 0x20) {
          std::ostringstream escaped;
          escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                  << static_cast<int>(static_cast<unsigned char>(ch));
          result += escaped.str();
        } else {
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
  for (std::size_t index = 0; index < object.size(); ++index) {
    char const ch = object[index];
    if (in_string) {
      if (escaped) {
        escaped = false;
        continue;
      }
      if (ch == '\\') {
        escaped = true;
        continue;
      }
      if (ch == '"') in_string = false;
      continue;
    }

    if (ch == '"') {
      if (object_depth == 1 && array_depth == 0 && object.substr(index, needle.size()) == needle) {
        auto colon = index + needle.size();
        while (colon < object.size() && std::isspace(static_cast<unsigned char>(object[colon])) != 0) ++colon;
        if (colon < object.size() && object[colon] == ':') {
          ++colon;
          while (colon < object.size() && std::isspace(static_cast<unsigned char>(object[colon])) != 0) ++colon;
          return colon;
        }
      }
      in_string = true;
      continue;
    }

    if (ch == '{') {
      ++object_depth;
    } else if (ch == '}') {
      if (object_depth > 0) --object_depth;
    } else if (ch == '[') {
      ++array_depth;
    } else if (ch == ']') {
      if (array_depth > 0) --array_depth;
    }
  }
  return std::nullopt;
}

std::optional<std::string> string_field(std::string_view object, std::string_view key)
{
  auto const start = field_value_start(object, key);
  if (!start) return std::nullopt;
  return detail::parse_string_at(object, *start);
}

std::optional<long long> integer_field(std::string_view object, std::string_view key)
{
  auto const start = field_value_start(object, key);
  if (!start) return std::nullopt;
  std::size_t end = *start;
  if (end < object.size() && object[end] == '-') ++end;
  while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end])) != 0) ++end;
  if (end == *start) return std::nullopt;
  try {
    return std::stoll(std::string(object.substr(*start, end - *start)));
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> object_field(std::string_view object, std::string_view key)
{
  auto const start = field_value_start(object, key);
  if (!start) return std::nullopt;
  return detail::parse_balanced(object, *start, '{', '}');
}

std::vector<std::string> objects_in_array_field(std::string_view object, std::string_view key)
{
  std::vector<std::string> result;
  auto const start = field_value_start(object, key);
  if (!start || *start >= object.size() || object[*start] != '[') return result;
  auto const array = detail::parse_balanced(object, *start, '[', ']');
  if (!array) return result;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = 1; index + 1 < array->size(); ++index) {
    char const ch = (*array)[index];
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
    if (!in_string && ch == '{') {
      if (auto parsed = detail::parse_balanced(*array, index, '{', '}')) {
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
  if (!start || *start >= object.size() || object[*start] != '[') return result;
  auto const array = detail::parse_balanced(object, *start, '[', ']');
  if (!array) return result;

  for (std::size_t index = 1; index + 1 < array->size();) {
    while (index + 1 < array->size() &&
           (std::isspace(static_cast<unsigned char>((*array)[index])) != 0 || (*array)[index] == ',')) {
      ++index;
    }
    if (index + 1 >= array->size()) break;

    if ((*array)[index] == '"') {
      if (auto parsed = detail::parse_string_at(*array, index)) result.push_back(*parsed);
      if (auto end = detail::string_literal_end(*array, index)) {
        index = *end + 1;
      } else {
        break;
      }
      continue;
    }
    if ((*array)[index] == '{') {
      if (auto parsed = detail::parse_balanced(*array, index, '{', '}')) {
        index += parsed->size();
        continue;
      }
    }
    if ((*array)[index] == '[') {
      if (auto parsed = detail::parse_balanced(*array, index, '[', ']')) {
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
  return detail::JsonValidator(value).valid_object();
}

}  // namespace ava::core::json
