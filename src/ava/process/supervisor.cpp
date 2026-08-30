#include "sys.h"
#include "ava/process/supervisor.h"
#include "ava/core/error.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <climits>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <dirent.h>
#include <sys/syscall.h>
#endif
#endif

namespace ava::process {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr auto kLaunchHandshakeTimeout = 2s;
constexpr auto kMonitorPollInterval = 10ms;
constexpr auto kPostKillObservation = 20ms;
constexpr auto kDefaultCleanupBudget = 2s;
constexpr std::size_t kMaxArgumentCount = 256;
constexpr std::size_t kMaxEnvironmentCount = 256;
constexpr std::size_t kMaxPreparedBytes = 1024 * 1024;

ava::core::Error process_error(ava::core::ErrorCategory category, std::string message)
{
  return ava::core::Error(category, std::move(message));
}

#if defined(_WIN32)
ava::core::Error unsupported_error()
{
  return process_error(ava::core::ErrorCategory::Io, "process supervision is unsupported on this platform");
}
#endif

ava::core::Error invalid_error(std::string message)
{
  return process_error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
}

ava::core::Error io_error(std::string message, int error_number)
{
  auto error = process_error(ava::core::ErrorCategory::Io, std::move(message));
  error.with_context("cause", std::strerror(error_number));
  return error;
}

bool requestable_reason(TerminationReasonV1 reason) noexcept
{
  switch (reason)
  {
    case TerminationReasonV1::Canceled:
    case TerminationReasonV1::DeadlineExpired:
    case TerminationReasonV1::OwnerShutdown:
    case TerminationReasonV1::ApplicationShutdown:
    case TerminationReasonV1::OutputLimit:
    case TerminationReasonV1::ProtocolFailure:
    case TerminationReasonV1::UnsupportedSuspension:
      return true;
    case TerminationReasonV1::NaturalExit:
    case TerminationReasonV1::LaunchFailed:
    case TerminationReasonV1::ExecFailed:
      return false;
  }
  return false;
}

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept
{
  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  return right > maximum - left ? maximum : left + right;
}

std::uint64_t elapsed_milliseconds(Clock::time_point begin, Clock::time_point end) noexcept
{
  if (end <= begin)
    return 0;
  auto const value = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
  return value <= 0 ? 0 : static_cast<std::uint64_t>(value);
}

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
  void reset(int descriptor = -1) noexcept
  {
    if (descriptor_ >= 0)
      static_cast<void>(::close(descriptor_));
    descriptor_ = descriptor;
  }

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

ava::core::Result<int> move_above_standard_descriptors(int descriptor)
{
  if (descriptor > STDERR_FILENO)
    return descriptor;
  int const moved = ::fcntl(descriptor, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
  int const saved_errno = errno;
  static_cast<void>(::close(descriptor));
  if (moved < 0)
    return std::unexpected(io_error("failed to move a process descriptor above standard streams", saved_errno));
  return moved;
}

ava::core::Result<Pipe> make_cloexec_pipe()
{
  std::array<int, 2> raw{-1, -1};
#if defined(__linux__)
  if (::pipe2(raw.data(), O_CLOEXEC) != 0)
    return std::unexpected(io_error("failed to create a close-on-exec process pipe", errno));
#else
  if (::pipe(raw.data()) != 0)
    return std::unexpected(io_error("failed to create a process pipe", errno));
  if (::fcntl(raw[0], F_SETFD, FD_CLOEXEC) != 0 || ::fcntl(raw[1], F_SETFD, FD_CLOEXEC) != 0)
  {
    int const saved_errno = errno;
    static_cast<void>(::close(raw[0]));
    static_cast<void>(::close(raw[1]));
    return std::unexpected(io_error("failed to make a process pipe close-on-exec", saved_errno));
  }
#endif

  auto read_end = move_above_standard_descriptors(raw[0]);
  if (!read_end)
  {
    static_cast<void>(::close(raw[1]));
    return std::unexpected(std::move(read_end.error()));
  }
  auto write_end = move_above_standard_descriptors(raw[1]);
  if (!write_end)
  {
    static_cast<void>(::close(*read_end));
    return std::unexpected(std::move(write_end.error()));
  }
  return Pipe{.read_end = UniqueFd(*read_end), .write_end = UniqueFd(*write_end)};
}

ava::core::VoidResult set_nonblocking(int descriptor)
{
  int const flags = ::fcntl(descriptor, F_GETFL, 0);
  if (flags < 0 || ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0)
    return std::unexpected(io_error("failed to make a parent process endpoint nonblocking", errno));
  return {};
}

int poll_timeout(ProcessDeadline deadline) noexcept
{
  auto const now = Clock::now();
  if (deadline <= now)
    return 0;
  auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
  if (remaining >= std::chrono::milliseconds(INT_MAX))
    return INT_MAX;
  return std::max(1, static_cast<int>(remaining.count()));
}

ava::core::Result<bool> wait_descriptor(int descriptor, short events, ProcessDeadline deadline)
{
  while (true)
  {
    pollfd item{.fd = descriptor, .events = events, .revents = 0};
    int const result = ::poll(&item, 1, poll_timeout(deadline));
    if (result > 0)
    {
      if ((item.revents & POLLNVAL) != 0)
        return std::unexpected(invalid_error("process pipe endpoint is closed"));
      return (item.revents & (events | POLLERR | POLLHUP)) != 0;
    }
    if (result == 0)
      return false;
    if (errno != EINTR)
      return std::unexpected(io_error("failed while waiting for a process pipe endpoint", errno));
    if (Clock::now() >= deadline)
      return false;
  }
}

bool write_without_sigpipe(int descriptor, void const* data, std::size_t size) noexcept
{
  sigset_t blocked{};
  sigset_t previous{};
  if (::sigemptyset(&blocked) != 0 || ::sigaddset(&blocked, SIGPIPE) != 0 || ::pthread_sigmask(SIG_BLOCK, &blocked, &previous) != 0)
    return false;

  bool const was_blocked = ::sigismember(&previous, SIGPIPE) == 1;
  auto const* next = static_cast<unsigned char const*>(data);
  std::size_t offset = 0;
  bool success = true;
  bool broken = false;
  while (offset < size)
  {
    auto const written = ::write(descriptor, next + offset, size - offset);
    if (written > 0)
    {
      offset += static_cast<std::size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR)
      continue;
    broken = written < 0 && errno == EPIPE;
    success = false;
    break;
  }
  if (broken && !was_blocked)
  {
    timespec const no_wait{};
    while (::sigtimedwait(&blocked, nullptr, &no_wait) < 0 && errno == EINTR)
    {
    }
  }
  if (::pthread_sigmask(SIG_SETMASK, &previous, nullptr) != 0)
    success = false;
  return success;
}

pid_t waitpid_retry(pid_t process, int* status, int options) noexcept
{
  while (true)
  {
    auto const result = ::waitpid(process, status, options);
    if (result < 0 && errno == EINTR)
      continue;
    return result;
  }
}

int waitid_retry(pid_t process, siginfo_t* information, int options) noexcept
{
  while (true)
  {
    std::memset(information, 0, sizeof(*information));
    auto const result = ::waitid(P_PID, static_cast<id_t>(process), information, options);
    if (result < 0 && errno == EINTR)
      continue;
    return result;
  }
}

bool exact_provisional_cleanup(pid_t leader, pid_t sentinel, ProcessDeadline deadline) noexcept
{
  std::array<pid_t, 2> children{leader, sentinel};
  bool complete = true;
  for (pid_t child : children)
  {
    if (child > 1 && ::kill(child, SIGKILL) != 0 && errno != ESRCH)
      complete = false;
  }
  for (pid_t child : children)
  {
    if (child <= 1)
      continue;
    bool reaped = false;
    int status = 0;
    while (Clock::now() < deadline)
    {
      auto const waited = waitpid_retry(child, &status, WNOHANG);
      if (waited == child)
      {
        reaped = true;
        break;
      }
      if (waited < 0)
        break;
      std::this_thread::sleep_for(1ms);
    }
    if (!reaped)
      reaped = waitpid_retry(child, &status, WNOHANG) == child;
    complete = complete && reaped;
  }
  return complete;
}

bool reset_child_signal_state() noexcept
{
  sigset_t empty{};
  if (::sigemptyset(&empty) != 0 || ::sigprocmask(SIG_SETMASK, &empty, nullptr) != 0)
    return false;
  struct sigaction action{};
  action.sa_handler = SIG_DFL;
  if (::sigemptyset(&action.sa_mask) != 0)
    return false;
  for (int signal_number = 1; signal_number < NSIG; ++signal_number)
  {
    if (signal_number == SIGKILL || signal_number == SIGSTOP)
      continue;
    if (::sigaction(signal_number, &action, nullptr) != 0 && errno != EINVAL)
      return false;
  }
  return true;
}

ssize_t child_read_retry(int descriptor, void* data, std::size_t size) noexcept
{
  while (true)
  {
    auto const result = ::read(descriptor, data, size);
    if (result < 0 && errno == EINTR)
      continue;
    return result;
  }
}

bool child_write_all(int descriptor, void const* data, std::size_t size) noexcept
{
  auto const* next = static_cast<unsigned char const*>(data);
  std::size_t offset = 0;
  while (offset < size)
  {
    auto const result = ::write(descriptor, next + offset, size - offset);
    if (result > 0)
    {
      offset += static_cast<std::size_t>(result);
      continue;
    }
    if (result < 0 && errno == EINTR)
      continue;
    return false;
  }
  return true;
}

void close_nonstandard_descriptors(int preserved, int maximum) noexcept
{
#if defined(__linux__) && defined(SYS_close_range)
  bool ranges_closed = true;
  if (preserved > STDERR_FILENO + 1)
  {
    ranges_closed = ::syscall(SYS_close_range, static_cast<unsigned int>(STDERR_FILENO + 1), static_cast<unsigned int>(preserved - 1), 0U) == 0;
  }
  ranges_closed = (::syscall(SYS_close_range, static_cast<unsigned int>(preserved + 1), std::numeric_limits<unsigned int>::max(), 0U) == 0) && ranges_closed;
  if (ranges_closed)
    return;
#endif
  for (int descriptor = STDERR_FILENO + 1; descriptor < maximum; ++descriptor)
  {
    if (descriptor != preserved)
      static_cast<void>(::close(descriptor));
  }
}

int descriptor_limit() noexcept
{
  rlimit limit{};
  if (::getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY)
    return static_cast<int>(std::min<rlim_t>(limit.rlim_cur, static_cast<rlim_t>(INT_MAX)));
  return 4096;
}

#endif

}  // namespace

namespace detail {

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

}  // namespace detail

namespace {

void release_owner_alias_locked(detail::SupervisorState& state, detail::Record const& record)
{
  auto found = state.owner_aliases.find(record.owner_key);
  if (found == state.owner_aliases.end())
    return;
  if (found->second.references > 1)
    --found->second.references;
  else
    state.owner_aliases.erase(found);
}

void prune_terminal_locked(detail::SupervisorState& state)
{
  while (state.terminal_fifo.size() > kMaxRetainedProcessRecordsV1)
  {
    auto const identity = state.terminal_fifo.front();
    state.terminal_fifo.pop_front();
    auto found = state.records.find(identity);
    if (found == state.records.end() || found->second->state != ProcessStateV1::Finished)
      continue;
    release_owner_alias_locked(state, *found->second);
    state.records.erase(found);
  }
}

void await_internal_settlement(std::shared_ptr<detail::HandleState> const& handle, ProcessDeadline deadline) noexcept
{
  if (!handle)
    return;
  std::unique_lock lock(handle->mutex);
  static_cast<void>(handle->changed.wait_until(lock, deadline, [&] { return handle->final_status.has_value(); }));
}

void publish_final_locked(detail::Record& record, ExitStatusV1 status)
{
  if (!record.handle)
    return;
  {
    std::lock_guard handle_lock(record.handle->mutex);
    if (!record.handle->final_status)
      record.handle->final_status = status;
  }
  record.handle->changed.notify_all();
}

void finalize_locked(detail::SupervisorState& state, detail::Record& record, CleanupStateV1 cleanup)
{
  if (record.state == ProcessStateV1::Finished)
    return;
  if (!record.reason)
    record.reason = TerminationReasonV1::LaunchFailed;
  record.cleanup = cleanup;
  if (cleanup == CleanupStateV1::Incomplete && record.exit_kind == ExitKindV1::None)
    record.exit_kind = ExitKindV1::CleanupIncomplete;
  record.state = ProcessStateV1::Finished;
  record.finished = Clock::now();
  ++record.settlement_count;
  if (state.live_records > 0)
    --state.live_records;
  ++state.settled_records;

  ExitStatusV1 status{.reason = *record.reason,
                      .kind = record.exit_kind,
                      .cleanup = cleanup,
                      .exit_code = record.exit_code,
                      .signal_number = record.signal_number,
                      .has_exit_code = record.has_exit_code,
                      .has_signal_number = record.has_signal_number};
  publish_final_locked(record, status);
  state.terminal_fifo.push_back(record.id);
  prune_terminal_locked(state);
  state.changed.notify_all();
}

bool commit_reason_locked(detail::Record& record, TerminationReasonV1 reason) noexcept
{
  if (record.reason)
    return false;
  record.reason = reason;
  return true;
}

void abandon_reservation(std::shared_ptr<detail::SupervisorState> const& state, std::uint64_t identity) noexcept
{
  if (!state || identity == 0)
    return;
  std::lock_guard lock(state->mutex);
  auto found = state->records.find(identity);
  if (found == state->records.end() || found->second->state != ProcessStateV1::Reserved || found->second->registered)
    return;
  release_owner_alias_locked(*state, *found->second);
  state->records.erase(found);
  if (state->live_records > 0)
    --state->live_records;
  state->changed.notify_all();
}

void finish_unregistered(std::shared_ptr<detail::SupervisorState> const& state, std::uint64_t identity, TerminationReasonV1 fallback,
                         CleanupStateV1 cleanup = CleanupStateV1::NotRequired) noexcept
{
  if (!state || identity == 0)
    return;
  std::lock_guard lock(state->mutex);
  auto found = state->records.find(identity);
  if (found == state->records.end() || found->second->state == ProcessStateV1::Finished)
    return;
  auto& record = *found->second;
  static_cast<void>(commit_reason_locked(record, fallback));
  record.exit_kind = ExitKindV1::LaunchError;
  finalize_locked(*state, record, cleanup);
}

#if !defined(_WIN32)

void observe_status(detail::Record& record, siginfo_t const& information) noexcept
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

bool observe_member(pid_t process, bool& observed, detail::Record& record, bool leader) noexcept
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

bool reap_member(pid_t process, bool observed, bool& reaped, detail::Record& record) noexcept
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

bool signal_verified_group(detail::Record& record, int signal_number) noexcept
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
      continue;
    int const stat_descriptor = ::openat(process_directory, "stat", O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    static_cast<void>(::close(process_directory));
    if (stat_descriptor < 0)
      continue;
    std::array<char, 4096> buffer{};
    auto const count = ::read(stat_descriptor, buffer.data(), buffer.size() - 1);
    static_cast<void>(::close(stat_descriptor));
    if (count <= 0)
      continue;
    buffer[static_cast<std::size_t>(count)] = '\0';
    char* command_end = nullptr;
    for (char* current = buffer.data(); current[0] != '\0' && current[1] != '\0'; ++current)
    {
      if (current[0] == ')' && current[1] == ' ')
        command_end = current;
    }
    if (command_end == nullptr)
      continue;
    char process_state = '\0';
    long long parent = 0;
    long long process_group = 0;
    if (std::sscanf(command_end + 2, "%c %lld %lld", &process_state, &parent, &process_group) == 3 && process_group == group && process_state != 'Z' &&
        process_state != 'X')
    {
      live = true;
      break;
    }
  }
  static_cast<void>(::closedir(directory));
  return failed ? std::nullopt : std::optional<bool>(live);
#else
  // Conservative POSIX has no race-free group enumeration primitive while the
  // waitable leader is deliberately retained. Signal delivery and exact child
  // observation are the semantic floor; platform backends that cannot prove
  // more must report incomplete rather than inventing success.
  static_cast<void>(group);
  return std::nullopt;
#endif
}

void begin_stop_locked(detail::Record& record, Clock::time_point now)
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

void monitor_record_locked(detail::SupervisorState& state, detail::Record& record, Clock::time_point now)
{
  if (!record.registered || record.state == ProcessStateV1::Finished)
    return;
  // The spawning thread owns the CLOEXEC exec-error handshake. Do not let an
  // immediately failing image race natural-exit classification ahead of the
  // typed ExecFailed reason; shutdown reasons still wake and stop a launch.
  if (record.state == ProcessStateV1::Launching && !record.reason)
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

void monitor_main(std::shared_ptr<detail::SupervisorState> state) noexcept
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

ava::core::VoidResult ensure_monitor_started(std::shared_ptr<detail::SupervisorState> const& state)
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

void join_monitor(std::shared_ptr<detail::SupervisorState> const& state) noexcept
{
  std::lock_guard lifecycle_lock(state->monitor_lifecycle_mutex);
  if (state->monitor.joinable())
    state->monitor.join();
  state->monitor_joined = true;
}

#endif

}  // namespace

struct PipeEndpoint::Impl
{
#if !defined(_WIN32)
  explicit Impl(int value, bool can_read, bool can_write) noexcept : descriptor(value), readable(can_read), writable(can_write) { }
  UniqueFd descriptor;
#else
  explicit Impl(bool can_read, bool can_write) noexcept : readable(can_read), writable(can_write) { }
#endif
  bool readable = false;
  bool writable = false;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

PipeEndpoint::PipeEndpoint() noexcept = default;
PipeEndpoint::PipeEndpoint(std::unique_ptr<Impl> implementation) noexcept : implementation_(std::move(implementation))
{
}
PipeEndpoint::PipeEndpoint(PipeEndpoint&&) noexcept = default;
PipeEndpoint& PipeEndpoint::operator=(PipeEndpoint&&) noexcept = default;
PipeEndpoint::~PipeEndpoint() = default;

bool PipeEndpoint::valid() const noexcept
{
#if !defined(_WIN32)
  return implementation_ && implementation_->descriptor.get() >= 0;
#else
  return false;
#endif
}

void PipeEndpoint::close() noexcept
{
#if !defined(_WIN32)
  if (implementation_)
    implementation_->descriptor.reset();
#endif
}

ava::core::Result<PipeIoResultV1> PipeEndpoint::read(std::span<std::byte> destination)
{
#if defined(_WIN32)
  static_cast<void>(destination);
  return std::unexpected(unsupported_error());
#else
  if (!valid() || !implementation_->readable)
    return std::unexpected(invalid_error("process pipe endpoint is not readable"));
  if (destination.empty())
    return PipeIoResultV1{.bytes = 0, .state = PipeIoStateV1::Progress};
  while (true)
  {
    auto const result = ::read(implementation_->descriptor.get(), destination.data(), destination.size());
    if (result > 0)
      return PipeIoResultV1{.bytes = static_cast<std::size_t>(result), .state = PipeIoStateV1::Progress};
    if (result == 0)
      return PipeIoResultV1{.bytes = 0, .state = PipeIoStateV1::EndOfStream};
    if (errno == EINTR)
      continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return PipeIoResultV1{.bytes = 0, .state = PipeIoStateV1::WouldBlock};
    return std::unexpected(io_error("failed to read a process pipe endpoint", errno));
  }
#endif
}

ava::core::Result<PipeIoResultV1> PipeEndpoint::write(std::span<std::byte const> source)
{
#if defined(_WIN32)
  static_cast<void>(source);
  return std::unexpected(unsupported_error());
#else
  if (!valid() || !implementation_->writable)
    return std::unexpected(invalid_error("process pipe endpoint is not writable"));
  if (source.empty())
    return PipeIoResultV1{.bytes = 0, .state = PipeIoStateV1::Progress};

  sigset_t blocked{};
  sigset_t previous{};
  if (::sigemptyset(&blocked) != 0 || ::sigaddset(&blocked, SIGPIPE) != 0 || ::pthread_sigmask(SIG_BLOCK, &blocked, &previous) != 0)
    return std::unexpected(io_error("failed to block SIGPIPE for process pipe write", errno));
  auto const result = ::write(implementation_->descriptor.get(), source.data(), source.size());
  int const saved_errno = errno;
  bool const was_blocked = ::sigismember(&previous, SIGPIPE) == 1;
  if (result < 0 && saved_errno == EPIPE && !was_blocked)
  {
    timespec const no_wait{};
    while (::sigtimedwait(&blocked, nullptr, &no_wait) < 0 && errno == EINTR)
    {
    }
  }
  if (::pthread_sigmask(SIG_SETMASK, &previous, nullptr) != 0)
    return std::unexpected(io_error("failed to restore the signal mask after process pipe write", errno));
  if (result >= 0)
    return PipeIoResultV1{.bytes = static_cast<std::size_t>(result), .state = PipeIoStateV1::Progress};
  if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK)
    return PipeIoResultV1{.bytes = 0, .state = PipeIoStateV1::WouldBlock};
  return std::unexpected(io_error("failed to write a process pipe endpoint", saved_errno));
#endif
}

ava::core::Result<bool> PipeEndpoint::wait_readable(ProcessDeadline deadline) const
{
#if defined(_WIN32)
  static_cast<void>(deadline);
  return std::unexpected(unsupported_error());
#else
  if (!valid() || !implementation_->readable)
    return std::unexpected(invalid_error("process pipe endpoint is not readable"));
  return wait_descriptor(implementation_->descriptor.get(), POLLIN, deadline);
#endif
}

ava::core::Result<bool> PipeEndpoint::wait_writable(ProcessDeadline deadline) const
{
#if defined(_WIN32)
  static_cast<void>(deadline);
  return std::unexpected(unsupported_error());
#else
  if (!valid() || !implementation_->writable)
    return std::unexpected(invalid_error("process pipe endpoint is not writable"));
  return wait_descriptor(implementation_->descriptor.get(), POLLOUT, deadline);
#endif
}

Reservation::Reservation() noexcept = default;
Reservation::Reservation(std::shared_ptr<detail::SupervisorState> state, std::uint64_t record) noexcept : state_(std::move(state)), record_(record)
{
}
Reservation::Reservation(Reservation&& other) noexcept : state_(std::move(other.state_)), record_(std::exchange(other.record_, 0))
{
}
Reservation& Reservation::operator=(Reservation&& other) noexcept
{
  if (this != &other)
  {
    abandon();
    state_ = std::move(other.state_);
    record_ = std::exchange(other.record_, 0);
  }
  return *this;
}
Reservation::~Reservation()
{
  abandon();
}

bool Reservation::valid() const noexcept
{
  return state_ && record_ != 0;
}

void Reservation::abandon() noexcept
{
  abandon_reservation(state_, record_);
  record_ = 0;
  state_.reset();
}

ProcessHandle::ProcessHandle() noexcept = default;
ProcessHandle::ProcessHandle(std::shared_ptr<detail::HandleState> state) noexcept : state_(std::move(state))
{
}
ProcessHandle::ProcessHandle(ProcessHandle&&) noexcept = default;
ProcessHandle& ProcessHandle::operator=(ProcessHandle&&) noexcept = default;
ProcessHandle::~ProcessHandle() = default;

bool ProcessHandle::valid() const noexcept
{
  return state_ && state_->record != 0;
}

struct AdoptionGate::Impl
{
  std::shared_ptr<detail::SupervisorState> state;
  std::shared_ptr<detail::HandleState> handle;
  std::uint64_t record = 0;
  bool child_branch = false;
  bool registered = false;
#if !defined(_WIN32)
  Pipe leader_status;
  Pipe leader_control;
  std::optional<Pipe> sentinel_status;
  std::optional<Pipe> sentinel_control;
  pid_t leader = -1;
  pid_t sentinel = -1;
#endif

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

AdoptionGate::AdoptionGate() noexcept = default;
AdoptionGate::AdoptionGate(std::unique_ptr<Impl> implementation) noexcept : implementation_(std::move(implementation))
{
}
AdoptionGate::AdoptionGate(AdoptionGate&&) noexcept = default;
AdoptionGate& AdoptionGate::operator=(AdoptionGate&& other) noexcept
{
  if (this != &other)
  {
    abandon();
    implementation_ = std::move(other.implementation_);
  }
  return *this;
}
AdoptionGate::~AdoptionGate()
{
  abandon();
}

void AdoptionGate::abandon() noexcept
{
  if (!implementation_)
    return;
  auto& gate = *implementation_;
#if !defined(_WIN32)
  if (gate.child_branch)
  {
    implementation_.reset();
    return;
  }
  gate.leader_control.write_end.reset();
  if (gate.sentinel_control)
    gate.sentinel_control->write_end.reset();
  if (!gate.registered && (gate.leader > 1 || gate.sentinel > 1))
  {
    bool const cleaned = exact_provisional_cleanup(gate.leader, gate.sentinel, Clock::now() + 500ms);
    finish_unregistered(gate.state, gate.record, TerminationReasonV1::LaunchFailed, cleaned ? CleanupStateV1::Complete : CleanupStateV1::Incomplete);
    gate.record = 0;
    implementation_.reset();
    return;
  }
#endif
  if (!gate.registered)
    abandon_reservation(gate.state, gate.record);
  gate.record = 0;
  implementation_.reset();
}

bool AdoptionGate::valid() const noexcept
{
  return implementation_ && implementation_->state && implementation_->record != 0;
}

ava::core::Result<AdoptionForkBranchV1> AdoptionGate::fork_leader()
{
#if defined(_WIN32)
  return std::unexpected(unsupported_error());
#else
  if (!valid() || implementation_->leader > 1 || implementation_->child_branch)
    return std::unexpected(invalid_error("secure adoption gate cannot fork another leader"));
  auto& gate = *implementation_;
  {
    std::lock_guard lock(gate.state->mutex);
    auto found = gate.state->records.find(gate.record);
    if (found == gate.state->records.end() || found->second->state != ProcessStateV1::Launching || gate.state->shutting_down)
      return std::unexpected(process_error(ava::core::ErrorCategory::Io, "secure adoption was stopped before fork"));
  }
  pid_t const process = ::fork();
  if (process < 0)
    return std::unexpected(io_error("failed to fork the secure-adoption leader", errno));
  if (process == 0)
  {
    gate.child_branch = true;
    gate.leader = -1;
    gate.leader_status.read_end.reset();
    gate.leader_control.write_end.reset();
    int status = 0;
    if (!reset_child_signal_state() || ::setpgid(0, 0) != 0)
      status = errno == 0 ? EIO : errno;
    static_cast<void>(child_write_all(gate.leader_status.write_end.get(), &status, sizeof(status)));
    gate.leader_status.write_end.reset();
    if (status != 0)
      _exit(127);
    char release = '\0';
    if (child_read_retry(gate.leader_control.read_end.get(), &release, 1) != 1 || release != 'G')
      _exit(127);
    gate.leader_control.read_end.reset();
    return AdoptionForkBranchV1::Child;
  }
  gate.leader = process;
  gate.leader_status.write_end.reset();
  gate.leader_control.read_end.reset();
  return AdoptionForkBranchV1::Parent;
#endif
}

ava::core::VoidResult AdoptionGate::fork_sentinel()
{
#if defined(_WIN32)
  return std::unexpected(unsupported_error());
#else
  if (!valid() || implementation_->child_branch || implementation_->leader <= 1 || implementation_->sentinel > 1)
    return std::unexpected(invalid_error("secure adoption sentinel requires exactly one gated parent leader"));
  {
    std::lock_guard lock(implementation_->state->mutex);
    auto found = implementation_->state->records.find(implementation_->record);
    if (found == implementation_->state->records.end() || found->second->state != ProcessStateV1::Launching || implementation_->state->shutting_down)
      return std::unexpected(process_error(ava::core::ErrorCategory::Io, "secure-adoption sentinel was stopped before fork"));
  }
  auto status_pipe = make_cloexec_pipe();
  if (!status_pipe)
    return std::unexpected(std::move(status_pipe.error()));
  auto control_pipe = make_cloexec_pipe();
  if (!control_pipe)
    return std::unexpected(std::move(control_pipe.error()));
  implementation_->sentinel_status = std::move(*status_pipe);
  implementation_->sentinel_control = std::move(*control_pipe);

  auto& gate = *implementation_;
  pid_t const process = ::fork();
  if (process < 0)
    return std::unexpected(io_error("failed to fork the secure-adoption sentinel", errno));
  if (process == 0)
  {
    gate.child_branch = true;
    gate.leader_status.read_end.reset();
    gate.leader_status.write_end.reset();
    gate.leader_control.read_end.reset();
    gate.leader_control.write_end.reset();
    gate.sentinel_status->read_end.reset();
    gate.sentinel_control->write_end.reset();
    int status = 0;
    if (!reset_child_signal_state() || ::setpgid(0, gate.leader) != 0)
      status = errno == 0 ? EIO : errno;
    static_cast<void>(child_write_all(gate.sentinel_status->write_end.get(), &status, sizeof(status)));
    gate.sentinel_status->write_end.reset();
    if (status != 0)
      _exit(127);
    char release = '\0';
    if (child_read_retry(gate.sentinel_control->read_end.get(), &release, 1) != 1 || release != 'G')
      _exit(127);
    gate.sentinel_control->read_end.reset();
    while (true)
      static_cast<void>(::pause());
  }
  gate.sentinel = process;
  gate.sentinel_status->write_end.reset();
  gate.sentinel_control->read_end.reset();
  return {};
#endif
}

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

Supervisor::Supervisor() : implementation_(std::make_unique<Impl>())
{
}

Supervisor::~Supervisor() noexcept
{
  if (!implementation_)
    return;
  static_cast<void>(shutdown(Clock::now() + kDefaultCleanupBudget));
}

ava::core::Result<Reservation> Supervisor::reserve(OwnerPathV1 const& owner, ProcessRoleV1 role, LifecyclePolicyV1 policy)
{
  if (!owner.is_launch_owner())
    return std::unexpected(invalid_error("process reservation requires a generated operation owner"));
  if (!is_valid(role))
    return std::unexpected(invalid_error("process reservation has an unknown version-1 role"));
  if (policy.termination_grace < 0ms || policy.termination_grace > 5s)
    return std::unexpected(invalid_error("process termination grace is outside the supported bound"));

  try
  {
    auto key = owner.key();
    auto state = implementation_->state;
    std::lock_guard lock(state->mutex);
    if (!state->accepting || state->shutting_down)
      return std::unexpected(process_error(ava::core::ErrorCategory::Io, "process supervisor is not accepting reservations"));
    if (state->live_records >= kMaxLiveProcessRecordsV1)
      return std::unexpected(process_error(ava::core::ErrorCategory::Io, "process supervisor live-record capacity is exhausted"));

    auto alias = state->owner_aliases.find(key);
    if (alias == state->owner_aliases.end())
      alias = state->owner_aliases.emplace(key, detail::OwnerAliasEntry{.alias = state->next_owner_alias++, .references = 0}).first;
    ++alias->second.references;
    auto const identity = state->next_record++;
    auto record = std::make_unique<detail::Record>(identity, owner, key, alias->second.alias, role, policy);
    state->records.emplace(identity, std::move(record));
    ++state->live_records;
    state->changed.notify_all();
    return Reservation(state, identity);
  }
  catch (std::exception const& error)
  {
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "failed to allocate a process reservation").with_context("cause", error.what()));
  }
  catch (...)
  {
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "failed to allocate a process reservation"));
  }
}

