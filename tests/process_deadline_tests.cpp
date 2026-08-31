#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/process/environment.h"
#include "ava/process/supervisor.h"
#include "ava/process/supervisor_test_support.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#if !defined(_WIN32)
#include <signal.h>
#endif

#ifndef AVA_FAKE_PROCESS_CHILD_PATH
#define AVA_FAKE_PROCESS_CHILD_PATH ""
#endif

#if !defined(_WIN32)
namespace {

using namespace std::chrono_literals;
using ava::process::testing::SupervisorTestAccess;
using Clock = std::chrono::steady_clock;

ava::process::OwnerPathV1 application_owner()
{
  auto owner = ava::process::OwnerPathV1::application();
  if (!owner)
    throw std::runtime_error(owner.error().format());
  return std::move(*owner);
}

ava::process::OwnerPathV1 operation_owner(ava::process::OwnerPathV1 const& prefix)
{
  auto owner = prefix.operation();
  if (!owner)
    throw std::runtime_error(owner.error().format());
  return std::move(*owner);
}

ava::core::Result<ava::process::SpawnResultV1> spawn_fixture(ava::process::Supervisor& supervisor, ava::process::OwnerPathV1 const& owner, std::string mode,
                                                             ava::process::ProcessDeadline execution_deadline,
                                                             std::chrono::milliseconds termination_grace = 100ms, bool capture_input = false,
                                                             bool capture_output = false, std::vector<std::string> extra = {})
{
  auto reservation =
      supervisor.reserve(owner, ava::process::ProcessRoleV1::Plugin, {.termination_grace = termination_grace, .execution_deadline = execution_deadline});
  if (!reservation)
    return std::unexpected(std::move(reservation.error()));
  auto environment = ava::process::make_plugin_environment_v1("/");
  if (!environment)
    return std::unexpected(std::move(environment.error()));
  std::vector<std::string> argv{AVA_FAKE_PROCESS_CHILD_PATH, std::move(mode)};
  argv.insert(argv.end(), std::make_move_iterator(extra.begin()), std::make_move_iterator(extra.end()));
  return supervisor.spawn(std::move(*reservation), {.executable = AVA_FAKE_PROCESS_CHILD_PATH,
                                                    .argv = std::move(argv),
                                                    .environment = std::move(*environment),
                                                    .cwd = "/",
                                                    .stdin_mode = capture_input ? ava::process::StreamModeV1::Capture : ava::process::StreamModeV1::Discard,
                                                    .stdout_mode = capture_output ? ava::process::StreamModeV1::Capture : ava::process::StreamModeV1::Discard,
                                                    .stderr_mode = ava::process::StreamModeV1::Discard});
}

void wait_until(ava::process::ProcessDeadline deadline)
{
  std::mutex mutex;
  std::condition_variable changed;
  std::unique_lock lock(mutex);
  while (Clock::now() < deadline)
    changed.wait_until(lock, deadline);
}

template <typename Predicate>
std::optional<ava::process::ProcessSnapshotV1> wait_for_snapshot(ava::process::Supervisor& supervisor, Predicate predicate,
                                                                 ava::process::ProcessDeadline deadline)
{
  while (Clock::now() < deadline)
  {
    auto snapshot = supervisor.snapshot();
    if (predicate(snapshot))
      return snapshot;
    auto const cycle = SupervisorTestAccess::monitor_cycle(supervisor);
    static_cast<void>(SupervisorTestAccess::wait_for_monitor_cycle(supervisor, cycle, deadline));
  }
  auto snapshot = supervisor.snapshot();
  return predicate(snapshot) ? std::optional(std::move(snapshot)) : std::nullopt;
}

std::string snapshot_text(ava::process::ProcessSnapshotV1 const& snapshot)
{
  std::ostringstream output;
  output << snapshot.schema_version << ' ' << snapshot.accepting << ' ' << snapshot.monitor_started << ' ' << snapshot.live_records << ' '
         << snapshot.retained_terminal_records;
  for (auto const& record : snapshot.records)
  {
    output << ' ' << record.schema_version << ' ' << record.record_alias << ' ' << record.owner_alias << ' ' << ava::process::to_string(record.role) << ' '
           << ava::process::to_string(record.state) << ' ' << ava::process::to_string(record.cleanup) << ' ' << ava::process::to_string(record.exit_kind) << ' '
           << record.monotonic_milliseconds << ' ' << record.settlement_count;
    if (record.reason)
      output << ' ' << ava::process::to_string(*record.reason);
  }
  return output.str();
}

bool read_until(ava::process::PipeEndpoint& endpoint, std::string_view marker, ava::process::ProcessDeadline deadline)
{
  std::array<char, 256> buffer{};
  std::string received;
  while (received.find(marker) == std::string::npos && received.size() < 4096)
  {
    auto read = endpoint.read(std::span(reinterpret_cast<std::byte*>(buffer.data()), buffer.size()));
    if (!read)
      return false;
    if (read->state == ava::process::PipeIoStateV1::Progress)
    {
      received.append(buffer.data(), read->bytes);
      continue;
    }
    if (read->state == ava::process::PipeIoStateV1::EndOfStream)
      return false;
    auto ready = endpoint.wait_readable(deadline);
    if (!ready || !*ready)
      return false;
  }
  return received.find(marker) != std::string::npos;
}

bool write_byte(ava::process::PipeEndpoint& endpoint, ava::process::ProcessDeadline deadline)
{
  std::byte const value{'P'};
  while (Clock::now() < deadline)
  {
    auto written = endpoint.write(std::span(&value, 1));
    if (!written)
      return false;
    if (written->state == ava::process::PipeIoStateV1::Progress)
      return written->bytes == 1;
    auto ready = endpoint.wait_writable(deadline);
    if (!ready || !*ready)
      return false;
  }
  return false;
}

class HookLatch final
{
 public:
  ~HookLatch() { release(); }

