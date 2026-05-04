#include "ava/provider/provider_utils.h"

#include <algorithm>
#include <cctype>

namespace ava::provider {
namespace {

std::string_view trim(std::string_view value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  return value;
}

bool is_hex_digit(char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'); }

bool is_json_whitespace(char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'; }

class JsonValidator {
 public:
  explicit JsonValidator(std::string_view value) : value_(value) {}

  [[nodiscard]] bool valid_object() {
    skip_ws();
    if (!parse_object()) return false;
    skip_ws();
    return offset_ == value_.size();
  }

 private:
  void skip_ws() {
    while (offset_ < value_.size() && is_json_whitespace(value_[offset_])) ++offset_;
  }

  [[nodiscard]] bool consume(char expected) {
    if (offset_ >= value_.size() || value_[offset_] != expected) return false;
    ++offset_;
    return true;
  }

  [[nodiscard]] bool consume_literal(std::string_view literal) {
    if (value_.substr(offset_, literal.size()) != literal) return false;
    offset_ += literal.size();
    return true;
  }

  [[nodiscard]] bool parse_value() {
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

  [[nodiscard]] bool parse_object() {
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

  [[nodiscard]] bool parse_array() {
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

  [[nodiscard]] bool parse_string() {
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

  [[nodiscard]] bool parse_number() {
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

  std::string_view value_;
  std::size_t offset_ = 0;
};

void redact_json_string_value(std::string& snippet, std::string_view key) {
  std::string const needle = "\"" + std::string(key) + "\"";
  std::size_t position = 0;
  while ((position = snippet.find(needle, position)) != std::string::npos) {
    auto value = position + needle.size();
    while (value < snippet.size() && std::isspace(static_cast<unsigned char>(snippet[value])) != 0) ++value;
    if (value >= snippet.size() || snippet[value] != ':') {
      position += needle.size();
      continue;
    }
    ++value;
    while (value < snippet.size() && std::isspace(static_cast<unsigned char>(snippet[value])) != 0) ++value;
    if (value >= snippet.size() || snippet[value] != '"') {
      position = value;
      continue;
    }
    bool escaped = false;
    bool redacted = false;
    for (std::size_t end = value + 1; end < snippet.size(); ++end) {
      char const ch = snippet[end];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (ch == '\\') {
        escaped = true;
        continue;
      }
      if (ch == '"') {
        snippet.replace(value + 1, end - value - 1, "[redacted]");
        position = value + std::string_view("[redacted]").size() + 2;
        redacted = true;
        break;
      }
    }
    if (!redacted) {
      snippet.replace(value + 1, std::string::npos, "[redacted]");
      position = snippet.size();
    }
  }
}

}  // namespace

bool is_json_object_shape(std::string_view value) {
  value = trim(value);
  if (value.size() < 2 || value.front() != '{') return false;
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  std::size_t end = std::string_view::npos;
  for (std::size_t index = 0; index < value.size(); ++index) {
    char const ch = value[index];
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
    if (ch == '{') ++depth;
    if (ch == '}') {
      --depth;
      if (depth == 0) {
        end = index;
        break;
      }
      if (depth < 0) return false;
    }
  }
  if (in_string || depth != 0 || end == std::string_view::npos) return false;
  if (!trim(value.substr(end + 1)).empty()) return false;
  auto const first_member = trim(value.substr(1, end - 1));
  return first_member.empty() || first_member.front() == '"';
}

bool is_valid_json_object(std::string_view value) { return JsonValidator(value).valid_object(); }

std::string sanitized_body_snippet(std::string_view body, std::initializer_list<std::string_view> secret_keys) {
  constexpr std::size_t kMaxSnippet = 256;
  constexpr std::size_t kMaxSanitizeBytes = 4096;
  std::string snippet(body.substr(0, std::min(body.size(), kMaxSanitizeBytes)));
  for (std::string_view const secret_key : secret_keys) {
    redact_json_string_value(snippet, secret_key);
  }
  std::size_t bearer = 0;
  while ((bearer = snippet.find("Bearer ", bearer)) != std::string::npos) {
    auto end = bearer + std::string_view("Bearer ").size();
    while (end < snippet.size() && std::isspace(static_cast<unsigned char>(snippet[end])) == 0 && snippet[end] != '"') {
      ++end;
    }
    snippet.replace(bearer, end - bearer, "Bearer [redacted]");
    bearer += std::string_view("Bearer [redacted]").size();
  }
  if (snippet.size() > kMaxSnippet) snippet.resize(kMaxSnippet);
  return snippet;
}

}  // namespace ava::provider
