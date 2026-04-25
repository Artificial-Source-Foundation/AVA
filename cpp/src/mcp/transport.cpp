#include "ava/mcp/transport.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
extern char** environ;
#endif

namespace ava::mcp {

namespace {

constexpr std::size_t kMaxReceiveBufferBytes = 1024U * 1024U;

constexpr std::array<const char*, 9> kSafeBaselineEnvVars = {
    "PATH",
    "HOME",
    "USER",
    "LOGNAME",
    "SHELL",
    "LANG",
    "TERM",
    "TMPDIR",
    "PWD",
};

[[nodiscard]] std::string trim_ascii(std::string_view text) {
  std::size_t begin = 0;
  while(begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }

  std::size_t end = text.size();
  while(end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

[[nodiscard]] std::string errno_message(const std::string& context) {
  return context + ": " + std::strerror(errno);
}

[[nodiscard]] std::string receive_timeout_message(std::chrono::milliseconds timeout) {
  return "MCP stdio transport receive timed out after " + std::to_string(timeout.count()) + "ms";
}

[[nodiscard]] std::string send_timeout_message(std::chrono::milliseconds timeout) {
  return "MCP stdio transport send timed out after " + std::to_string(timeout.count()) + "ms";
}

[[nodiscard]] std::chrono::milliseconds remaining_timeout_until(
    std::chrono::steady_clock::time_point deadline
) {
  const auto now = std::chrono::steady_clock::now();
  if(now >= deadline) {
    return std::chrono::milliseconds{0};
  }

  auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
  if(remaining.count() <= 0) {
    remaining = std::chrono::milliseconds{1};
  }
  return remaining;
}

[[nodiscard]] bool is_safe_baseline_env_var(std::string_view key) {
  if(key.rfind("LC_", 0) == 0) {
    return true;
  }
  return std::any_of(kSafeBaselineEnvVars.begin(), kSafeBaselineEnvVars.end(), [&](const char* allowed) {
    return key == allowed;
  });
}

#if !defined(_WIN32)

[[nodiscard]] std::vector<std::string> snapshot_inherited_env_keys() {
  std::vector<std::string> keys;
  for(char** entry = ::environ; entry != nullptr && *entry != nullptr; ++entry) {
    const std::string_view env_entry(*entry);
    const auto delimiter = env_entry.find('=');
    if(delimiter == std::string_view::npos || delimiter == 0) {
      continue;
    }
    keys.emplace_back(env_entry.substr(0, delimiter));
  }
  return keys;
}

[[nodiscard]] std::map<std::string, std::string> build_allowlisted_child_env(
    const std::vector<std::string>& inherited_env_keys,
    const std::map<std::string, std::string>& explicit_env
) {
  std::map<std::string, std::string> child_env;
  for(const auto& key : inherited_env_keys) {
    if(!is_safe_baseline_env_var(key)) {
      continue;
    }
    if(const char* value = ::getenv(key.c_str()); value != nullptr) {
      child_env.insert_or_assign(key, value);
    }
  }

  for(const auto& [key, value] : explicit_env) {
    child_env.insert_or_assign(key, value);
  }

  return child_env;
}

[[nodiscard]] bool apply_child_env(
    const std::vector<std::string>& inherited_env_keys,
    const std::map<std::string, std::string>& child_env
) {
  for(const auto& key : inherited_env_keys) {
    if(::unsetenv(key.c_str()) != 0 && errno != EINVAL) {
      return false;
    }
  }

  for(const auto& [key, value] : child_env) {
    if(::setenv(key.c_str(), value.c_str(), 1) != 0) {
      return false;
    }
  }

  return true;
}

void set_nonblocking_fd(int fd, const char* context) {
  const auto flags = ::fcntl(fd, F_GETFL, 0);
  if(flags < 0) {
    throw std::runtime_error(errno_message(std::string("MCP stdio transport failed to read fd flags for ") + context));
  }
  if(::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
    throw std::runtime_error(errno_message(std::string("MCP stdio transport failed to enable non-blocking mode for ") + context));
  }
}

class ScopedSigpipeBlock final {
 public:
  ScopedSigpipeBlock() {
    ::sigemptyset(&sigpipe_set_);
    ::sigaddset(&sigpipe_set_, SIGPIPE);

    const auto mask_result = ::pthread_sigmask(SIG_BLOCK, &sigpipe_set_, &previous_mask_);
    if(mask_result != 0) {
      throw std::runtime_error(
          "MCP stdio transport failed to block SIGPIPE: " + std::string(std::strerror(mask_result))
      );
    }
    active_ = true;

    sigset_t pending{};
    if(::sigpending(&pending) == 0) {
      had_pending_sigpipe_ = ::sigismember(&pending, SIGPIPE) == 1;
    }
  }

  ~ScopedSigpipeBlock() {
    if(active_) {
      ::pthread_sigmask(SIG_SETMASK, &previous_mask_, nullptr);
    }
  }

  void consume_generated_sigpipe_if_needed() const {
    if(!active_ || had_pending_sigpipe_) {
      return;
    }

    timespec timeout{};
    constexpr int kMaxDrainAttempts = 8;
    for(int attempt = 0; attempt < kMaxDrainAttempts; ++attempt) {
      const auto consumed = ::sigtimedwait(&sigpipe_set_, nullptr, &timeout);
      if(consumed == SIGPIPE) {
        return;
      }
      if(consumed < 0 && errno == EINTR) {
        continue;
      }
      if(consumed < 0 && errno == EAGAIN) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      return;
    }
  }

 private:
  sigset_t sigpipe_set_{};
  sigset_t previous_mask_{};
  bool active_{false};
  bool had_pending_sigpipe_{false};
};

struct SpawnedStdioProcess {
  int stdin_fd{-1};
  int stdout_fd{-1};
  pid_t child_pid{-1};
};

void close_fd_if_open(int& fd) {
  if(fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

void close_inherited_fds_for_child() {
  int max_fd = 1024;
  const long configured_max_fd = ::sysconf(_SC_OPEN_MAX);
  if(configured_max_fd > 0 && configured_max_fd <= static_cast<long>(std::numeric_limits<int>::max())) {
    max_fd = static_cast<int>(configured_max_fd);
  }
  for(int fd = STDERR_FILENO + 1; fd < max_fd; ++fd) {
    ::close(fd);
  }
}

[[nodiscard]] SpawnedStdioProcess spawn_stdio_process(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::map<std::string, std::string>& env
) {
  const auto inherited_env_keys = snapshot_inherited_env_keys();
  const auto child_env = build_allowlisted_child_env(inherited_env_keys, env);

  std::array<int, 2> stdin_pipe{-1, -1};
  std::array<int, 2> stdout_pipe{-1, -1};

  if(::pipe(stdin_pipe.data()) != 0) {
    throw std::runtime_error(errno_message("MCP stdio transport failed to create stdin pipe"));
  }
  if(::pipe(stdout_pipe.data()) != 0) {
    close_fd_if_open(stdin_pipe[0]);
    close_fd_if_open(stdin_pipe[1]);
    throw std::runtime_error(errno_message("MCP stdio transport failed to create stdout pipe"));
  }

  const pid_t child_pid = ::fork();
  if(child_pid < 0) {
    close_fd_if_open(stdin_pipe[0]);
    close_fd_if_open(stdin_pipe[1]);
    close_fd_if_open(stdout_pipe[0]);
    close_fd_if_open(stdout_pipe[1]);
    throw std::runtime_error(errno_message("MCP stdio transport failed to fork"));
  }

  if(child_pid == 0) {
    if(::setsid() < 0) {
      _exit(127);
    }

    ::dup2(stdin_pipe[0], STDIN_FILENO);
    ::dup2(stdout_pipe[1], STDOUT_FILENO);

    const int dev_null = ::open("/dev/null", O_WRONLY);
    if(dev_null >= 0) {
      ::dup2(dev_null, STDERR_FILENO);
      ::close(dev_null);
    }

    ::close(stdin_pipe[0]);
    ::close(stdin_pipe[1]);
    ::close(stdout_pipe[0]);
    ::close(stdout_pipe[1]);

    close_inherited_fds_for_child();

    if(!apply_child_env(inherited_env_keys, child_env)) {
      _exit(127);
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>(command.c_str()));
    for(const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    ::execvp(command.c_str(), argv.data());
    _exit(127);
  }

  ::close(stdin_pipe[0]);
  ::close(stdout_pipe[1]);
  return SpawnedStdioProcess{
      .stdin_fd = stdin_pipe[1],
      .stdout_fd = stdout_pipe[0],
      .child_pid = child_pid,
  };
}

[[nodiscard]] bool wait_for_writable(int fd, std::chrono::milliseconds timeout) {
  if(timeout.count() <= 0) {
    return false;
  }

  const auto timeout_ms = static_cast<int>(std::min<std::int64_t>(
      timeout.count(),
      static_cast<std::int64_t>(std::numeric_limits<int>::max())
  ));

  pollfd descriptor{};
  descriptor.fd = fd;
  descriptor.events = POLLOUT;

  while(true) {
    descriptor.revents = 0;
    const auto ready = ::poll(&descriptor, 1, timeout_ms);
    if(ready > 0) {
      if((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
        throw std::runtime_error("MCP stdio transport send pipe error");
      }
      if((descriptor.revents & POLLHUP) != 0) {
        throw std::runtime_error("MCP stdio transport stdin closed");
      }
      return (descriptor.revents & POLLOUT) != 0;
    }
    if(ready == 0) {
      return false;
    }
    if(errno == EINTR) {
      continue;
    }
    throw std::runtime_error(errno_message("MCP stdio transport poll failed while waiting to send"));
  }
}

void write_all_fd(int fd, std::string_view payload, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::size_t offset = 0;
  while(offset < payload.size()) {
    if(!wait_for_writable(fd, remaining_timeout_until(deadline))) {
      throw std::runtime_error(send_timeout_message(timeout));
    }

    const auto* data = payload.data() + offset;
    const auto remaining = payload.size() - offset;
    const ScopedSigpipeBlock sigpipe_block;
    const auto written = ::write(fd, data, remaining);
    if(written < 0) {
      if(errno == EINTR) {
        continue;
      }
      if(errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      if(errno == EPIPE) {
        sigpipe_block.consume_generated_sigpipe_if_needed();
        throw std::runtime_error("MCP stdio transport stdin closed");
      }
      throw std::runtime_error(errno_message("MCP stdio transport failed to write"));
    }
    if(written == 0) {
      throw std::runtime_error("MCP stdio transport wrote zero bytes");
    }
    offset += static_cast<std::size_t>(written);
  }
}

[[nodiscard]] bool wait_for_readable(int fd, std::chrono::milliseconds timeout) {
  if(timeout.count() <= 0) {
    return false;
  }
  const auto timeout_ms = static_cast<int>(std::min<std::int64_t>(
      timeout.count(),
      static_cast<std::int64_t>(std::numeric_limits<int>::max())
  ));

  pollfd descriptor{};
  descriptor.fd = fd;
  descriptor.events = POLLIN;

  while(true) {
    descriptor.revents = 0;
    const auto ready = ::poll(&descriptor, 1, timeout_ms);
    if(ready > 0) {
      if((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
        throw std::runtime_error("MCP stdio transport receive pipe error");
      }
      if((descriptor.revents & POLLHUP) != 0 && (descriptor.revents & POLLIN) == 0) {
        throw std::runtime_error("MCP stdio transport stdout closed");
      }
      return true;
    }
    if(ready == 0) {
      return false;
    }
    if(errno == EINTR) {
      continue;
    }
    throw std::runtime_error(errno_message("MCP stdio transport poll failed"));
  }
}

void signal_child_process_group(pid_t child_pid, int signal_number) {
  if(child_pid <= 0) {
    return;
  }

  if(::kill(-child_pid, signal_number) != 0) {
    if(errno != ESRCH) {
      // Best effort only.
    }
  }
}

void terminate_child_process(pid_t& child_pid) {
  if(child_pid <= 0) {
    child_pid = -1;
    return;
  }

  int status = 0;
  pid_t waited = ::waitpid(child_pid, &status, WNOHANG);
  if(waited == 0) {
    signal_child_process_group(child_pid, SIGTERM);
    constexpr int kMaxWaitLoops = 50;
    for(int attempt = 0; attempt < kMaxWaitLoops; ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      waited = ::waitpid(child_pid, &status, WNOHANG);
      if(waited == child_pid) {
        break;
      }
      if(waited < 0 && errno == ECHILD) {
        break;
      }
    }
  }

  if(waited == 0) {
    signal_child_process_group(child_pid, SIGKILL);
    ::waitpid(child_pid, &status, 0);
  }

  if(waited < 0 && errno != ECHILD) {
    // Best effort only.
  }
  child_pid = -1;
}

#endif

}  // namespace

class StdioTransport::Impl {
 public:
  explicit Impl(std::chrono::milliseconds timeout)
      : receive_timeout(timeout) {}

  std::chrono::milliseconds receive_timeout;
  std::string receive_buffer;
  int stdin_fd{-1};
  int stdout_fd{-1};
#if !defined(_WIN32)
  pid_t child_pid{-1};
#endif
  std::atomic<bool> closed{false};
};

StdioTransport::StdioTransport(
    std::string command,
    std::vector<std::string> args,
    std::map<std::string, std::string> env,
    std::chrono::milliseconds receive_timeout
)
    : impl_(std::make_unique<Impl>(receive_timeout)) {
  if(command.empty()) {
    throw std::runtime_error("MCP stdio transport command must not be empty");
  }
  if(receive_timeout.count() <= 0) {
    throw std::runtime_error("MCP stdio transport receive timeout must be positive");
  }

#if defined(_WIN32)
  (void)command;
  (void)args;
  (void)env;
  throw std::runtime_error("MCP stdio transport is not implemented on Windows");
#else
  try {
    auto process = spawn_stdio_process(command, args, env);
    impl_->stdin_fd = process.stdin_fd;
    impl_->stdout_fd = process.stdout_fd;
    impl_->child_pid = process.child_pid;
    set_nonblocking_fd(impl_->stdin_fd, "stdin pipe");
    set_nonblocking_fd(impl_->stdout_fd, "stdout pipe");
  } catch(...) {
    close_fd_if_open(impl_->stdin_fd);
    close_fd_if_open(impl_->stdout_fd);
    terminate_child_process(impl_->child_pid);
    throw;
  }
#endif
}

StdioTransport::~StdioTransport() {
  try {
    close();
  } catch(...) {
    // Destructor is best-effort and must not throw.
  }
}

void StdioTransport::send(const JsonRpcMessage& message) {
  if(!impl_ || impl_->closed.load(std::memory_order_acquire)) {
    throw std::runtime_error("MCP stdio transport is closed");
  }

#if defined(_WIN32)
  (void)message;
  throw std::runtime_error("MCP stdio transport is not implemented on Windows");
#else
  std::string payload = encode_message(message).dump();
  payload.push_back('\n');
  write_all_fd(impl_->stdin_fd, payload, impl_->receive_timeout);
#endif
}

JsonRpcMessage StdioTransport::receive() {
  if(!impl_ || impl_->closed.load(std::memory_order_acquire)) {
    throw std::runtime_error("MCP stdio transport is closed");
  }

#if defined(_WIN32)
  throw std::runtime_error("MCP stdio transport is not implemented on Windows");
#else
  const auto deadline = std::chrono::steady_clock::now() + impl_->receive_timeout;

  while(true) {
    if(std::chrono::steady_clock::now() >= deadline) {
      throw std::runtime_error(receive_timeout_message(impl_->receive_timeout));
    }

    const auto newline = impl_->receive_buffer.find('\n');
    if(newline != std::string::npos) {
      const auto line = trim_ascii(std::string_view(impl_->receive_buffer).substr(0, newline));
      impl_->receive_buffer.erase(0, newline + 1);
      if(line.empty()) {
        continue;
      }

      nlohmann::json json_message;
      try {
        json_message = nlohmann::json::parse(line);
      } catch(const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("MCP stdio transport received invalid JSON: ") + e.what());
      }
      return decode_message(json_message);
    }

    if(!wait_for_readable(impl_->stdout_fd, remaining_timeout_until(deadline))) {
      throw std::runtime_error(receive_timeout_message(impl_->receive_timeout));
    }

    std::array<char, 4096> buffer{};
    const auto bytes_read = ::read(impl_->stdout_fd, buffer.data(), buffer.size());
    if(bytes_read < 0) {
      if(errno == EINTR) {
        continue;
      }
      if(errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      throw std::runtime_error(errno_message("MCP stdio transport failed to read"));
    }
    if(bytes_read == 0) {
      throw std::runtime_error("MCP stdio transport reached EOF while waiting for message");
    }
    impl_->receive_buffer.append(buffer.data(), static_cast<std::size_t>(bytes_read));
    if(impl_->receive_buffer.size() > kMaxReceiveBufferBytes) {
      throw std::runtime_error(
          "MCP stdio transport receive buffer exceeded " + std::to_string(kMaxReceiveBufferBytes) +
          " bytes without newline delimiter"
      );
    }
  }
#endif
}

void StdioTransport::close() {
  if(!impl_) {
    return;
  }

  if(impl_->closed.exchange(true, std::memory_order_acq_rel)) {
    return;
  }

#if !defined(_WIN32)
  close_fd_if_open(impl_->stdin_fd);
  close_fd_if_open(impl_->stdout_fd);
  terminate_child_process(impl_->child_pid);
#endif
}

void InMemoryTransport::send(const JsonRpcMessage& message) {
  if(closed_) {
    throw std::runtime_error("MCP in-memory transport is closed");
  }
  outbound_.push_back(message);
}

JsonRpcMessage InMemoryTransport::receive() {
  if(closed_) {
    throw std::runtime_error("MCP in-memory transport is closed");
  }
  if(inbound_.empty()) {
    throw std::runtime_error("MCP in-memory transport has no inbound message");
  }
  auto message = std::move(inbound_.front());
  inbound_.pop_front();
  return message;
}

void InMemoryTransport::close() {
  closed_ = true;
}

void InMemoryTransport::push_inbound(JsonRpcMessage message) {
  inbound_.push_back(std::move(message));
}

JsonRpcMessage InMemoryTransport::pop_outbound() {
  if(outbound_.empty()) {
    throw std::runtime_error("MCP in-memory transport has no outbound message");
  }
  auto message = std::move(outbound_.front());
  outbound_.pop_front();
  return message;
}

bool InMemoryTransport::has_outbound() const {
  return !outbound_.empty();
}

bool InMemoryTransport::closed() const {
  return closed_;
}

}  // namespace ava::mcp