  void arrive_and_wait()
  {
    std::unique_lock lock(mutex_);
    arrived_ = true;
    changed_.notify_all();
    changed_.wait(lock, [&] { return released_; });
  }

  bool wait_until(ava::process::ProcessDeadline deadline)
  {
    std::unique_lock lock(mutex_);
    return changed_.wait_until(lock, deadline, [&] { return arrived_; });
  }

  void release() noexcept
  {
    {
      std::lock_guard lock(mutex_);
      released_ = true;
    }
    changed_.notify_all();
  }

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool arrived_ = false;
  bool released_ = false;
};

bool deadline_status(ava::process::ExitStatusV1 const& status)
{
  return status.reason == ava::process::TerminationReasonV1::DeadlineExpired &&
         (status.cleanup == ava::process::CleanupStateV1::Complete || status.cleanup == ava::process::CleanupStateV1::Incomplete);
}

void test_reservation_policy_validation()
{
  ava::process::Supervisor supervisor;
  auto application = application_owner();
  auto const past_instant = Clock::now();
  auto past = supervisor.reserve(operation_owner(application), ava::process::ProcessRoleV1::Plugin, {.execution_deadline = past_instant});
  auto browser_missing = supervisor.reserve(operation_owner(application), ava::process::ProcessRoleV1::BrowserOpener);
  auto const far_instant = Clock::now() + 11s;
  auto browser_far = supervisor.reserve(operation_owner(application), ava::process::ProcessRoleV1::BrowserOpener, {.execution_deadline = far_instant});

  bool boundary_valid = false;
  auto const boundary_base = Clock::now();
  {
    auto boundary = supervisor.reserve(operation_owner(application), ava::process::ProcessRoleV1::BrowserOpener, {.execution_deadline = boundary_base + 10s});
    boundary_valid = boundary.has_value();
  }
  bool optional_for_other_roles = false;
  {
    auto optional = supervisor.reserve(operation_owner(application), ava::process::ProcessRoleV1::Plugin);
    optional_for_other_roles = optional.has_value();
  }

  std::string errors;
  if (!past)
    errors += past.error().format();
  if (!browser_missing)
    errors += browser_missing.error().format();
  if (!browser_far)
    errors += browser_far.error().format();
  auto const snapshot = supervisor.snapshot();
  auto const past_text = std::to_string(past_instant.time_since_epoch().count());
  auto const far_text = std::to_string(far_instant.time_since_epoch().count());
  expect(!ava::process::LifecyclePolicyV1{}.execution_deadline && !past && !browser_missing && !browser_far && boundary_valid && optional_for_other_roles &&
             snapshot.records.empty() && snapshot.live_records == 0 && !snapshot.monitor_started,
         "past deadlines and missing/overlong Browser deadlines fail before records while the exact Browser boundary and optional general policy pass");
  expect(errors.find(past_text) == std::string::npos && errors.find(far_text) == std::string::npos,
         "execution-deadline policy errors do not disclose absolute clock values");
}

void test_reserved_deadline_expires_before_spawn()
{
  ava::process::Supervisor supervisor;
  auto application = application_owner();
  auto const root = create_empty_root("process-deadline-reserved");
  auto const marker = root / "EXECUTION_CONTENT_CANARY";
  auto const deadline = Clock::now() + 80ms;
  auto reservation = supervisor.reserve(operation_owner(application), ava::process::ProcessRoleV1::Plugin, {.execution_deadline = deadline});
  wait_until(deadline + 5ms);
  auto environment = ava::process::make_plugin_environment_v1("/");
  auto launched =
      reservation && environment
          ? supervisor.spawn(std::move(*reservation), {.executable = AVA_FAKE_PROCESS_CHILD_PATH,
                                                       .argv = {AVA_FAKE_PROCESS_CHILD_PATH, "exec-marker", marker.string()},
                                                       .environment = std::move(*environment),
                                                       .cwd = "/",
                                                       .stdin_mode = ava::process::StreamModeV1::Discard,
                                                       .stdout_mode = ava::process::StreamModeV1::Discard,
                                                       .stderr_mode = ava::process::StreamModeV1::Discard})
          : ava::core::Result<ava::process::SpawnResultV1>(std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "deadline fixture setup failed")));
  auto const snapshot = supervisor.snapshot();
  bool const exact = snapshot.records.size() == 1 && snapshot.records.front().state == ava::process::ProcessStateV1::Finished &&
                     snapshot.records.front().reason == ava::process::TerminationReasonV1::DeadlineExpired &&
                     snapshot.records.front().cleanup == ava::process::CleanupStateV1::NotRequired && snapshot.records.front().settlement_count == 1;
  expect(reservation && environment && !launched && exact && snapshot.live_records == 0 && !snapshot.monitor_started && !std::filesystem::exists(marker),
         "a reservation that expires before spawn settles once as content-free DeadlineExpired without a fork or monitor");
}

