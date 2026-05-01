#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "ava/agent/mode.h"
#include "ava/core/result.h"

namespace ava::session {

enum class EntryType {
  SessionStart,
  UserMessage,
  AssistantMessage,
  ToolCall,
  ToolResult,
  PermissionDecision,
  ModeChange,
  Compaction,
  Error,
  Cancel,
};

struct SessionEntry {
  std::string id;
  std::string parent_id;
  EntryType type;
  std::string timestamp;
  std::string data_json;
};

struct SessionStoreOptions {
  std::filesystem::path root_dir;
  std::filesystem::path workspace_dir;
  std::string session_id;
};

struct SessionSummary {
  std::string session_id;
  std::filesystem::path path;
  std::string last_updated;
  std::size_t entry_count = 0;
};

class SessionStore {
 public:
  explicit SessionStore(SessionStoreOptions options);

  [[nodiscard]] const std::string& session_id() const noexcept;
  [[nodiscard]] std::filesystem::path session_path() const;

  [[nodiscard]] ava::core::VoidResult append(const SessionEntry& entry);
  [[nodiscard]] ava::core::Result<std::vector<SessionEntry>> load() const;

  [[nodiscard]] static ava::core::Result<SessionStore> create(
      const std::filesystem::path& workspace_dir,
      const std::filesystem::path& root_dir = default_root_dir());
  [[nodiscard]] static ava::core::Result<SessionStore> open(
      const std::filesystem::path& workspace_dir,
      std::string session_id,
      const std::filesystem::path& root_dir = default_root_dir());
  [[nodiscard]] static ava::core::Result<std::vector<SessionSummary>> list_sessions(
      const std::filesystem::path& workspace_dir,
      const std::filesystem::path& root_dir = default_root_dir());
  [[nodiscard]] static std::filesystem::path default_root_dir();
 private:
  SessionStoreOptions options_;
};

[[nodiscard]] std::string to_string(EntryType type);
[[nodiscard]] ava::core::Result<EntryType> parse_entry_type(std::string_view value);
[[nodiscard]] bool is_internal_replay_user_message(const SessionEntry& entry);
[[nodiscard]] std::string now_timestamp();
[[nodiscard]] std::string json_escape(std::string_view value);

}  // namespace ava::session
