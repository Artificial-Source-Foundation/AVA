#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ava::mcp::detail {

[[nodiscard]] inline bool case_insensitive_equal(std::string_view left,
                                                 std::string_view right) {
  if (left.size() != right.size()) {
    return false;
  }

  for (std::size_t index = 0; index < left.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(left[index])) !=
        std::tolower(static_cast<unsigned char>(right[index]))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline bool
starts_with_case_insensitive(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         case_insensitive_equal(value.substr(0, prefix.size()), prefix);
}

[[nodiscard]] inline bool has_http_scheme(std::string_view url) {
  return starts_with_case_insensitive(url, "https://") ||
         starts_with_case_insensitive(url, "http://");
}

[[nodiscard]] inline bool has_https_scheme(std::string_view url) {
  return starts_with_case_insensitive(url, "https://");
}

[[nodiscard]] inline bool has_url_userinfo(std::string_view url) {
  const auto scheme_delimiter = url.find("://");
  if (scheme_delimiter == std::string_view::npos) {
    return false;
  }

  const auto authority_begin = scheme_delimiter + 3;
  const auto authority_end = url.find_first_of("/?#", authority_begin);
  const auto authority =
      url.substr(authority_begin, authority_end - authority_begin);
  return authority.find('@') != std::string_view::npos;
}

template <typename HeaderMap>
[[nodiscard]] inline bool has_header(const HeaderMap &headers,
                                     std::string_view name) {
  return std::any_of(headers.begin(), headers.end(), [&](const auto &entry) {
    return case_insensitive_equal(entry.first, name);
  });
}

[[nodiscard]] inline bool
is_inline_credential_header(std::string_view header_name) {
  return case_insensitive_equal(header_name, "Authorization") ||
         case_insensitive_equal(header_name, "Proxy-Authorization") ||
         case_insensitive_equal(header_name, "Cookie");
}

[[nodiscard]] inline bool is_http_field_name_token_char(unsigned char ch) {
  if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
      (ch >= 'a' && ch <= 'z')) {
    return true;
  }

  switch (ch) {
  case '!':
  case '#':
  case '$':
  case '%':
  case '&':
  case '\'':
  case '*':
  case '+':
  case '-':
  case '.':
  case '^':
  case '_':
  case '`':
  case '|':
  case '~':
    return true;
  default:
    return false;
  }
}

[[nodiscard]] inline bool is_valid_http_field_name(std::string_view name) {
  if (name.empty()) {
    return false;
  }

  return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
    return is_http_field_name_token_char(ch);
  });
}

[[nodiscard]] inline bool contains_cr_or_lf(std::string_view value) {
  return value.find('\r') != std::string_view::npos ||
         value.find('\n') != std::string_view::npos;
}

inline void
validate_remote_headers(const std::map<std::string, std::string> &headers,
                        std::string_view error_prefix) {
  for (const auto &[key, value] : headers) {
    if (!is_valid_http_field_name(key)) {
      throw std::runtime_error(
          std::string(error_prefix) +
          " header name must be a valid HTTP field-name token: " + key);
    }
    if (contains_cr_or_lf(value)) {
      throw std::runtime_error(
          std::string(error_prefix) +
          " header value must not contain CR or LF: " + key);
    }

    if (!is_inline_credential_header(key)) {
      continue;
    }
    throw std::runtime_error(
        std::string(error_prefix) +
        " header must not contain inline credentials: " + key);
  }
}

} // namespace ava::mcp::detail
