#pragma once

#include "ava/agent/mode.h"
#include "ava/core/result.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "debug.h"

namespace ava::session {

inline constexpr long long kCurrentSessionEntryVersion = 3;

enum class EntryType
{
  SessionStart,
  SessionMetadata,
  UserMessage,
  AssistantMessage,
  ToolCall,
  ToolResult,
  PermissionDecision,
  ModeChange,
  ModelChange,
  ReasoningBlock,
  ReasoningChange,
  Compaction,
  BranchSummary,
  Error,
  Cancel,
};

struct SessionEntry
{
  std::string id;
  std::string parent_id;
  EntryType type;
  std::string timestamp;
  std::string data_json;
  long long version = kCurrentSessionEntryVersion;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct SessionStoreOptions
{
  std::filesystem::path root_dir;
  std::filesystem::path workspace_dir;
  std::string session_id;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct SessionSummary
{
  std::string session_id;
  std::filesystem::path path;
  std::string last_updated;
  std::size_t entry_count = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

class SessionStore
{
 public:
  explicit SessionStore(SessionStoreOptions options);

  [[nodiscard]] std::string const& session_id() const noexcept;
  [[nodiscard]] std::filesystem::path session_path() const;
  [[nodiscard]] bool is_ephemeral() const noexcept;

  [[nodiscard]] ava::core::VoidResult append(SessionEntry const& entry);
  [[nodiscard]] ava::core::Result<std::vector<SessionEntry>> load() const;

  [[nodiscard]] static ava::core::Result<SessionStore> create(std::filesystem::path const& workspace_dir,
                                                              std::filesystem::path const& root_dir = default_root_dir());
  [[nodiscard]] static ava::core::Result<SessionStore> create_ephemeral(std::filesystem::path const& workspace_dir);
  [[nodiscard]] static ava::core::Result<SessionStore> open(std::filesystem::path const& workspace_dir, std::string session_id,
                                                            std::filesystem::path const& root_dir = default_root_dir());
  [[nodiscard]] static ava::core::Result<std::vector<SessionSummary>> list_sessions(std::filesystem::path const& workspace_dir,
                                                                                    std::filesystem::path const& root_dir = default_root_dir());
  [[nodiscard]] static std::filesystem::path default_root_dir();

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  struct EphemeralState;

  explicit SessionStore(SessionStoreOptions options, std::shared_ptr<EphemeralState> ephemeral_state);

  SessionStoreOptions options_;
  std::shared_ptr<EphemeralState> ephemeral_state_;
};

[[nodiscard]] std::string to_string(EntryType type);
[[nodiscard]] ava::core::Result<EntryType> parse_entry_type(std::string_view value);
[[nodiscard]] bool is_internal_replay_user_message(SessionEntry const& entry);
[[nodiscard]] std::string now_timestamp();
[[nodiscard]] std::string json_escape(std::string_view value);

}  // namespace ava::session