void Supervisor::stop_accepting() noexcept
{
  if (!implementation_)
    return;
  auto const state = implementation_->state;
  {
    std::lock_guard lock(state->mutex);
    state->accepting = false;
  }
  state->changed.notify_all();
}

ava::core::Result<std::uint64_t> Supervisor::consume_reservation(Reservation& reservation)
{
  auto const expected = implementation_->state;
  if (!reservation.valid() || reservation.state_.get() != expected.get())
    return std::unexpected(invalid_error("process reservation does not belong to this supervisor"));
  auto const identity = reservation.record_;
  {
    std::lock_guard lock(expected->mutex);
    auto found = expected->records.find(identity);
    if (found == expected->records.end() || found->second->state != ProcessStateV1::Reserved)
      return std::unexpected(invalid_error("process reservation is no longer launchable"));
    found->second->state = ProcessStateV1::Launching;
    found->second->cleanup = CleanupStateV1::Pending;
  }
  reservation.record_ = 0;
  reservation.state_.reset();
  expected->changed.notify_all();
  return identity;
}

namespace {

#if !defined(_WIN32)

struct PreparedStream
{
  StreamModeV1 mode = StreamModeV1::Inherit;
  UniqueFd child_end;
  UniqueFd parent_end;
  bool parent_reads = false;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct PreparedSpawn
{
  std::string executable;
  std::vector<std::string> argv_storage;
  std::vector<char*> argv;
  std::vector<std::string> environment_storage;
  std::vector<char*> environment;
  UniqueFd cwd;
  PreparedStream standard_input;
  PreparedStream standard_output;
  PreparedStream standard_error;
  Pipe gate;
  Pipe exec_error;
  int maximum_descriptor = 4096;

  void build_pointers()
  {
    argv.clear();
    argv.reserve(argv_storage.size() + 1);
    for (auto& argument : argv_storage)
      argv.push_back(argument.data());
    argv.push_back(nullptr);
    environment.clear();
    environment.reserve(environment_storage.size() + 1);
    for (auto& entry : environment_storage)
      environment.push_back(entry.data());
    environment.push_back(nullptr);
  }

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

bool contains_nul(std::string const& value) noexcept
{
  return value.find('\0') != std::string::npos;
}

ava::core::Result<std::string> resolve_executable(std::string const& requested, std::vector<EnvironmentVariableV1> const& environment)
{
  if (requested.empty() || contains_nul(requested))
    return std::unexpected(invalid_error("process executable must be nonempty and NUL-free"));

  auto executable_usable = [](std::string const& candidate) {
    struct stat metadata{};
    return ::stat(candidate.c_str(), &metadata) == 0 && S_ISREG(metadata.st_mode) && ::access(candidate.c_str(), X_OK) == 0;
  };

  if (requested.find('/') != std::string::npos)
  {
    if (!requested.starts_with('/'))
      return std::unexpected(invalid_error("process executable paths must be absolute"));
    if (!executable_usable(requested))
      return std::unexpected(io_error("process executable is unavailable or not executable", errno == 0 ? EACCES : errno));
    return requested;
  }

  auto path = std::find_if(environment.begin(), environment.end(), [](auto const& entry) { return entry.name == "PATH"; });
  if (path == environment.end())
    return std::unexpected(invalid_error("bare process executables require an explicit exact PATH environment entry"));
  std::string_view remaining(path->value);
  while (true)
  {
    auto const separator = remaining.find(':');
    auto const directory = remaining.substr(0, separator);
    if (directory.empty() || !directory.starts_with('/'))
      return std::unexpected(invalid_error("process PATH entries must be nonempty absolute directories"));
    std::string candidate(directory);
    candidate.push_back('/');
    candidate += requested;
    if (executable_usable(candidate))
      return candidate;
    if (separator == std::string_view::npos)
      break;
    remaining.remove_prefix(separator + 1);
  }
  return std::unexpected(process_error(ava::core::ErrorCategory::NotFound, "process executable was not found in the exact PATH"));
}

ava::core::Result<PreparedStream> prepare_stream(StreamModeV1 mode, bool input)
{
  PreparedStream result;
  result.mode = mode;
  if (mode == StreamModeV1::Inherit)
    return result;
  if (mode == StreamModeV1::Discard)
  {
    int descriptor = ::open("/dev/null", (input ? O_RDONLY : O_WRONLY) | O_CLOEXEC | O_NONBLOCK);
    if (descriptor < 0)
      return std::unexpected(io_error("failed to prepare a discarded process stream", errno));
    auto moved = move_above_standard_descriptors(descriptor);
    if (!moved)
      return std::unexpected(std::move(moved.error()));
    result.child_end = UniqueFd(*moved);
    return result;
  }

  auto pipe = make_cloexec_pipe();
  if (!pipe)
    return std::unexpected(std::move(pipe.error()));
  if (input)
  {
    result.child_end = std::move(pipe->read_end);
    result.parent_end = std::move(pipe->write_end);
    result.parent_reads = false;
  }
  else
  {
    result.parent_end = std::move(pipe->read_end);
    result.child_end = std::move(pipe->write_end);
    result.parent_reads = true;
  }
  if (auto nonblocking = set_nonblocking(result.parent_end.get()); !nonblocking)
    return std::unexpected(std::move(nonblocking.error()));
  return result;
}

ava::core::Result<PreparedSpawn> prepare_spawn(SpawnSpecV1 specification)
{
  if (!is_valid(specification.stdin_mode) || !is_valid(specification.stdout_mode) || !is_valid(specification.stderr_mode))
    return std::unexpected(invalid_error("process specification has an unknown stream mode"));
  if (specification.argv.empty() || specification.argv.size() > kMaxArgumentCount)
    return std::unexpected(invalid_error("process argv count is outside the supported bound"));
  if (specification.environment.size() > kMaxEnvironmentCount)
    return std::unexpected(invalid_error("process environment count is outside the supported bound"));
  if (specification.cwd.empty() || !specification.cwd.starts_with('/') || contains_nul(specification.cwd))
    return std::unexpected(invalid_error("process cwd must be a NUL-free absolute path"));

  if (specification.executable.size() > kMaxPreparedBytes || specification.cwd.size() > kMaxPreparedBytes - specification.executable.size())
    return std::unexpected(invalid_error("process executable and cwd exceed the aggregate byte bound"));
  if (specification.argv.front().empty())
    return std::unexpected(invalid_error("process argv[0] must be nonempty"));
  std::size_t prepared_bytes = specification.executable.size() + specification.cwd.size();
  for (auto const& argument : specification.argv)
  {
    if (contains_nul(argument))
      return std::unexpected(invalid_error("process argv contains a NUL byte"));
    if (argument.size() > kMaxPreparedBytes - std::min(prepared_bytes, kMaxPreparedBytes))
      return std::unexpected(invalid_error("process argv exceeds the aggregate byte bound"));
    prepared_bytes += argument.size();
  }

  std::vector<std::string> names;
  names.reserve(specification.environment.size());
  for (auto const& variable : specification.environment)
  {
    if (variable.name.empty() || contains_nul(variable.name) || variable.name.find('=') != std::string::npos || contains_nul(variable.value))
      return std::unexpected(invalid_error("process environment contains an invalid exact entry"));
    if (std::find(names.begin(), names.end(), variable.name) != names.end())
      return std::unexpected(invalid_error("process environment contains a duplicate name"));
    names.push_back(variable.name);
    auto const bytes = variable.name.size() + variable.value.size() + 1;
    if (bytes > kMaxPreparedBytes - std::min(prepared_bytes, kMaxPreparedBytes))
      return std::unexpected(invalid_error("process environment exceeds the aggregate byte bound"));
    prepared_bytes += bytes;
  }

  auto executable = resolve_executable(specification.executable, specification.environment);
  if (!executable)
    return std::unexpected(std::move(executable.error()));

  int cwd_descriptor = ::open(specification.cwd.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK);
  if (cwd_descriptor < 0)
    return std::unexpected(io_error("failed to prepare the process cwd", errno));
  auto moved_cwd = move_above_standard_descriptors(cwd_descriptor);
  if (!moved_cwd)
    return std::unexpected(std::move(moved_cwd.error()));

  auto standard_input = prepare_stream(specification.stdin_mode, true);
  if (!standard_input)
    return std::unexpected(std::move(standard_input.error()));
  auto standard_output = prepare_stream(specification.stdout_mode, false);
  if (!standard_output)
    return std::unexpected(std::move(standard_output.error()));
  auto standard_error = prepare_stream(specification.stderr_mode, false);
  if (!standard_error)
    return std::unexpected(std::move(standard_error.error()));
  auto gate = make_cloexec_pipe();
  if (!gate)
    return std::unexpected(std::move(gate.error()));
  auto exec_error = make_cloexec_pipe();
  if (!exec_error)
    return std::unexpected(std::move(exec_error.error()));
  if (auto nonblocking = set_nonblocking(exec_error->read_end.get()); !nonblocking)
    return std::unexpected(std::move(nonblocking.error()));

  PreparedSpawn result;
  result.executable = std::move(*executable);
  result.argv_storage = std::move(specification.argv);
  result.environment_storage.reserve(specification.environment.size());
  for (auto& variable : specification.environment)
    result.environment_storage.push_back(std::move(variable.name) + "=" + std::move(variable.value));
  result.cwd = UniqueFd(*moved_cwd);
  result.standard_input = std::move(*standard_input);
  result.standard_output = std::move(*standard_output);
  result.standard_error = std::move(*standard_error);
  result.gate = std::move(*gate);
  result.exec_error = std::move(*exec_error);
  result.maximum_descriptor = descriptor_limit();
  result.build_pointers();
  return result;
}

ava::core::Result<PreparedSpawn> prepare_spawn_checked(SpawnSpecV1 specification)
{
  try
  {
    return prepare_spawn(std::move(specification));
  }
  catch (std::exception const& error)
  {
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "failed to prepare a bounded process launch").with_context("cause", error.what()));
  }
  catch (...)
  {
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "failed to prepare a bounded process launch"));
  }
}

