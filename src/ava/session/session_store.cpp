#include "ava/session/session_store.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include "ava/config/xdg_paths.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

namespace ava::session {

namespace {

constexpr std::size_t max_session_line_bytes = 1024 * 1024;
constexpr long long current_session_entry_version = 1;

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

void append_utf8(std::string& out, int codepoint) {
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

void append_utf8_codepoint(std::string& out, int codepoint) {
  if (codepoint <= 0xFFFF) {
    append_utf8(out, codepoint);
  } else {
    out.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

ava::core::Result<bool> read_limited_line(std::ifstream& file, std::string& line) {
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
    if (line.size() + 1 >= max_session_line_bytes) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session entry line is too large");
      error.with_context("max_bytes", std::to_string(max_session_line_bytes));
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

std::string project_key(const std::filesystem::path& workspace_dir) {
  const auto normalized = std::filesystem::absolute(workspace_dir).lexically_normal().string();
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char ch : normalized) {
    hash ^= ch;
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << hash;
  return out.str();
}

ava::core::VoidResult validate_session_id(std::string_view session_id) {
  if (session_id.empty() || session_id == "." || session_id == "..") {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid session id");
    error.with_context("reason", "empty or reserved path segment");
    return std::unexpected(std::move(error));
  }

  for (const char ch : session_id) {
    const auto byte = static_cast<unsigned char>(ch);
    if (ch == '/' || ch == '\\' || byte < 0x20 || byte == 0x7F) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid session id");
      error.with_context("reason", "contains a path separator or control character");
      return std::unexpected(std::move(error));
    }
  }

  return {};
}

ava::core::VoidResult validate_parent_id(std::string_view parent_id, std::string_view entry_id) {
  if (parent_id.empty()) return {};
  if (parent_id == "." || parent_id == "..") {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid session parent_id");
    error.with_context("entry_id", std::string(entry_id));
    error.with_context("reason", "reserved path segment");
    return std::unexpected(std::move(error));
  }

  for (const char ch : parent_id) {
    const auto byte = static_cast<unsigned char>(ch);
    if (ch == '/' || ch == '\\' || byte < 0x20 || byte == 0x7F) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid session parent_id");
      error.with_context("entry_id", std::string(entry_id));
      error.with_context("reason", "contains a path separator or control character");
      return std::unexpected(std::move(error));
    }
  }

  return {};
}

bool is_json_value_delimiter(char ch) {
  return ch == ',' || ch == '}' || std::isspace(static_cast<unsigned char>(ch)) != 0;
}

ava::core::Result<std::optional<long long>> extract_entry_version(std::string_view line) {
  const auto start = ava::core::json::field_value_start(line, "version");
  if (!start) return std::optional<long long>{};
  if (*start >= line.size()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "invalid session entry version");
    error.with_context("reason", "missing version value");
    return std::unexpected(std::move(error));
  }

  std::size_t index = *start;
  if (line[index] == '-') ++index;
  const auto digits_start = index;
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

ava::core::VoidResult validate_entry_version(std::string_view line, const std::filesystem::path& path) {
  auto version = extract_entry_version(line);
  if (!version) {
    auto error = std::move(version.error());
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (!*version) return {};
  if (**version == current_session_entry_version) return {};

  auto error = ava::core::Error(ava::core::ErrorCategory::Session, "unsupported session entry version");
  error.with_context("path", path.string());
  error.with_context("version", std::to_string(**version));
  error.with_context("supported_version", std::to_string(current_session_entry_version));
  return std::unexpected(std::move(error));
}

bool is_unsupported_session_version_error(const ava::core::Error& error) {
  return error.category() == ava::core::ErrorCategory::Session &&
         error.message() == "unsupported session entry version";
}

std::string extract_json_string(std::string_view line, std::string_view key) {
  const auto start = ava::core::json::field_value_start(line, key);
  if (!start || *start >= line.size() || line[*start] != '"') {
    return {};
  }
  const auto value_start = *start + 1;
  std::string result;
  bool escaped = false;
  bool closed = false;
  for (auto index = value_start; index < line.size(); ++index) {
    const char ch = line[index];
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
            const int a = hex_value(line[index + 1]);
            const int b = hex_value(line[index + 2]);
            const int c = hex_value(line[index + 3]);
            const int d = hex_value(line[index + 4]);
            if (a >= 0 && b >= 0 && c >= 0 && d >= 0) {
              const int codepoint = (a << 12) | (b << 8) | (c << 4) | d;
              if (codepoint >= 0xD800 && codepoint <= 0xDBFF && index + 10 < line.size() && line[index + 5] == '\\' &&
                  line[index + 6] == 'u') {
                const int e = hex_value(line[index + 7]);
                const int f = hex_value(line[index + 8]);
                const int g = hex_value(line[index + 9]);
                const int h = hex_value(line[index + 10]);
                if (e >= 0 && f >= 0 && g >= 0 && h >= 0) {
                  const int low = (e << 12) | (f << 8) | (g << 4) | h;
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

std::string extract_json_object(std::string_view line, std::string_view key) {
  const auto start = ava::core::json::field_value_start(line, key);
  if (!start) {
    return "{}";
  }
  const auto value_start = *start;
  if (value_start >= line.size() || line[value_start] != '{') {
    return "{}";
  }

  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (auto index = value_start; index < line.size(); ++index) {
    const char ch = line[index];
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

}  // namespace

SessionStore::SessionStore(SessionStoreOptions options) : options_(std::move(options)) {}

const std::string& SessionStore::session_id() const noexcept { return options_.session_id; }

std::filesystem::path SessionStore::session_path() const {
  return options_.root_dir / project_key(options_.workspace_dir) / (options_.session_id + ".jsonl");
}

ava::core::VoidResult SessionStore::append(const SessionEntry& entry) {
  if (auto valid_session_id = validate_session_id(options_.session_id); !valid_session_id) {
    return valid_session_id;
  }

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
    return valid_parent_id;
  }

  const auto path = session_path();
  std::error_code mkdir_error;
  std::filesystem::create_directories(path.parent_path(), mkdir_error);
  if (mkdir_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to create session directory");
    error.with_context("path", path.parent_path().string());
    error.with_context("cause", mkdir_error.message());
    return std::unexpected(std::move(error));
  }

  for (const auto& directory : {options_.root_dir, path.parent_path()}) {
    std::error_code permissions_error;
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace,
                                 permissions_error);
    if (permissions_error) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to set session directory permissions");
      error.with_context("path", directory.string());
      error.with_context("cause", permissions_error.message());
      return std::unexpected(std::move(error));
    }
  }

  std::error_code status_error;
  const auto status = std::filesystem::symlink_status(path, status_error);
  if (!status_error && std::filesystem::exists(status)) {
    if (std::filesystem::is_symlink(status)) {
      auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "session path must not be a symlink");
      error.with_context("session_id", session_id());
      error.with_context("path", path.string());
      return std::unexpected(std::move(error));
    }
    if (!std::filesystem::is_regular_file(status)) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session path is not a regular file");
      error.with_context("session_id", session_id());
      error.with_context("path", path.string());
      return std::unexpected(std::move(error));
    }
  } else if (status_error && status_error != std::errc::no_such_file_or_directory) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect session file");
    error.with_context("path", path.string());
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }

  std::ofstream file(path, std::ios::app);
  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open session file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  std::error_code file_permissions_error;
  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, file_permissions_error);
  if (file_permissions_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to set session file permissions");
    error.with_context("path", path.string());
    error.with_context("cause", file_permissions_error.message());
    return std::unexpected(std::move(error));
  }

  std::string line = "{\"version\":1,";
  line += "\"id\":\"" + json_escape(entry.id) + "\",";
  line += "\"parent_id\":\"" + json_escape(entry.parent_id) + "\",";
  line += "\"type\":\"" + std::string(to_string(entry.type)) + "\",";
  line += "\"timestamp\":\"" + json_escape(entry.timestamp) + "\",";
  line += "\"data\":" + entry.data_json + "}";
  if (line.size() >= max_session_line_bytes) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session entry line is too large");
    error.with_context("max_bytes", std::to_string(max_session_line_bytes));
    error.with_context("entry_type", std::string(to_string(entry.type)));
    return std::unexpected(std::move(error));
  }

  file << line << '\n';
  file.flush();

  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to write session entry");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  return {};
}

ava::core::Result<std::vector<SessionEntry>> SessionStore::load() const {
  if (auto valid_session_id = validate_session_id(options_.session_id); !valid_session_id) {
    return std::unexpected(std::move(valid_session_id.error()));
  }

  std::error_code status_error;
  const auto status = std::filesystem::symlink_status(session_path(), status_error);
  if (status_error || !std::filesystem::exists(status)) {
    return std::vector<SessionEntry>{};
  }
  if (std::filesystem::is_symlink(status)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "session path must not be a symlink");
    error.with_context("session_id", session_id());
    error.with_context("path", session_path().string());
    return std::unexpected(std::move(error));
  }
  if (!std::filesystem::is_regular_file(status)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session path is not a regular file");
    error.with_context("session_id", session_id());
    error.with_context("path", session_path().string());
    return std::unexpected(std::move(error));
  }

  std::ifstream file(session_path());
  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open session file");
    error.with_context("session_id", session_id());
    error.with_context("path", session_path().string());
    return std::unexpected(std::move(error));
  }

