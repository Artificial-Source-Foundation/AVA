#pragma once

#include "ava/process/supervisor.h"
#include "ava/core/error.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/types.h>
#endif

namespace ava::process::detail {

using Clock = std::chrono::steady_clock;
using AfterForkBeforeReleaseHook = std::function<void()>;

inline constexpr auto kMonitorPollInterval = std::chrono::milliseconds(10);
inline constexpr auto kPostKillObservation = std::chrono::milliseconds(20);
inline constexpr auto kDefaultCleanupBudget = std::chrono::seconds(2);
inline constexpr auto kMaximumStartupTimeout = std::chrono::seconds(30);

[[nodiscard]] ava::core::Error process_error(ava::core::ErrorCategory category, std::string message);
[[nodiscard]] ava::core::Error unsupported_error();
[[nodiscard]] ava::core::Error invalid_error(std::string message);
[[nodiscard]] ava::core::Error io_error(std::string message, int error_number);
[[nodiscard]] bool requestable_reason(TerminationReasonV1 reason) noexcept;
[[nodiscard]] std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept;
[[nodiscard]] std::uint64_t elapsed_milliseconds(Clock::time_point begin, Clock::time_point end) noexcept;

struct SupervisorState;

struct HandleState
{
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::optional<ExitStatusV1> final_status;
  std::weak_ptr<SupervisorState> supervisor;
  std::uint64_t record = 0;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct Record
{
  Record(std::uint64_t identity, OwnerPathV1 owner_path, std::string key, std::uint64_t aliased_owner, ProcessRoleV1 process_role,
         LifecyclePolicyV1 lifecycle_policy)
      : id(identity),
        owner(std::move(owner_path)),
        owner_key(std::move(key)),
        owner_alias(aliased_owner),
        role(process_role),
        policy(lifecycle_policy),
        created(Clock::now())
  {
  }

  std::uint64_t id = 0;
  OwnerPathV1 owner;
  std::string owner_key;
  std::uint64_t owner_alias = 0;
  ProcessRoleV1 role = ProcessRoleV1::Curl;
  LifecyclePolicyV1 policy;
  ProcessStateV1 state = ProcessStateV1::Reserved;
  std::optional<TerminationReasonV1> reason;
  CleanupStateV1 cleanup = CleanupStateV1::NotRequired;
  ExitKindV1 exit_kind = ExitKindV1::None;
  Clock::time_point created;
  std::optional<Clock::time_point> finished;
  std::optional<ProcessDeadline> stop_deadline;
  std::optional<Clock::time_point> kill_due;
  std::optional<Clock::time_point> post_kill_due;
  std::shared_ptr<HandleState> handle;
  std::uint64_t stdout_bytes = 0;
  std::uint64_t stderr_bytes = 0;
  std::uint32_t settlement_count = 0;
  int exit_code = 0;
  int signal_number = 0;
  bool stdout_truncated = false;
  bool stderr_truncated = false;
  bool has_exit_code = false;
  bool has_signal_number = false;
  bool registered = false;
  bool group_verified = false;
  bool release_committed = false;
  bool startup_handshake_complete = false;
  bool included_in_shutdown = false;
  bool term_sent = false;
  bool kill_sent = false;
  bool cleanup_failed = false;
  std::uint8_t quiet_group_observations = 0;
#if !defined(_WIN32)
  pid_t leader = -1;
  pid_t sentinel = -1;
  bool leader_observed = false;
  bool sentinel_observed = false;
  bool leader_reaped = false;
  bool sentinel_reaped = false;
#endif

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct OwnerAliasEntry
{
  std::uint64_t alias = 0;
  std::size_t references = 0;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct SupervisorState
{
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::map<std::uint64_t, std::unique_ptr<Record>> records;
  std::deque<std::uint64_t> terminal_fifo;
  std::map<std::string, OwnerAliasEntry> owner_aliases;
  std::shared_ptr<AfterForkBeforeReleaseHook> after_fork_before_release_for_test;
  std::uint64_t next_record = 1;
  std::uint64_t next_owner_alias = 1;
  std::size_t live_records = 0;
  std::size_t settled_records = 0;
  bool accepting = true;
  bool shutting_down = false;
  bool monitor_started = false;
  bool stop_monitor = false;
  bool monitor_joined = false;
  std::mutex monitor_lifecycle_mutex;
  std::thread monitor;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

void release_owner_alias_locked(SupervisorState& state, Record const& record);
void prune_terminal_locked(SupervisorState& state);
void await_internal_settlement(std::shared_ptr<HandleState> const& handle, ProcessDeadline deadline) noexcept;
void publish_final_locked(Record& record, ExitStatusV1 status);
void finalize_locked(SupervisorState& state, Record& record, CleanupStateV1 cleanup);
[[nodiscard]] bool commit_reason_locked(Record& record, TerminationReasonV1 reason) noexcept;
void abandon_reservation(std::shared_ptr<SupervisorState> const& state, std::uint64_t identity) noexcept;
void finish_unregistered(std::shared_ptr<SupervisorState> const& state, std::uint64_t identity, TerminationReasonV1 fallback,
                         CleanupStateV1 cleanup = CleanupStateV1::NotRequired) noexcept;
[[nodiscard]] ProcessDeadline startup_deadline_for_record(std::shared_ptr<SupervisorState> const& state, std::uint64_t identity) noexcept;
[[nodiscard]] ava::core::VoidResult invoke_after_fork_before_release_hook(std::shared_ptr<SupervisorState> const& state);

struct GateReleaseDecision
{
  bool committed = false;
  TerminationReasonV1 reason = TerminationReasonV1::LaunchFailed;
  ProcessDeadline cleanup_deadline{};

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// The caller holds state.mutex. Committing Running is the launch linearization
// point; physical gate writes happen afterward without the supervisor lock.
[[nodiscard]] GateReleaseDecision commit_gate_release_locked(SupervisorState& state, std::uint64_t identity, ProcessDeadline startup_deadline) noexcept;
[[nodiscard]] ava::core::Error canceled_launch_error(std::string operation, TerminationReasonV1 reason);
[[nodiscard]] ProcessDeadline fail_registered_launch(std::shared_ptr<SupervisorState> const& state, std::uint64_t identity,
                                                     TerminationReasonV1 fallback) noexcept;

#if !defined(_WIN32)

class UniqueFd final
{
 public:
  explicit UniqueFd(int descriptor = -1) noexcept : descriptor_(descriptor) { }
  UniqueFd(UniqueFd const&) = delete;
  UniqueFd& operator=(UniqueFd const&) = delete;
  UniqueFd(UniqueFd&& other) noexcept : descriptor_(other.release()) { }
  UniqueFd& operator=(UniqueFd&& other) noexcept
  {
    if (this != &other)
      reset(other.release());
    return *this;
  }
  ~UniqueFd() { reset(); }

  [[nodiscard]] int get() const noexcept { return descriptor_; }
  [[nodiscard]] int release() noexcept { return std::exchange(descriptor_, -1); }
  void reset(int descriptor = -1) noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  int descriptor_ = -1;
};

struct Pipe
{
  UniqueFd read_end;
  UniqueFd write_end;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[nodiscard]] ava::core::Result<int> move_above_standard_descriptors(int descriptor);
[[nodiscard]] ava::core::Result<Pipe> make_cloexec_pipe();
[[nodiscard]] ava::core::VoidResult set_nonblocking(int descriptor);
[[nodiscard]] ava::core::Result<bool> wait_descriptor(int descriptor, short events, ProcessDeadline deadline);
[[nodiscard]] bool write_without_sigpipe(int descriptor, void const* data, std::size_t size) noexcept;
[[nodiscard]] pid_t waitpid_retry(pid_t process, int* status, int options) noexcept;
[[nodiscard]] int waitid_retry(pid_t process, siginfo_t* information, int options) noexcept;
[[nodiscard]] bool exact_provisional_cleanup(pid_t leader, pid_t sentinel, ProcessDeadline deadline) noexcept;
[[nodiscard]] bool reset_child_signal_state() noexcept;
[[nodiscard]] ssize_t child_read_retry(int descriptor, void* data, std::size_t size) noexcept;
[[nodiscard]] bool child_write_all(int descriptor, void const* data, std::size_t size) noexcept;
void close_nonstandard_descriptors(int preserved, int maximum) noexcept;
[[nodiscard]] int descriptor_limit() noexcept;

void observe_status(Record& record, siginfo_t const& information) noexcept;
[[nodiscard]] bool observe_member(pid_t process, bool& observed, Record& record, bool leader) noexcept;
[[nodiscard]] bool reap_member(pid_t process, bool observed, bool& reaped, Record& record) noexcept;
[[nodiscard]] bool signal_verified_group(Record& record, int signal_number) noexcept;
[[nodiscard]] std::optional<bool> verified_group_has_live_member(pid_t group) noexcept;
void begin_stop_locked(Record& record, Clock::time_point now);
void monitor_record_locked(SupervisorState& state, Record& record, Clock::time_point now);
void monitor_main(std::shared_ptr<SupervisorState> state) noexcept;
[[nodiscard]] ava::core::VoidResult ensure_monitor_started(std::shared_ptr<SupervisorState> const& state);
void join_monitor(std::shared_ptr<SupervisorState> const& state) noexcept;

#endif

}  // namespace ava::process::detail

namespace ava::process {

struct PipeEndpoint::Impl
{
#if !defined(_WIN32)
  explicit Impl(int value, bool can_read, bool can_write) noexcept : descriptor(value), readable(can_read), writable(can_write) { }
  detail::UniqueFd descriptor;
#else
  explicit Impl(bool can_read, bool can_write) noexcept : readable(can_read), writable(can_write) { }
#endif
  bool readable = false;
  bool writable = false;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct AdoptionGate::Impl
{
  std::shared_ptr<detail::SupervisorState> state;
  std::shared_ptr<detail::HandleState> handle;
  std::uint64_t record = 0;
  ProcessDeadline startup_deadline{};
  bool child_branch = false;
  bool registered = false;
#if !defined(_WIN32)
  detail::Pipe leader_status;
  detail::Pipe leader_control;
  std::optional<detail::Pipe> sentinel_status;
  std::optional<detail::Pipe> sentinel_control;
  pid_t leader = -1;
  pid_t sentinel = -1;
#endif

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct Supervisor::Impl
{
  Impl() : state(std::make_shared<detail::SupervisorState>()) { }

  // Supervisor methods, capabilities, and the one monitor share the state so
  // an RAII capability can always release itself during bounded destruction.
  std::shared_ptr<detail::SupervisorState> state;
  std::mutex shutdown_mutex;
  bool shutdown_called = false;
  ShutdownResultV1 shutdown_result;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::process
