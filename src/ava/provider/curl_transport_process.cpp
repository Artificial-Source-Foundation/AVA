#include "ava/provider/curl_transport_process.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <utility>

namespace ava::provider::detail {

UniqueFd::UniqueFd(int fd) noexcept : fd_(fd)
{
}

UniqueFd::UniqueFd(UniqueFd&& other) noexcept : fd_(other.release())
{
}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept
{
  if (this != &other) reset(other.release());
  return *this;
}

UniqueFd::~UniqueFd()
{
  reset();
}

int UniqueFd::get() const noexcept
{
  return fd_;
}

int UniqueFd::release() noexcept
{
  int const fd = fd_;
  fd_ = -1;
  return fd;
}

void UniqueFd::reset(int fd) noexcept
{
  if (fd_ >= 0) close(fd_);
  fd_ = fd;
}

TempBodyFile::TempBodyFile(TempBodyFile&& other) noexcept : path_(std::move(other.path_))
{
  other.path_.clear();
}

TempBodyFile& TempBodyFile::operator=(TempBodyFile&& other) noexcept
{
  if (this != &other) {
    cleanup();
    path_ = std::move(other.path_);
    other.path_.clear();
  }
  return *this;
}

TempBodyFile::~TempBodyFile()
{
  cleanup();
}

std::string const& TempBodyFile::path() const noexcept
{
  return path_;
}

ava::core::Result<TempBodyFile> TempBodyFile::create(std::string_view body)
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

void TempBodyFile::cleanup() noexcept
{
  if (!path_.empty()) {
    unlink(path_.c_str());
    path_.clear();
  }
}

ScopedSignalIgnore::ScopedSignalIgnore(int signal) noexcept : signal_(signal)
{
  struct sigaction action {};
  action.sa_handler = SIG_IGN;
  sigemptyset(&action.sa_mask);
  active_ = sigaction(signal_, &action, &previous_) == 0;
}

ScopedSignalIgnore::~ScopedSignalIgnore()
{
  if (active_) sigaction(signal_, &previous_, nullptr);
}

ava::core::Result<std::array<int, 2>> make_curl_pipe()
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

}  // namespace ava::provider::detail