  std::vector<SessionEntry> entries;
  std::string line;
  while (true) {
    auto line_read = read_limited_line(file, line);
    if (!line_read) {
      return std::unexpected(line_read.error());
    }
    if (!*line_read) {
      break;
    }
    if (auto valid_version = validate_entry_version(line, session_path()); !valid_version) {
      return std::unexpected(std::move(valid_version.error()));
    }
    const auto id = extract_json_string(line, "id");
    const auto type_text = extract_json_string(line, "type");
    const auto timestamp = extract_json_string(line, "timestamp");
    if (id.empty() || type_text.empty() || timestamp.empty()) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "malformed session entry");
      error.with_context("path", session_path().string());
      return std::unexpected(std::move(error));
    }
    const auto data_start = ava::core::json::field_value_start(line, "data");
    if (!data_start || *data_start >= line.size() || line[*data_start] != '{') {
      auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session entry data must be a JSON object");
      error.with_context("path", session_path().string());
      return std::unexpected(std::move(error));
    }
    auto type = parse_entry_type(type_text);
    if (!type) {
      return std::unexpected(type.error());
    }
    const auto parent_id = extract_json_string(line, "parent_id");
    if (auto valid_parent_id = validate_parent_id(parent_id, id); !valid_parent_id) {
      auto error = std::move(valid_parent_id.error());
      error.with_context("path", session_path().string());
      return std::unexpected(std::move(error));
    }
    entries.push_back(SessionEntry{
        .id = id,
        .parent_id = parent_id,
        .type = *type,
        .timestamp = timestamp,
        .data_json = extract_json_object(line, "data"),
    });
  }
  return entries;
}