void test_after_fork_before_release_deadline()
{
  ava::process::Supervisor supervisor;
  auto application = application_owner();
  auto const root = create_empty_root("process-deadline-gated");
  auto const marker = root / "executed";
  auto latch = std::make_shared<HookLatch>();
  SupervisorTestAccess::set_after_fork_before_release_hook(supervisor, [latch] { latch->arrive_and_wait(); });
  auto const deadline = Clock::now() + 500ms;
  std::optional<ava::core::Result<ava::process::SpawnResultV1>> launch;
  std::thread launcher(
      [&] { launch.emplace(spawn_fixture(supervisor, operation_owner(application), "exec-marker", deadline, 5s, false, false, {marker.string()})); });
  bool const reached = latch->wait_until(deadline);
  auto expired = reached ? wait_for_snapshot(
                               supervisor,
                               [](ava::process::ProcessSnapshotV1 const& snapshot) {
                                 return snapshot.records.size() == 1 && snapshot.records.front().reason == ava::process::TerminationReasonV1::DeadlineExpired &&
                                        snapshot.records.front().state == ava::process::ProcessStateV1::Finished;
                               },
                               deadline + 1s)
                         : std::nullopt;
  latch->release();
  launcher.join();
  SupervisorTestAccess::clear_after_fork_before_release_hook(supervisor);
  auto const snapshot = supervisor.snapshot();
  bool const once = snapshot.records.size() == 1 && snapshot.records.front().settlement_count == 1;
  expect(reached && expired && launch && !*launch && once && !std::filesystem::exists(marker),
         "a deadline after exact-child registration but before release keeps the gate closed and performs one exact DeadlineExpired settlement");
}

