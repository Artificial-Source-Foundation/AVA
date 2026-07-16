#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/observability/run_observer.h"
#include "ava/agent/mode.h"
#include "ava/debug/print_members_on.h"
#include "ava/core/result.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

struct SessionReadLimits
{
  std::size_t max_file_bytes = 8U * 1024U * 1024U;
  std::size_t max_line_bytes = 1024U * 1024U;
  std::size_t max_entries = 16384;
  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct SessionListLimits
{
  SessionReadLimits per_session = {};
  std::size_t max_sessions = 4096;
  std::size_t max_total_file_bytes = 32U * 1024U * 1024U;
  AVA_DEBUG_PRINT_MEMBERS_ON
};

using SessionCancelCallback = std::function<bool()>;
using SessionEntryVisitor = std::function<ava::core::Result<bool>(SessionEntry const&)>;

struct SessionSummary
{
  std::string session_id = {};
  std::filesystem::path path = {};
  std::string last_updated = {};
  std::size_t entry_count = 0;
  std::filesystem::path original_cwd = {};
  std::string title = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Exclusive advisory lease for one regular session file. The originally
// requested final path component is opened with O_NOFOLLOW; the descriptor is
// CLOEXEC and the lock is released automatically on destruction.
class SessionLease
{
 public:
  SessionLease() = default;
  SessionLease(SessionLease const&) = delete;
  SessionLease& operator=(SessionLease const&) = delete;
  SessionLease(SessionLease&& other) noexcept;
  SessionLease& operator=(SessionLease&& other) noexcept;
  ~SessionLease();

  [[nodiscard]] static ava::core::Result<SessionLease> create_and_acquire(std::filesystem::path const& session_path);
  [[nodiscard]] static ava::core::Result<SessionLease> acquire(std::filesystem::path const& session_path);
  [[nodiscard]] std::filesystem::path const& canonical_path() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  friend class SessionStore;

  SessionLease(int fd, std::filesystem::path canonical_path, bool created);
  int fd_ = -1;
  std::filesystem::path canonical_path_;
  bool created_ = false;
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
  // Removes a newly-created persistent session only when the supplied active
  // lease still identifies its sole linked pathname. Intended for rollback
  // before ownership is transferred into a runtime::Session.
  [[nodiscard]] ava::core::VoidResult remove_created_file(SessionLease const& lease) const;
  // Test-only deterministic race seams. Production callers must not install them.
  void set_before_append_identity_check_for_test(std::function<void()> hook);
  void set_after_append_write_for_test(std::function<void()> hook);
  void set_after_recovery_quarantine_publication_for_test(std::function<void()> hook);
  void set_before_created_file_rollback_detach_for_test(std::function<void()> hook);
  void set_after_created_file_rollback_detach_for_test(std::function<void()> hook);
  // Repairs only a single incomplete final JSONL record while the matching
  // exclusive lease is held. All scanning and mutation are subject to explicit
  // read limits and cancellation. A returned path names the quarantined exact
  // tail; no path means the file was already framed or only needed a final LF.
  [[nodiscard]] ava::core::Result<std::optional<std::filesystem::path>> recover_torn_tail(SessionLease const& lease, SessionReadLimits limits,
                                                                                          SessionCancelCallback cancel_requested = nullptr) const;
  [[nodiscard]] ava::core::Result<std::vector<SessionEntry>> load() const;
  [[nodiscard]] ava::core::Result<std::vector<SessionEntry>> load_bounded(SessionReadLimits limits, SessionCancelCallback cancel_requested = nullptr) const;
  [[nodiscard]] ava::core::VoidResult visit_entries(SessionReadLimits limits, SessionEntryVisitor const& visitor,
                                                    SessionCancelCallback cancel_requested = nullptr) const;
  [[nodiscard]] ava::core::Result<SessionSummary> inspect_bounded(SessionReadLimits limits, SessionCancelCallback cancel_requested = nullptr) const;

  [[nodiscard]] static ava::core::Result<SessionStore> create(std::filesystem::path const& workspace_dir,
                                                              std::filesystem::path const& root_dir = default_root_dir());
  [[nodiscard]] static ava::core::Result<SessionStore> create_ephemeral(std::filesystem::path const& workspace_dir);
  [[nodiscard]] static ava::core::Result<SessionStore> open(std::filesystem::path const& workspace_dir, std::string session_id,
                                                            std::filesystem::path const& root_dir = default_root_dir());
  [[nodiscard]] static ava::core::Result<std::vector<SessionSummary>> list_sessions(std::filesystem::path const& workspace_dir,
                                                                                    std::filesystem::path const& root_dir = default_root_dir());
  [[nodiscard]] static ava::core::Result<std::vector<SessionSummary>> list_sessions_bounded(std::filesystem::path const& workspace_dir,
                                                                                            std::filesystem::path const& root_dir, SessionListLimits limits,
                                                                                            SessionCancelCallback cancel_requested = nullptr);
  [[nodiscard]] static std::filesystem::path default_root_dir();

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  struct EphemeralState;
  struct ObservationAttachment;

  [[nodiscard]] ava::core::Result<SessionSummary> inspect_bounded_for_listing(SessionReadLimits limits, bool inspect_metadata,
                                                                              SessionCancelCallback cancel_requested = nullptr) const;
  explicit SessionStore(SessionStoreOptions options, std::shared_ptr<EphemeralState> ephemeral_state);

  SessionStoreOptions options_;
  std::shared_ptr<EphemeralState> ephemeral_state_;
  std::shared_ptr<ObservationAttachment> observation_attachment_;
  std::function<void()> before_append_identity_check_for_test_;
  std::function<void()> after_append_write_for_test_;
  std::function<void()> after_recovery_quarantine_publication_for_test_;
  std::function<void()> before_created_file_rollback_detach_for_test_;
  std::function<void()> after_created_file_rollback_detach_for_test_;
};

// Legacy CLI/TUI/RPC reads retain their historical unbounded file/entry policy
// while still enforcing the hard session-line and strict JSON nesting bounds.
[[nodiscard]] SessionReadLimits legacy_unbounded_session_read_limits();

[[nodiscard]] std::string to_string(EntryType type);
[[nodiscard]] ava::core::Result<EntryType> parse_entry_type(std::string_view value);
[[nodiscard]] bool is_internal_replay_user_message(SessionEntry const& entry);
[[nodiscard]] std::string now_timestamp();
[[nodiscard]] std::string json_escape(std::string_view value);

}  // namespace ava::session
