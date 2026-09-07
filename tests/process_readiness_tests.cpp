#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/process/environment.h"
#include "ava/process/supervisor.h"
#include "ava/process/supervisor_test_support.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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
#include <pthread.h>
#include <signal.h>
#endif

#ifndef AVA_FAKE_PROCESS_CHILD_PATH
#define AVA_FAKE_PROCESS_CHILD_PATH ""
#endif

#ifndef AVA_PROCESS_PUBLIC_HEADER_PATH
#define AVA_PROCESS_PUBLIC_HEADER_PATH ""
#endif

#if !defined(_WIN32)
namespace {

using namespace std::chrono_literals;
using ava::process::PipeEndpoint;
using ava::process::PipeInterestV1;
using ava::process::PipeIoStateV1;
using ava::process::PipeReadyV1;
using ava::process::PipeWatchV1;
using ava::process::ProcessActivityV1;

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

ava::process::SpawnSpecV1 fake_spec(std::string mode, ava::process::StreamModeV1 input = ava::process::StreamModeV1::Discard,
                                    ava::process::StreamModeV1 output = ava::process::StreamModeV1::Capture,
                                    ava::process::StreamModeV1 error = ava::process::StreamModeV1::Capture)
{
  return {.executable = AVA_FAKE_PROCESS_CHILD_PATH,
          .argv = {AVA_FAKE_PROCESS_CHILD_PATH, std::move(mode)},
          .environment = {},
          .cwd = "/",
          .stdin_mode = input,
          .stdout_mode = output,
          .stderr_mode = error};
}

ava::core::Result<ava::process::SpawnResultV1> spawn_fake(ava::process::Supervisor& supervisor, ava::process::OwnerPathV1 const& application, std::string mode,
                                                          ava::process::StreamModeV1 input = ava::process::StreamModeV1::Discard,
                                                          ava::process::StreamModeV1 output = ava::process::StreamModeV1::Capture,
                                                          ava::process::StreamModeV1 error = ava::process::StreamModeV1::Capture)
{
  auto reservation = supervisor.reserve(operation_owner(application), ava::process::ProcessRoleV1::Plugin);
  if (!reservation)
    return std::unexpected(std::move(reservation.error()));
  auto environment = ava::process::make_plugin_environment_v1("/");
  if (!environment)
    return std::unexpected(std::move(environment.error()));
  auto specification = fake_spec(std::move(mode), input, output, error);
  specification.environment = std::move(*environment);
  return supervisor.spawn(std::move(*reservation), std::move(specification));
}

class SupervisorFallback final
{
 public:
  explicit SupervisorFallback(ava::process::Supervisor& supervisor) : supervisor_(supervisor) { }
  ~SupervisorFallback() { static_cast<void>(supervisor_.shutdown(std::chrono::steady_clock::now() + 2s)); }

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  ava::process::Supervisor& supervisor_;
};

class Latch final
{
 public:
  ~Latch() { release(); }

  void arrive_and_wait()
  {
    std::unique_lock lock(mutex_);
    arrived_ = true;
    changed_.notify_all();
    changed_.wait(lock, [&] { return released_; });
  }

