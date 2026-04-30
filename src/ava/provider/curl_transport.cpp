#include "ava/provider/curl_transport.h"

#include <array>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <signal.h>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace ava::provider {
namespace {

constexpr char kTrustedExecPath[] = "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";

class UniqueFd {
 public:
  explicit UniqueFd(int fd = -1) : fd_(fd) {}
  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;
  UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
  UniqueFd& operator=(UniqueFd&& other) noexcept {
    if (this != &other) reset(other.release());
    return *this;
  }
  ~UniqueFd() { reset(); }

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] int release() noexcept {
    const int fd = fd_;
    fd_ = -1;
    return fd;
  }
  void reset(int fd = -1) noexcept {
    if (fd_ >= 0) close(fd_);
    fd_ = fd;
  }

 private:
  int fd_ = -1;
};

class TempBodyFile {
 public:
  TempBodyFile() = default;
  TempBodyFile(const TempBodyFile&) = delete;
  TempBodyFile& operator=(const TempBodyFile&) = delete;
  TempBodyFile(TempBodyFile&& other) noexcept : path_(std::move(other.path_)) { other.path_.clear(); }
  TempBodyFile& operator=(TempBodyFile&& other) noexcept {
    if (this != &other) {
      cleanup();
      path_ = std::move(other.path_);
      other.path_.clear();
    }
    return *this;
  }
  ~TempBodyFile() { cleanup(); }

  [[nodiscard]] const std::string& path() const noexcept { return path_; }

  [[nodiscard]] static ava::core::Result<TempBodyFile> create(std::string_view body) {
    std::error_code temp_error;
    auto temp_dir = std::filesystem::temp_directory_path(temp_error);
    if (temp_error) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to resolve temporary directory");
      error.with_context("cause", temp_error.message());
      return std::unexpected(std::move(error));
    }
    std::string tmpl = (temp_dir / "ava-request-body-XXXXXX").string();
    const int fd = mkstemp(tmpl.data());
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
      const auto count = write(file.get(), body.data() + written, body.size() - written);
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
  void cleanup() noexcept {
    if (!path_.empty()) {
      unlink(path_.c_str());
      path_.clear();
    }
  }

  std::string path_;
};

class ScopedSignalIgnore {
 public:
  explicit ScopedSignalIgnore(int signal) : signal_(signal) {
    struct sigaction action {};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    active_ = sigaction(signal_, &action, &previous_) == 0;
  }
  ScopedSignalIgnore(const ScopedSignalIgnore&) = delete;
  ScopedSignalIgnore& operator=(const ScopedSignalIgnore&) = delete;
  ~ScopedSignalIgnore() {
    if (active_) sigaction(signal_, &previous_, nullptr);
  }

 private:
  int signal_ = 0;
  bool active_ = false;
  struct sigaction previous_ {};
};

ava::core::Result<std::array<int, 2>> make_pipe() {
  std::array<int, 2> pipe_fds{-1, -1};
  if (pipe(pipe_fds.data()) != 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to create curl pipe");
    error.with_context("cause", std::strerror(errno));
    return std::unexpected(std::move(error));
  }
  return pipe_fds;
}

void close_nonstandard_fds() {
  const long open_max = sysconf(_SC_OPEN_MAX);
  const int max_fd = open_max > 0 ? static_cast<int>(open_max) : 1024;
  for (int fd = STDERR_FILENO + 1; fd < max_fd; ++fd) {
    close(fd);
  }
}

std::string curl_config_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
      case '\r':
        escaped += ' ';
        break;
      default:
        escaped += ch;
        break;
    }
  }
  return escaped;
}

std::string build_curl_config(const HttpRequest& request, const std::string& body_path) {
  std::string config;
  config += "url = \"" + curl_config_escape(request.url) + "\"\n";
  config += "request = \"" + curl_config_escape(request.method.empty() ? "POST" : request.method) + "\"\n";
  config += "location\n";
  config += "silent\n";
  config += "show-error\n";
  config += "no-progress-meter\n";
  config += "max-time = \"" + std::to_string(std::max(1, request.timeout_ms / 1000)) + "\"\n";
  for (const auto& [name, value] : request.headers) {
    config += "header = \"" + curl_config_escape(name + ": " + value) + "\"\n";
  }
  if (!request.body.empty()) {
    config += "data-binary = \"@" + curl_config_escape(body_path) + "\"\n";
  }
  return config;
}