enum class ChildFailureStage : std::uint8_t
{
  SignalReset,
  ProcessGroup,
  Gate,
  Streams,
  Cwd,
  Execute,
};

struct ChildFailure
{
  ChildFailureStage stage;
  int error_number;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[noreturn]] void child_fail(int descriptor, ChildFailureStage stage, int error_number) noexcept
{
  ChildFailure const failure{.stage = stage, .error_number = error_number};
  static_cast<void>(child_write_all(descriptor, &failure, sizeof(failure)));
  _exit(127);
}

void child_duplicate_stream(PreparedStream const& stream, int target, int exec_error) noexcept
{
  if (stream.mode != StreamModeV1::Inherit && ::dup2(stream.child_end.get(), target) < 0)
    child_fail(exec_error, ChildFailureStage::Streams, errno);
}

[[noreturn]] void spawn_child(PreparedSpawn const& prepared) noexcept
{
  int const error_descriptor = prepared.exec_error.write_end.get();
  static_cast<void>(::close(prepared.gate.write_end.get()));
  static_cast<void>(::close(prepared.exec_error.read_end.get()));
  if (prepared.standard_input.parent_end.get() >= 0)
    static_cast<void>(::close(prepared.standard_input.parent_end.get()));
  if (prepared.standard_output.parent_end.get() >= 0)
    static_cast<void>(::close(prepared.standard_output.parent_end.get()));
  if (prepared.standard_error.parent_end.get() >= 0)
    static_cast<void>(::close(prepared.standard_error.parent_end.get()));

  if (!reset_child_signal_state())
    child_fail(error_descriptor, ChildFailureStage::SignalReset, errno == 0 ? EIO : errno);
  if (::setpgid(0, 0) != 0)
    child_fail(error_descriptor, ChildFailureStage::ProcessGroup, errno);
  char release = '\0';
  if (child_read_retry(prepared.gate.read_end.get(), &release, 1) != 1 || release != 'G')
    child_fail(error_descriptor, ChildFailureStage::Gate, errno == 0 ? EIO : errno);

  child_duplicate_stream(prepared.standard_input, STDIN_FILENO, error_descriptor);
  child_duplicate_stream(prepared.standard_output, STDOUT_FILENO, error_descriptor);
  child_duplicate_stream(prepared.standard_error, STDERR_FILENO, error_descriptor);
  if (::fchdir(prepared.cwd.get()) != 0)
    child_fail(error_descriptor, ChildFailureStage::Cwd, errno);
  close_nonstandard_descriptors(error_descriptor, prepared.maximum_descriptor);
  ::execve(prepared.executable.c_str(), prepared.argv.data(), prepared.environment.data());
  child_fail(error_descriptor, ChildFailureStage::Execute, errno);
}

