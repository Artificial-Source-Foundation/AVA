#pragma once

#include "ava/agent/background_job_registry.h"
#include "ava/agent/question.h"
#include "ava/agent/subagent_job.h"
#include "ava/permissions/permission.h"
#include "ava/core/result.h"
#include "ava/debug/print_members_on.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ava::agent {

// Opaque path-free live inspection types. Complete definitions live in
// subagent_inspector.h so public coordinator fanout never pulls session types.
class SubagentLiveInspectionSource;
struct SubagentInspectorFrame;

// Error context exposed by failed start() calls. Process-local publication is
// atomic, so coordinator failures are always ProvenUnpublished. The uncertain
// value remains reserved for schema-version-1 API compatibility.
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

// Owns foreground-only frontend callbacks. Promotion reserves this gate before
// changing coordinator state, preventing a new callback from racing promotion.
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
  BackgroundJobRegistryOptions registry_options = {};
  // Deterministic test seam for process-local identity collision coverage.
  // Production leaves this empty and uses ava::core::make_id.
  std::function<std::string(std::string_view)> id_generator = nullptr;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Protocol-neutral notification emitted after terminal background state is
// process-locally published. The application sink must be nonblocking.
using SubagentTerminalSink = std::function<void(SubagentCoordinatorJobSnapshot const&)>;

class SubagentCoordinator final
{
 public:
  [[nodiscard]] static ava::core::Result<std::shared_ptr<SubagentCoordinator>> create(SubagentCoordinatorOptions options = {});
  ~SubagentCoordinator();

  SubagentCoordinator(SubagentCoordinator const&) = delete;
  SubagentCoordinator& operator=(SubagentCoordinator const&) = delete;
  SubagentCoordinator(SubagentCoordinator&&) = delete;
  SubagentCoordinator& operator=(SubagentCoordinator&&) = delete;

  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> start(std::string parent_session_id, SubagentJobMode mode, BackgroundJobStartOptions options,
                                                                        BackgroundJobWorker worker,
                                                                        std::shared_ptr<SubagentInteractionGate> interaction_gate = nullptr,
                                                                        std::shared_ptr<SubagentLiveInspectionSource> inspection_source = nullptr);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> start_background(std::string parent_session_id, BackgroundJobStartOptions options,
                                                                                   BackgroundJobWorker worker,
                                                                                   std::shared_ptr<SubagentLiveInspectionSource> inspection_source = nullptr);
  [[nodiscard]] std::vector<SubagentCoordinatorJobSnapshot> list(std::string_view parent_session_id) const;
  [[nodiscard]] ava::core::Result<std::vector<SubagentCoordinatorJobSnapshot>> pending_deliveries(std::string_view parent_session_id);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> snapshot(std::string_view parent_session_id, std::string_view job_id);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> wait(std::string_view parent_session_id, std::string_view job_id,
                                                                       std::chrono::milliseconds timeout, SubagentWaitMode mode = SubagentWaitMode::Terminal);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> result(std::string_view parent_session_id, std::string_view job_id);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> cancel(std::string_view parent_session_id, std::string_view job_id);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> promote(std::string_view parent_session_id, std::string_view job_id);
  // Owner-bound path-free live inspection. known_generation skips reload when
  // the lease-bound fingerprint is unchanged for that generation.
  [[nodiscard]] ava::core::Result<std::shared_ptr<SubagentInspectorFrame const>> inspect(std::string_view parent_session_id, std::string_view job_id,
                                                                                         std::optional<std::uint64_t> known_generation = std::nullopt);

  void set_terminal_sink(SubagentTerminalSink sink);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> record_delivery_attempt(std::string_view parent_session_id, std::string_view job_id,
                                                                                          std::string attempt_id, std::string prompt_fingerprint);
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> acknowledge_delivery(std::string_view parent_session_id, std::string_view job_id,
                                                                                       std::string_view attempt_id, std::string committed_turn_id);
  // Settles retry exhaustion without changing the public schema or delivery
  // enum. The latest bounded result remains queryable until normal retention.
  [[nodiscard]] ava::core::Result<SubagentCoordinatorJobSnapshot> exhaust_delivery(std::string_view parent_session_id, std::string_view job_id,
                                                                                   std::string_view attempt_id);

  void shutdown();

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct JobState;
  class StartAdmission final
  {
   public:
    explicit StartAdmission(SubagentCoordinator& coordinator) : coordinator_(coordinator) { }
    ~StartAdmission();
    StartAdmission(StartAdmission const&) = delete;
    StartAdmission& operator=(StartAdmission const&) = delete;

    AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

   private:
    SubagentCoordinator& coordinator_;
  };

  explicit SubagentCoordinator(SubagentCoordinatorOptions options);
  [[nodiscard]] std::shared_ptr<JobState> find_owned_locked(std::string_view parent_session_id, std::string_view job_id) const;
  [[nodiscard]] SubagentCoordinatorJobSnapshot public_snapshot_locked(JobState const& state, bool timed_out = false) const;
  [[nodiscard]] BackgroundJobCompletion complete(std::shared_ptr<JobState> const& state, BackgroundJobCompletion completion);
  void publish_terminal_notification(std::shared_ptr<JobState> const& state) noexcept;
  void prune_eligible_locked();
  [[nodiscard]] bool erase_oldest_eligible_locked();

  SubagentCoordinatorOptions options_;
  BackgroundJobRegistry registry_;
  // Protects map/admission/sink state. When both locks are required, acquire
  // this mutex before JobState::mutex; external components are called unlocked.
  mutable std::mutex mutex_;
  std::condition_variable admission_changed_;
  std::size_t active_starts_ = 0;
  bool accepting_ = true;
  bool shutdown_complete_ = false;
  std::size_t next_job_sequence_ = 1;
  std::unordered_map<std::string, std::shared_ptr<JobState>> jobs_;
  SubagentTerminalSink terminal_sink_ = nullptr;
};

}  // namespace ava::agent
