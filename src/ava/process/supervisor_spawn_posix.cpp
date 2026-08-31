#include "sys.h"
#include "ava/process/launch_protocol_posix.h"
#include "ava/process/supervisor_internal.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

namespace ava::process::detail {

#if !defined(_WIN32)
namespace {

using namespace std::chrono_literals;

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

}  // namespace

void UniqueFd::reset(int descriptor) noexcept
{
  if (descriptor_ >= 0)
    static_cast<void>(::close(descriptor_));
  descriptor_ = descriptor;
}

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
  std::array<int, 1> const preserved_descriptors{preserved};
  close_nonstandard_descriptors_except(preserved_descriptors, maximum);
}

void close_nonstandard_descriptors_except(std::span<int const> preserved, int maximum) noexcept
{
  constexpr std::size_t kMaximumPreservedDescriptors = kMaxRetainedScriptDescriptorsV1 + 2;
  std::array<int, kMaximumPreservedDescriptors> ordered{};
  std::size_t count = 0;
  for (int descriptor : preserved)
  {
    if (descriptor <= STDERR_FILENO || count == ordered.size())
      continue;
    bool duplicate = false;
    for (std::size_t index = 0; index < count; ++index)
      duplicate = duplicate || ordered[index] == descriptor;
    if (duplicate)
      continue;
    std::size_t position = count;
    while (position > 0 && ordered[position - 1] > descriptor)
    {
      ordered[position] = ordered[position - 1];
      --position;
    }
    ordered[position] = descriptor;
    ++count;
  }

#if defined(__linux__) && defined(SYS_close_range)
  bool ranges_closed = true;
  unsigned int first = static_cast<unsigned int>(STDERR_FILENO + 1);
  for (std::size_t index = 0; index < count; ++index)
  {
    auto const kept = static_cast<unsigned int>(ordered[index]);
    if (first < kept && ::syscall(SYS_close_range, first, kept - 1, 0U) != 0)
      ranges_closed = false;
    if (kept < std::numeric_limits<unsigned int>::max())
      first = kept + 1;
  }
  if (::syscall(SYS_close_range, first, std::numeric_limits<unsigned int>::max(), 0U) != 0)
    ranges_closed = false;
  if (ranges_closed)
    return;
#endif
  for (int descriptor = STDERR_FILENO + 1; descriptor < maximum; ++descriptor)
  {
    bool keep = false;
    for (std::size_t index = 0; index < count; ++index)
      keep = keep || ordered[index] == descriptor;
    if (!keep)
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

}  // namespace ava::process::detail

namespace ava::process {
namespace {

using detail::Clock;
using detail::Pipe;
using detail::UniqueFd;
using namespace std::chrono_literals;

#if !defined(_WIN32)

constexpr std::size_t kMaxArgumentCount = 256;
constexpr std::size_t kMaxPreparedBytes = 1024 * 1024;

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
  Pipe launch_status;
  int maximum_descriptor = 4096;
  bool fail_working_directory_for_test = false;

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
    return std::unexpected(detail::invalid_error("process executable must be nonempty and NUL-free"));

  auto executable_usable = [](std::string const& candidate) {
    struct stat metadata{};
    return ::stat(candidate.c_str(), &metadata) == 0 && S_ISREG(metadata.st_mode) && ::access(candidate.c_str(), X_OK) == 0;
  };

  if (requested.find('/') != std::string::npos)
  {
    if (!requested.starts_with('/'))
      return std::unexpected(detail::invalid_error("process executable paths must be absolute"));
    if (!executable_usable(requested))
      return std::unexpected(detail::io_error("process executable is unavailable or not executable", errno == 0 ? EACCES : errno));
    return requested;
  }

  auto path = std::find_if(environment.begin(), environment.end(), [](auto const& entry) { return entry.name == "PATH"; });
  if (path == environment.end())
    return std::unexpected(detail::invalid_error("bare process executables require an explicit exact PATH environment entry"));
  std::string_view remaining(path->value);
  while (true)
  {
    auto const separator = remaining.find(':');
    auto const directory = remaining.substr(0, separator);
    if (directory.empty() || !directory.starts_with('/'))
      return std::unexpected(detail::invalid_error("process PATH entries must be nonempty absolute directories"));
    std::string candidate(directory);
    candidate.push_back('/');
    candidate += requested;
    if (executable_usable(candidate))
      return candidate;
    if (separator == std::string_view::npos)
      break;
    remaining.remove_prefix(separator + 1);
  }
  return std::unexpected(detail::process_error(ava::core::ErrorCategory::NotFound, "process executable was not found in the exact PATH"));
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
      return std::unexpected(detail::io_error("failed to prepare a discarded process stream", errno));
    auto moved = detail::move_above_standard_descriptors(descriptor);
    if (!moved)
      return std::unexpected(std::move(moved.error()));
    result.child_end = UniqueFd(*moved);
    return result;
  }

  auto pipe = detail::make_cloexec_pipe();
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
  if (auto nonblocking = detail::set_nonblocking(result.parent_end.get()); !nonblocking)
    return std::unexpected(std::move(nonblocking.error()));
  return result;
}

ava::core::Result<PreparedSpawn> prepare_spawn(SpawnSpecV1& specification)
{
  if (!is_valid(specification.stdin_mode) || !is_valid(specification.stdout_mode) || !is_valid(specification.stderr_mode))
    return std::unexpected(detail::invalid_error("process specification has an unknown stream mode"));
  if (specification.argv.empty() || specification.argv.size() > kMaxArgumentCount)
    return std::unexpected(detail::invalid_error("process argv count is outside the supported bound"));
  if (!detail::EnvironmentAccess::revalidate(specification.environment))
    return std::unexpected(detail::invalid_error("process specification requires one valid exact-environment capability"));
  auto const& exact_environment = detail::EnvironmentAccess::variables(specification.environment);
  if (specification.cwd.empty() || !specification.cwd.starts_with('/') || contains_nul(specification.cwd))
    return std::unexpected(detail::invalid_error("process cwd must be a NUL-free absolute path"));

  if (specification.executable.size() > kMaxPreparedBytes || specification.cwd.size() > kMaxPreparedBytes - specification.executable.size())
    return std::unexpected(detail::invalid_error("process executable and cwd exceed the aggregate byte bound"));
  if (specification.argv.front().empty())
    return std::unexpected(detail::invalid_error("process argv[0] must be nonempty"));
  std::size_t prepared_bytes = specification.executable.size() + specification.cwd.size();
  for (auto const& argument : specification.argv)
  {
    if (contains_nul(argument))
      return std::unexpected(detail::invalid_error("process argv contains a NUL byte"));
    if (argument.size() > kMaxPreparedBytes - std::min(prepared_bytes, kMaxPreparedBytes))
      return std::unexpected(detail::invalid_error("process argv exceeds the aggregate byte bound"));
    prepared_bytes += argument.size();
  }

  std::vector<std::string> names;
  names.reserve(exact_environment.size());
  for (auto const& variable : exact_environment)
  {
    if (variable.name.empty() || contains_nul(variable.name) || variable.name.find('=') != std::string::npos || contains_nul(variable.value))
      return std::unexpected(detail::invalid_error("process environment contains an invalid exact entry"));
    if (std::find(names.begin(), names.end(), variable.name) != names.end())
      return std::unexpected(detail::invalid_error("process environment contains a duplicate name"));
    names.push_back(variable.name);
    auto const bytes = variable.name.size() + variable.value.size() + 2;
    if (bytes > kMaxPreparedBytes - std::min(prepared_bytes, kMaxPreparedBytes))
      return std::unexpected(detail::invalid_error("process environment exceeds the aggregate byte bound"));
    prepared_bytes += bytes;
  }

  auto executable = resolve_executable(specification.executable, exact_environment);
  if (!executable)
    return std::unexpected(std::move(executable.error()));

  int cwd_descriptor = ::open(specification.cwd.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK);
  if (cwd_descriptor < 0)
    return std::unexpected(detail::io_error("failed to prepare the process cwd", errno));
  auto moved_cwd = detail::move_above_standard_descriptors(cwd_descriptor);
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
  auto gate = detail::make_cloexec_pipe();
  if (!gate)
    return std::unexpected(std::move(gate.error()));
  auto launch_status = detail::make_cloexec_pipe();
  if (!launch_status)
    return std::unexpected(std::move(launch_status.error()));
  if (auto nonblocking = detail::set_nonblocking(launch_status->read_end.get()); !nonblocking)
    return std::unexpected(std::move(nonblocking.error()));

  PreparedSpawn result;
  result.executable = std::move(*executable);
  result.argv_storage = std::move(specification.argv);
  result.environment_storage.reserve(exact_environment.size());
  for (auto const& variable : exact_environment)
    result.environment_storage.push_back(variable.name + "=" + variable.value);
  result.cwd = UniqueFd(*moved_cwd);
  result.standard_input = std::move(*standard_input);
  result.standard_output = std::move(*standard_output);
  result.standard_error = std::move(*standard_error);
  result.gate = std::move(*gate);
  result.launch_status = std::move(*launch_status);
  result.maximum_descriptor = detail::descriptor_limit();
  result.build_pointers();
  return result;
}

ava::core::Result<PreparedSpawn> prepare_spawn_checked(SpawnSpecV1& specification)
{
  try
  {
    return prepare_spawn(specification);
  }
  catch (std::exception const& error)
  {
    return std::unexpected(
        detail::process_error(ava::core::ErrorCategory::Io, "failed to prepare a bounded process launch").with_context("cause", error.what()));
  }
  catch (...)
  {
    return std::unexpected(detail::process_error(ava::core::ErrorCategory::Io, "failed to prepare a bounded process launch"));
  }
}

[[noreturn]] void child_fail(int descriptor, detail::LaunchFailureStageV1 stage, int error_number) noexcept
{
  static_cast<void>(detail::child_write_launch_failed(descriptor, stage, error_number));
  _exit(127);
}

void child_duplicate_stream(PreparedStream const& stream, int target, int launch_status) noexcept
{
  if (stream.mode != StreamModeV1::Inherit && ::dup2(stream.child_end.get(), target) < 0)
    child_fail(launch_status, detail::LaunchFailureStageV1::Streams, errno);
}

[[noreturn]] void spawn_child(PreparedSpawn const& prepared) noexcept
{
  int const status_descriptor = prepared.launch_status.write_end.get();
  static_cast<void>(::close(prepared.gate.write_end.get()));
  static_cast<void>(::close(prepared.launch_status.read_end.get()));
  if (prepared.standard_input.parent_end.get() >= 0)
    static_cast<void>(::close(prepared.standard_input.parent_end.get()));
  if (prepared.standard_output.parent_end.get() >= 0)
    static_cast<void>(::close(prepared.standard_output.parent_end.get()));
  if (prepared.standard_error.parent_end.get() >= 0)
    static_cast<void>(::close(prepared.standard_error.parent_end.get()));

  if (!detail::reset_child_signal_state())
    child_fail(status_descriptor, detail::LaunchFailureStageV1::SignalReset, errno == 0 ? EIO : errno);
  if (::setpgid(0, 0) != 0)
    child_fail(status_descriptor, detail::LaunchFailureStageV1::ProcessGroup, errno);
  char release = '\0';
  auto const release_count = detail::child_read_retry(prepared.gate.read_end.get(), &release, 1);
  if (release_count != 1 || release != 'G')
    child_fail(status_descriptor, detail::LaunchFailureStageV1::Gate, release_count < 0 && errno > 0 ? errno : EIO);

  child_duplicate_stream(prepared.standard_input, STDIN_FILENO, status_descriptor);
  child_duplicate_stream(prepared.standard_output, STDOUT_FILENO, status_descriptor);
  child_duplicate_stream(prepared.standard_error, STDERR_FILENO, status_descriptor);
  if (prepared.fail_working_directory_for_test)
    static_cast<void>(::close(prepared.cwd.get()));
  if (::fchdir(prepared.cwd.get()) != 0)
    child_fail(status_descriptor, detail::LaunchFailureStageV1::WorkingDirectory, errno);
  detail::close_nonstandard_descriptors(status_descriptor, prepared.maximum_descriptor);
  if (!detail::child_write_exec_attempt(status_descriptor))
    _exit(127);
  ::execve(prepared.executable.c_str(), prepared.argv.data(), prepared.environment.data());
  int const execute_error = errno == 0 ? EIO : errno;
  static_cast<void>(detail::child_write_exec_failed(status_descriptor, execute_error));
  _exit(127);
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
  auto const role = detail::record_role(state, identity);
  if (!role || !detail::EnvironmentAccess::matches_common_launch(specification.environment, *role, specification.cwd))
  {
    detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(detail::invalid_error("reserved common process launch requires its matching exact-environment capability"));
  }
  auto const startup_deadline = detail::startup_deadline_for_record(state, identity);
#if defined(_WIN32)
  static_cast<void>(specification);
  detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
  return std::unexpected(detail::unsupported_error());
#else
  auto prepared = prepare_spawn_checked(specification);
  if (!prepared)
  {
    detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
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
    detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(
        detail::process_error(ava::core::ErrorCategory::Io, "failed to allocate process launch capabilities").with_context("cause", error.what()));
  }
  catch (...)
  {
    detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(detail::process_error(ava::core::ErrorCategory::Io, "failed to allocate process launch capabilities"));
  }

  bool launchable = false;
  TerminationReasonV1 stopped_reason = TerminationReasonV1::LaunchFailed;
  {
    std::lock_guard lock(state->mutex);
    auto found = state->records.find(identity);
    launchable = found != state->records.end() && found->second->state == ProcessStateV1::Launching && !found->second->reason && !state->shutting_down &&
                 Clock::now() < startup_deadline;
    if (found != state->records.end() && found->second->reason)
      stopped_reason = *found->second->reason;
    else if (state->shutting_down)
      stopped_reason = TerminationReasonV1::ApplicationShutdown;
  }
  if (!launchable)
  {
    detail::finish_unregistered(state, identity, stopped_reason);
    return std::unexpected(detail::canceled_launch_error("process launch", stopped_reason));
  }
  {
    std::lock_guard lock(state->mutex);
    prepared->fail_working_directory_for_test = state->fail_next_common_child_working_directory_for_test;
    state->fail_next_common_child_working_directory_for_test = false;
  }

  // Repeat the immutable role/profile/logical-cwd capability check at the
  // final pre-fork boundary after every allocator-backed launch preparation.
  if (!detail::EnvironmentAccess::matches_common_launch(specification.environment, *role, specification.cwd))
  {
    detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(detail::invalid_error("prepared common process launch lost its exact-environment binding"));
  }

  pid_t const parent_group = ::getpgrp();
  pid_t const child = ::fork();
  if (child < 0)
  {
    auto error = detail::io_error("failed to fork a reserved process", errno);
    detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed);
    return std::unexpected(std::move(error));
  }
  if (child == 0)
    spawn_child(*prepared);

  prepared->gate.read_end.reset();
  prepared->launch_status.write_end.reset();
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
    bool const cleaned = detail::exact_provisional_cleanup(child, -1, Clock::now() + 500ms);
    detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed, cleaned ? CleanupStateV1::Complete : CleanupStateV1::Incomplete);
    return std::unexpected(detail::io_error("failed to verify a private process group before exec", group_set ? get_group_error : set_group_error));
  }

  if (auto monitor = detail::ensure_monitor_started(state); !monitor)
  {
    prepared->gate.write_end.reset();
    bool const cleaned = detail::exact_provisional_cleanup(child, -1, Clock::now() + 500ms);
    detail::finish_unregistered(state, identity, TerminationReasonV1::LaunchFailed, cleaned ? CleanupStateV1::Complete : CleanupStateV1::Incomplete);
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
    static_cast<void>(detail::exact_provisional_cleanup(child, -1, Clock::now() + 500ms));
    return std::unexpected(detail::process_error(ava::core::ErrorCategory::Io, "process reservation ended during launch"));
  }
  state->changed.notify_all();

  if (auto hook = detail::invoke_after_fork_before_release_hook(state); !hook)
  {
    prepared->gate.write_end.reset();
    auto const cleanup_deadline = detail::fail_registered_launch(state, identity, TerminationReasonV1::LaunchFailed);
    state->changed.notify_all();
    detail::await_internal_settlement(handle_state, cleanup_deadline);
    return std::unexpected(std::move(hook.error()));
  }

  detail::GateReleaseDecision release_decision;
  {
    std::lock_guard lock(state->mutex);
    release_decision = detail::commit_gate_release_locked(*state, identity, startup_deadline);
  }
  state->changed.notify_all();
  if (!release_decision.committed)
  {
    prepared->gate.write_end.reset();
    detail::await_internal_settlement(handle_state, release_decision.cleanup_deadline);
    return std::unexpected(detail::canceled_launch_error("process launch", release_decision.reason));
  }

  char const release = 'G';
  if (!detail::write_without_sigpipe(prepared->gate.write_end.get(), &release, 1))
  {
    prepared->gate.write_end.reset();
    auto const cleanup_deadline = detail::fail_registered_launch(state, identity, TerminationReasonV1::LaunchFailed);
    state->changed.notify_all();
    detail::await_internal_settlement(handle_state, cleanup_deadline);
    return std::unexpected(detail::process_error(ava::core::ErrorCategory::Io, "failed to release the registered process exec gate"));
  }
  prepared->gate.write_end.reset();

  auto const confirmation = detail::await_launch_exec_confirmation(prepared->launch_status.read_end.get(), startup_deadline, false);
  if (confirmation.disposition != detail::LaunchProtocolDispositionV1::ExecConfirmed)
  {
    auto const fallback_reason =
        confirmation.disposition == detail::LaunchProtocolDispositionV1::ExecFailed ? TerminationReasonV1::ExecFailed : TerminationReasonV1::LaunchFailed;
    auto const cleanup_deadline = detail::fail_registered_launch(state, identity, fallback_reason);
    state->changed.notify_all();
    detail::await_internal_settlement(handle_state, cleanup_deadline);
    return std::unexpected(detail::launch_protocol_error(confirmation, "process"));
  }

  std::optional<TerminationReasonV1> startup_stop_reason;
  {
    std::lock_guard lock(state->mutex);
    auto found = state->records.find(identity);
    if (found == state->records.end())
      startup_stop_reason = TerminationReasonV1::LaunchFailed;
    else if (found->second->state == ProcessStateV1::Finished || found->second->reason)
      startup_stop_reason = found->second->reason.value_or(TerminationReasonV1::LaunchFailed);
    else
      found->second->startup_handshake_complete = true;
  }
  state->changed.notify_all();
  if (startup_stop_reason)
  {
    auto const cleanup_deadline = detail::fail_registered_launch(state, identity, TerminationReasonV1::LaunchFailed);
    detail::await_internal_settlement(handle_state, cleanup_deadline);
    return std::unexpected(detail::startup_stopped_error("process", *startup_stop_reason));
  }

  SpawnResultV1 result;
  result.handle = ProcessHandle(std::move(handle_state));
  if (input_endpoint)
  {
    input_endpoint->state->descriptor.reset(prepared->standard_input.parent_end.release());
    result.standard_input.emplace(PipeEndpoint(std::move(input_endpoint)));
  }
  if (output_endpoint)
  {
    output_endpoint->state->descriptor.reset(prepared->standard_output.parent_end.release());
    result.standard_output.emplace(PipeEndpoint(std::move(output_endpoint)));
  }
  if (error_endpoint)
  {
    error_endpoint->state->descriptor.reset(prepared->standard_error.parent_end.release());
    result.standard_error.emplace(PipeEndpoint(std::move(error_endpoint)));
  }
  return result;
#endif
}

}  // namespace ava::process