  bool wait_until(std::chrono::steady_clock::time_point deadline)
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

std::span<std::byte> writable_bytes(std::array<char, 4096>& buffer)
{
  return {reinterpret_cast<std::byte*>(buffer.data()), buffer.size()};
}

bool read_until(PipeEndpoint& endpoint, std::string& received, std::string_view marker, ava::process::ProcessDeadline deadline)
{
  std::array<char, 4096> buffer{};
  while (received.find(marker) == std::string::npos && received.size() < 64 * 1024)
  {
    auto result = endpoint.read(writable_bytes(buffer));
    if (!result)
      return false;
    if (result->state == PipeIoStateV1::Progress)
    {
      received.append(buffer.data(), result->bytes);
      continue;
    }
    if (result->state == PipeIoStateV1::EndOfStream)
      return received.find(marker) != std::string::npos;
    auto ready = endpoint.wait_readable(deadline);
    if (!ready || !*ready)
      return false;
  }
  return received.find(marker) != std::string::npos;
}

bool write_text(PipeEndpoint& endpoint, std::string_view text, ava::process::ProcessDeadline deadline)
{
  std::size_t offset = 0;
  while (offset < text.size())
  {
    auto source = std::span(reinterpret_cast<std::byte const*>(text.data() + offset), text.size() - offset);
    auto result = endpoint.write(source);
    if (!result)
      return false;
    if (result->state == PipeIoStateV1::Progress)
    {
      offset += result->bytes;
      continue;
    }
    auto ready = endpoint.wait_writable(deadline);
    if (!ready || !*ready)
      return false;
  }
  return true;
}

std::optional<ava::process::ExitStatusV1> wait_status(ava::process::Supervisor& supervisor, ava::process::ProcessHandle const& handle)
{
  auto status = supervisor.wait(handle, std::chrono::steady_clock::now() + 4s);
  return status ? std::optional(*status) : std::nullopt;
}

bool same_status(ava::process::ExitStatusV1 const& left, ava::process::ExitStatusV1 const& right)
{
  return left.schema_version == right.schema_version && left.reason == right.reason && left.kind == right.kind && left.cleanup == right.cleanup &&
         left.exit_code == right.exit_code && left.signal_number == right.signal_number && left.has_exit_code == right.has_exit_code &&
         left.has_signal_number == right.has_signal_number;
}

std::optional<PipeReadyV1> ready_for(ProcessActivityV1 const& activity, std::uint32_t token)
{
  for (auto const& ready : activity.ready)
  {
    if (ready.token == token)
      return ready;
  }
  return std::nullopt;
}

std::optional<std::size_t> descriptor_count()
{
#if defined(__linux__)
  std::error_code error;
  std::size_t count = 0;
  for (std::filesystem::directory_iterator entry("/proc/self/fd", error), end; !error && entry != end; entry.increment(error))
    ++count;
  return error ? std::nullopt : std::optional(count);
#else
  return std::nullopt;
#endif
}

void test_try_wait_final_values()
{
  auto application = application_owner();
  ava::process::Supervisor supervisor;
  SupervisorFallback fallback(supervisor);

  auto gated = spawn_fake(supervisor, application, "ready-gate", ava::process::StreamModeV1::Capture);
  std::string ready_text;
  bool const running = gated && gated->standard_input && gated->standard_output &&
                       read_until(*gated->standard_output, ready_text, "READY\n", std::chrono::steady_clock::now() + 2s);
  auto empty = gated ? supervisor.try_wait(gated->handle) : ava::core::Result<std::optional<ava::process::ExitStatusV1>>(std::unexpected(gated.error()));
  ava::process::Supervisor foreign_supervisor;
  auto foreign =
      gated ? foreign_supervisor.try_wait(gated->handle) : ava::core::Result<std::optional<ava::process::ExitStatusV1>>(std::unexpected(gated.error()));
  ava::process::ProcessHandle invalid_handle;
  auto invalid = supervisor.try_wait(invalid_handle);
  bool const released = gated && gated->standard_input && write_text(*gated->standard_input, "G", std::chrono::steady_clock::now() + 2s);
  if (gated && gated->standard_input)
    gated->standard_input->close();
  auto normal_status = gated ? wait_status(supervisor, gated->handle) : std::nullopt;
  auto first_final = gated ? supervisor.try_wait(gated->handle) : ava::core::Result<std::optional<ava::process::ExitStatusV1>>(std::unexpected(gated.error()));
  auto repeated_final =
      gated ? supervisor.try_wait(gated->handle) : ava::core::Result<std::optional<ava::process::ExitStatusV1>>(std::unexpected(gated.error()));

  auto nonzero = spawn_fake(supervisor, application, "nonzero", ava::process::StreamModeV1::Discard, ava::process::StreamModeV1::Discard,
                            ava::process::StreamModeV1::Discard);
  auto nonzero_waited = nonzero ? wait_status(supervisor, nonzero->handle) : std::nullopt;
  auto nonzero_final =
      nonzero ? supervisor.try_wait(nonzero->handle) : ava::core::Result<std::optional<ava::process::ExitStatusV1>>(std::unexpected(nonzero.error()));

  auto signaled = spawn_fake(supervisor, application, "signal-exit", ava::process::StreamModeV1::Discard, ava::process::StreamModeV1::Discard,
                             ava::process::StreamModeV1::Discard);
  auto signal_waited = signaled ? wait_status(supervisor, signaled->handle) : std::nullopt;
  auto signal_final =
      signaled ? supervisor.try_wait(signaled->handle) : ava::core::Result<std::optional<ava::process::ExitStatusV1>>(std::unexpected(signaled.error()));

  ava::process::ExitStatusV1 normal_value{};
  ava::process::ExitStatusV1 first_value{};
  ava::process::ExitStatusV1 repeated_value{};
  bool const has_normal_values = normal_status && first_final && *first_final && repeated_final && *repeated_final;
  if (has_normal_values)
  {
    normal_value = *normal_status;
    first_value = **first_final;
    repeated_value = **repeated_final;
  }
  bool const stable_normal = has_normal_values && same_status(normal_value, first_value) && same_status(first_value, repeated_value) &&
                             normal_value.kind == ava::process::ExitKindV1::Exited && normal_value.exit_code == 0;

  ava::process::ExitStatusV1 nonzero_value{};
  ava::process::ExitStatusV1 nonzero_final_value{};
  bool const has_nonzero_values = nonzero_waited && nonzero_final && *nonzero_final;
  if (has_nonzero_values)
  {
    nonzero_value = *nonzero_waited;
    nonzero_final_value = **nonzero_final;
  }
  bool const stable_nonzero = has_nonzero_values && same_status(nonzero_value, nonzero_final_value) && nonzero_value.kind == ava::process::ExitKindV1::Exited &&
                              nonzero_value.exit_code == 23;

  ava::process::ExitStatusV1 signal_value{};
  ava::process::ExitStatusV1 signal_final_value{};
  bool const has_signal_values = signal_waited && signal_final && *signal_final;
  if (has_signal_values)
  {
    signal_value = *signal_waited;
    signal_final_value = **signal_final;
  }
  bool const stable_signal = has_signal_values && same_status(signal_value, signal_final_value) && signal_value.kind == ava::process::ExitKindV1::Signaled &&
                             signal_value.signal_number == SIGUSR2;
  expect(running && empty && !*empty && !foreign && !invalid && released && stable_normal && stable_nonzero && stable_signal,
         "try_wait reads only an empty-or-stable final cell for running, normal, nonzero, and signaled managed processes");
}

void test_stream_readiness_and_tokens()
{
  auto application = application_owner();
  ava::process::Supervisor supervisor;
  SupervisorFallback fallback(supervisor);
  auto child = spawn_fake(supervisor, application, "staged-stdio", ava::process::StreamModeV1::Capture);
  bool const endpoints = child && child->standard_input && child->standard_output && child->standard_error;
  if (!endpoints)
  {
    expect(false, "staged readiness fixture returns all three capture endpoints");
    return;
  }

  auto output_watch = child->standard_output->watch(PipeInterestV1::Readable, 30);
  auto error_watch = child->standard_error->watch(PipeInterestV1::Readable, 10);
  std::array<PipeWatchV1, 2> output_watches{*error_watch, *output_watch};
  auto first = supervisor.wait_for_activity(child->handle, output_watches, std::chrono::steady_clock::now() + 2s);
  auto first_output = first ? ready_for(*first, 30) : std::nullopt;
  auto first_error = first ? ready_for(*first, 10) : std::nullopt;
  std::string output_text;
  bool const output_read = first_output && first_output->readable && !first_error &&
                           read_until(*child->standard_output, output_text, "OUT\n", std::chrono::steady_clock::now() + 2s);

  auto input_watch = child->standard_input->watch(PipeInterestV1::Writable, 77);
  std::array<PipeWatchV1, 1> input_watches{*input_watch};
  auto writable = supervisor.wait_for_activity(child->handle, input_watches, std::chrono::steady_clock::now() + 2s);
  auto input_ready = writable ? ready_for(*writable, 77) : std::nullopt;
  bool const first_release = input_ready && input_ready->writable && write_text(*child->standard_input, "A", std::chrono::steady_clock::now() + 2s);

  auto second = supervisor.wait_for_activity(child->handle, output_watches, std::chrono::steady_clock::now() + 2s);
  auto second_output = second ? ready_for(*second, 30) : std::nullopt;
  auto second_error = second ? ready_for(*second, 10) : std::nullopt;
  std::string error_text;
  bool const error_read = second_error && second_error->readable && !second_output &&
                          read_until(*child->standard_error, error_text, "ERR\n", std::chrono::steady_clock::now() + 2s);
  bool const second_release = write_text(*child->standard_input, "B", std::chrono::steady_clock::now() + 2s);
  child->standard_input->close();
  auto status = wait_status(supervisor, child->handle);

  expect(output_watch && error_watch && input_watch && first && output_read && writable && first_release && second && error_read && second_release && status &&
             status->exit_code == 0,
         "one bounded wait maps deterministic tokens while stdout, writable stdin, and then stderr become ready independently");
}

void test_hup_is_separate_and_drainable()
{
  auto application = application_owner();
  ava::process::Supervisor supervisor;
  SupervisorFallback fallback(supervisor);
  auto child = spawn_fake(supervisor, application, "buffered-hup", ava::process::StreamModeV1::Discard, ava::process::StreamModeV1::Capture,
                          ava::process::StreamModeV1::Discard);
  auto status = child ? wait_status(supervisor, child->handle) : std::nullopt;
  auto watch = child && child->standard_output ? child->standard_output->watch(PipeInterestV1::Readable, 51)
                                               : ava::core::Result<PipeWatchV1>(std::unexpected(child.error()));
  std::array<PipeWatchV1, 1> watches{*watch};
  auto activity = child && watch ? supervisor.wait_for_activity(child->handle, watches, std::chrono::steady_clock::now() + 2s)
                                 : ava::core::Result<ProcessActivityV1>(std::unexpected(watch.error()));
  auto ready = activity ? ready_for(*activity, 51) : std::nullopt;
  std::string payload;
  bool const drained =
      child && child->standard_output && read_until(*child->standard_output, payload, "BUFFERED-BEFORE-HUP\n", std::chrono::steady_clock::now() + 2s);
  std::array<char, 16> tail{};
  auto eof = child && child->standard_output ? child->standard_output->read(std::span(reinterpret_cast<std::byte*>(tail.data()), tail.size()))
                                             : ava::core::Result<ava::process::PipeIoResultV1>(std::unexpected(child.error()));
  auto after_drain = child && watch ? supervisor.wait_for_activity(child->handle, watches, std::chrono::steady_clock::now() + 2s)
                                    : ava::core::Result<ProcessActivityV1>(std::unexpected(watch.error()));
  auto closed = after_drain ? ready_for(*after_drain, 51) : std::nullopt;

  expect(status && watch && activity && activity->process_finished && ready && ready->readable && ready->peer_closed && !ready->error && drained && eof &&
             eof->state == PipeIoStateV1::EndOfStream && after_drain && closed && closed->peer_closed,
         "buffered bytes and peer closure co-occur without protocol failure, remain drainable, and are followed by EOF");
}

void test_completion_wakeup_and_final_before_wait()
{
  auto application = application_owner();
  ava::process::Supervisor supervisor;
  SupervisorFallback fallback(supervisor);
  auto child = spawn_fake(supervisor, application, "ready-gate", ava::process::StreamModeV1::Capture);
  std::string ready_text;
  bool const child_ready = child && child->standard_input && child->standard_output &&
                           read_until(*child->standard_output, ready_text, "READY\n", std::chrono::steady_clock::now() + 2s);
  auto latch = std::make_shared<Latch>();
  ava::process::testing::SupervisorTestAccess::set_after_completion_channel_create_hook(supervisor, [latch] { latch->arrive_and_wait(); });
  std::optional<ProcessActivityV1> observed;
  bool observation_failed = false;
  std::thread waiter;
  if (child)
  {
    waiter = std::thread([&] {
      auto activity = supervisor.wait_for_activity(child->handle, std::span<PipeWatchV1 const>{}, std::chrono::steady_clock::now() + 2s);
      if (activity)
        observed.emplace(std::move(*activity));
      else
        observation_failed = true;
    });
  }
  bool const channel_created = child && latch->wait_until(std::chrono::steady_clock::now() + 2s);
  bool const released = channel_created && child->standard_input && write_text(*child->standard_input, "G", std::chrono::steady_clock::now() + 2s);
  latch->release();
  if (waiter.joinable())
    waiter.join();
  ava::process::testing::SupervisorTestAccess::clear_after_completion_channel_create_hook(supervisor);

  auto already_finished = spawn_fake(supervisor, application, "normal", ava::process::StreamModeV1::Discard, ava::process::StreamModeV1::Discard,
                                     ava::process::StreamModeV1::Discard);
  auto finished_status = already_finished ? wait_status(supervisor, already_finished->handle) : std::nullopt;
  auto const begin = std::chrono::steady_clock::now();
  auto immediate = already_finished ? supervisor.wait_for_activity(already_finished->handle, std::span<PipeWatchV1 const>{}, begin + 2s)
                                    : ava::core::Result<ProcessActivityV1>(std::unexpected(already_finished.error()));
  auto const elapsed = std::chrono::steady_clock::now() - begin;

  expect(child_ready && channel_created && released && observed && !observation_failed && observed->process_finished && !observed->deadline_expired &&
             finished_status && immediate && immediate->process_finished && !immediate->deadline_expired && elapsed < 250ms,
         "the hidden completion channel wakes a zero-watch call and a final value published before channel creation returns immediately");
}

void no_op_signal_handler(int) noexcept
{
}

void test_absolute_deadline_survives_eintr()
{
  auto application = application_owner();
  ava::process::Supervisor supervisor;
  SupervisorFallback fallback(supervisor);
  auto child = spawn_fake(supervisor, application, "input-gate", ava::process::StreamModeV1::Capture, ava::process::StreamModeV1::Discard,
                          ava::process::StreamModeV1::Discard);

  struct sigaction action{};
  struct sigaction previous{};
  action.sa_handler = no_op_signal_handler;
  static_cast<void>(::sigemptyset(&action.sa_mask));
  bool const installed = ::sigaction(SIGUSR1, &action, &previous) == 0;

  std::mutex mutex;
  std::condition_variable changed;
  bool target_ready = false;
  pthread_t target{};
  std::atomic<bool> stop_signals{false};
  ava::process::testing::SupervisorTestAccess::set_after_completion_channel_create_hook(supervisor, [&] {
    {
      std::lock_guard lock(mutex);
      target = ::pthread_self();
      target_ready = true;
    }
    changed.notify_all();
  });
  std::thread interrupter([&] {
    {
      std::unique_lock lock(mutex);
      changed.wait(lock, [&] { return target_ready; });
    }
    while (!stop_signals.load(std::memory_order_acquire))
    {
      static_cast<void>(::pthread_kill(target, SIGUSR1));
      std::this_thread::sleep_for(1ms);
    }
  });

  auto const begin = std::chrono::steady_clock::now();
  auto interrupted = child ? supervisor.wait_for_activity(child->handle, std::span<PipeWatchV1 const>{}, begin + 120ms)
                           : ava::core::Result<ProcessActivityV1>(std::unexpected(child.error()));
  auto const elapsed = std::chrono::steady_clock::now() - begin;
  stop_signals.store(true, std::memory_order_release);
  {
    std::lock_guard lock(mutex);
    if (!target_ready)
      target_ready = true;
  }
  changed.notify_all();
  interrupter.join();
  ava::process::testing::SupervisorTestAccess::clear_after_completion_channel_create_hook(supervisor);

  auto const past_begin = std::chrono::steady_clock::now();
  auto past = child ? supervisor.wait_for_activity(child->handle, std::span<PipeWatchV1 const>{}, past_begin - 1s)
                    : ava::core::Result<ProcessActivityV1>(std::unexpected(child.error()));
  auto const past_elapsed = std::chrono::steady_clock::now() - past_begin;
  bool const released = child && child->standard_input && write_text(*child->standard_input, "X", std::chrono::steady_clock::now() + 2s);
  if (child && child->standard_input)
    child->standard_input->close();
  auto status = child ? wait_status(supervisor, child->handle) : std::nullopt;
  if (installed)
    static_cast<void>(::sigaction(SIGUSR1, &previous, nullptr));

  expect(installed && interrupted && interrupted->deadline_expired && !interrupted->process_finished && interrupted->ready.empty() && elapsed >= 80ms &&
             elapsed < 500ms && past && past->deadline_expired && past_elapsed < 50ms && released && status && status->exit_code == 0,
         "EINTR recomputes one absolute steady-clock budget and an already-past deadline performs only an immediate observation");
}

void test_invalid_watches_are_rejected()
{
  auto application = application_owner();
  ava::process::Supervisor supervisor;
  SupervisorFallback fallback(supervisor);
  auto child = spawn_fake(supervisor, application, "ready-gate", ava::process::StreamModeV1::Capture);
  std::string ready_text;
  bool const ready = child && child->standard_input && child->standard_output && child->standard_error &&
                     read_until(*child->standard_output, ready_text, "READY\n", std::chrono::steady_clock::now() + 2s);
  if (!ready)
  {
    expect(false, "invalid-watch fixture reaches its child gate");
    return;
  }

  auto output_one = child->standard_output->watch(PipeInterestV1::Readable, 1);
  auto output_two = child->standard_output->watch(PipeInterestV1::Readable, 2);
  auto error_same_token = child->standard_error->watch(PipeInterestV1::Readable, 1);
  auto input_watch = child->standard_input->watch(PipeInterestV1::Writable, 3);
  auto invalid_interest = child->standard_output->watch(static_cast<PipeInterestV1>(999), 4);
  auto wrong_output = child->standard_output->watch(PipeInterestV1::Writable, 5);
  auto wrong_input = child->standard_input->watch(PipeInterestV1::Readable, 6);

  std::vector<PipeWatchV1> nine(9, *output_one);
  auto too_many = supervisor.wait_for_activity(child->handle, nine, std::chrono::steady_clock::now() + 1s);
  std::array<PipeWatchV1, 2> duplicate_tokens{*output_one, *error_same_token};
  auto token_error = supervisor.wait_for_activity(child->handle, duplicate_tokens, std::chrono::steady_clock::now() + 1s);
  std::array<PipeWatchV1, 2> duplicate_interests{*output_one, *output_two};
  auto interest_error = supervisor.wait_for_activity(child->handle, duplicate_interests, std::chrono::steady_clock::now() + 1s);

  auto closed_watch = child->standard_error->watch(PipeInterestV1::Readable, 8);
  child->standard_error->close();
  std::array<PipeWatchV1, 1> closed_watches{*closed_watch};
  auto closed_error = supervisor.wait_for_activity(child->handle, closed_watches, std::chrono::steady_clock::now() + 1s);
  auto watch_after_close = child->standard_error->watch(PipeInterestV1::Readable, 9);

  auto stale_child = spawn_fake(supervisor, application, "normal", ava::process::StreamModeV1::Discard, ava::process::StreamModeV1::Capture,
                                ava::process::StreamModeV1::Discard);
  auto stale_watch = stale_child && stale_child->standard_output ? stale_child->standard_output->watch(PipeInterestV1::Readable, 10)
                                                                 : ava::core::Result<PipeWatchV1>(std::unexpected(stale_child.error()));
  if (stale_child)
    stale_child->standard_output.reset();
  std::array<PipeWatchV1, 1> stale_watches{*stale_watch};
  auto stale_error = supervisor.wait_for_activity(child->handle, stale_watches, std::chrono::steady_clock::now() + 1s);

  ava::process::Supervisor foreign;
  auto foreign_error = foreign.wait_for_activity(child->handle, std::span<PipeWatchV1 const>{}, std::chrono::steady_clock::now());
  ava::process::ProcessHandle invalid_handle;
  auto invalid_handle_error = supervisor.wait_for_activity(invalid_handle, std::span<PipeWatchV1 const>{}, std::chrono::steady_clock::now());

  bool const released = write_text(*child->standard_input, "G", std::chrono::steady_clock::now() + 2s);
  child->standard_input->close();
  auto status = wait_status(supervisor, child->handle);
  expect(
      output_one && output_two && error_same_token && input_watch && !invalid_interest && !wrong_output && !wrong_input && !too_many && !token_error &&
          !interest_error && closed_watch && !closed_error && !watch_after_close && stale_watch && !stale_error && !foreign_error && !invalid_handle_error &&
          released && status,
      "activity validation rejects nine watches, duplicate tokens/interests, bad directions/enums, closed/stale endpoints, and foreign handles before polling");
}

void test_completion_creation_races_repeat()
{
  auto application = application_owner();
  ava::process::Supervisor supervisor;
  SupervisorFallback fallback(supervisor);
  bool final_before_create = true;
  bool create_before_final = true;

  for (int iteration = 0; iteration < 6; ++iteration)
  {
    auto finished = spawn_fake(supervisor, application, "normal", ava::process::StreamModeV1::Discard, ava::process::StreamModeV1::Discard,
                               ava::process::StreamModeV1::Discard);
    auto status = finished ? wait_status(supervisor, finished->handle) : std::nullopt;
    auto activity = finished ? supervisor.wait_for_activity(finished->handle, std::span<PipeWatchV1 const>{}, std::chrono::steady_clock::now() + 1s)
                             : ava::core::Result<ProcessActivityV1>(std::unexpected(finished.error()));
    final_before_create = final_before_create && status && activity && activity->process_finished && !activity->deadline_expired;

    auto gated = spawn_fake(supervisor, application, "input-gate", ava::process::StreamModeV1::Capture, ava::process::StreamModeV1::Discard,
                            ava::process::StreamModeV1::Discard);
    auto latch = std::make_shared<Latch>();
    ava::process::testing::SupervisorTestAccess::set_after_completion_channel_create_hook(supervisor, [latch] { latch->arrive_and_wait(); });
    std::optional<ProcessActivityV1> observed;
    bool observation_failed = false;
    std::thread waiter;
    if (gated)
    {
      waiter = std::thread([&] {
        auto activity = supervisor.wait_for_activity(gated->handle, std::span<PipeWatchV1 const>{}, std::chrono::steady_clock::now() + 2s);
        if (activity)
          observed.emplace(std::move(*activity));
        else
          observation_failed = true;
      });
    }
    bool const created = gated && latch->wait_until(std::chrono::steady_clock::now() + 2s);
    bool const released = created && gated->standard_input && write_text(*gated->standard_input, "X", std::chrono::steady_clock::now() + 1s);
    latch->release();
    if (waiter.joinable())
      waiter.join();
    ava::process::testing::SupervisorTestAccess::clear_after_completion_channel_create_hook(supervisor);
    create_before_final =
        create_before_final && created && released && observed && !observation_failed && observed->process_finished && !observed->deadline_expired;
  }

  expect(final_before_create && create_before_final,
         "final-before-channel and channel-before-final publication orders repeat without a lost completion wakeup");
}

void test_completion_descriptor_hygiene_and_bounds()
{
  auto application = application_owner();
  auto baseline = descriptor_count();
  bool later_exec_clean = false;
  bool no_per_wait_growth = false;
  bool no_record_growth = false;
  {
    ava::process::Supervisor supervisor;
    SupervisorFallback fallback(supervisor);
    auto gated = spawn_fake(supervisor, application, "input-gate", ava::process::StreamModeV1::Capture, ava::process::StreamModeV1::Discard,
                            ava::process::StreamModeV1::Discard);
    auto before_channel = descriptor_count();
    auto first = gated ? supervisor.wait_for_activity(gated->handle, std::span<PipeWatchV1 const>{}, std::chrono::steady_clock::now() - 1s)
                       : ava::core::Result<ProcessActivityV1>(std::unexpected(gated.error()));
    auto after_channel = descriptor_count();
    bool repeats = first && first->deadline_expired;
    for (int iteration = 0; iteration < 20; ++iteration)
    {
      auto repeated = gated ? supervisor.wait_for_activity(gated->handle, std::span<PipeWatchV1 const>{}, std::chrono::steady_clock::now() - 1s)
                            : ava::core::Result<ProcessActivityV1>(std::unexpected(gated.error()));
      repeats = repeats && repeated && repeated->deadline_expired;
    }
    auto after_repeats = descriptor_count();
    no_per_wait_growth =
        !before_channel || !after_channel || !after_repeats || (*after_channel == *before_channel + 2 && *after_repeats == *after_channel && repeats);

    auto checker = spawn_fake(supervisor, application, "check-only-standard-fds", ava::process::StreamModeV1::Discard, ava::process::StreamModeV1::Capture,
                              ava::process::StreamModeV1::Discard);
    std::string checker_text;
    later_exec_clean =
        checker && checker->standard_output && read_until(*checker->standard_output, checker_text, "CLEAN\n", std::chrono::steady_clock::now() + 2s);
    auto checker_status = checker ? wait_status(supervisor, checker->handle) : std::nullopt;
    later_exec_clean = later_exec_clean && checker_status && checker_status->exit_code == 0;
    if (checker)
    {
      checker->handle = ava::process::ProcessHandle{};
      checker->standard_output.reset();
    }

    bool const released = gated && gated->standard_input && write_text(*gated->standard_input, "X", std::chrono::steady_clock::now() + 2s);
    if (gated && gated->standard_input)
      gated->standard_input->close();
    auto gated_status = gated ? wait_status(supervisor, gated->handle) : std::nullopt;
    if (gated)
    {
      gated->handle = ava::process::ProcessHandle{};
      gated->standard_input.reset();
    }

    bool records_ok = released && gated_status.has_value();
    for (int iteration = 0; iteration < 20; ++iteration)
    {
      auto child = spawn_fake(supervisor, application, "normal", ava::process::StreamModeV1::Discard, ava::process::StreamModeV1::Discard,
                              ava::process::StreamModeV1::Discard);
      auto status = child ? wait_status(supervisor, child->handle) : std::nullopt;
      auto activity = child ? supervisor.wait_for_activity(child->handle, std::span<PipeWatchV1 const>{}, std::chrono::steady_clock::now() + 1s)
                            : ava::core::Result<ProcessActivityV1>(std::unexpected(child.error()));
      records_ok = records_ok && status && activity && activity->process_finished;
    }
    auto const cycle = ava::process::testing::SupervisorTestAccess::pulse_monitor(supervisor);
    static_cast<void>(ava::process::testing::SupervisorTestAccess::wait_for_monitor_cycle(supervisor, cycle, std::chrono::steady_clock::now() + 1s));
    auto monitor = ava::process::testing::SupervisorTestAccess::monitor_snapshot(supervisor);
    auto after_records = descriptor_count();
    no_record_growth = !baseline || !after_records ||
                       (*after_records == *baseline + monitor.counters.current_wake_descriptors && monitor.counters.current_watches == 0 && records_ok);
  }
  auto after_supervisor = descriptor_count();
  bool const teardown_clean = !baseline || !after_supervisor || *baseline == *after_supervisor;

  expect(later_exec_clean && no_per_wait_growth && no_record_growth && teardown_clean,
         "completion channels are CLOEXEC, created once per used handle, and released across repeated waits, records, and supervisor teardown");
}

void test_public_header_has_no_native_identity_accessor()
{
  std::ifstream input(AVA_PROCESS_PUBLIC_HEADER_PATH, std::ios::binary);
  std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  bool const neutral = input.good() || input.eof();
  bool const no_native_names = source.find("pid_t") == std::string::npos && source.find("native_handle") == std::string::npos &&
                               source.find("file_descriptor") == std::string::npos && source.find("descriptor()") == std::string::npos &&
                               source.find("process_id()") == std::string::npos;
  expect(!source.empty() && neutral && no_native_names, "the public supervisor header exposes no descriptor or native process identity accessor");
}

}  // namespace
#endif

void run_process_readiness_tests()
{
#if defined(_WIN32)
  ava::tests::request_skip("process readiness POSIX backend is compile-time unsupported");
#else
  test_try_wait_final_values();
  test_stream_readiness_and_tokens();
  test_hup_is_separate_and_drainable();
  test_completion_wakeup_and_final_before_wait();
  test_absolute_deadline_survives_eintr();
  test_invalid_watches_are_rejected();
  test_completion_creation_races_repeat();
  test_completion_descriptor_hygiene_and_bounds();
  test_public_header_has_no_native_identity_accessor();
#endif
}