ava::core::Result<std::optional<ChildFailure>> await_exec_result(int descriptor, ProcessDeadline deadline)
{
  std::array<std::byte, sizeof(ChildFailure)> bytes{};
  std::size_t count = 0;
  while (true)
  {
    auto ready = wait_descriptor(descriptor, POLLIN, deadline);
    if (!ready)
      return std::unexpected(std::move(ready.error()));
    if (!*ready)
      return std::unexpected(process_error(ava::core::ErrorCategory::Io, "process exec handshake timed out"));
    auto const result = ::read(descriptor, bytes.data() + count, bytes.size() - count);
    if (result > 0)
    {
      count += static_cast<std::size_t>(result);
      if (count == bytes.size())
      {
        ChildFailure failure{};
        std::memcpy(&failure, bytes.data(), sizeof(failure));
        return failure;
      }
      continue;
    }
    if (result == 0)
    {
      if (count == 0)
        return std::optional<ChildFailure>{};
      return std::unexpected(process_error(ava::core::ErrorCategory::Io, "process exec handshake was truncated"));
    }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
      continue;
    return std::unexpected(io_error("failed to read the process exec handshake", errno));
  }
}

ava::core::Error child_failure_error(ChildFailure const& failure)
{
  std::string operation;
  switch (failure.stage)
  {
    case ChildFailureStage::SignalReset:
      operation = "reset child signals";
      break;
    case ChildFailureStage::ProcessGroup:
      operation = "establish child process group";
      break;
    case ChildFailureStage::Gate:
      operation = "wait for registry gate";
      break;
    case ChildFailureStage::Streams:
      operation = "apply child stream actions";
      break;
    case ChildFailureStage::Cwd:
      operation = "enter child cwd";
      break;
    case ChildFailureStage::Execute:
      operation = "execute child image";
      break;
  }
  auto error = io_error("process failed in its closed pre-exec sequence", failure.error_number);
  error.with_context("operation", std::move(operation));
  return error;
}