void test_running_deadlines_and_clipped_escalation()
{
  ava::process::Supervisor supervisor;
  auto application = application_owner();
  auto const deadline = Clock::now() + 800ms;
  auto cooperative = spawn_fixture(supervisor, operation_owner(application), "input-gate", deadline, 5s, true, false);
  auto refusing = spawn_fixture(supervisor, operation_owner(application), "ignore-term", deadline, 5s, false, true);
  bool const ready = refusing && refusing->standard_output && read_until(*refusing->standard_output, "READY\n", deadline);
  auto cooperative_status = cooperative ? supervisor.wait(cooperative->handle, deadline + 750ms)
                                        : ava::core::Result<ava::process::ExitStatusV1>(std::unexpected(cooperative.error()));
  auto refusing_status =
      refusing ? supervisor.wait(refusing->handle, deadline + 750ms) : ava::core::Result<ava::process::ExitStatusV1>(std::unexpected(refusing.error()));
  auto const completed = Clock::now();
  bool refusing_signal_honest = refusing_status.has_value();
  if (refusing_status && refusing_status->cleanup == ava::process::CleanupStateV1::Complete)
    refusing_signal_honest = refusing_status->kind == ava::process::ExitKindV1::Signaled && refusing_status->signal_number == SIGKILL;
  expect(cooperative && refusing && ready && cooperative_status && refusing_status && deadline_status(*cooperative_status) &&
             deadline_status(*refusing_status) && refusing_signal_honest && completed <= deadline + 750ms,
         "cooperative and TERM-refusing children expire without caller stops, clip a five-second grace immediately, and report complete or honest incomplete "
         "cleanup");
}

void test_expiry_during_launch_framing()
{
  ava::process::Supervisor supervisor;
  auto application = application_owner();
  auto latch = std::make_shared<HookLatch>();
  SupervisorTestAccess::set_after_gate_release_hook(supervisor, [latch] { latch->arrive_and_wait(); });
  auto const deadline = Clock::now() + 500ms;
  std::optional<ava::core::Result<ava::process::SpawnResultV1>> launch;
  std::thread launcher([&] { launch.emplace(spawn_fixture(supervisor, operation_owner(application), "input-gate", deadline, 5s, true)); });
  bool const reached = latch->wait_until(deadline);
  auto expired = reached ? wait_for_snapshot(
                               supervisor,
                               [](ava::process::ProcessSnapshotV1 const& snapshot) {
                                 return snapshot.records.size() == 1 && snapshot.records.front().reason == ava::process::TerminationReasonV1::DeadlineExpired;
                               },
                               deadline + 1s)
                         : std::nullopt;
  latch->release();
  launcher.join();
  SupervisorTestAccess::clear_after_gate_release_hook(supervisor);
  std::string error = launch && !*launch ? launch->error().format() : std::string();
  expect(reached && expired && launch && !*launch && error.find("stopped before exec confirmation") != std::string::npos &&
             error.find("deadline_expired") != std::string::npos,
         "expiry after gate release but during parent launch framing prevents a successful launch return and reports stopped-before-confirmation");
}

std::pair<std::shared_ptr<HookLatch>, bool> freeze_monitor(ava::process::Supervisor& supervisor, ava::process::ProcessDeadline deadline)
{
  auto latch = std::make_shared<HookLatch>();
  SupervisorTestAccess::set_after_poll_snapshot_hook(supervisor, [latch] { latch->arrive_and_wait(); });
  static_cast<void>(SupervisorTestAccess::pulse_monitor(supervisor));
  return {latch, latch->wait_until(deadline)};
}

