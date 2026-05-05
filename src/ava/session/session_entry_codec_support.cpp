#include "ava/session/session_entry_codec_support.h"

#include <cctype>
#include <string>
#include <utility>

#include "ava/core/error.h"
#include "ava/core/json.h"

namespace ava::session::detail {

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
  } else {
    out.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

void append_utf8_codepoint(std::string& out, int codepoint)
{
  if (codepoint <= 0xFFFF) {
    append_utf8(out, codepoint);
  } else {
    out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

bool is_json_value_delimiter(char ch)
{
  return ch == ',' || ch == '}' || std::isspace(static_cast<unsigned char>(ch)) != 0;
}

ava::core::Result<std::optional<long long>> extract_entry_version(std::string_view line)
{
  auto const start = ava::core::json::field_value_start(line, "version");
  if (!start) return std::optional<long long>{};
  if (*start >= line.size()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "invalid session entry version");
    error.with_context("reason", "missing version value");
    return std::unexpected(std::move(error));
  }

  std::size_t index = *start;
  if (line[index] == '-') ++index;
  auto const digits_start = index;
  while (index < line.size() && std::isdigit(static_cast<unsigned char>(line[index])) != 0) ++index;
  if (index == digits_start || (index < line.size() && !is_json_value_delimiter(line[index]))) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "invalid session entry version");
    error.with_context("reason", "version must be an integer");
    return std::unexpected(std::move(error));
  }

  try {
    return std::stoll(std::string(line.substr(*start, index - *start)));
  } catch (...) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "invalid session entry version");
    error.with_context("reason", "version is outside supported integer range");
    return std::unexpected(std::move(error));
  }
}

std::string extract_json_string(std::string_view line, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(line, key);
  if (!start || *start >= line.size() || line[*start] != '"') {
    return {};
  }
  auto const value_start = *start + 1;
  std::string result;
  bool escaped = false;
  bool closed = false;
  for (auto index = value_start; index < line.size(); ++index) {
    char const ch = line[index];
    if (escaped) {
      switch (ch) {
        case '"':
          result.push_back('"');
          break;
        case '\\':
          result.push_back('\\');
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
          if (index + 4 < line.size()) {
            int const a = hex_value(line[index + 1]);
            int const b = hex_value(line[index + 2]);
            int const c = hex_value(line[index + 3]);
            int const d = hex_value(line[index + 4]);
            if (a >= 0 && b >= 0 && c >= 0 && d >= 0) {
              int const codepoint = (a << 12) | (b << 8) | (c << 4) | d;
              if (codepoint >= 0xD800 && codepoint <= 0xDBFF && index + 10 < line.size() && line[index + 5] == '\\' &&
                  line[index + 6] == 'u') {
                int const e = hex_value(line[index + 7]);
                int const f = hex_value(line[index + 8]);
                int const g = hex_value(line[index + 9]);
                int const h = hex_value(line[index + 10]);
                if (e >= 0 && f >= 0 && g >= 0 && h >= 0) {
                  int const low = (e << 12) | (f << 8) | (g << 4) | h;
                  if (low >= 0xDC00 && low <= 0xDFFF) {
                    append_utf8_codepoint(result, ((codepoint - 0xD800) << 10) + (low - 0xDC00) + 0x10000);
                    index += 10;
                  } else {
                    append_utf8(result, 0xFFFD);
                    index += 4;
                  }
                } else {
                  append_utf8(result, 0xFFFD);
                  index += 4;
                }
              } else if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
                append_utf8(result, 0xFFFD);
                index += 4;
              } else {
                append_utf8(result, codepoint);
                index += 4;
              }
            } else {
              return {};
            }
          } else {
            return {};
          }
          break;
        case '/':
          result.push_back('/');
          break;
        default:
          return {};
      }
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      closed = true;
      break;
    }
    result.push_back(ch);
  }
  if (escaped || !closed) {
    return {};
  }
  return result;
}

bool json_object_field_has_object_value(std::string_view line, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(line, key);
  return start && *start < line.size() && line[*start] == '{';
}

std::string extract_json_object(std::string_view line, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(line, key);
  if (!start) {
    return "{}";
  }
  auto const value_start = *start;
  if (value_start >= line.size() || line[value_start] != '{') {
    return "{}";
  }

  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (auto index = value_start; index < line.size(); ++index) {
    char const ch = line[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = in_string;
      continue;
    }
    if (ch == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) {
      continue;
    }
    if (ch == '{') {
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0) {
        return std::string(line.substr(value_start, index - value_start + 1));
      }
    }
  }
  return "{}";
}

}  // namespace ava::session::detail
