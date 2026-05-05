#include "ava/session/session_store.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <utility>

#include "ava/config/xdg_paths.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/session/session_entry_codec.h"
#include "ava/session/session_store_support.h"

namespace ava::session {

SessionStore::SessionStore(SessionStoreOptions options) : options_(std::move(options))
{
}

std::string const& SessionStore::session_id() const noexcept
{
  return options_.session_id;
}

std::filesystem::path SessionStore::session_path() const
{
  return detail::session_file_path(options_.root_dir, options_.workspace_dir, options_.session_id);
}

ava::core::VoidResult SessionStore::append(SessionEntry const& entry)
{
  if (auto valid_session_id = validate_session_id(options_.session_id); !valid_session_id) {
    return valid_session_id;
  }

  auto line = encode_session_entry_line(entry);
  if (!line) return std::unexpected(std::move(line.error()));

  auto const path = session_path();
  if (auto directories = detail::create_private_session_directories(options_.root_dir, path.parent_path());
      !directories) {
    return std::unexpected(std::move(directories.error()));
  }
  auto inspected = detail::inspect_session_file(path, session_id(), detail::MissingSessionFile::Allow);
  if (!inspected) {
    return std::unexpected(std::move(inspected.error()));
  }

  std::ofstream file(path, std::ios::app);
  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open session file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  if (auto permissions = detail::set_private_session_file_permissions(path); !permissions) {
    return permissions;
  }

  file << *line << '\n';
  file.flush();

  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to write session entry");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  return {};
}

ava::core::Result<std::vector<SessionEntry>> SessionStore::load() const
{
  if (auto valid_session_id = validate_session_id(options_.session_id); !valid_session_id) {
    return std::unexpected(std::move(valid_session_id.error()));
  }

  auto inspected = detail::inspect_session_file(session_path(), session_id(), detail::MissingSessionFile::Allow);
  if (!inspected) {
    return std::unexpected(std::move(inspected.error()));
  }
  if (!*inspected) {
    return std::vector<SessionEntry>{};
  }
  return detail::read_session_entries(session_path(), session_id());
}

ava::core::Result<SessionStore> SessionStore::create(std::filesystem::path const& workspace_dir,
                                                     std::filesystem::path const& root_dir)
{
  return SessionStore(SessionStoreOptions{
      .root_dir = root_dir,
      .workspace_dir = workspace_dir,
      .session_id = ava::core::make_id("session"),
  });
}

ava::core::Result<SessionStore> SessionStore::open(std::filesystem::path const& workspace_dir, std::string session_id,
                                                   std::filesystem::path const& root_dir)
{
  if (auto valid_session_id = validate_session_id(session_id); !valid_session_id) {
    return std::unexpected(std::move(valid_session_id.error()));
  }

  SessionStore store(SessionStoreOptions{
      .root_dir = root_dir,
      .workspace_dir = workspace_dir,
      .session_id = std::move(session_id),
  });
  auto inspected =
      detail::inspect_session_file(store.session_path(), store.session_id(), detail::MissingSessionFile::NotFoundError);
  if (!inspected) {
    return std::unexpected(std::move(inspected.error()));
  }

  return store;
}

ava::core::Result<std::vector<SessionSummary>> SessionStore::list_sessions(std::filesystem::path const& workspace_dir,
                                                                           std::filesystem::path const& root_dir)
{
  auto const directory = detail::session_project_directory(root_dir, workspace_dir);
  std::error_code exists_error;
  bool const directory_exists = std::filesystem::exists(directory, exists_error);
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
    auto const& entry = *iter;
    if (!detail::is_listable_session_file(entry)) {
      continue;
    }
    auto const session_id = entry.path().stem().string();
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

  detail::sort_session_summaries(summaries);

  return summaries;
}

std::filesystem::path SessionStore::default_root_dir()
{
  return ava::config::xdg_paths().sessions_dir;
}

std::string to_string(EntryType type)
{
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
    case EntryType::ModelChange:
      return "model_change";
    case EntryType::ReasoningBlock:
      return "reasoning_block";
    case EntryType::ReasoningChange:
      return "reasoning_change";
    case EntryType::Compaction:
      return "compaction";
    case EntryType::Error:
      return "error";
    case EntryType::Cancel:
      return "cancel";
  }
  return "error";
}

ava::core::Result<EntryType> parse_entry_type(std::string_view value)
{
  if (value == "session_start") return EntryType::SessionStart;
  if (value == "user_message") return EntryType::UserMessage;
  if (value == "assistant_message") return EntryType::AssistantMessage;
  if (value == "tool_call") return EntryType::ToolCall;
  if (value == "tool_result") return EntryType::ToolResult;
  if (value == "permission_decision") return EntryType::PermissionDecision;
  if (value == "mode_change") return EntryType::ModeChange;
  if (value == "model_change") return EntryType::ModelChange;
  if (value == "reasoning_block") return EntryType::ReasoningBlock;
  if (value == "reasoning_change") return EntryType::ReasoningChange;
  if (value == "compaction") return EntryType::Compaction;
  if (value == "error") return EntryType::Error;
  if (value == "cancel") return EntryType::Cancel;

  auto error = ava::core::Error(ava::core::ErrorCategory::Session, "unknown session entry type");
  error.with_context("type", std::string(value));
  return std::unexpected(std::move(error));
}

bool is_internal_replay_user_message(SessionEntry const& entry)
{
  if (entry.type != EntryType::UserMessage) return false;
  auto const start = ava::core::json::field_value_start(entry.data_json, "internal_replay");
  return start && entry.data_json.substr(*start, 4) == "true";
}

std::string now_timestamp()
{
  auto const now = std::chrono::system_clock::now();
  auto const time = std::chrono::system_clock::to_time_t(now);
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

std::string json_escape(std::string_view value)
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

}  // namespace ava::session