void test_first_reason_and_due_mutator_ordering()
{
  auto application = application_owner();
  {
    ava::process::Supervisor supervisor;
    auto const deadline = Clock::now() + 2s;
    auto child = spawn_fixture(supervisor, operation_owner(application), "ignore-term", deadline, 50ms, false, true);
    bool const ready = child && child->standard_output && read_until(*child->standard_output, "READY\n", Clock::now() + 1s);
    auto stopped = child ? supervisor.request_stop(child->handle, ava::process::TerminationReasonV1::Canceled, Clock::now() + 500ms)
                         : ava::core::Result<ava::process::StopResultV1>(std::unexpected(child.error()));
    auto status = child ? supervisor.wait(child->handle, Clock::now() + 1s) : ava::core::Result<ava::process::ExitStatusV1>(std::unexpected(child.error()));
    expect(ready && stopped && status && status->reason == ava::process::TerminationReasonV1::Canceled,
           "cancellation committed before an execution deadline remains the immutable first reason");
  }

  {
    ava::process::Supervisor supervisor;
    auto session = application.session();
    auto const deadline = Clock::now() + 2s;
    auto child = session
                     ? spawn_fixture(supervisor, operation_owner(*session), "ignore-term", deadline, 50ms)
                     : ava::core::Result<ava::process::SpawnResultV1>(std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "owner fixture failed")));
    auto stopped = session
                       ? supervisor.request_stop(*session, ava::process::TerminationReasonV1::OwnerShutdown, Clock::now() + 500ms)
                       : ava::core::Result<ava::process::StopResultV1>(std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "owner fixture failed")));
    auto status = child ? supervisor.wait(child->handle, Clock::now() + 1s) : ava::core::Result<ava::process::ExitStatusV1>(std::unexpected(child.error()));
    expect(session && child && stopped && status && status->reason == ava::process::TerminationReasonV1::OwnerShutdown,
           "owner shutdown committed before an execution deadline remains the immutable first reason");
  }

  {
    ava::process::Supervisor supervisor;
    auto const deadline = Clock::now() + 650ms;
    auto child = spawn_fixture(supervisor, operation_owner(application), "ignore-term", deadline, 5s);
    auto [latch, frozen] = freeze_monitor(supervisor, deadline);
    if (frozen)
      wait_until(deadline + 5ms);
    auto stopped = child ? supervisor.request_stop(child->handle, ava::process::TerminationReasonV1::Canceled, Clock::now() + 500ms)
                         : ava::core::Result<ava::process::StopResultV1>(std::unexpected(child.error()));
    auto before_release = supervisor.snapshot();
    latch->release();
    SupervisorTestAccess::clear_after_poll_snapshot_hook(supervisor);
    auto status = child ? supervisor.wait(child->handle, Clock::now() + 1s) : ava::core::Result<ava::process::ExitStatusV1>(std::unexpected(child.error()));
    bool const due_visible = before_release.records.size() == 1 && before_release.records.front().reason == ava::process::TerminationReasonV1::DeadlineExpired;
    expect(child && frozen && stopped && stopped->newly_requested == 1 && due_visible && status && deadline_status(*status),
           "a handle stop mutator commits an already-due execution deadline before its requested reason even while the monitor is delayed");
  }

  {
    ava::process::Supervisor supervisor;
    auto session = application.session();
    auto const deadline = Clock::now() + 650ms;
    auto child = session
                     ? spawn_fixture(supervisor, operation_owner(*session), "ignore-term", deadline, 5s)
                     : ava::core::Result<ava::process::SpawnResultV1>(std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "owner fixture failed")));
    auto [latch, frozen] = freeze_monitor(supervisor, deadline);
    if (frozen)
      wait_until(deadline + 5ms);
    auto stopped = session
                       ? supervisor.request_stop(*session, ava::process::TerminationReasonV1::OwnerShutdown, Clock::now() + 500ms)
                       : ava::core::Result<ava::process::StopResultV1>(std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "owner fixture failed")));
    auto before_release = supervisor.snapshot();
    latch->release();
    SupervisorTestAccess::clear_after_poll_snapshot_hook(supervisor);
    auto status = child ? supervisor.wait(child->handle, Clock::now() + 1s) : ava::core::Result<ava::process::ExitStatusV1>(std::unexpected(child.error()));
    bool const due_visible = before_release.records.size() == 1 && before_release.records.front().reason == ava::process::TerminationReasonV1::DeadlineExpired;
    expect(session && child && frozen && stopped && due_visible && status && deadline_status(*status),
           "an owner stop mutator gives an already-due execution deadline the same first-reason ordering");
  }

  {
    ava::process::Supervisor supervisor;
    auto const deadline = Clock::now() + 650ms;
    auto child = spawn_fixture(supervisor, operation_owner(application), "ignore-term", deadline, 5s);
    auto [latch, frozen] = freeze_monitor(supervisor, deadline);
    if (frozen)
      wait_until(deadline + 5ms);
    std::optional<ava::process::ShutdownResultV1> shutdown;
    std::thread shutdown_thread([&] { shutdown = supervisor.shutdown(Clock::now() + 750ms); });
    bool due_visible = false;
    auto const observation_deadline = Clock::now() + 300ms;
    while (Clock::now() < observation_deadline)
    {
      auto snapshot = supervisor.snapshot();
      due_visible = snapshot.records.size() == 1 && snapshot.records.front().reason == ava::process::TerminationReasonV1::DeadlineExpired;
      if (due_visible)
        break;
      std::this_thread::yield();
    }
    latch->release();
    shutdown_thread.join();
    SupervisorTestAccess::clear_after_poll_snapshot_hook(supervisor);
    auto snapshot = supervisor.snapshot();
    bool const exact = snapshot.records.size() == 1 && snapshot.records.front().reason == ava::process::TerminationReasonV1::DeadlineExpired &&
                       snapshot.records.front().settlement_count == 1;
    expect(child && frozen && due_visible && shutdown && exact,
           "shutdown commits an already-due execution deadline before ApplicationShutdown while the monitor is delayed");
  }
}