ssize_t read_retry(int fd, char* data, std::size_t size) {
  while (true) {
    const auto bytes = read(fd, data, size);
    if (bytes < 0 && errno == EINTR) continue;
    return bytes;
  }
}

pid_t waitpid_retry(pid_t pid, int* status, int options) {
  while (true) {
    const auto waited = waitpid(pid, status, options);
    if (waited < 0 && errno == EINTR) continue;
    return waited;
  }
}

ava::core::Result<HttpResponse> parse_curl_output(std::string output) {
  constexpr std::string_view marker = "\nAVA_HTTP_STATUS:";
  const auto marker_pos = output.rfind(marker);
  if (marker_pos == std::string::npos) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "curl response did not include an HTTP status");
    if (!output.empty()) error.with_context("output", output.substr(0, 512));
    return std::unexpected(std::move(error));
  }

  const auto status_text = output.substr(marker_pos + marker.size());
  int status = 0;
  for (const char ch : status_text) {
    if (ch < '0' || ch > '9') break;
    status = (status * 10) + (ch - '0');
  }
  output.resize(marker_pos);
  return HttpResponse{.status_code = status, .headers = {}, .body = std::move(output)};
}

}  // namespace

ava::core::Result<HttpResponse> CurlCliTransport::send(const HttpRequest& request) {
  auto body_file = TempBodyFile::create(request.body);
  if (!body_file) return std::unexpected(body_file.error());
  const auto config = build_curl_config(request, body_file->path());

  auto stdin_pipe = make_pipe();
  if (!stdin_pipe) return std::unexpected(stdin_pipe.error());
  auto stdout_pipe = make_pipe();
  if (!stdout_pipe) return std::unexpected(stdout_pipe.error());

  UniqueFd stdin_read((*stdin_pipe)[0]);
  UniqueFd stdin_write((*stdin_pipe)[1]);
  UniqueFd stdout_read((*stdout_pipe)[0]);
  UniqueFd stdout_write((*stdout_pipe)[1]);

  const pid_t pid = fork();
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

  const ScopedSignalIgnore ignore_sigpipe(SIGPIPE);
  std::size_t written = 0;
  while (written < config.size()) {
    const auto count = write(stdin_write.get(), config.data() + written, config.size() - written);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to write curl configuration");
      error.with_context("cause", count < 0 ? std::strerror(errno) : "short write");
      kill(pid, SIGKILL);
      int status = 0;
      waitpid_retry(pid, &status, 0);
      return std::unexpected(std::move(error));
    }
    written += static_cast<std::size_t>(count);
  }
  stdin_write.reset();

  std::string output;
  std::array<char, 4096> buffer{};
  while (true) {
    const auto count = read_retry(stdout_read.get(), buffer.data(), buffer.size());
    if (count < 0) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read curl output");
      error.with_context("cause", std::strerror(errno));
      kill(pid, SIGKILL);
      int status = 0;
      waitpid_retry(pid, &status, 0);
      return std::unexpected(std::move(error));
    }
    if (count == 0) break;
    output.append(buffer.data(), static_cast<std::size_t>(count));
    if (output.size() > 8 * 1024 * 1024) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "curl response exceeded byte limit");
      error.with_context("max_bytes", std::to_string(8 * 1024 * 1024));
      kill(pid, SIGKILL);
      int status = 0;
      waitpid_retry(pid, &status, 0);
      return std::unexpected(std::move(error));
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
    if (!output.empty()) error.with_context("output", output.substr(0, 512));
    return std::unexpected(std::move(error));
  }
  return parse_curl_output(std::move(output));
}

}  // namespace ava::provider
