#pragma once

#include "ava/observability/run_observer.h"
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
};

class SessionStore
{
 public:
  explicit SessionStore(SessionStoreOptions options);

  [[nodiscard]] std::string const& session_id() const noexcept;
  [[nodiscard]] std::filesystem::path session_path() const;
  [[nodiscard]] bool is_ephemeral() const noexcept;

  // Observation is non-authoritative and never serialized into session JSONL.
  // Copies of a store share this attachment. A generation lets a finished stale
  // run clear only the attachment it installed, never a newer run's attachment.
  // This is a no-throw best-effort transaction. It returns zero on a disabled
  // observer or setup failure and leaves any existing attachment unchanged.
  [[nodiscard]] std::uint64_t set_run_observation(std::shared_ptr<ava::observability::RunObservation> const& observation,
                                                  ava::observability::TraceContext const& context = {}) noexcept;
  void clear_run_observation(std::uint64_t generation) noexcept;
  // Test-only deterministic fault injection for the no-throw attachment path.
  void fail_next_run_observation_attachment_for_test() noexcept;
  // A background persistence copy intentionally has no live trace attachment.
  // It may write the same session, but cannot inherit or clear a newer run.
  [[nodiscard]] SessionStore detached_copy_for_background_persistence() const;

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
  struct ObservationAttachment;

  explicit SessionStore(SessionStoreOptions options, std::shared_ptr<EphemeralState> ephemeral_state);

  SessionStoreOptions options_;
  std::shared_ptr<EphemeralState> ephemeral_state_;
  std::shared_ptr<ObservationAttachment> observation_attachment_;
};

[[nodiscard]] std::string to_string(EntryType type);
[[nodiscard]] ava::core::Result<EntryType> parse_entry_type(std::string_view value);
[[nodiscard]] bool is_internal_replay_user_message(SessionEntry const& entry);
[[nodiscard]] std::string now_timestamp();
[[nodiscard]] std::string json_escape(std::string_view value);

}  // namespace ava::session
