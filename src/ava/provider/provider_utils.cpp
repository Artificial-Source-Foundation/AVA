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

void redact_json_string_value(std::string& snippet, std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\"";
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
      const char ch = snippet[end];
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
    const char ch = value[index];
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
  const auto first_member = trim(value.substr(1, end - 1));
  return first_member.empty() || first_member.front() == '"';
}

std::string sanitized_body_snippet(std::string_view body, std::initializer_list<std::string_view> secret_keys) {
  constexpr std::size_t kMaxSnippet = 256;
  constexpr std::size_t kMaxSanitizeBytes = 4096;
  std::string snippet(body.substr(0, std::min(body.size(), kMaxSanitizeBytes)));
  for (const std::string_view secret_key : secret_keys) {
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