#endif

}  // namespace

ava::core::Result<SpawnResultV1> Supervisor::spawn(Reservation&& reservation, SpawnSpecV1 specification)
{
  auto state = implementation_->state;
  auto consumed = consume_reservation(reservation);
  if (!consumed)
    return std::unexpected(std::move(consumed.error()));
  auto const identity = *consumed;
#if defined(_WIN32)
  static_cast<void>(specification);
  finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
  return std::unexpected(unsupported_error());
#else
  auto prepared = prepare_spawn_checked(std::move(specification));
  if (!prepared)
  {
    finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(std::move(prepared.error()));
  }

  std::shared_ptr<detail::HandleState> handle_state;
  std::unique_ptr<PipeEndpoint::Impl> input_endpoint;
  std::unique_ptr<PipeEndpoint::Impl> output_endpoint;
  std::unique_ptr<PipeEndpoint::Impl> error_endpoint;
  try
  {
    handle_state = std::make_shared<detail::HandleState>();
    if (prepared->standard_input.mode == StreamModeV1::Capture)
      input_endpoint = std::make_unique<PipeEndpoint::Impl>(-1, false, true);
    if (prepared->standard_output.mode == StreamModeV1::Capture)
      output_endpoint = std::make_unique<PipeEndpoint::Impl>(-1, true, false);
    if (prepared->standard_error.mode == StreamModeV1::Capture)
      error_endpoint = std::make_unique<PipeEndpoint::Impl>(-1, true, false);
  }
  catch (std::exception const& error)
  {
    finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "failed to allocate process launch capabilities").with_context("cause", error.what()));
  }
  catch (...)
  {
    finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "failed to allocate process launch capabilities"));
  }

  bool launchable = false;
  {
    std::lock_guard lock(state->mutex);
    auto found = state->records.find(identity);
    launchable = found != state->records.end() && found->second->state == ProcessStateV1::Launching && !found->second->reason && !state->shutting_down;
  }
  if (!launchable)
  {
    finish_unregistered(state, identity, TerminationReasonV1::ApplicationShutdown);
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "process launch was stopped before fork"));
  }

  pid_t const parent_group = ::getpgrp();
  pid_t const child = ::fork();
  if (child < 0)
  {
    auto error = io_error("failed to fork a reserved process", errno);
    finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(std::move(error));
  }
  if (child == 0)
    spawn_child(*prepared);

  prepared->gate.read_end.reset();
  prepared->exec_error.write_end.reset();
  prepared->standard_input.child_end.reset();
  prepared->standard_output.child_end.reset();
  prepared->standard_error.child_end.reset();
  prepared->cwd.reset();

  bool group_set = false;
  while (true)
  {
    if (::setpgid(child, child) == 0)
    {
      group_set = true;
      break;
    }
    if (errno != EINTR)
      break;
  }
  int const set_group_error = errno;
  pid_t const observed_group = ::getpgid(child);
  int const get_group_error = errno;
  if (!group_set || child <= 1 || parent_group <= 0 || observed_group != child || observed_group == parent_group)
  {
    prepared->gate.write_end.reset();
    bool const cleaned = exact_provisional_cleanup(child, -1, Clock::now() + 500ms);
    finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed, cleaned ? CleanupStateV1::Complete : CleanupStateV1::Incomplete);
    return std::unexpected(io_error("failed to verify a private process group before exec", group_set ? get_group_error : set_group_error));
  }

  if (auto monitor = ensure_monitor_started(state); !monitor)
  {
    prepared->gate.write_end.reset();
    bool const cleaned = exact_provisional_cleanup(child, -1, Clock::now() + 500ms);
    finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed, cleaned ? CleanupStateV1::Complete : CleanupStateV1::Incomplete);
    return std::unexpected(std::move(monitor.error()));
  }

  bool reservation_ended = false;
  {
    std::lock_guard lock(state->mutex);
    auto found = state->records.find(identity);
    if (found == state->records.end() || found->second->state == ProcessStateV1::Finished)
    {
      reservation_ended = true;
    }
    else
    {
      auto& record = *found->second;
      handle_state->supervisor = state;
      handle_state->record = identity;
      record.handle = handle_state;
      record.registered = true;
      record.group_verified = true;
      record.leader = child;
      record.cleanup = CleanupStateV1::Pending;
      record.state = record.reason ? ProcessStateV1::StopRequested : ProcessStateV1::Launching;
    }
  }
  if (reservation_ended)
  {
    prepared->gate.write_end.reset();
    static_cast<void>(exact_provisional_cleanup(child, -1, Clock::now() + 500ms));
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "process reservation ended during launch"));
  }
  state->changed.notify_all();

  char const release = 'G';
  if (!write_without_sigpipe(prepared->gate.write_end.get(), &release, 1))
  {
    prepared->gate.write_end.reset();
    {
      std::lock_guard lock(state->mutex);
      auto found = state->records.find(identity);
      if (found != state->records.end())
      {
        static_cast<void>(commit_reason_locked(*found->second, TerminationReasonV1::LaunchFailed));
        found->second->stop_deadline = Clock::now() + kDefaultCleanupBudget;
      }
    }
    state->changed.notify_all();
    await_internal_settlement(handle_state, Clock::now() + kDefaultCleanupBudget);
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "failed to release the registered process exec gate"));
  }
  prepared->gate.write_end.reset();

  auto exec_result = await_exec_result(prepared->exec_error.read_end.get(), Clock::now() + kLaunchHandshakeTimeout);
  if (!exec_result || *exec_result)
  {
    {
      std::lock_guard lock(state->mutex);
      auto found = state->records.find(identity);
      if (found != state->records.end())
      {
        static_cast<void>(
            commit_reason_locked(*found->second, exec_result && *exec_result ? TerminationReasonV1::ExecFailed : TerminationReasonV1::LaunchFailed));
        found->second->stop_deadline = Clock::now() + kDefaultCleanupBudget;
        found->second->state = ProcessStateV1::StopRequested;
      }
    }
    state->changed.notify_all();
    await_internal_settlement(handle_state, Clock::now() + kDefaultCleanupBudget);
    if (!exec_result)
      return std::unexpected(std::move(exec_result.error()));
    return std::unexpected(child_failure_error(**exec_result));
  }

  {
    std::lock_guard lock(state->mutex);
    auto found = state->records.find(identity);
    if (found != state->records.end() && found->second->state == ProcessStateV1::Launching)
      found->second->state = ProcessStateV1::Running;
  }
  state->changed.notify_all();

  SpawnResultV1 result;
  result.handle = ProcessHandle(std::move(handle_state));
  if (input_endpoint)
  {
    input_endpoint->descriptor.reset(prepared->standard_input.parent_end.release());
    result.standard_input.emplace(PipeEndpoint(std::move(input_endpoint)));
  }
  if (output_endpoint)
  {
    output_endpoint->descriptor.reset(prepared->standard_output.parent_end.release());
    result.standard_output.emplace(PipeEndpoint(std::move(output_endpoint)));
  }
  if (error_endpoint)
  {
    error_endpoint->descriptor.reset(prepared->standard_error.parent_end.release());
    result.standard_error.emplace(PipeEndpoint(std::move(error_endpoint)));
  }
  return result;
