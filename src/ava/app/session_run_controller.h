#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/agent/run_phase.h"
#include "ava/session/session_store.h"
#include "ava/core/result.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ava::app {

// The app owns lifecycle policy; AgentLoop publishes the same boundary enum.
using RunPhase = ava::agent::RunPhase;

enum class StopReason
{
  Completed,
  UserCanceled,
  Deadline,
  MaxTurns,
  MaxToolCalls,
  NoProgress,
  ProviderError,
  ToolError,
  PersistenceError,
};

struct RunRequest
{
  std::string request_id;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Admission is intentionally bounded: callers with the active correlation may
// wait for that request's outcome; a different request is rejected rather than
// creating an unbounded backend prompt queue. RPC/TUI keep their own visible
// follow-up queues before invoking run_prompt.
enum class AdmissionDisposition
{
  Admit,
  JoinExistingOutcome,
  RejectDifferentRequest,
  RejectMaintenanceReservation,
  RejectClosing,
  RejectPersistenceFailure,
};

struct RunCommand
{
  enum class Kind
  {
    Steering,
    FollowUp,
    Wake
  };
  Kind kind = Kind::Wake;
  std::string correlation_id;
  std::string message;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct RunOutcome
{
  std::string run_id;
  StopReason reason = StopReason::Completed;
  std::optional<ava::core::Error> error = std::nullopt;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct RunSnapshot
{
  bool active = false;
  bool maintenance_reserved = false;
  std::string run_id;
  RunPhase phase = RunPhase::Admitted;
  bool stop_requested = false;
  std::size_t queued_commands = 0;
  std::size_t queued_appends = 0;
  std::size_t queued_append_bytes = 0;
  std::optional<RunOutcome> outcome = std::nullopt;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

inline constexpr std::size_t kMaxSessionRunCommands = 64;
inline constexpr std::size_t kMaxSessionRunCommandBytes = 64 * 1024;
inline constexpr std::size_t kMaxSessionAppendQueueEntries = 256;
inline constexpr std::size_t kMaxSessionAppendQueueBytes = 4 * 1024 * 1024;
inline constexpr std::size_t kMaxRetainedRunOutcomes = 64;

class SessionRunController;
class SessionMaintenanceReservation;

class ActiveRunGuard
{
 public:
  ActiveRunGuard() = default;
  ~ActiveRunGuard();
  ActiveRunGuard(ActiveRunGuard&& other) noexcept;
  ActiveRunGuard& operator=(ActiveRunGuard&& other) noexcept;
  ActiveRunGuard(ActiveRunGuard const&) = delete;
  ActiveRunGuard& operator=(ActiveRunGuard const&) = delete;

  [[nodiscard]] std::stop_token stop_token() const noexcept;
  [[nodiscard]] bool stop_requested() const noexcept;
  [[nodiscard]] ava::core::VoidResult transition(RunPhase next);
  // Returns the effective terminal outcome. Persistence failure may override
  // the caller's proposed result after queued writes drain.
  [[nodiscard]] ava::core::Result<RunOutcome> complete(RunOutcome outcome);
  [[nodiscard]] bool active() const noexcept;
  // Immutable-generation route.  Copies remain safe after session teardown
  // because they only retain controller state, never runtime::Session.
  [[nodiscard]] ava::agent::SessionAppendSink append_route() const;
  [[nodiscard]] ava::agent::SessionAppendBatchSink append_batch_route() const;

 private:
  struct State;
  explicit ActiveRunGuard(std::shared_ptr<State> state, std::uint64_t generation);
  void release() noexcept;

  std::shared_ptr<State> state_;
  std::uint64_t generation_ = 0;
  friend class SessionRunController;
  friend class SessionMaintenanceReservation;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Move-only exclusive authority for short current-session maintenance. It
// excludes normal run admission and every owner append admission without
// directly owning or exposing the underlying SessionAppendTarget.
class SessionMaintenanceReservation
{
 public:
  SessionMaintenanceReservation() = default;
  ~SessionMaintenanceReservation();
  SessionMaintenanceReservation(SessionMaintenanceReservation&& other) noexcept;
  SessionMaintenanceReservation& operator=(SessionMaintenanceReservation&& other) noexcept;
  SessionMaintenanceReservation(SessionMaintenanceReservation const&) = delete;
  SessionMaintenanceReservation& operator=(SessionMaintenanceReservation const&) = delete;

  [[nodiscard]] bool active() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  explicit SessionMaintenanceReservation(std::shared_ptr<ActiveRunGuard::State> state, std::uint64_t generation);
  void release() noexcept;

  std::shared_ptr<ActiveRunGuard::State> state_;
  std::uint64_t generation_ = 0;
  friend class SessionRunController;
};

class SessionRunController
{
 public:
  explicit SessionRunController(std::shared_ptr<ava::session::SessionAppendTarget> append_target);
  ~SessionRunController();
  SessionRunController(SessionRunController const&) = delete;
  SessionRunController& operator=(SessionRunController const&) = delete;
  SessionRunController(SessionRunController&&) = delete;
  SessionRunController& operator=(SessionRunController&&) = delete;

  [[nodiscard]] AdmissionDisposition inspect_admission(RunRequest const& request) const;
  [[nodiscard]] ava::core::Result<ActiveRunGuard> admit(RunRequest request);
  // Nonblocking exclusive acquisition. The append driver lock is sampled
  // before controller state, so an already queued or in-flight append rejects
  // atomically rather than being waited out and missed.
  [[nodiscard]] ava::core::Result<SessionMaintenanceReservation> reserve_maintenance();
  // Wait only joins the currently active request (or returns its most recent
  // terminal result). It never admits work or waits for a different request.
  [[nodiscard]] ava::core::Result<RunOutcome> wait_outcome(std::string_view correlation_id);
  [[nodiscard]] ava::core::VoidResult wake(RunCommand command);
  // Consume only the requested kind; other correlated commands remain queued
  // for their owning adapter.
  [[nodiscard]] ava::core::Result<std::deque<RunCommand>> take_commands(std::string_view correlation_id, RunCommand::Kind kind = RunCommand::Kind::Steering);
  // Stop is a request only. Returns true only when this call won arbitration
  // before Completing; terminal/duplicate requests are bounded no-ops.
  [[nodiscard]] ava::core::Result<bool> request_stop(StopReason reason = StopReason::UserCanceled);
  [[nodiscard]] RunSnapshot snapshot() const;

  // Compatibility owner route for notifications that are not tied to a run.
  // It is still session-bound, serialized, and rejects once terminal/closed.
  [[nodiscard]] ava::core::VoidResult append(ava::session::SessionEntry entry);
  [[nodiscard]] ava::core::Result<ava::session::SessionConditionalAppendResult> append_branch_summary_if_absent(
      ava::session::SessionEntry entry, ava::session::SessionCancelCallback cancel_requested = nullptr);
  [[nodiscard]] ava::core::VoidResult append_batch(std::vector<ava::session::SessionEntry> entries);
  [[nodiscard]] ava::agent::SessionAppendSink owner_append_route() const;
  [[nodiscard]] ava::agent::SessionAppendBatchSink owner_append_batch_route() const;
  // Reject new work, finish or fail accepted appends, then release the target.
  // Stale copied routes retain only controller state and cannot retain a lease.
  void shutdown() noexcept;
  // Explicit recovery boundary after a persistence latch. Never clears it
  // implicitly on later admission.
  [[nodiscard]] ava::core::VoidResult reset_persistence_failure();

 private:
  [[nodiscard]] static ava::core::VoidResult append_for_generation(std::shared_ptr<ActiveRunGuard::State> const& state, std::uint64_t generation,
                                                                   std::vector<ava::session::SessionEntry> entries, bool owner_route,
                                                                   ava::session::SessionConditionalAppendResult* conditional_result = nullptr,
                                                                   ava::session::SessionCancelCallback cancel_requested = nullptr);
  std::shared_ptr<ActiveRunGuard::State> state_;
  friend class ActiveRunGuard;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[nodiscard]] std::string_view to_string(RunPhase phase);
[[nodiscard]] std::string_view to_string(StopReason reason);
[[nodiscard]] std::string_view to_string(AdmissionDisposition disposition);

}  // namespace ava::app
