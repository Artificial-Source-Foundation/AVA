#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/agent/subagent_job.h"
#include "ava/core/AnchorSet.h"
#include "ava/core/result.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::agent {

inline constexpr long long kSubagentJobJournalSchemaVersion = 1;
inline constexpr std::string_view kSubagentJobJournalDirectoryName = "subagent-jobs";

enum class JobJournalTransitionKind
{
  Started,
  Promoted,
  CancelRequested,
  Terminal,
  DeliveryPending,
  DeliveryAttempt,
  DeliveryAck,
  Interrupted,
};

struct JobJournalRecord
{
  long long schema_version = kSubagentJobJournalSchemaVersion;
  JobJournalTransitionKind kind = JobJournalTransitionKind::Started;
  SubagentJobIdentity identity = {};
  std::string at = {};
  std::optional<SubagentJobMode> mode = std::nullopt;
  std::optional<SubagentTerminalState> terminal_state = std::nullopt;
  std::optional<std::string> summary = std::nullopt;
  std::optional<std::string> error_category = std::nullopt;
  std::optional<std::string> error = std::nullopt;
  std::optional<std::string> stop_reason = std::nullopt;
  std::optional<bool> summary_truncated = std::nullopt;
  std::optional<bool> error_truncated = std::nullopt;
  std::optional<bool> stop_reason_truncated = std::nullopt;
  std::optional<std::size_t> provider_iterations = std::nullopt;
  std::optional<std::size_t> tool_calls = std::nullopt;
  std::optional<std::size_t> tool_iterations = std::nullopt;
  std::optional<std::string> attempt_id = std::nullopt;
  std::optional<std::string> prompt_fingerprint = std::nullopt;
  std::optional<std::string> committed_turn_id = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct JobJournalLimits
{
  std::size_t max_file_bytes = 16U * 1024U * 1024U;
  std::size_t max_line_bytes = 48U * 1024U;
  std::size_t max_records = 4096;
  std::size_t max_jobs = 256;
  std::size_t max_id_bytes = 96;
  std::size_t max_timestamp_bytes = 64;
  std::size_t max_summary_bytes = 16U * 1024U;
  std::size_t max_error_bytes = 4U * 1024U;
  std::size_t max_stop_reason_bytes = 1024;
  std::size_t max_accounting_value = 1U * 1024U * 1024U;
  std::size_t max_delivery_attempts_per_job = 64;
  std::size_t max_retained_terminal_jobs = 64;
  std::size_t compact_after_file_bytes = 12U * 1024U * 1024U;
  std::size_t compact_after_records = 3072;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[nodiscard]] std::string_view to_string(JobJournalTransitionKind value) noexcept;
[[nodiscard]] ava::core::Result<JobJournalTransitionKind> parse_job_journal_transition_kind(std::string_view value);
[[nodiscard]] ava::core::Result<SubagentJobProjection> project_job_journal(std::vector<JobJournalRecord> const& records, JobJournalLimits const& limits = {});

// One strict per-parent journal under <ava_state_dir>/subagent-jobs. The
// directory and lock/journal files are descriptor-anchored and owner-only.
// The class is move-only because it owns CLOEXEC descriptors.
class JobJournal final
{
 public:
  JobJournal(JobJournal const&) = delete;
  JobJournal& operator=(JobJournal const&) = delete;
  JobJournal(JobJournal&& other) noexcept;
  JobJournal& operator=(JobJournal&& other) noexcept;
  ~JobJournal();

  [[nodiscard]] static ava::core::Result<JobJournal> open(std::filesystem::path const& ava_state_dir, std::string parent_session_id,
                                                          JobJournalLimits limits = {}, ava::core::AnchorSet const* trusted_state_anchors = nullptr);
  // Coordinator ownership is a separate process-lifetime, nonblocking lease.
  // A disengaged optional means another process currently owns this parent;
  // journals for unrelated parents remain independently acquirable. A runtime
  // may supply its startup-opened shared AnchorSet so a logical state path with
  // trusted symlinked ancestors is used without canonicalizing it.
  [[nodiscard]] static ava::core::Result<std::optional<JobJournal>> try_open_owned(std::filesystem::path const& ava_state_dir, std::string parent_session_id,
                                                                                   JobJournalLimits limits = {},
                                                                                   ava::core::AnchorSet const* trusted_state_anchors = nullptr);

  [[nodiscard]] ava::core::Result<SubagentJobProjection> append(JobJournalRecord const& record);
  // Atomically publishes exactly one terminal background transition followed
  // by its delivery-pending transition. No partial terminal state is exposed.
  [[nodiscard]] ava::core::Result<SubagentJobProjection> append_batch(std::vector<JobJournalRecord> const& records);
  // Removes only a never-published job whose complete durable history is one
  // matching Started transition. This is intentionally stricter than general
  // journal mutation so failed admission cannot erase a live job.
  [[nodiscard]] ava::core::Result<SubagentJobProjection> rollback_unpublished_started(SubagentJobIdentity const& identity);
  [[nodiscard]] ava::core::Result<SubagentJobProjection> projection() const;
  [[nodiscard]] ava::core::Result<SubagentJobProjection> compact();
  // Coordinator-only startup operation. Ordinary open() is non-mutating and
  // never classifies a live in-process job as interrupted.
  [[nodiscard]] ava::core::Result<SubagentJobProjection> recover_interrupted_jobs();
  [[nodiscard]] std::string const& parent_session_id() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  JobJournal(int directory_fd, int lock_fd, int owner_fd, std::string parent_session_id, JobJournalLimits limits);

  int directory_fd_ = -1;
  int lock_fd_ = -1;
  int owner_fd_ = -1;
  std::string parent_session_id_;
  JobJournalLimits limits_;
};

struct OwnedJobJournalRecovery
{
  JobJournal journal;
  SubagentJobProjection projection;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Maintenance/testing discovery acquires each available process-lifetime
// owner lease before recovery and returns the still-owned journals. Normal
// coordinator startup uses per-parent activation instead. Journals actively
// managed by another process are skipped without mutation.
[[nodiscard]] ava::core::Result<std::vector<OwnedJobJournalRecovery>> recover_owned_interrupted_job_journals(
    std::filesystem::path const& ava_state_dir, JobJournalLimits const& limits = {}, ava::core::AnchorSet const* trusted_state_anchors = nullptr);

// Compatibility/testing surface: recovers only currently unowned journals and
// releases each owner lease after producing its projection.
[[nodiscard]] ava::core::Result<std::vector<SubagentJobProjection>> recover_all_interrupted_job_journals(
    std::filesystem::path const& ava_state_dir, JobJournalLimits const& limits = {}, ava::core::AnchorSet const* trusted_state_anchors = nullptr);

}  // namespace ava::agent