#endif
}

ava::core::Result<AdoptionGate> Supervisor::begin_secure_adoption(Reservation&& reservation)
{
  auto state = implementation_->state;
#if defined(_WIN32)
  auto consumed = consume_reservation(reservation);
  if (consumed)
    finish_unregistered(state, *consumed, TerminationReasonV1::LaunchFailed);
  return std::unexpected(unsupported_error());
#else
  auto leader_status = make_cloexec_pipe();
  if (!leader_status)
    return std::unexpected(std::move(leader_status.error()));
  auto leader_control = make_cloexec_pipe();
  if (!leader_control)
    return std::unexpected(std::move(leader_control.error()));
  auto consumed = consume_reservation(reservation);
  if (!consumed)
    return std::unexpected(std::move(consumed.error()));
  std::unique_ptr<AdoptionGate::Impl> implementation;
  try
  {
    implementation = std::make_unique<AdoptionGate::Impl>();
    implementation->handle = std::make_shared<detail::HandleState>();
  }
  catch (std::exception const& error)
  {
    finish_unregistered(state, *consumed, TerminationReasonV1::LaunchFailed);
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "failed to allocate secure-adoption capabilities").with_context("cause", error.what()));
  }
  catch (...)
  {
    finish_unregistered(state, *consumed, TerminationReasonV1::LaunchFailed);
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "failed to allocate secure-adoption capabilities"));
  }
  implementation->state = state;
  implementation->record = *consumed;
  implementation->leader_status = std::move(*leader_status);
  implementation->leader_control = std::move(*leader_control);
  return AdoptionGate(std::move(implementation));
