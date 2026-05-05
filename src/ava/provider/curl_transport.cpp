#include "ava/provider/curl_transport.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <thread>
#include <vector>

#include "ava/provider/curl_transport_protocol.h"

namespace ava::provider {
namespace {

constexpr char kTrustedExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
constexpr std::size_t kMaxCurlResponseBytes = 8 * 1024 * 1024;
constexpr std::size_t kMaxCurlStderrBytes = 64 * 1024;

class UniqueFd {
 public:
  explicit UniqueFd(int fd = -1) : fd_(fd) {}
  UniqueFd(UniqueFd const&) = delete;
  UniqueFd& operator=(UniqueFd const&) = delete;
  UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
  UniqueFd& operator=(UniqueFd&& other) noexcept
  {
    if (this != &other) reset(other.release());
    return *this;
  }
  ~UniqueFd() { reset(); }

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] int release() noexcept
  {
    int const fd = fd_;
    fd_ = -1;
    return fd;
  }
  void reset(int fd = -1) noexcept
  {
    if (fd_ >= 0) close(fd_);
    fd_ = fd;
  }

 private:
  int fd_ = -1;
};

class TempBodyFile {
 public:
  TempBodyFile() = default;
  TempBodyFile(TempBodyFile const&) = delete;
  TempBodyFile& operator=(TempBodyFile const&) = delete;
  TempBodyFile(TempBodyFile&& other) noexcept : path_(std::move(other.path_)) { other.path_.clear(); }
  TempBodyFile& operator=(TempBodyFile&& other) noexcept
  {
    if (this != &other) {
      cleanup();
      path_ = std::move(other.path_);
      other.path_.clear();
    }
    return *this;
  }
  ~TempBodyFile() { cleanup(); }

  [[nodiscard]] std::string const& path() const noexcept { return path_; }

  [[nodiscard]] static ava::core::Result<TempBodyFile> create(std::string_view body)
  {
    std::error_code temp_error;
    auto temp_dir = std::filesystem::temp_directory_path(temp_error);
    if (temp_error) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to resolve temporary directory");
      error.with_context("cause", temp_error.message());
      return std::unexpected(std::move(error));
    }
    std::string tmpl = (temp_dir / "ava-request-body-XXXXXX").string();
    int const fd = mkstemp(tmpl.data());
    if (fd < 0) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to create temporary request body file");
      error.with_context("cause", std::strerror(errno));
      return std::unexpected(std::move(error));
    }
    UniqueFd file(fd);
    if (fchmod(file.get(), S_IRUSR | S_IWUSR) != 0) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to secure temporary request body file");
      error.with_context("path", tmpl);
      error.with_context("cause", std::strerror(errno));
      unlink(tmpl.c_str());
      return std::unexpected(std::move(error));
    }

    std::size_t written = 0;
    while (written < body.size()) {
      auto const count = write(file.get(), body.data() + written, body.size() - written);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) {
        auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to write temporary request body file");
        error.with_context("path", tmpl);
        error.with_context("cause", count < 0 ? std::strerror(errno) : "short write");
        unlink(tmpl.c_str());
        return std::unexpected(std::move(error));
      }
      written += static_cast<std::size_t>(count);
    }

    TempBodyFile result;
    result.path_ = std::move(tmpl);
    return result;
  }

 private:
  void cleanup() noexcept
  {
    if (!path_.empty()) {
      unlink(path_.c_str());
      path_.clear();
    }
  }

  std::string path_;
};

class ScopedSignalIgnore {
 public:
  explicit ScopedSignalIgnore(int signal) : signal_(signal)
  {
    struct sigaction action {};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    active_ = sigaction(signal_, &action, &previous_) == 0;
  }
  ScopedSignalIgnore(ScopedSignalIgnore const&) = delete;
  ScopedSignalIgnore& operator=(ScopedSignalIgnore const&) = delete;
  ~ScopedSignalIgnore()
  {
    if (active_) sigaction(signal_, &previous_, nullptr);
  }

 private:
  int signal_ = 0;
  bool active_ = false;
  struct sigaction previous_ {};
};

