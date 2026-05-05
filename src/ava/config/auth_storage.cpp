#include "ava/config/auth_storage.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace ava::config {
namespace {

std::string errno_message()
{
  return std::strerror(errno);
}

ava::core::Error auth_file_error(ava::core::ErrorCategory category, std::string message,
                                 std::filesystem::path const& path)
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("path", path.string());
  return error;
}

}  // namespace

AuthFileLock::AuthFileLock(int fd) noexcept : fd_(fd)
{
}

AuthFileLock::AuthFileLock(AuthFileLock&& other) noexcept : fd_(std::exchange(other.fd_, -1))
{
}

AuthFileLock& AuthFileLock::operator=(AuthFileLock&& other) noexcept
{
  if (this != &other) {
    close_if_open();
    fd_ = std::exchange(other.fd_, -1);
  }
  return *this;
}

AuthFileLock::~AuthFileLock()
{
  close_if_open();
}

int AuthFileLock::get() const noexcept
{
  return fd_;
}

void AuthFileLock::close_if_open() noexcept
{
  if (fd_ >= 0) static_cast<void>(::close(fd_));
}

bool auth_error_has_context(ava::core::Error const& error, std::string_view key, std::string_view value)
{
  return std::ranges::any_of(
      error.context(), [&](ava::core::ErrorContext const& item) { return item.key == key && item.value == value; });
}

ava::core::VoidResult ensure_auth_directory(XdgPaths const& paths)
{
  std::error_code mkdir_error;
  std::filesystem::create_directories(paths.auth_file.parent_path(), mkdir_error);
  if (mkdir_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to create auth directory");
    error.with_context("path", paths.auth_file.parent_path().string());
    error.with_context("cause", mkdir_error.message());
    return std::unexpected(std::move(error));
  }
  if (::chmod(paths.auth_file.parent_path().c_str(), S_IRWXU) != 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to set auth directory permissions");
    error.with_context("path", paths.auth_file.parent_path().string());
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::Result<AuthFileLock> acquire_auth_file_lock(XdgPaths const& paths)
{
  auto const lock_path = paths.auth_file.parent_path() / (paths.auth_file.filename().string() + ".lock");
  int flags = O_RDWR | O_CREAT | O_CLOEXEC;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
  AuthFileLock fd(::open(lock_path.c_str(), flags, S_IRUSR | S_IWUSR));
  if (fd.get() < 0) {
    auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to open auth lock file", lock_path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }

  struct stat opened_st {};
  if (::fstat(fd.get(), &opened_st) != 0) {
    auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to inspect auth lock file", lock_path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  if (!S_ISREG(opened_st.st_mode)) {
    return std::unexpected(
        auth_file_error(ava::core::ErrorCategory::PermissionDenied, "auth lock file is not regular", lock_path));
  }
  if (::fchmod(fd.get(), S_IRUSR | S_IWUSR) != 0) {
    auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to set auth lock permissions", lock_path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  while (::flock(fd.get(), LOCK_EX) != 0) {
    if (errno == EINTR) continue;
    auto error = auth_file_error(ava::core::ErrorCategory::Io, "failed to lock auth file", lock_path);
    error.with_context("cause", errno_message());
    return std::unexpected(std::move(error));
  }
  return fd;
}

}  // namespace ava::config
