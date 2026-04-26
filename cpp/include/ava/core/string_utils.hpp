#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace ava::core {

[[nodiscard]] inline char lowercase_ascii_char(unsigned char ch) {
  if(ch >= static_cast<unsigned char>('A') && ch <= static_cast<unsigned char>('Z')) {
    constexpr unsigned char kAsciiCaseOffset = static_cast<unsigned char>('a' - 'A');
    return static_cast<char>(ch + kAsciiCaseOffset);
  }
  return static_cast<char>(ch);
}

[[nodiscard]] inline std::string lowercase_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return lowercase_ascii_char(ch);
  });
  return value;
}

[[nodiscard]] inline std::string lowercase_ascii(const char* value) {
  if(value == nullptr) {
    return {};
  }
  return lowercase_ascii(std::string(value));
}

[[nodiscard]] inline std::string lowercase_ascii(std::string_view value) {
  return lowercase_ascii(std::string(value));
}

[[nodiscard]] inline bool is_ascii_whitespace(char ch) {
  switch(ch) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case '\f':
    case '\v':
      return true;
    default:
      return false;
  }
}

[[nodiscard]] inline std::string_view trim_ascii_view(std::string_view value) {
  while(!value.empty() && is_ascii_whitespace(value.front())) {
    value.remove_prefix(1);
  }
  while(!value.empty() && is_ascii_whitespace(value.back())) {
    value.remove_suffix(1);
  }
  return value;
}

[[nodiscard]] inline std::string trim_copy(std::string value) {
  const auto not_space = [](unsigned char ch) {
    return std::isspace(ch) == 0;
  };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

[[nodiscard]] inline std::vector<std::string_view> split_words(std::string_view value) {
  std::vector<std::string_view> words;
  std::size_t start = 0;
  while(start < value.size()) {
    while(start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
      ++start;
    }
    if(start >= value.size()) {
      break;
    }

    std::size_t end = start;
    while(end < value.size() && std::isspace(static_cast<unsigned char>(value[end])) == 0) {
      ++end;
    }
    words.emplace_back(value.substr(start, end - start));
    start = end;
  }
  return words;
}

}  // namespace ava::core