ava::core::Result<SessionStore> SessionStore::create(const std::filesystem::path& workspace_dir,
                                                     const std::filesystem::path& root_dir) {
  return SessionStore(SessionStoreOptions{
      .root_dir = root_dir,
      .workspace_dir = workspace_dir,
      .session_id = ava::core::make_id("session"),
  });
}

ava::core::Result<SessionStore> SessionStore::open(const std::filesystem::path& workspace_dir, std::string session_id,
                                                   const std::filesystem::path& root_dir) {
  if (auto valid_session_id = validate_session_id(session_id); !valid_session_id) {
    return std::unexpected(std::move(valid_session_id.error()));
  }

  SessionStore store(SessionStoreOptions{
      .root_dir = root_dir,
      .workspace_dir = workspace_dir,
      .session_id = std::move(session_id),
  });
  std::error_code status_error;
  const auto status = std::filesystem::symlink_status(store.session_path(), status_error);
  if (status_error || !std::filesystem::exists(status)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "session not found");
    error.with_context("session_id", store.session_id());
    error.with_context("path", store.session_path().string());
    return std::unexpected(std::move(error));
  }
  if (std::filesystem::is_symlink(status)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "session path must not be a symlink");
    error.with_context("session_id", store.session_id());
    error.with_context("path", store.session_path().string());
    return std::unexpected(std::move(error));
  }
  if (!std::filesystem::is_regular_file(status)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Session, "session path is not a regular file");
    error.with_context("session_id", store.session_id());
    error.with_context("path", store.session_path().string());
    return std::unexpected(std::move(error));
  }

  return store;
}