#endif
}

namespace {

#if !defined(_WIN32)
ava::core::Result<int> read_gate_status(int descriptor, std::string_view member)
{
  auto ready = wait_descriptor(descriptor, POLLIN, Clock::now() + kLaunchHandshakeTimeout);
  if (!ready)
    return std::unexpected(std::move(ready.error()));
  if (!*ready)
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "secure-adoption child gate timed out").with_context("member", std::string(member)));
  int status = EIO;
  std::size_t offset = 0;
  while (offset < sizeof(status))
  {
    auto const result = ::read(descriptor, reinterpret_cast<unsigned char*>(&status) + offset, sizeof(status) - offset);
    if (result > 0)
    {
      offset += static_cast<std::size_t>(result);
      continue;
    }
    if (result < 0 && errno == EINTR)
      continue;
    return std::unexpected(
        process_error(ava::core::ErrorCategory::Io, "secure-adoption child gate closed before acknowledgement").with_context("member", std::string(member)));
  }
  return status;
}
#endif

}  // namespace

ava::core::Result<ProcessHandle> Supervisor::adopt(AdoptionGate&& gate)
{
#if defined(_WIN32)
  static_cast<void>(gate);
  return std::unexpected(unsupported_error());
#else
  auto state = implementation_->state;
  if (!gate.valid() || gate.implementation_->state.get() != state.get() || gate.implementation_->child_branch || gate.implementation_->leader <= 1)
    return std::unexpected(invalid_error("secure adoption gate does not contain this supervisor's exact gated leader"));
  auto& ticket = *gate.implementation_;
  auto leader_status = read_gate_status(ticket.leader_status.read_end.get(), "leader");
  if (!leader_status || *leader_status != 0)
    return std::unexpected(leader_status ? io_error("secure-adoption leader setup failed", *leader_status) : std::move(leader_status.error()));

  bool leader_group_set = false;
  while (true)
  {
    if (::setpgid(ticket.leader, ticket.leader) == 0)
    {
      leader_group_set = true;
      break;
    }
    if (errno != EINTR)
      break;
  }
  int const leader_group_error = errno;
  pid_t const parent_group = ::getpgrp();
  pid_t const observed_leader_group = ::getpgid(ticket.leader);
  if (!leader_group_set || observed_leader_group != ticket.leader || parent_group <= 0 || observed_leader_group == parent_group)
    return std::unexpected(io_error("failed to prove the secure-adoption leader process group", leader_group_error));

  if (ticket.sentinel > 1)
  {
    auto sentinel_status = read_gate_status(ticket.sentinel_status->read_end.get(), "sentinel");
    if (!sentinel_status || *sentinel_status != 0)
      return std::unexpected(sentinel_status ? io_error("secure-adoption sentinel setup failed", *sentinel_status) : std::move(sentinel_status.error()));
    bool sentinel_group_set = false;
    while (true)
    {
      if (::setpgid(ticket.sentinel, ticket.leader) == 0)
      {
        sentinel_group_set = true;
        break;
      }
      if (errno != EINTR)
        break;
    }
    int const sentinel_group_error = errno;
    if (!sentinel_group_set || ::getpgid(ticket.sentinel) != ticket.leader)
      return std::unexpected(io_error("failed to prove the exact secure-adoption sentinel membership", sentinel_group_error));
  }

  if (auto monitor = ensure_monitor_started(state); !monitor)
    return std::unexpected(std::move(monitor.error()));

  auto handle_state = ticket.handle;
  {
    std::lock_guard lock(state->mutex);
    auto found = state->records.find(ticket.record);
    if (found == state->records.end() || found->second->state == ProcessStateV1::Finished)
      return std::unexpected(process_error(ava::core::ErrorCategory::Io, "secure-adoption reservation ended before registry commit"));
    auto& record = *found->second;
    handle_state->supervisor = state;
    handle_state->record = ticket.record;
    record.handle = handle_state;
    record.registered = true;
    record.group_verified = true;
    record.leader = ticket.leader;
    record.sentinel = ticket.sentinel;
    record.cleanup = CleanupStateV1::Pending;
    record.state = record.reason ? ProcessStateV1::StopRequested : ProcessStateV1::Launching;
    ticket.registered = true;
  }
  state->changed.notify_all();

  char const release = 'G';
  bool released = true;
  if (ticket.sentinel_control)
    released = write_without_sigpipe(ticket.sentinel_control->write_end.get(), &release, 1);
  released = write_without_sigpipe(ticket.leader_control.write_end.get(), &release, 1) && released;
  ticket.sentinel_control.reset();
  ticket.leader_control.write_end.reset();
  if (!released)
  {
    {
      std::lock_guard lock(state->mutex);
      auto found = state->records.find(ticket.record);
      if (found != state->records.end())
      {
        static_cast<void>(commit_reason_locked(*found->second, TerminationReasonV1::LaunchFailed));
        found->second->stop_deadline = Clock::now() + kDefaultCleanupBudget;
      }
    }
    state->changed.notify_all();
    await_internal_settlement(handle_state, Clock::now() + kDefaultCleanupBudget);
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "failed to release a committed secure-adoption gate"));
  }

  {
    std::lock_guard lock(state->mutex);
    auto found = state->records.find(ticket.record);
    if (found != state->records.end() && found->second->state == ProcessStateV1::Launching)
      found->second->state = ProcessStateV1::Running;
  }
  state->changed.notify_all();
  ticket.record = 0;
  return ProcessHandle(std::move(handle_state));
#endif
}

ava::core::Result<StopResultV1> Supervisor::request_stop(ProcessHandle const& handle, TerminationReasonV1 reason, ProcessDeadline deadline)
{
  if (!requestable_reason(reason))
    return std::unexpected(invalid_error("process stop request has a non-requestable termination reason"));
  auto state = implementation_->state;
  if (!handle.valid() || handle.state_->supervisor.lock().get() != state.get())
    return std::unexpected(invalid_error("process handle does not belong to this supervisor"));

  StopResultV1 result;
  {
    std::lock_guard lock(state->mutex);
    auto found = state->records.find(handle.state_->record);
    if (found == state->records.end())
      return result;
    ++result.matched;
    auto& record = *found->second;
    if (record.state != ProcessStateV1::Finished)
    {
      result.newly_requested += commit_reason_locked(record, reason) ? 1U : 0U;
      if (!record.stop_deadline || deadline < *record.stop_deadline)
        record.stop_deadline = deadline;
      if (record.registered && record.state != ProcessStateV1::Reaping)
        record.state = ProcessStateV1::StopRequested;
      else if (!record.registered)
        finalize_locked(*state, record, CleanupStateV1::NotRequired);
    }
  }
  state->changed.notify_all();
  return result;
}