ava::core::Result<std::array<int, 2>> make_pipe()
{
  std::array<int, 2> pipe_fds{-1, -1};
  if (pipe(pipe_fds.data()) != 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to create curl pipe");
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }
  return pipe_fds;
}

void close_nonstandard_fds()
{
  long const open_max = sysconf(_SC_OPEN_MAX);
  int const max_fd = open_max > 0 ? static_cast<int>(open_max) : 1024;
  for (int fd = STDERR_FILENO + 1; fd < max_fd; ++fd) {
    close(fd);
  }
}

ssize_t read_retry(int fd, char* data, std::size_t size)
{
  while (true) {
    auto const bytes = read(fd, data, size);
    if (bytes < 0 && errno == EINTR) continue;
    return bytes;
  }
}

pid_t waitpid_retry(pid_t pid, int* status, int options)
{
  while (true) {
    auto const waited = waitpid(pid, status, options);
    if (waited < 0 && errno == EINTR) continue;
    return waited;
  }
}

void kill_and_wait(pid_t pid)
{
  kill(pid, SIGKILL);
  int status = 0;
  waitpid_retry(pid, &status, 0);
}

ava::core::VoidResult write_curl_config(int fd, pid_t pid, std::string_view config)
{
  ScopedSignalIgnore const ignore_sigpipe(SIGPIPE);
  std::size_t written = 0;
  while (written < config.size()) {
    auto const count = write(fd, config.data() + written, config.size() - written);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to write curl configuration");
      error.with_context("cause", count < 0 ? std::strerror(errno) : "short write");
      kill_and_wait(pid);
      return std::unexpected(std::move(error));
    }
    written += static_cast<std::size_t>(count);
  }
  return {};
}

void append_bounded(std::string& value, char const* data, std::size_t size, std::size_t limit)
{
  if (value.size() >= limit) return;
  auto const available = limit - value.size();
  value.append(data, std::min(size, available));
}

}  // namespace

ava::core::Result<HttpResponse> CurlCliTransport::send(HttpRequest const& request)
{
  return send(request, nullptr);
}

ava::core::Result<HttpResponse> CurlCliTransport::send(HttpRequest const& request, CancelCallback cancel_requested)
{
  if (cancel_requested && cancel_requested()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }
  auto body_file = TempBodyFile::create(request.body);
  if (!body_file) return std::unexpected(body_file.error());
  auto const config = detail::build_curl_config(request, body_file->path());

  auto stdin_pipe = make_pipe();
  if (!stdin_pipe) return std::unexpected(stdin_pipe.error());
  auto stdout_pipe = make_pipe();
  if (!stdout_pipe) return std::unexpected(stdout_pipe.error());

  UniqueFd stdin_read((*stdin_pipe)[0]);
  UniqueFd stdin_write((*stdin_pipe)[1]);
  UniqueFd stdout_read((*stdout_pipe)[0]);
  UniqueFd stdout_write((*stdout_pipe)[1]);

  pid_t const pid = fork();
  if (pid < 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to fork curl process");
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }

  if (pid == 0) {
    close(stdin_write.get());
    close(stdout_read.get());
    dup2(stdin_read.get(), STDIN_FILENO);
    dup2(stdout_write.get(), STDOUT_FILENO);
    dup2(stdout_write.get(), STDERR_FILENO);
    close(stdin_read.get());
    close(stdout_write.get());
    close_nonstandard_fds();
    if (setenv("PATH", kTrustedExecPath, 1) != 0) _exit(127);
    execlp("curl", "curl", "-q", "--config", "-", "--write-out", "\nAVA_HTTP_STATUS:%{http_code}",
           static_cast<char*>(nullptr));
    _exit(127);
  }

  stdin_read.reset();
  stdout_write.reset();

  if (auto wrote_config = write_curl_config(stdin_write.get(), pid, config); !wrote_config) {
    return std::unexpected(std::move(wrote_config.error()));
  }
  stdin_write.reset();

  std::string output;
  std::array<char, 4096> buffer{};
  bool stdout_open = true;
  while (stdout_open) {
    if (cancel_requested && cancel_requested()) {
      kill_and_wait(pid);
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
    }

    pollfd poll_fd{.fd = stdout_read.get(), .events = POLLIN, .revents = 0};
    int const poll_result = poll(&poll_fd, 1, 100);
    if (poll_result < 0 && errno == EINTR) continue;
    if (poll_result < 0) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to poll curl output");
      error.with_context("cause", std::strerror(errno));
      kill_and_wait(pid);
      return std::unexpected(std::move(error));
    }
    if (poll_result == 0) continue;
    if ((poll_fd.revents & (POLLIN | POLLHUP | POLLERR)) == 0) continue;

    auto const count = read_retry(stdout_read.get(), buffer.data(), buffer.size());
    if (count < 0) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read curl output");
      error.with_context("cause", std::strerror(errno));
      kill_and_wait(pid);
      return std::unexpected(std::move(error));
    }
    if (count == 0) {
      stdout_open = false;
      stdout_read.reset();
      break;
    }
    output.append(buffer.data(), static_cast<std::size_t>(count));
    if (output.size() > kMaxCurlResponseBytes) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "curl response exceeded byte limit");
      error.with_context("max_bytes", std::to_string(kMaxCurlResponseBytes));
      kill_and_wait(pid);
      return std::unexpected(std::move(error));
    }
  }

  if (cancel_requested && cancel_requested()) {
    kill_and_wait(pid);
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
  }

  int status = 0;
  if (waitpid_retry(pid, &status, 0) < 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to wait for curl process");
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "curl transport failed");
    error.with_context("exit_code", WIFEXITED(status) ? std::to_string(WEXITSTATUS(status)) : "signaled");
    if (!output.empty()) error.with_context("output", output.substr(0, 512));
    return std::unexpected(std::move(error));
  }
  return detail::parse_curl_output(std::move(output), request.include_response_headers);
}