ava::core::Result<std::vector<SessionSummary>> SessionStore::list_sessions(const std::filesystem::path& workspace_dir,
                                                                           const std::filesystem::path& root_dir) {
  const auto directory = root_dir / project_key(workspace_dir);
  std::error_code exists_error;
  const bool directory_exists = std::filesystem::exists(directory, exists_error);
  if (exists_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect session directory");
    error.with_context("path", directory.string());
    error.with_context("cause", exists_error.message());
    return std::unexpected(std::move(error));
  }
  if (!directory_exists) {
    return std::vector<SessionSummary>{};
  }

  std::vector<SessionSummary> summaries;
  std::error_code iter_error;
  for (std::filesystem::directory_iterator iter(directory, iter_error), end; iter != end; iter.increment(iter_error)) {
    if (iter_error) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to list sessions");
      error.with_context("path", directory.string());
      error.with_context("cause", iter_error.message());
      return std::unexpected(std::move(error));
    }
    const auto& entry = *iter;
    std::error_code entry_error;
    const auto entry_status = entry.symlink_status(entry_error);
    if (entry_error || std::filesystem::is_symlink(entry_status) || !std::filesystem::is_regular_file(entry_status) ||
        entry.path().extension() != ".jsonl") {
      continue;
    }
    const auto session_id = entry.path().stem().string();
    auto store = SessionStore::open(workspace_dir, session_id, root_dir);
    if (!store) {
      continue;
    }
    auto entries = store->load();
    if (!entries) {
      if (is_unsupported_session_version_error(entries.error())) {
        auto error = std::move(entries.error());
        error.with_context("session_id", session_id);
        return std::unexpected(std::move(error));
      }
      continue;
    }
    std::string last_updated;
    if (!entries->empty()) {
      last_updated = entries->back().timestamp;
    }
    summaries.push_back(SessionSummary{.session_id = session_id,
                                       .path = entry.path(),
                                       .last_updated = std::move(last_updated),
                                       .entry_count = entries->size()});
  }

  std::ranges::sort(summaries, [](const SessionSummary& left, const SessionSummary& right) {
    std::error_code left_error;
    std::error_code right_error;
    const auto left_time = std::filesystem::last_write_time(left.path, left_error);
    const auto right_time = std::filesystem::last_write_time(right.path, right_error);
    if (!left_error && !right_error && left_time != right_time) {
      return left_time > right_time;
    }
    return left.session_id > right.session_id;
  });

  return summaries;
}

std::filesystem::path SessionStore::default_root_dir() { return ava::config::xdg_paths().sessions_dir; }

std::string to_string(EntryType type) {
  switch (type) {
    case EntryType::SessionStart:
      return "session_start";
    case EntryType::UserMessage:
      return "user_message";
    case EntryType::AssistantMessage:
      return "assistant_message";
    case EntryType::ToolCall:
      return "tool_call";
    case EntryType::ToolResult:
      return "tool_result";
    case EntryType::PermissionDecision:
      return "permission_decision";
    case EntryType::ModeChange:
      return "mode_change";
    case EntryType::Compaction:
      return "compaction";
    case EntryType::Error:
      return "error";
    case EntryType::Cancel:
      return "cancel";
  }
  return "error";
}

ava::core::Result<EntryType> parse_entry_type(std::string_view value) {
  if (value == "session_start") return EntryType::SessionStart;
  if (value == "user_message") return EntryType::UserMessage;
  if (value == "assistant_message") return EntryType::AssistantMessage;
  if (value == "tool_call") return EntryType::ToolCall;
  if (value == "tool_result") return EntryType::ToolResult;
  if (value == "permission_decision") return EntryType::PermissionDecision;
  if (value == "mode_change") return EntryType::ModeChange;
  if (value == "compaction") return EntryType::Compaction;
  if (value == "error") return EntryType::Error;
  if (value == "cancel") return EntryType::Cancel;

  auto error = ava::core::Error(ava::core::ErrorCategory::Session, "unknown session entry type");
  error.with_context("type", std::string(value));
  return std::unexpected(std::move(error));
}

bool is_internal_replay_user_message(const SessionEntry& entry) {
  if (entry.type != EntryType::UserMessage) return false;
  const auto start = ava::core::json::field_value_start(entry.data_json, "internal_replay");
  return start && entry.data_json.substr(*start, 4) == "true";
}

std::string now_timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

std::string json_escape(std::string_view value) {
  std::string result;
  result.reserve(value.size());
  for (const char ch : value) {
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

}  // namespace ava::session
