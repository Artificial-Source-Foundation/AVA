#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/process/environment.h"
#include "ava/process/scope.h"
#include "ava/process/supervisor.h"
#include "ava/process/supervisor_test_support.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#if !defined(_WIN32)
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef AVA_FAKE_PROCESS_CHILD_PATH
#define AVA_FAKE_PROCESS_CHILD_PATH ""
#endif

#if !defined(_WIN32)
namespace {

using namespace std::chrono_literals;
using ava::process::testing::ProcessMonitorBackendMode;
using ava::process::testing::ProcessMonitorSnapshot;
using ava::process::testing::SupervisorTestAccess;

ava::process::OwnerPathV1 application_owner()
{
  auto owner = ava::process::OwnerPathV1::application();
  if (!owner)
    throw std::runtime_error(owner.error().format());
  return std::move(*owner);
}

ava::process::OwnerPathV1 operation_owner(ava::process::OwnerPathV1 const& application)
{
  auto owner = application.operation();
  if (!owner)
    throw std::runtime_error(owner.error().format());
  return std::move(*owner);
}

ava::core::Result<ava::process::SpawnResultV1> spawn_fixture(ava::process::Supervisor& supervisor, ava::process::OwnerPathV1 const& application,
                                                             std::string mode, ava::process::LifecyclePolicyV1 policy = {}, bool capture_output = false)
{
  auto reservation = supervisor.reserve(operation_owner(application), ava::process::ProcessRoleV1::Plugin, policy);
  if (!reservation)
    return std::unexpected(std::move(reservation.error()));
  auto environment = ava::process::make_plugin_environment_v1("/");
  if (!environment)
    return std::unexpected(std::move(environment.error()));
  bool const gated = mode == "input-gate";
  return supervisor.spawn(std::move(*reservation), {.executable = AVA_FAKE_PROCESS_CHILD_PATH,
                                                    .argv = {AVA_FAKE_PROCESS_CHILD_PATH, std::move(mode)},
                                                    .environment = std::move(*environment),
                                                    .cwd = "/",
                                                    .stdin_mode = gated ? ava::process::StreamModeV1::Capture : ava::process::StreamModeV1::Discard,
                                                    .stdout_mode = capture_output ? ava::process::StreamModeV1::Capture : ava::process::StreamModeV1::Discard,
                                                    .stderr_mode = ava::process::StreamModeV1::Discard});
}

bool read_until(ava::process::PipeEndpoint& endpoint, std::string_view marker, ava::process::ProcessDeadline deadline)
{
  std::array<char, 256> buffer{};
  std::string received;
  while (received.find(marker) == std::string::npos && received.size() < 4096)
  {
    auto result = endpoint.read(std::span(reinterpret_cast<std::byte*>(buffer.data()), buffer.size()));
    if (!result)
      return false;
    if (result->state == ava::process::PipeIoStateV1::Progress)
    {
      received.append(buffer.data(), result->bytes);
      continue;
    }
    if (result->state == ava::process::PipeIoStateV1::EndOfStream)
      break;
    auto ready = endpoint.wait_readable(deadline);
    if (!ready || !*ready)
      return false;
  }
  return received.find(marker) != std::string::npos;
}

bool wait_for_cycle(ava::process::Supervisor& supervisor, std::uint64_t previous, std::chrono::milliseconds timeout = 2s)
{
  return SupervisorTestAccess::wait_for_monitor_cycle(supervisor, previous, std::chrono::steady_clock::now() + timeout);
}

template <typename Predicate>
std::optional<ProcessMonitorSnapshot> wait_for_snapshot(ava::process::Supervisor& supervisor, Predicate predicate, std::chrono::milliseconds timeout = 4s)
{
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline)
  {
    auto snapshot = SupervisorTestAccess::monitor_snapshot(supervisor);
    if (predicate(snapshot))
      return snapshot;
    auto const cycle = snapshot.counters.monitor_cycles;
    if (!SupervisorTestAccess::wait_for_monitor_cycle(supervisor, cycle, deadline))
      break;
  }
  auto snapshot = SupervisorTestAccess::monitor_snapshot(supervisor);
  return predicate(snapshot) ? std::optional(std::move(snapshot)) : std::nullopt;
}

