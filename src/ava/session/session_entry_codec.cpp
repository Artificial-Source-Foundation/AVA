#include "ava/session/session_entry_codec.h"

#include <istream>
#include <string>
#include <utility>

#include "ava/session/session_entry_codec_support.h"

namespace ava::session {

ava::core::VoidResult validate_session_id(std::string_view session_id)
{
  if (session_id.empty() || session_id == "." || session_id == "..") {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid session id");
    error.with_context("reason", "empty or reserved path segment");
    return std::unexpected(std::move(error));
  }

  for (char const ch : session_id) {
    auto const byte = static_cast<unsigned char>(ch);
    if (ch == '/' || ch == '\\' || byte < 0x20 || byte == 0x7F) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid session id");
      error.with_context("reason", "contains a path separator or control character");
      return std::unexpected(std::move(error));
    }
  }

  return {};
}

ava::core::VoidResult validate_parent_id(std::string_view parent_id, std::string_view entry_id)
{
  if (parent_id.empty()) return {};
  if (parent_id == "." || parent_id == "..") {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid session parent_id");
    error.with_context("entry_id", std::string(entry_id));
    error.with_context("reason", "reserved path segment");
    return std::unexpected(std::move(error));
  }

  for (char const ch : parent_id) {
    auto const byte = static_cast<unsigned char>(ch);
    if (ch == '/' || ch == '\\' || byte < 0x20 || byte == 0x7F) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid session parent_id");
      error.with_context("entry_id", std::string(entry_id));
      error.with_context("reason", "contains a path separator or control character");
      return std::unexpected(std::move(error));
    }
  }

  return {};
}

ava::core::Result<bool> read_limited_session_line(std::istream& file, std::string& line)
{
  line.clear();
  bool saw_character = false;
  char ch = '\0';
  while (file.get(ch)) {
    saw_character = true;
    if (ch == '\n') {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      return true;
    }
    if (line.size() + 1 >= kMaxSessionLineBytes) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session entry line is too large");
      error.with_context("max_bytes", std::to_string(kMaxSessionLineBytes));
      return std::unexpected(std::move(error));
    }
    line.push_back(ch);
  }
  if (!file.eof() && file.fail()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading session file");
    return std::unexpected(std::move(error));
  }
  if (saw_character && !line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  return saw_character;
}

ava::core::Result<long long> read_supported_entry_version(std::string_view line, std::filesystem::path const& path)
{
  auto version = detail::extract_entry_version(line);
  if (!version) {
    auto error = std::move(version.error());
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (!*version) return 0;
  if (**version >= 1 && **version <= kCurrentSessionEntryVersion) return **version;

  auto error = ava::core::Error(ava::core::ErrorCategory::Session, "unsupported session entry version");
  error.with_context("path", path.string());
  error.with_context("version", std::to_string(**version));
  error.with_context("supported_version", std::to_string(kCurrentSessionEntryVersion));
  return std::unexpected(std::move(error));
}

bool is_unsupported_session_version_error(ava::core::Error const& error)
{
  return error.category() == ava::core::ErrorCategory::Session &&
         error.message() == "unsupported session entry version";
}

ava::core::Result<SessionEntry> decode_session_entry_line(std::string_view line, std::filesystem::path const& path)
{
  auto version = read_supported_entry_version(line, path);
  if (!version) return std::unexpected(std::move(version.error()));

  auto const id = detail::extract_json_string(line, "id");
  auto const type_text = detail::extract_json_string(line, "type");
  auto const timestamp = detail::extract_json_string(line, "timestamp");
  if (id.empty() || type_text.empty() || timestamp.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "malformed session entry");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  if (!detail::json_object_field_has_object_value(line, "data")) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session entry data must be a JSON object");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  auto const data_json = detail::extract_json_object(line, "data");
  auto type = parse_entry_type(type_text);
  if (!type) return std::unexpected(type.error());

  auto const parent_id = detail::extract_json_string(line, "parent_id");
  if (auto valid_parent_id = validate_parent_id(parent_id, id); !valid_parent_id) {
    auto error = std::move(valid_parent_id.error());
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  return SessionEntry{
      .id = id,
      .parent_id = parent_id,
      .type = *type,
      .timestamp = timestamp,
      .data_json = data_json,
      .version = *version,
  };
}

ava::core::Result<std::string> encode_session_entry_line(SessionEntry const& entry)
{
  if (entry.data_json.empty() || entry.data_json.front() != '{' || entry.data_json.back() != '}') {
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session entry data must be a JSON object");
    error.with_context("entry_id", entry.id);
    return std::unexpected(std::move(error));
  }
  if (entry.data_json.find('\n') != std::string::npos || entry.data_json.find('\r') != std::string::npos) {
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session entry data must not contain raw newlines");
    error.with_context("entry_id", entry.id);
    return std::unexpected(std::move(error));
  }
  if (auto valid_parent_id = validate_parent_id(entry.parent_id, entry.id); !valid_parent_id) {
    return std::unexpected(std::move(valid_parent_id.error()));
  }

  std::string line = "{\"version\":" + std::to_string(kCurrentSessionEntryVersion) + ",";
  line += "\"id\":\"" + json_escape(entry.id) + "\",";
  line += "\"parent_id\":\"" + json_escape(entry.parent_id) + "\",";
  line += "\"type\":\"" + std::string(to_string(entry.type)) + "\",";
  line += "\"timestamp\":\"" + json_escape(entry.timestamp) + "\",";
  line += "\"data\":" + entry.data_json + "}";
  if (line.size() >= kMaxSessionLineBytes) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session entry line is too large");
    error.with_context("max_bytes", std::to_string(kMaxSessionLineBytes));
    error.with_context("entry_type", std::string(to_string(entry.type)));
    return std::unexpected(std::move(error));
  }

  return line;
}

}  // namespace ava::session