ava::core::Result<StopResultV1> Supervisor::request_stop(OwnerPathV1 const& owner_prefix, TerminationReasonV1 reason, ProcessDeadline deadline)
{
  if (!owner_prefix.is_valid_prefix())
    return std::unexpected(invalid_error("process owner stop requires a valid generated owner prefix"));
  if (!requestable_reason(reason))
    return std::unexpected(invalid_error("process owner stop has a non-requestable termination reason"));
  auto state = implementation_->state;
  StopResultV1 result;
  {
    std::lock_guard lock(state->mutex);
    for (auto& [identity, record_pointer] : state->records)
    {
      static_cast<void>(identity);
      auto& record = *record_pointer;
      if (!record.owner.matches_prefix(owner_prefix))
        continue;
      ++result.matched;
      if (record.state == ProcessStateV1::Finished)
        continue;
      result.newly_requested += commit_reason_locked(record, reason) ? 1U : 0U;
      if (!record.stop_deadline || deadline < *record.stop_deadline)
        record.stop_deadline = deadline;
      if ((record.registered || record.state == ProcessStateV1::Launching) && record.state != ProcessStateV1::Reaping)
        record.state = ProcessStateV1::StopRequested;
      else if (!record.registered && record.state != ProcessStateV1::Reaping)
        finalize_locked(*state, record, CleanupStateV1::NotRequired);
    }
  }
  state->changed.notify_all();
  return result;
}

ava::core::Result<ExitStatusV1> Supervisor::wait(ProcessHandle const& handle, ProcessDeadline deadline) const
{
  auto state = implementation_->state;
  if (!handle.valid() || handle.state_->supervisor.lock().get() != state.get())
    return std::unexpected(invalid_error("process handle does not belong to this supervisor"));
  std::unique_lock lock(handle.state_->mutex);
  if (!handle.state_->changed.wait_until(lock, deadline, [&] { return handle.state_->final_status.has_value(); }))
    return std::unexpected(process_error(ava::core::ErrorCategory::Io, "process wait deadline expired"));
  return *handle.state_->final_status;
}

ava::core::VoidResult Supervisor::account_output(ProcessHandle const& handle, StreamKindV1 stream, std::uint64_t bytes, bool truncated)
{
  auto state = implementation_->state;
  if (!handle.valid() || handle.state_->supervisor.lock().get() != state.get())
    return std::unexpected(invalid_error("process handle does not belong to this supervisor"));
  if (stream != StreamKindV1::StandardOutput && stream != StreamKindV1::StandardError)
    return std::unexpected(invalid_error("process output accounting accepts only stdout or stderr"));
  std::lock_guard lock(state->mutex);
  auto found = state->records.find(handle.state_->record);
  if (found == state->records.end())
    return {};
  if (stream == StreamKindV1::StandardOutput)
  {
    found->second->stdout_bytes = saturating_add(found->second->stdout_bytes, bytes);
    found->second->stdout_truncated = found->second->stdout_truncated || truncated;
  }
  else
  {
    found->second->stderr_bytes = saturating_add(found->second->stderr_bytes, bytes);
    found->second->stderr_truncated = found->second->stderr_truncated || truncated;
  }
  return {};
}

ProcessSnapshotV1 Supervisor::snapshot() const
{
  ProcessSnapshotV1 result;
  if (!implementation_)
    return result;
  auto state = implementation_->state;
  std::lock_guard lock(state->mutex);
  result.accepting = state->accepting;
  result.monitor_started = state->monitor_started;
  result.live_records = state->live_records;
  result.retained_terminal_records = state->terminal_fifo.size();
  result.records.reserve(state->records.size());
  auto const now = Clock::now();
  for (auto const& [identity, record_pointer] : state->records)
  {
    static_cast<void>(identity);
    auto const& record = *record_pointer;
    auto const end = record.finished.value_or(now);
    result.records.push_back(ProcessSnapshotRecordV1{.record_alias = record.id,
                                                     .owner_alias = record.owner_alias,
                                                     .role = record.role,
                                                     .state = record.state,
                                                     .reason = record.reason,
                                                     .cleanup = record.cleanup,
                                                     .exit_kind = record.exit_kind,
                                                     .monotonic_milliseconds = elapsed_milliseconds(record.created, end),
                                                     .stdout_bytes = record.stdout_bytes,
                                                     .stderr_bytes = record.stderr_bytes,
                                                     .settlement_count = record.settlement_count,
                                                     .exit_code = record.exit_code,
                                                     .signal_number = record.signal_number,
                                                     .group_verified = record.group_verified,
                                                     .stdout_truncated = record.stdout_truncated,
                                                     .stderr_truncated = record.stderr_truncated,
                                                     .has_exit_code = record.has_exit_code,
                                                     .has_signal_number = record.has_signal_number});
  }
  return result;
}

ShutdownResultV1 Supervisor::shutdown(ProcessDeadline deadline) noexcept
{
  if (!implementation_)
    return {};
  std::lock_guard shutdown_lock(implementation_->shutdown_mutex);
  if (implementation_->shutdown_called)
    return implementation_->shutdown_result;
  implementation_->shutdown_called = true;
  auto state = implementation_->state;
  std::size_t settled_before = 0;
  {
    std::lock_guard lock(state->mutex);
    state->accepting = false;
    state->shutting_down = true;
    settled_before = state->settled_records;
    for (auto& [identity, record_pointer] : state->records)
    {
      static_cast<void>(identity);
      auto& record = *record_pointer;
      if (record.state == ProcessStateV1::Finished)
        continue;
      record.included_in_shutdown = true;
      static_cast<void>(commit_reason_locked(record, TerminationReasonV1::ApplicationShutdown));
      if (!record.stop_deadline || deadline < *record.stop_deadline)
        record.stop_deadline = deadline;
#if !defined(_WIN32)
      if (record.registered)
      {
        begin_stop_locked(record, Clock::now());
        continue;
      }
#endif
      if (record.state == ProcessStateV1::Launching && record.state != ProcessStateV1::Reaping)
        record.state = ProcessStateV1::StopRequested;
      else if (!record.registered && record.state != ProcessStateV1::Reaping)
        finalize_locked(*state, record, CleanupStateV1::NotRequired);
    }
  }
  state->changed.notify_all();

  {
    std::unique_lock lock(state->mutex);
    static_cast<void>(state->changed.wait_until(lock, deadline, [&] { return state->live_records == 0; }));
    if (state->live_records != 0)
    {
      // The common absolute budget is exhausted. Escalate every remaining
      // verified group in one nonblocking sweep, then make exact nonblocking
      // observations without adding a scheduling grace period.
#if !defined(_WIN32)
      for (auto& [identity, record_pointer] : state->records)
      {
        static_cast<void>(identity);
        auto& record = *record_pointer;
        if (record.state != ProcessStateV1::Finished && record.registered && !record.kill_sent)
        {
          static_cast<void>(signal_verified_group(record, SIGKILL));
          record.kill_sent = true;
        }
      }
#endif
      for (auto& [identity, record_pointer] : state->records)
      {
        static_cast<void>(identity);
        auto& record = *record_pointer;
#if !defined(_WIN32)
        if (record.state != ProcessStateV1::Finished && record.registered)
        {
          static_cast<void>(observe_member(record.leader, record.leader_observed, record, true));
          if (record.sentinel > 1)
            static_cast<void>(observe_member(record.sentinel, record.sentinel_observed, record, false));
          bool const direct_children_observed = record.leader_observed && (record.sentinel <= 1 || record.sentinel_observed);
          if (record.quiet_group_observations >= 2 && direct_children_observed)
          {
            static_cast<void>(reap_member(record.leader, true, record.leader_reaped, record));
            if (record.sentinel > 1)
              static_cast<void>(reap_member(record.sentinel, true, record.sentinel_reaped, record));
          }
          bool const reaped = record.leader_reaped && (record.sentinel <= 1 || record.sentinel_reaped);
          finalize_locked(*state, record, reaped && !record.cleanup_failed ? CleanupStateV1::Complete : CleanupStateV1::Incomplete);
          continue;
        }
#endif
        if (record.state != ProcessStateV1::Finished)
          finalize_locked(*state, record, CleanupStateV1::Incomplete);
      }
    }
    std::size_t incomplete = 0;
    for (auto const& [identity, record] : state->records)
    {
      static_cast<void>(identity);
      if (record->included_in_shutdown && (record->state != ProcessStateV1::Finished || record->cleanup == CleanupStateV1::Incomplete))
        ++incomplete;
    }
    implementation_->shutdown_result = ShutdownResultV1{.complete = incomplete == 0,
                                                        .incomplete_count = std::min(incomplete, kMaxLiveProcessRecordsV1),
                                                        .settled_count = state->settled_records - settled_before};
    state->stop_monitor = true;
  }
  state->changed.notify_all();
#if !defined(_WIN32)
  join_monitor(state);
#endif
  return implementation_->shutdown_result;
}

}  // namespace ava::process
