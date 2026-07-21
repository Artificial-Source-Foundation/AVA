#pragma once

#include "ava/agent/background_job_registry.h"
#include "ava/agent/job_journal.h"
#include "ava/agent/question.h"
#include "ava/permissions/permission.h"
#include "ava/core/result.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ava::agent {

// Error context exposed by failed start_background() calls. AgentLoop may
// remove a newly created child only for ProvenUnpublished; all other failures
// conservatively retain the child session for durable recovery.
enum class SubagentPublicationCommitState
{
  ProvenUnpublished,
  PublicationUncertain,
};

[[nodiscard]] std::string_view to_string(SubagentPublicationCommitState value) noexcept;
[[nodiscard]] SubagentPublicationCommitState subagent_publication_commit_state(ava::core::Error const& error) noexcept;

struct SubagentCoordinatorJobSnapshot
{
  SubagentJobSnapshot job;
  bool timed_out = false;
  bool coordinator_latched = false;
  std::optional<std::string> coordinator_error = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

enum class SubagentWaitMode
{
  Terminal,
  TerminalOrPromotion,
};

// Owns foreground-only frontend callbacks. Promotion first reserves this gate,
// which rejects an outstanding interaction and prevents any new callback from
// starting while the durable Promoted transition is committed.
class SubagentInteractionGate final : public std::enable_shared_from_this<SubagentInteractionGate>
{
 public:
  [[nodiscard]] static std::shared_ptr<SubagentInteractionGate> create(SubagentJobMode mode, ava::permissions::PermissionResolver permission_resolver,
                                                                       QuestionResolver question_resolver);
  [[nodiscard]] ava::permissions::PermissionResolver permission_resolver();
  [[nodiscard]] QuestionResolver question_resolver();
  [[nodiscard]] ava::core::VoidResult prepare_promotion();
  void commit_promotion();
  void abort_promotion();
  void finish();

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  SubagentInteractionGate(SubagentJobMode mode, ava::permissions::PermissionResolver permission_resolver, QuestionResolver question_resolver);

  std::mutex mutex_;
  SubagentJobMode mode_ = SubagentJobMode::Foreground;
  bool promotion_pending_ = false;
  std::size_t outstanding_ = 0;
  ava::permissions::PermissionResolver permission_resolver_ = nullptr;
  QuestionResolver question_resolver_ = nullptr;
};

struct SubagentCoordinatorOptions
{
  std::filesystem::path ava_state_dir;
  // Optional application-startup authority for the exact logical state root.
  // One coordinator retains this descriptor set for all of its journals, so
  // trusted symlinked ancestors need not be canonicalized or reopened.
  std::shared_ptr<ava::core::AnchorSet const> anchor_set = nullptr;
  BackgroundJobRegistryOptions registry_options = {};
  JobJournalLimits journal_limits = {};
  // Deterministic failure seams for coordinator tests. Production leaves
  // these empty; JobJournal remains the durable authority.
  std::function<ava::core::VoidResult(JobJournalRecord const&)> journal_append_preflight = nullptr;
  std::function<ava::core::VoidResult(SubagentJobIdentity const&)> journal_rollback_preflight = nullptr;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Protocol-neutral notification emitted only after terminal background state
// and DeliveryPending are durable. The application sink must be nonblocking;
// coordinator completion never depends on sink success.
using SubagentTerminalSink = std::function<void(SubagentCoordinatorJobSnapshot const&)>;

class SubagentCoordinator final
{
 public:
  [[nodiscard]] static ava::core::Result<std::shared_ptr<SubagentCoordinator>> create(SubagentCoordinatorOptions options);
  ~SubagentCoordinator();

  SubagentCoordinator(SubagentCoordinator const&) = delete;
  SubagentCoordinator& operator=(SubagentCoordinator const&) = delete;
  SubagentCoordinator(SubagentCoordinator&&) = delete;
  SubagentCoordinator& operator=(SubagentCoordinator&&) = delete;

  // Acquires this parent's process-lifetime journal owner lease, recovers
  // interrupted work once, and restores its bounded durable projection. This
  // is deliberately per-parent: construction never discovers or owns other
  // parents' journals.
  [[nodiscard]] ava::core::VoidResult activate_parent(std::string parent_session_id);