void test_progress_and_eintr_do_not_reset_deadline()
{
  ava::process::Supervisor supervisor;
  auto application = application_owner();
  auto const deadline = Clock::now() + 850ms;
  auto child = spawn_fixture(supervisor, operation_owner(application), "progress-gate", deadline, 5s, true, true);
  bool const ready = child && child->standard_output && read_until(*child->standard_output, "READY\n", deadline);

  auto [latch, frozen] = freeze_monitor(supervisor, deadline);
  if (frozen)
    SupervisorTestAccess::interrupt_next_monitor_poll(supervisor);
  latch->release();
  SupervisorTestAccess::clear_after_poll_snapshot_hook(supervisor);

  bool progressed = ready && child->standard_input && child->standard_output;
  auto watch = progressed
                   ? child->standard_output->watch(ava::process::PipeInterestV1::Readable, 17)
                   : ava::core::Result<ava::process::PipeWatchV1>(std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "progress fixture failed")));
  for (auto const instant : {deadline - 260ms, deadline - 80ms})
  {
    wait_until(instant);
    progressed = progressed && write_byte(*child->standard_input, deadline);
    if (progressed && watch)
    {
      std::array<ava::process::PipeWatchV1, 1> watches{*watch};
      auto activity = supervisor.wait_for_activity(child->handle, watches, deadline);
      progressed = activity && !activity->deadline_expired && !activity->ready.empty() && read_until(*child->standard_output, "PROGRESS\n", deadline);
      if (progressed)
        progressed = supervisor.account_output(child->handle, ava::process::StreamKindV1::StandardOutput, 9, false).has_value();
    }
  }
  auto status = child ? supervisor.wait(child->handle, deadline + 750ms) : ava::core::Result<ava::process::ExitStatusV1>(std::unexpected(child.error()));
  auto const monitor = SupervisorTestAccess::monitor_snapshot(supervisor);
  expect(child && ready && frozen && progressed && status && deadline_status(*status) && Clock::now() <= deadline + 750ms &&
             monitor.counters.poll_interruptions >= 1,
         "output progress, readiness waits, accounting, and an interrupted monitor poll retain the original immutable absolute execution deadline");
}

void test_dropped_handle_deadline_settles_snapshot()
{
  ava::process::Supervisor supervisor;
  auto application = application_owner();
  auto const deadline = Clock::now() + 500ms;
  bool launched = false;
  {
    auto child = spawn_fixture(supervisor, operation_owner(application), "ignore-term", deadline, 5s);
    launched = child.has_value();
  }
  auto settled = wait_for_snapshot(
      supervisor,
      [](ava::process::ProcessSnapshotV1 const& snapshot) {
        return snapshot.records.size() == 1 && snapshot.records.front().state == ava::process::ProcessStateV1::Finished;
      },
      deadline + 1s);
  bool const exact =
      settled && settled->records.front().reason == ava::process::TerminationReasonV1::DeadlineExpired && settled->records.front().settlement_count == 1;
  expect(launched && exact, "dropping the only caller handle leaves deadline enforcement and one terminal snapshot settlement owned by the supervisor");
}

