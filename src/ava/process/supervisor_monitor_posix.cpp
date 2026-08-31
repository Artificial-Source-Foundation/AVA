#include "sys.h"
#include "ava/process/supervisor_internal.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <optional>
#include <thread>
#include <utility>
#if !defined(_WIN32)
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <dirent.h>
#endif
#endif

namespace ava::process::detail {

#if !defined(_WIN32)
namespace {

#if defined(__linux__)
bool disappearance_error(int error_number) noexcept
{
  return error_number == ENOENT || error_number == ESRCH;
}

bool valid_linux_process_state(char state) noexcept
{
  switch (state)
  {
    case 'R':
    case 'S':
    case 'D':
    case 'Z':
    case 'T':
    case 't':
    case 'X':
    case 'x':
    case 'K':
    case 'W':
    case 'P':
    case 'I':
      return true;
  }
  return false;
}
#endif

bool finish_proven_quiet_natural_exit_locked(SupervisorState& state, Record& record)
{
  auto const group_live = verified_group_has_live_member(record.leader);
  if (!group_live)
  {
    // Without a complete group observation, exact-child reaping cannot prove
    // that inherited group members are gone.
    record.cleanup_failed = true;
    return false;
  }
  if (*group_live)
  {
    record.quiet_group_observations = 0;
    return false;
  }

  if (record.quiet_group_observations < 2)
    ++record.quiet_group_observations;
  bool const direct_children_observed = record.leader_observed && (record.sentinel <= 1 || record.sentinel_observed);
  if (record.quiet_group_observations < 2 || !direct_children_observed)
    return true;

  bool const leader_reaped = reap_member(record.leader, true, record.leader_reaped, record);
  bool const sentinel_reaped = record.sentinel <= 1 || reap_member(record.sentinel, true, record.sentinel_reaped, record);
  if (leader_reaped && sentinel_reaped)
    finalize_locked(state, record, record.cleanup_failed ? CleanupStateV1::Incomplete : CleanupStateV1::Complete);
  return true;
}

}  // namespace

void observe_status(Record& record, siginfo_t const& information) noexcept
{
  if (information.si_code == CLD_EXITED)
  {
    record.exit_kind = ExitKindV1::Exited;
    record.exit_code = information.si_status;
    record.has_exit_code = true;
  }
  else if (information.si_code == CLD_KILLED || information.si_code == CLD_DUMPED)
  {
    record.exit_kind = ExitKindV1::Signaled;
    record.signal_number = information.si_status;
    record.has_signal_number = true;
  }
  else
  {
    record.cleanup_failed = true;
  }
}

bool observe_member(pid_t process, bool& observed, Record& record, bool leader) noexcept
{
  if (process <= 1 || observed)
    return true;
  siginfo_t information{};
  if (waitid_retry(process, &information, WEXITED | WSTOPPED | WNOHANG | WNOWAIT) != 0)
  {
    if (errno == ECHILD)
    {
      record.cleanup_failed = true;
      observed = true;
      return false;
    }
    record.cleanup_failed = true;
    return false;
  }
  if (information.si_pid != process)
    return true;
  if (information.si_code == CLD_STOPPED)
  {
    static_cast<void>(commit_reason_locked(record, leader ? TerminationReasonV1::UnsupportedSuspension : TerminationReasonV1::ProtocolFailure));
    return true;
  }
  observed = true;
  if (leader)
    observe_status(record, information);
  return true;
}

bool reap_member(pid_t process, bool observed, bool& reaped, Record& record) noexcept
{
  if (process <= 1 || reaped)
    return true;
  if (!observed)
    return false;
  int status = 0;
  auto const waited = waitpid_retry(process, &status, WNOHANG);
  if (waited == process)
  {
    reaped = true;
    return true;
  }
  record.cleanup_failed = true;
  if (waited < 0 && errno == ECHILD)
    reaped = true;
  return false;
}

bool signal_verified_group(Record& record, int signal_number) noexcept
{
  if (!record.registered || !record.group_verified || record.leader <= 1)
  {
    record.cleanup_failed = true;
    return false;
  }
  if (::kill(-record.leader, signal_number) == 0 || errno == ESRCH)
    return true;
  record.cleanup_failed = true;
  return false;
}

std::optional<bool> verified_group_has_live_member(pid_t group) noexcept
{
#if defined(__linux__)
  int const proc_descriptor = ::open("/proc", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK);
  if (proc_descriptor < 0)
    return std::nullopt;
  DIR* directory = ::fdopendir(proc_descriptor);
  if (directory == nullptr)
  {
    static_cast<void>(::close(proc_descriptor));
    return std::nullopt;
  }

  bool live = false;
  bool failed = false;
  while (true)
  {
    errno = 0;
    dirent* entry = ::readdir(directory);
    if (entry == nullptr)
    {
      failed = errno != 0;
      break;
    }
    char const* name = entry->d_name;
    if (*name == '\0')
      continue;
    bool numeric = true;
    for (char const* character = name; *character != '\0'; ++character)
      numeric = numeric && *character >= '0' && *character <= '9';
    if (!numeric)
      continue;

    int const process_directory = ::openat(proc_descriptor, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK);
    if (process_directory < 0)
    {
      if (disappearance_error(errno))
        continue;
      failed = true;
      break;
    }
    int const stat_descriptor = ::openat(process_directory, "stat", O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    int const stat_open_error = errno;
    static_cast<void>(::close(process_directory));
    if (stat_descriptor < 0)
    {
      if (disappearance_error(stat_open_error))
        continue;
      failed = true;
      break;
    }

    std::array<char, 4096> buffer{};
    ssize_t count = -1;
    do
      count = ::read(stat_descriptor, buffer.data(), buffer.size() - 1);
    while (count < 0 && errno == EINTR);
    int const read_error = errno;
    static_cast<void>(::close(stat_descriptor));
    if (count < 0)
    {
      if (disappearance_error(read_error))
        continue;
      failed = true;
      break;
    }
    if (count == 0)
    {
      failed = true;
      break;
    }

    buffer[static_cast<std::size_t>(count)] = '\0';
    char* command_end = nullptr;
    for (char* current = buffer.data(); current[0] != '\0' && current[1] != '\0'; ++current)
    {
      if (current[0] == ')' && current[1] == ' ')
        command_end = current;
    }
    if (command_end == nullptr)
    {
      failed = true;
      break;
    }
    char process_state = '\0';
    long long parent = 0;
    long long process_group = 0;
    if (std::sscanf(command_end + 2, "%c %lld %lld", &process_state, &parent, &process_group) != 3 || !valid_linux_process_state(process_state))
    {
      failed = true;
      break;
    }
    if (process_group == group && process_state != 'Z' && process_state != 'X' && process_state != 'x')
    {
      live = true;
      break;
    }
  }
  if (::closedir(directory) != 0 && !live)
    failed = true;
  return live ? std::optional<bool>(true) : (failed ? std::nullopt : std::optional<bool>(false));
#else
  // Conservative POSIX has no race-free group enumeration primitive while the
  // waitable leader is deliberately retained. Signal delivery and exact child
  // observation are the semantic floor; platform backends that cannot prove
  // more must report incomplete rather than inventing success.
  static_cast<void>(group);
  return std::nullopt;
#endif
}

void begin_stop_locked(Record& record, Clock::time_point now)
{
  if (!record.term_sent)
  {
    if (record.reason == TerminationReasonV1::UnsupportedSuspension)
      static_cast<void>(signal_verified_group(record, SIGCONT));
    static_cast<void>(signal_verified_group(record, SIGTERM));
    record.term_sent = true;
    auto due = now + record.policy.termination_grace;
    if (record.stop_deadline && *record.stop_deadline < due)
      due = *record.stop_deadline;
    record.kill_due = due;
  }
  if (record.state != ProcessStateV1::Reaping)
    record.state = ProcessStateV1::StopRequested;
}

void monitor_record_locked(SupervisorState& state, Record& record, Clock::time_point now)
{
  if (!record.registered || record.state == ProcessStateV1::Finished)
    return;
  // The spawning thread owns the CLOEXEC exec-error handshake. Do not let an
  // immediately failing image race natural-exit classification ahead of the
  // typed ExecFailed reason; shutdown reasons still wake and stop a launch.
  if (!record.startup_handshake_complete && !record.reason)
    return;

  static_cast<void>(observe_member(record.leader, record.leader_observed, record, true));
  if (record.sentinel > 1)
    static_cast<void>(observe_member(record.sentinel, record.sentinel_observed, record, false));

  if (record.leader_observed)
  {
    static_cast<void>(commit_reason_locked(record, TerminationReasonV1::NaturalExit));
    record.state = ProcessStateV1::Reaping;
    if (!record.stop_deadline)
      record.stop_deadline = now + kDefaultCleanupBudget;
  }
  else if (record.sentinel > 1 && record.sentinel_observed && !record.reason)
  {
    static_cast<void>(commit_reason_locked(record, TerminationReasonV1::ProtocolFailure));
  }

  if (record.reason == TerminationReasonV1::NaturalExit && record.leader_observed && finish_proven_quiet_natural_exit_locked(state, record))
    return;

  if (record.reason)
    begin_stop_locked(record, now);

  if (record.kill_due && now >= *record.kill_due && !record.kill_sent)
  {
    static_cast<void>(signal_verified_group(record, SIGKILL));
    record.kill_sent = true;
    record.post_kill_due = now + kPostKillObservation;
  }

  if (record.kill_sent)
  {
    static_cast<void>(observe_member(record.leader, record.leader_observed, record, true));
    if (record.sentinel > 1)
      static_cast<void>(observe_member(record.sentinel, record.sentinel_observed, record, false));
  }

  bool const direct_children_observed = record.leader_observed && (record.sentinel <= 1 || record.sentinel_observed);
  if (record.kill_sent && record.post_kill_due && now >= *record.post_kill_due && direct_children_observed)
  {
    auto const group_live = verified_group_has_live_member(record.leader);
    if (!group_live)
      record.cleanup_failed = true;
    if (group_live && *group_live)
      record.quiet_group_observations = 0;
    else if (group_live && record.quiet_group_observations < 2)
      ++record.quiet_group_observations;
    if (record.quiet_group_observations >= 2)
    {
      bool const leader_reaped = reap_member(record.leader, record.leader_observed, record.leader_reaped, record);
      bool const sentinel_reaped = record.sentinel <= 1 || reap_member(record.sentinel, record.sentinel_observed, record.sentinel_reaped, record);
      if (leader_reaped && sentinel_reaped)
      {
        finalize_locked(state, record, record.cleanup_failed ? CleanupStateV1::Incomplete : CleanupStateV1::Complete);
        return;
      }
    }
  }

  if (record.stop_deadline && now >= *record.stop_deadline)
  {
    if (!record.kill_sent)
    {
      static_cast<void>(signal_verified_group(record, SIGKILL));
      record.kill_sent = true;
    }
    static_cast<void>(observe_member(record.leader, record.leader_observed, record, true));
    if (record.sentinel > 1)
      static_cast<void>(observe_member(record.sentinel, record.sentinel_observed, record, false));
    auto const group_live = verified_group_has_live_member(record.leader);
    if (!group_live || *group_live)
      record.cleanup_failed = true;
    if (group_live && !*group_live && record.quiet_group_observations < 2)
      ++record.quiet_group_observations;
    if (record.quiet_group_observations >= 2)
    {
      if (record.leader_observed)
        static_cast<void>(reap_member(record.leader, true, record.leader_reaped, record));
      if (record.sentinel > 1 && record.sentinel_observed)
        static_cast<void>(reap_member(record.sentinel, true, record.sentinel_reaped, record));
    }
    bool const reaped = record.leader_reaped && (record.sentinel <= 1 || record.sentinel_reaped);
    finalize_locked(state, record, reaped && !record.cleanup_failed ? CleanupStateV1::Complete : CleanupStateV1::Incomplete);
  }
}

void monitor_main(std::shared_ptr<SupervisorState> state) noexcept
{
  std::unique_lock lock(state->mutex);
  while (true)
  {
    auto const now = Clock::now();
    for (auto& [identity, record] : state->records)
    {
      static_cast<void>(identity);
      monitor_record_locked(*state, *record, now);
    }

    if (state->stop_monitor)
      break;

    bool has_registered_child = false;
    for (auto const& [identity, record] : state->records)
    {
      static_cast<void>(identity);
      if (record->registered && record->state != ProcessStateV1::Finished)
      {
        has_registered_child = true;
        break;
      }
    }
    if (has_registered_child)
      state->changed.wait_for(lock, kMonitorPollInterval);
    else
      state->changed.wait(lock);
  }
  state->changed.notify_all();
}

ava::core::VoidResult ensure_monitor_started(std::shared_ptr<SupervisorState> const& state)
{
  std::lock_guard lifecycle_lock(state->monitor_lifecycle_mutex);
  {
    std::lock_guard lock(state->mutex);
    if (state->stop_monitor || state->monitor_joined)
      return std::unexpected(process_error(ava::core::ErrorCategory::Io, "process supervisor shutdown won the launch race"));
  }
  if (state->monitor.joinable())
    return {};
  try
  {
    state->monitor = std::thread(monitor_main, state);
  }
  catch (std::exception const& error)
  {
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "failed to start the process monitor").with_context("cause", error.what()));
  }
  catch (...)
  {
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "failed to start the process monitor"));
  }
  {
    std::lock_guard lock(state->mutex);
    state->monitor_started = true;
  }
  state->changed.notify_all();
  return {};
}

void join_monitor(std::shared_ptr<SupervisorState> const& state) noexcept
{
  std::lock_guard lifecycle_lock(state->monitor_lifecycle_mutex);
  if (state->monitor.joinable())
    state->monitor.join();
  state->monitor_joined = true;
}

#endif

}  // namespace ava::process::detail