  // Releases an activated parent's journal owner only at an explicit
  // application detach/release boundary. It conservatively retains ownership
  // if a start is being admitted, work is live, or delivery remains pending.
  bool release_parent_if_idle(std::string_view parent_session_id);

  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> start(std::string parent_session_id, SubagentJobMode mode, BackgroundJobStartOptions options,
                                                                        BackgroundJobWorker worker,
                                                                        std::shared_ptr<SubagentInteractionGate> interaction_gate = nullptr);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> start_background(std::string parent_session_id, BackgroundJobStartOptions options,
                                                                                   BackgroundJobWorker worker);
  [[nodiscard]] std::vector<SubagentCoordinatorJobSnapshot> list(std::string_view parent_session_id) const;
  // Durable discovery surface for application delivery queues. Unlike list(),
  // this includes pending jobs evicted from live worker-result retention.
  [[nodiscard]] ava::core::Result<std::vector<SubagentCoordinatorJobSnapshot>> pending_deliveries(std::string_view parent_session_id);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> snapshot(std::string_view parent_session_id, std::string_view job_id);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> wait(std::string_view parent_session_id, std::string_view job_id,
                                                                       std::chrono::milliseconds timeout, SubagentWaitMode mode = SubagentWaitMode::Terminal);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> result(std::string_view parent_session_id, std::string_view job_id);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> cancel(std::string_view parent_session_id, std::string_view job_id);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> promote(std::string_view parent_session_id, std::string_view job_id);

  // Installs the application-owned delivery sink and immediately publishes
  // recovered Pending/Attempting jobs outside coordinator/state/journal locks.
  void set_terminal_sink(SubagentTerminalSink sink);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> record_delivery_attempt(std::string_view parent_session_id, std::string_view job_id,
                                                                                          std::string attempt_id, std::string prompt_fingerprint);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> acknowledge_delivery(std::string_view parent_session_id, std::string_view job_id,
                                                                                       std::string_view attempt_id, std::string committed_turn_id);

  // Rejects admission, durably requests cancellation where possible, then
  // joins workers using the registry's existing cooperative semantics.
  void shutdown();

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct JournalState;
  struct JobState;
  class StartAdmission final
  {
   public:
    StartAdmission(SubagentCoordinator& coordinator, std::string parent_session_id)
        : coordinator_(coordinator), parent_session_id_(std::move(parent_session_id))
    {
    }
    ~StartAdmission();
    StartAdmission(StartAdmission const&) = delete;
    StartAdmission& operator=(StartAdmission const&) = delete;

   private:
    SubagentCoordinator& coordinator_;
    std::string parent_session_id_;
  };

  explicit SubagentCoordinator(SubagentCoordinatorOptions options);
  [[nodiscard]] ava::core::Result<std::shared_ptr<JournalState>> journal_for_parent(std::string const& parent_session_id);
  [[nodiscard]] ava::core::Result<SubagentJobSnapshot> append_transition(std::shared_ptr<JobState> const& state, JobJournalRecord record);
  [[nodiscard]] std::shared_ptr<JobState> find_owned_locked(std::string_view parent_session_id, std::string_view job_id) const;
  [[nodiscard]] ava::core::Result<std::shared_ptr<JobState>> restore_owned(std::string_view parent_session_id, std::string_view job_id);
  [[nodiscard]] SubagentCoordinatorJobSnapshot public_snapshot_locked(JobState const& state, bool timed_out = false) const;
  // Lock order: coordinator mutex -> JobState mutex. JournalState mutex is
  // never held while acquiring the coordinator mutex; durable I/O completes
  // before state/coordinator publication.
  void latch(std::shared_ptr<JobState> const& state, ava::core::Error const& error);
  void synchronize_registry_retention();
  [[nodiscard]] BackgroundJobCompletion persist_terminal(std::shared_ptr<JobState> const& state, BackgroundJobCompletion completion);

  SubagentCoordinatorOptions options_;
  BackgroundJobRegistry registry_;
  mutable std::mutex mutex_;
  std::condition_variable admission_changed_;
  std::unordered_map<std::string, std::size_t> active_starts_by_parent_;
  bool accepting_ = true;
  bool coordinator_latched_ = false;
  bool shutdown_complete_ = false;
  std::unordered_map<std::string, std::shared_ptr<JournalState>> journals_;
  std::unordered_map<std::string, std::shared_ptr<JobState>> jobs_;
  SubagentTerminalSink terminal_sink_ = nullptr;
};

}  // namespace ava::agent