bool CurlCliTransport::supports_streaming() const noexcept
{
  return true;
}

ava::core::Result<HttpResponse> CurlCliTransport::send_streaming(HttpRequest const& request,
                                                                 BodyChunkSink on_body_chunk,
                                                                 CancelCallback cancel_requested)
{
  if (request.include_response_headers) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "streaming curl transport does not support response headers"));
  }
  auto body_file = TempBodyFile::create(request.body);
  if (!body_file) return std::unexpected(body_file.error());
  auto const config = detail::build_curl_config(request, body_file->path());

  auto stdin_pipe = make_pipe();
  if (!stdin_pipe) return std::unexpected(stdin_pipe.error());
  auto stdout_pipe = make_pipe();
  if (!stdout_pipe) return std::unexpected(stdout_pipe.error());
  auto stderr_pipe = make_pipe();
  if (!stderr_pipe) return std::unexpected(stderr_pipe.error());

  UniqueFd stdin_read((*stdin_pipe)[0]);
  UniqueFd stdin_write((*stdin_pipe)[1]);
  UniqueFd stdout_read((*stdout_pipe)[0]);
  UniqueFd stdout_write((*stdout_pipe)[1]);
  UniqueFd stderr_read((*stderr_pipe)[0]);
  UniqueFd stderr_write((*stderr_pipe)[1]);

  pid_t const pid = fork();
  if (pid < 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to fork curl process");
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }

  if (pid == 0) {
    close(stdin_write.get());
    close(stdout_read.get());
    close(stderr_read.get());
    dup2(stdin_read.get(), STDIN_FILENO);
    dup2(stdout_write.get(), STDOUT_FILENO);
    dup2(stderr_write.get(), STDERR_FILENO);
    close(stdin_read.get());
    close(stdout_write.get());
    close(stderr_write.get());
    close_nonstandard_fds();
    if (setenv("PATH", kTrustedExecPath, 1) != 0) _exit(127);
    execlp("curl", "curl", "-q", "--no-buffer", "--config", "-", "--write-out", "\nAVA_HTTP_STATUS:%{http_code}",
           static_cast<char*>(nullptr));
    _exit(127);
  }

  stdin_read.reset();
  stdout_write.reset();
  stderr_write.reset();

  if (auto wrote_config = write_curl_config(stdin_write.get(), pid, config); !wrote_config) {
    return std::unexpected(std::move(wrote_config.error()));
  }
  stdin_write.reset();

  std::string body;
  std::string pending_stdout;
  std::string stderr_output;
  std::array<char, 4096> buffer{};
  bool stdout_open = true;
  bool stderr_open = true;

  auto deliver_body = [&](std::string_view chunk) -> ava::core::VoidResult {
    if (chunk.empty()) return {};
    if (body.size() > kMaxCurlResponseBytes || chunk.size() > kMaxCurlResponseBytes - body.size()) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "curl response exceeded byte limit");
      error.with_context("max_bytes", std::to_string(kMaxCurlResponseBytes));
      return std::unexpected(std::move(error));
    }
    body.append(chunk);
    if (on_body_chunk) {
      if (auto delivered = on_body_chunk(chunk); !delivered) return std::unexpected(std::move(delivered.error()));
    }
    return {};
  };

  while (stdout_open || stderr_open) {
    if (cancel_requested && cancel_requested()) {
      kill_and_wait(pid);
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
    }

    std::array<pollfd, 2> poll_fds{pollfd{.fd = stdout_open ? stdout_read.get() : -1, .events = POLLIN, .revents = 0},
                                   pollfd{.fd = stderr_open ? stderr_read.get() : -1, .events = POLLIN, .revents = 0}};
    int const poll_result = poll(poll_fds.data(), poll_fds.size(), 100);
    if (poll_result < 0 && errno == EINTR) continue;
    if (poll_result < 0) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to poll curl output");
      error.with_context("cause", std::strerror(errno));
      kill_and_wait(pid);
      return std::unexpected(std::move(error));
    }
    if (poll_result == 0) continue;

    if (stdout_open && (poll_fds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      auto const count = read_retry(stdout_read.get(), buffer.data(), buffer.size());
      if (count < 0) {
        auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read curl output");
        error.with_context("cause", std::strerror(errno));
        kill_and_wait(pid);
        return std::unexpected(std::move(error));
      }
      if (count == 0) {
        stdout_open = false;
        stdout_read.reset();
      } else {
        pending_stdout.append(buffer.data(), static_cast<std::size_t>(count));
        if (body.size() + pending_stdout.size() > kMaxCurlResponseBytes + detail::kCurlStatusTailReserve) {
          auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "curl response exceeded byte limit");
          error.with_context("max_bytes", std::to_string(kMaxCurlResponseBytes));
          kill_and_wait(pid);
          return std::unexpected(std::move(error));
        }
        if (pending_stdout.size() > detail::kCurlStatusTailReserve) {
          auto const emit_size = pending_stdout.size() - detail::kCurlStatusTailReserve;
          if (auto delivered = deliver_body(std::string_view(pending_stdout).substr(0, emit_size)); !delivered) {
            kill_and_wait(pid);
            return std::unexpected(std::move(delivered.error()));
          }
          pending_stdout.erase(0, emit_size);
        }
      }
    }

    if (stderr_open && (poll_fds[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      auto const count = read_retry(stderr_read.get(), buffer.data(), buffer.size());
      if (count < 0) {
        auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read curl stderr");
        error.with_context("cause", std::strerror(errno));
        kill_and_wait(pid);
        return std::unexpected(std::move(error));
      }
      if (count == 0) {
        stderr_open = false;
        stderr_read.reset();
      } else {
        append_bounded(stderr_output, buffer.data(), static_cast<std::size_t>(count), kMaxCurlStderrBytes);
      }
    }
  }

  int status = 0;
  if (waitpid_retry(pid, &status, 0) < 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to wait for curl process");
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "curl transport failed");
    error.with_context("exit_code", WIFEXITED(status) ? std::to_string(WEXITSTATUS(status)) : "signaled");
    if (!stderr_output.empty()) error.with_context("stderr", stderr_output.substr(0, 512));
    if (!body.empty()) error.with_context("output", body.substr(0, 512));
    return std::unexpected(std::move(error));
  }

  auto final = detail::parse_curl_output(std::move(pending_stdout), false);
  if (!final) {
    if (!stderr_output.empty()) final.error().with_context("stderr", stderr_output.substr(0, 512));
    return std::unexpected(std::move(final.error()));
  }
  if (auto delivered = deliver_body(final->body); !delivered) return std::unexpected(std::move(delivered.error()));
  final->body = std::move(body);
  return final;
}

}  // namespace ava::provider