std::vector<ava::process::SpawnResultV1> spawn_idle_children(ava::process::Supervisor& supervisor, ava::process::OwnerPathV1 const& application,
                                                             std::size_t count)
{
  std::vector<ava::process::SpawnResultV1> children;
  children.reserve(count);
  for (std::size_t index = 0; index < count; ++index)
  {
    auto child = spawn_fixture(supervisor, application, "input-gate");
    if (!child)
      break;
    children.push_back(std::move(*child));
  }
  return children;
}

bool all_delays(ProcessMonitorSnapshot const& snapshot, std::size_t count, std::uint64_t delay)
{
  if (snapshot.records.size() != count)
    return false;
  for (auto const& row : snapshot.records)
  {
    if (row.current_delay_milliseconds != delay)
      return false;
  }
  return true;
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

void test_lazy_monitor_resources()
{
  auto supervisor = std::make_shared<ava::process::Supervisor>();
  auto scope = ava::process::ProcessScopeV1::application(supervisor);
  std::vector<ava::process::Reservation> reservations;
  reservations.reserve(ava::process::kMaxLiveProcessRecordsV1);
  auto application = application_owner();
  for (std::size_t index = 0; index < ava::process::kMaxLiveProcessRecordsV1; ++index)
  {
    auto reservation = supervisor->reserve(operation_owner(application), ava::process::ProcessRoleV1::Plugin);
    if (!reservation)
      break;
    reservations.push_back(std::move(*reservation));
  }
  reservations.clear();
  auto before_shutdown = SupervisorTestAccess::monitor_snapshot(*supervisor);
  auto shutdown = supervisor->shutdown(std::chrono::steady_clock::now() + 1s);
  auto after_shutdown = SupervisorTestAccess::monitor_snapshot(*supervisor);
  expect(scope && before_shutdown.records.empty() && !before_shutdown.monitor_started && before_shutdown.counters.current_wake_descriptors == 0 &&
             before_shutdown.counters.current_monitor_threads == 0 && shutdown.complete && after_shutdown.counters.current_wake_descriptors == 0 &&
             after_shutdown.counters.current_monitor_threads == 0,
         "Supervisor, process scopes, abandoned capacity, and never-started shutdown allocate no monitor descriptor or thread");
}

bool test_automatic_pidfd_counts()
{
  bool pidfd_available = true;
  for (std::size_t const count : {std::size_t(1), std::size_t(8), std::size_t(64)})
  {
    ava::process::Supervisor supervisor;
    auto application = application_owner();
    auto children = spawn_idle_children(supervisor, application, count);
    auto const pulse = SupervisorTestAccess::pulse_monitor(supervisor);
    bool const cycled = wait_for_cycle(supervisor, pulse);
    auto snapshot = SupervisorTestAccess::monitor_snapshot(supervisor);
    pidfd_available = pidfd_available && snapshot.pidfd_selected;
    bool const genuinely_unavailable = !snapshot.pidfd_selected && snapshot.counters.pidfd_resource_failures == 0 &&
                                       snapshot.counters.pidfd_other_failures == 0 &&
                                       snapshot.counters.pidfd_unavailable_failures + snapshot.counters.pidfd_denied_failures >= count;
    bool const strong =
        genuinely_unavailable ||
        (snapshot.pidfd_selected && snapshot.counters.current_watches == count && snapshot.counters.poll_peak_entries >= count + 1 &&
         snapshot.counters.exact_probes == 0 && snapshot.counters.fallback_probes == 0 && snapshot.counters.cloexec_invariant &&
         snapshot.counters.nonblocking_invariant && snapshot.counters.cloexec_checks >= count + 1 && snapshot.counters.nonblocking_checks >= count + 1);
    expect(children.size() == count && cycled && strong && snapshot.counters.poll_peak_entries <= 1 + 2 * ava::process::kMaxLiveProcessRecordsV1,
           "automatic monitoring blocks on one wake plus independently owned pidfds without periodic exact probes");
    auto shutdown = supervisor.shutdown(std::chrono::steady_clock::now() + 3s);
    auto ended = SupervisorTestAccess::monitor_snapshot(supervisor);
    expect(
        shutdown.complete && ended.counters.current_watches == 0 && ended.counters.current_wake_descriptors == 0 && ended.counters.current_monitor_threads == 0,
        "automatic monitor shutdown releases all shared watch, wake, and thread ownership");
  }
  return pidfd_available;
}

void test_adaptive_fallback(ProcessMonitorBackendMode mode)
{
  ava::process::Supervisor supervisor;
  auto application = application_owner();
  bool const selected = SupervisorTestAccess::set_monitor_backend_mode(supervisor, mode);
  auto children = spawn_idle_children(supervisor, application, 2);
  auto reached = wait_for_snapshot(supervisor, [](ProcessMonitorSnapshot const& snapshot) { return all_delays(snapshot, 2, 1000); });
  auto before = SupervisorTestAccess::monitor_snapshot(supervisor);
  auto const cycle = before.counters.monitor_cycles;
  bool const next_cycle = wait_for_cycle(supervisor, cycle, 1500ms);
  auto after = SupervisorTestAccess::monitor_snapshot(supervisor);
  bool buckets = true;
  for (std::size_t index = 0; index < 7; ++index)
    buckets = buckets && after.counters.fallback_delay_buckets[index] >= children.size();
  bool const failure_class = mode == ProcessMonitorBackendMode::ForceFallbackUnavailable ? after.counters.pidfd_unavailable_failures >= children.size()
                                                                                         : after.counters.pidfd_denied_failures >= children.size();
  expect(selected && children.size() == 2 && reached && next_cycle && all_delays(after, 2, 1000) && buckets && failure_class &&
             after.counters.poll_calls - before.counters.poll_calls <= 3,
         "forced unavailable and denied backends use exact 10-to-1000ms adaptive buckets without sustained 100Hz polling");
  static_cast<void>(supervisor.shutdown(std::chrono::steady_clock::now() + 3s));
}

void test_per_record_waiter_cap_and_wake()
{
  ava::process::Supervisor supervisor;
  auto application = application_owner();
  static_cast<void>(SupervisorTestAccess::set_monitor_backend_mode(supervisor, ProcessMonitorBackendMode::ForceFallbackUnavailable));
  auto children = spawn_idle_children(supervisor, application, 8);
  auto reached = wait_for_snapshot(supervisor, [](ProcessMonitorSnapshot const& snapshot) { return all_delays(snapshot, 8, 1000); });
  auto before = SupervisorTestAccess::monitor_snapshot(supervisor);
  auto const pulse_time = std::chrono::steady_clock::now();
  auto const pulse = SupervisorTestAccess::pulse_monitor(supervisor);
  bool const pulse_seen = wait_for_cycle(supervisor, pulse, 200ms);
  bool const pulse_prompt = pulse_seen && std::chrono::steady_clock::now() - pulse_time < 200ms;

  std::atomic<bool> waiter_finished{false};
  std::thread waiter([&] {
    static_cast<void>(supervisor.wait(children.front().handle, std::chrono::steady_clock::now() + 350ms));
    waiter_finished.store(true, std::memory_order_release);
  });
  auto active = wait_for_snapshot(supervisor, [](ProcessMonitorSnapshot const& snapshot) {
    if (snapshot.records.size() != 8 || snapshot.records.front().active_waiter_count != 1 || snapshot.records.front().current_delay_milliseconds > 50)
      return false;
    for (std::size_t index = 1; index < snapshot.records.size(); ++index)
    {
      if (snapshot.records[index].active_waiter_count != 0 || snapshot.records[index].current_delay_milliseconds != 1000)
        return false;
    }
    return true;
  });
  waiter.join();
  auto released = wait_for_snapshot(
      supervisor, [](ProcessMonitorSnapshot const& snapshot) { return snapshot.records.size() == 8 && snapshot.records.front().active_waiter_count == 0; });
  bool unrelated_stayed_slow = released.has_value();
  if (released)
  {
    for (std::size_t index = 1; index < released->records.size(); ++index)
      unrelated_stayed_slow = unrelated_stayed_slow && released->records[index].current_delay_milliseconds == 1000;
  }
  auto after = SupervisorTestAccess::monitor_snapshot(supervisor);
  expect(children.size() == 8 && reached && pulse_prompt && after.counters.wake_attempts > before.counters.wake_attempts && active &&
             waiter_finished.load(std::memory_order_acquire) && released && unrelated_stayed_slow,
         "test pulse and one record waiter wake promptly while only that record resets and receives the 50ms fallback cap");
  static_cast<void>(supervisor.shutdown(std::chrono::steady_clock::now() + 3s));
}

void test_quiet_proofs_and_prompt_stop()
{
  ava::process::Supervisor supervisor;
  auto application = application_owner();
  static_cast<void>(SupervisorTestAccess::set_monitor_backend_mode(supervisor, ProcessMonitorBackendMode::ForceFallbackDenied));

  auto normal = spawn_fixture(supervisor, application, "normal");
  auto normal_status = normal ? supervisor.wait(normal->handle, std::chrono::steady_clock::now() + 3s)
                              : ava::core::Result<ava::process::ExitStatusV1>(std::unexpected(normal.error()));
  auto after_normal = SupervisorTestAccess::monitor_snapshot(supervisor);

  auto leader_first = spawn_fixture(supervisor, application, "leader-exits-first", {.termination_grace = 30ms, .execution_deadline = std::nullopt});
  auto leader_status = leader_first ? supervisor.wait(leader_first->handle, std::chrono::steady_clock::now() + 3s)
                                    : ava::core::Result<ava::process::ExitStatusV1>(std::unexpected(leader_first.error()));
  auto after_leader = SupervisorTestAccess::monitor_snapshot(supervisor);

  auto refusing = spawn_fixture(supervisor, application, "ignore-term", {.termination_grace = 30ms, .execution_deadline = std::nullopt}, true);
  bool const refusing_ready = refusing && refusing->standard_output && read_until(*refusing->standard_output, "READY\n", std::chrono::steady_clock::now() + 1s);
  auto refusal_stop = refusing ? supervisor.request_stop(refusing->handle, ava::process::TerminationReasonV1::Canceled, std::chrono::steady_clock::now() + 2s)
                               : ava::core::Result<ava::process::StopResultV1>(std::unexpected(refusing.error()));
  auto refusal_status = refusing ? supervisor.wait(refusing->handle, std::chrono::steady_clock::now() + 2s)
                                 : ava::core::Result<ava::process::ExitStatusV1>(std::unexpected(refusing.error()));
  auto after_refusal = SupervisorTestAccess::monitor_snapshot(supervisor);

  auto idle = spawn_fixture(supervisor, application, "input-gate", {.termination_grace = 0ms, .execution_deadline = std::nullopt});
  auto stop_begin = std::chrono::steady_clock::now();
  auto stop = idle ? supervisor.request_stop(idle->handle, ava::process::TerminationReasonV1::Canceled, stop_begin + 2s)
                   : ava::core::Result<ava::process::StopResultV1>(std::unexpected(idle.error()));
  auto stopped = idle ? supervisor.wait(idle->handle, stop_begin + 1s) : ava::core::Result<ava::process::ExitStatusV1>(std::unexpected(idle.error()));
  auto final = SupervisorTestAccess::monitor_snapshot(supervisor);
  auto const final_cycle = SupervisorTestAccess::pulse_monitor(supervisor);
  bool const final_cycle_seen = wait_for_cycle(supervisor, final_cycle, 200ms);
  auto quiescent = SupervisorTestAccess::monitor_snapshot(supervisor);

  expect(normal_status && normal_status->reason == ava::process::TerminationReasonV1::NaturalExit && after_normal.counters.quiet_group_proofs == 2 &&
             leader_status && leader_status->reason == ava::process::TerminationReasonV1::NaturalExit && after_leader.counters.quiet_group_proofs == 4 &&
             refusing_ready && refusal_stop && refusal_status && refusal_status->signal_number == SIGKILL && after_refusal.counters.quiet_group_proofs == 6 &&
             stop && stopped && stopped->reason == ava::process::TerminationReasonV1::Canceled && std::chrono::steady_clock::now() - stop_begin < 1s &&
             final.counters.quiet_group_proofs == 8 && final_cycle_seen && quiescent.counters.group_observations == final.counters.group_observations,
         "immediate quiet, leader-first descendant, TERM refusal, and prompt KILL each use exactly two short quiet proofs without continued polling");
  static_cast<void>(supervisor.shutdown(std::chrono::steady_clock::now() + 2s));
}

void test_snapshot_lifetime_and_descriptor_churn()
{
  ava::process::Supervisor supervisor;
  auto application = application_owner();
  auto child = spawn_fixture(supervisor, application, "input-gate");
  auto latch = std::make_shared<HookLatch>();
  SupervisorTestAccess::set_after_poll_snapshot_hook(supervisor, [latch] { latch->arrive_and_wait(); });
  bool const frozen = latch->wait_until(std::chrono::steady_clock::now() + 1s);

  ava::process::ShutdownResultV1 shutdown;
  std::thread shutdown_thread([&] { shutdown = supervisor.shutdown(std::chrono::steady_clock::now() + 300ms); });
  auto const finalization_deadline = std::chrono::steady_clock::now() + 1s;
  while (supervisor.snapshot().live_records != 0 && std::chrono::steady_clock::now() < finalization_deadline)
    std::this_thread::yield();

  for (int index = 0; index < 512; ++index)
  {
    int const descriptor = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (descriptor >= 0)
      static_cast<void>(::close(descriptor));
  }
  latch->release();
  shutdown_thread.join();
  auto public_snapshot = supervisor.snapshot();
  auto monitor = SupervisorTestAccess::monitor_snapshot(supervisor);
  bool settled_once = public_snapshot.records.size() == 1 && public_snapshot.records.front().settlement_count == 1;
  expect(child && frozen && shutdown.settled_count == 1 && settled_once && monitor.counters.pollnval_events == 0 && monitor.counters.stale_events >= 1 &&
             monitor.counters.current_watches == 0 && monitor.counters.current_wake_descriptors == 0 && monitor.counters.current_monitor_threads == 0,
         "an in-flight snapshot owns descriptors across finalization and churn, then ignores stale readiness without UAF, POLLNVAL, or double settlement");
}

}  // namespace
#endif

void run_process_monitor_tests()
{
#if defined(_WIN32)
  ava::tests::request_skip("event-driven process monitoring is compile-time unsupported on Windows");
#else
  test_lazy_monitor_resources();
  bool const pidfd_available = test_automatic_pidfd_counts();
  test_adaptive_fallback(ProcessMonitorBackendMode::ForceFallbackUnavailable);
  test_adaptive_fallback(ProcessMonitorBackendMode::ForceFallbackDenied);
  test_per_record_waiter_cap_and_wake();
  test_quiet_proofs_and_prompt_stop();
  test_snapshot_lifetime_and_descriptor_churn();
  if (!pidfd_available)
    ava::tests::request_skip("the host kernel or process sandbox does not provide pidfd_open for the automatic-backend assertion");
#endif
}