void test_nearest_deadline_order_and_shared_shutdown()
{
  auto application = application_owner();
  {
    ava::process::Supervisor supervisor;
    auto const base = Clock::now();
    auto late = spawn_fixture(supervisor, operation_owner(application), "ignore-term", base + 1s, 5s);
    auto early = spawn_fixture(supervisor, operation_owner(application), "ignore-term", base + 550ms, 5s);
    auto first = wait_for_snapshot(
        supervisor,
        [](ava::process::ProcessSnapshotV1 const& snapshot) {
          return snapshot.records.size() == 2 && snapshot.records[1].reason == ava::process::TerminationReasonV1::DeadlineExpired;
        },
        base + 900ms);
    bool nearest_first = first && !first->records[0].reason && first->records[0].state != ava::process::ProcessStateV1::Finished;
    auto both = wait_for_snapshot(
        supervisor,
        [](ava::process::ProcessSnapshotV1 const& snapshot) {
          return snapshot.records.size() == 2 && snapshot.records[0].state == ava::process::ProcessStateV1::Finished &&
                 snapshot.records[1].state == ava::process::ProcessStateV1::Finished;
        },
        base + 1500ms);
    bool exact = both && both->records[0].reason == ava::process::TerminationReasonV1::DeadlineExpired &&
                 both->records[1].reason == ava::process::TerminationReasonV1::DeadlineExpired &&
                 both->records[1].monotonic_milliseconds <= both->records[0].monotonic_milliseconds;
    expect(late && early && nearest_first && exact,
           "the event-driven monitor fires the nearest immutable deadline first even when that record registered second");
  }

  {
    ava::process::Supervisor supervisor;
    std::vector<ava::process::SpawnResultV1> children;
    bool launched = true;
    auto const base = Clock::now();
    for (int index = 0; index < 4; ++index)
    {
      auto child = spawn_fixture(supervisor, operation_owner(application), "ignore-term", base + 2s + index * 100ms, 5s);
      launched = launched && child.has_value();
      if (child)
        children.push_back(std::move(*child));
    }
    auto const begin = Clock::now();
    auto shutdown = supervisor.shutdown(begin + 350ms);
    auto const elapsed = Clock::now() - begin;
    auto const snapshot = supervisor.snapshot();
    bool reasons = snapshot.records.size() == children.size();
    for (auto const& record : snapshot.records)
      reasons = reasons && record.reason == ava::process::TerminationReasonV1::ApplicationShutdown && record.settlement_count == 1;
    expect(launched && children.size() == 4 && shutdown.settled_count == 4 && reasons && elapsed < 750ms,
           "shared shutdown remains bounded across records with distinct later execution deadlines and preserves its earlier first reason");
  }
}

void test_browser_deadline_content_free_terminal()
{
  ava::process::Supervisor supervisor;
  auto application = application_owner();
  constexpr std::string_view canary = "BROWSER_DEADLINE_CONTENT_CANARY_61df";
  auto const deadline = Clock::now() + 90ms;
  auto reservation = supervisor.reserve(operation_owner(application), ava::process::ProcessRoleV1::BrowserOpener, {.execution_deadline = deadline});
  wait_until(deadline + 5ms);
  auto launched =
      reservation ? supervisor.spawn(std::move(*reservation), {.executable = std::string(canary), .argv = {std::string(canary)}, .environment = {}, .cwd = "/"})
                  : ava::core::Result<ava::process::SpawnResultV1>(std::unexpected(reservation.error()));
  auto const snapshot = supervisor.snapshot();
  auto const serialized = snapshot_text(snapshot);
  auto const raw_deadline = std::to_string(deadline.time_since_epoch().count());
  auto const error = !launched ? launched.error().format() : std::string();
  bool const exact = snapshot.records.size() == 1 && snapshot.records.front().role == ava::process::ProcessRoleV1::BrowserOpener &&
                     snapshot.records.front().reason == ava::process::TerminationReasonV1::DeadlineExpired && snapshot.records.front().settlement_count == 1;
  expect(reservation && !launched && exact && !snapshot.monitor_started && error.find(canary) == std::string::npos &&
             error.find(raw_deadline) == std::string::npos && serialized.find(canary) == std::string::npos &&
             serialized.find(raw_deadline) == std::string::npos,
         "Browser deadline failures and terminal snapshots retain only closed content-free fields");
}

}  // namespace
#endif

void run_process_deadline_tests()
{
#if defined(_WIN32)
  ava::tests::request_skip("absolute process execution deadlines require the POSIX lifecycle backend");
#else
  test_reservation_policy_validation();
  test_reserved_deadline_expires_before_spawn();
  test_after_fork_before_release_deadline();
  test_running_deadlines_and_clipped_escalation();
  test_expiry_during_launch_framing();
  test_first_reason_and_due_mutator_ordering();
  test_progress_and_eintr_do_not_reset_deadline();
  test_dropped_handle_deadline_settles_snapshot();
  test_nearest_deadline_order_and_shared_shutdown();
  test_browser_deadline_content_free_terminal();
#endif
}
