#include "sys.h"
#include "ava/lsp/bounded_file_reader.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <system_error>
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/openat2.h>
#include <sys/syscall.h>
#endif

namespace ava::lsp {
namespace {

class UniqueFd
{
 public:
  explicit UniqueFd(int fd = -1) noexcept : fd_(fd) { }
  ~UniqueFd()
  {
    if (fd_ >= 0)
      ::close(fd_);
  }
  UniqueFd(UniqueFd const&) = delete;
  UniqueFd& operator=(UniqueFd const&) = delete;
  UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) { }
  UniqueFd& operator=(UniqueFd&& other) noexcept
  {
    if (this != &other)
    {
      if (fd_ >= 0)
        ::close(fd_);
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }

  [[nodiscard]] int get() const noexcept { return fd_; }

 private:
  int fd_ = -1;
};

ava::core::Error read_error(ava::core::ErrorCategory category, std::string message, std::filesystem::path const& path, int error_number = 0)
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("path", path.string());
  if (error_number != 0)
    error.with_context("cause", std::strerror(error_number));
  return error;
}

bool is_canceled(BoundedFileReadOptions const& options)
{
  return options.cancel_requested && options.cancel_requested();
}

bool deadline_expired(BoundedFileReadOptions const& options)
{
  return std::chrono::steady_clock::now() >= options.deadline;
}

std::optional<ava::core::Error> abort_error(BoundedFileReadOptions const& options)
{
  if (is_canceled(options))
  {
    auto error = read_error(ava::core::ErrorCategory::Unknown, "LSP file read canceled", options.path);
    error.with_context("canceled", "true");
    return error;
  }
  if (deadline_expired(options))
  {
    auto error = read_error(ava::core::ErrorCategory::Tool, "timed out reading LSP file", options.path);
    error.with_context("deadline", "expired");
    return error;
  }
  return std::nullopt;
}

std::filesystem::path normalized_absolute(std::filesystem::path const& path, std::error_code& error)
{
  auto absolute = std::filesystem::absolute(path, error);
  return error ? std::filesystem::path{} : absolute.lexically_normal();
}

bool is_beneath(std::filesystem::path const& root, std::filesystem::path const& path)
{
  auto root_it = root.begin();
  auto path_it = path.begin();
  for (; root_it != root.end(); ++root_it, ++path_it)
    if (path_it == path.end() || *root_it != *path_it)
      return false;
  return true;
}

bool valid_relative(std::filesystem::path const& relative)
{
  for (auto const& component : relative)
  {
    if (component.empty() || component == "." || component == "..")
      return false;
  }
  return true;
}

int open_by_components(int anchor_fd, std::filesystem::path const& relative, int final_flags)
{
  int current = ::fcntl(anchor_fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
  if (current < 0)
    return -1;

  if (relative.empty())
  {
    int const opened = ::openat(current, ".", final_flags | O_NOFOLLOW);
    int const saved_errno = errno;
    ::close(current);
    errno = saved_errno;
    return opened;
  }

  for (auto component = relative.begin(); component != relative.end(); ++component)
  {
    auto next_component = component;
    ++next_component;
    auto const name = component->string();
    if (name.empty() || name == "." || name == "..")
    {
      ::close(current);
      errno = EXDEV;
      return -1;
    }
    int const flags = next_component == relative.end() ? final_flags : O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC | O_DIRECTORY;
    int const opened = ::openat(current, name.c_str(), flags | O_NOFOLLOW);
    int const saved_errno = errno;
    ::close(current);
    if (opened < 0)
    {
      errno = saved_errno;
      return -1;
    }
    current = opened;
  }
  return current;
}

int open_beneath_no_symlinks(int anchor_fd, std::filesystem::path const& relative, int flags, BoundedFileOpenStrategy strategy)
{
#ifdef __linux__
  if (strategy != BoundedFileOpenStrategy::ForceComponentFallback)
  {
    open_how how{};
    how.flags = static_cast<std::uint64_t>(flags);
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
    auto const text = relative.empty() ? std::string(".") : relative.generic_string();
    int const opened = static_cast<int>(::syscall(SYS_openat2, anchor_fd, text.c_str(), &how, sizeof(how)));
    if (opened >= 0)
      return opened;
    if (errno != ENOSYS && errno != EINVAL && errno != E2BIG)
      return -1;
  }
#else
  static_cast<void>(strategy);
#endif
  return open_by_components(anchor_fd, relative, flags);
}

ava::core::Result<UniqueFd> open_workspace_anchor(BoundedFileReadOptions const& options, std::filesystem::path const& workspace)
{
  UniqueFd slash(::open("/", O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC | O_DIRECTORY));
  if (slash.get() < 0)
    return std::unexpected(read_error(ava::core::ErrorCategory::Io, "failed to open LSP workspace filesystem anchor", workspace, errno));
  auto const relative = workspace.relative_path();
  int const fd = open_beneath_no_symlinks(slash.get(), relative, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC | O_DIRECTORY, options.open_strategy);
  if (fd < 0)
  {
    int const saved_errno = errno;
    auto const category = saved_errno == ELOOP || saved_errno == EXDEV ? ava::core::ErrorCategory::PermissionDenied : ava::core::ErrorCategory::Io;
    auto const message = category == ava::core::ErrorCategory::PermissionDenied ? "LSP workspace anchor must not contain a symlink or escape its root"
                                                                                : "failed to open LSP workspace anchor";
    return std::unexpected(read_error(category, message, workspace, saved_errno));
  }
  return UniqueFd(fd);
}

ava::core::Error open_error(BoundedFileReadOptions const& options, int error_number)
{
  if (error_number == ELOOP || error_number == EXDEV)
    return read_error(ava::core::ErrorCategory::PermissionDenied, "LSP file path must not contain a symlink or escape its anchor", options.path, error_number);
  if (error_number == ENOENT)
    return read_error(ava::core::ErrorCategory::NotFound, "LSP file was not found", options.path, error_number);
  return read_error(ava::core::ErrorCategory::Io, "failed to open LSP file", options.path, error_number);
}

ava::core::Result<UniqueFd> open_read_descriptor(BoundedFileReadOptions const& options)
{
  constexpr int final_flags = O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC;
  if (options.scope == BoundedFileReadScope::External)
  {
    // Global configuration is anchored at the filesystem root, not the
    // workspace. This keeps an absolute XDG path compatible while preserving
    // the same no-symlink component resolution as project configuration.
    std::error_code external_error;
    auto const path = normalized_absolute(options.path, external_error);
    if (external_error || path.empty() || !path.is_absolute())
      return std::unexpected(read_error(ava::core::ErrorCategory::InvalidArgument, "external LSP file path must be absolute", options.path));
    UniqueFd slash(::open("/", O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC | O_DIRECTORY));
    if (slash.get() < 0)
      return std::unexpected(read_error(ava::core::ErrorCategory::Io, "failed to open LSP external filesystem anchor", options.path, errno));
    int const fd = open_beneath_no_symlinks(slash.get(), path.relative_path(), final_flags, options.open_strategy);
    if (fd < 0)
    {
      int const saved_errno = errno;
      if (auto aborted = abort_error(options))
        return std::unexpected(std::move(*aborted));
      if (options.missing_ok && saved_errno == ENOENT)
        return UniqueFd{};
      return std::unexpected(open_error(options, saved_errno));
    }
    return UniqueFd(fd);
  }

  std::error_code workspace_error;
  auto const workspace = normalized_absolute(options.workspace_root, workspace_error);
  if (workspace_error || workspace.empty() || !workspace.is_absolute())
    return std::unexpected(read_error(ava::core::ErrorCategory::InvalidArgument, "LSP workspace root must be an absolute path", options.workspace_root));
  std::error_code path_error;
  auto const path = options.path.is_absolute() ? options.path.lexically_normal() : (workspace / options.path).lexically_normal();
  if (path_error || !path.is_absolute() || !is_beneath(workspace, path))
    return std::unexpected(read_error(ava::core::ErrorCategory::PermissionDenied, "LSP file is outside the workspace", options.path));
  auto relative = path.lexically_relative(workspace);
  if (relative == ".")
    relative.clear();
  if (!valid_relative(relative))
    return std::unexpected(read_error(ava::core::ErrorCategory::PermissionDenied, "LSP file path contains traversal", options.path));

  auto anchor = open_workspace_anchor(options, workspace);
  if (!anchor)
    return std::unexpected(std::move(anchor.error()));
  if (auto aborted = abort_error(options))
    return std::unexpected(std::move(*aborted));
  int const fd = open_beneath_no_symlinks(anchor->get(), relative, final_flags, options.open_strategy);
  if (fd < 0)
  {
    int const saved_errno = errno;
    if (auto aborted = abort_error(options))
      return std::unexpected(std::move(*aborted));
    if (options.missing_ok && saved_errno == ENOENT)
      return UniqueFd{};
    return std::unexpected(open_error(options, saved_errno));
  }
  return UniqueFd(fd);
}

}  // namespace

ava::core::Result<std::optional<std::string>> read_bounded_lsp_file(BoundedFileReadOptions const& options)
{
  if (options.path.empty() || options.max_bytes == 0)
    return std::unexpected(read_error(ava::core::ErrorCategory::InvalidArgument, "LSP bounded file reader options are invalid", options.path));
  if (auto aborted = abort_error(options))
    return std::unexpected(std::move(*aborted));

  auto opened = open_read_descriptor(options);
  if (!opened)
    return std::unexpected(std::move(opened.error()));
  if (opened->get() < 0)
    return std::optional<std::string>{};
  UniqueFd file(std::move(*opened));

  struct stat status{};
  if (::fstat(file.get(), &status) != 0)
  {
    int const saved_errno = errno;
    if (auto aborted = abort_error(options))
      return std::unexpected(std::move(*aborted));
    return std::unexpected(read_error(ava::core::ErrorCategory::Io, "failed to inspect opened LSP file", options.path, saved_errno));
  }
  if (auto aborted = abort_error(options))
    return std::unexpected(std::move(*aborted));
  if (!S_ISREG(status.st_mode))
    return std::unexpected(read_error(ava::core::ErrorCategory::InvalidArgument, "LSP file is not a regular file", options.path));
  if (status.st_size < 0 || static_cast<std::uintmax_t>(status.st_size) > options.max_bytes)
  {
    auto error = read_error(ava::core::ErrorCategory::InvalidArgument, "LSP file exceeds maximum size", options.path);
    error.with_context("max_bytes", std::to_string(options.max_bytes));
    return std::unexpected(std::move(error));
  }

  // This intentionally runs only after the final descriptor and fstat are
  // complete. Tests use it to replace the pathname and verify descriptor-only
  // reads; production callers leave it empty.
  if (options.after_open_for_testing)
    options.after_open_for_testing();
  if (auto aborted = abort_error(options))
    return std::unexpected(std::move(*aborted));

  std::string content;
  content.reserve(static_cast<std::size_t>(status.st_size));
  std::array<char, 4096> buffer{};
  while (true)
  {
    if (auto aborted = abort_error(options))
      return std::unexpected(std::move(*aborted));
    auto const count = ::read(file.get(), buffer.data(), buffer.size());
    if (count == 0)
    {
      if (auto aborted = abort_error(options))
        return std::unexpected(std::move(*aborted));
      return std::optional<std::string>(std::move(content));
    }
    if (count < 0)
    {
      int const saved_errno = errno;
      if (auto aborted = abort_error(options))
        return std::unexpected(std::move(*aborted));
      if (saved_errno == EINTR)
        continue;
      return std::unexpected(read_error(ava::core::ErrorCategory::Io, "failed to read LSP file", options.path, saved_errno));
    }
    if (auto aborted = abort_error(options))
      return std::unexpected(std::move(*aborted));
    auto const bytes = static_cast<std::size_t>(count);
    if (content.size() > options.max_bytes || bytes > options.max_bytes - content.size())
    {
      auto error = read_error(ava::core::ErrorCategory::InvalidArgument, "LSP file exceeds maximum size", options.path);
      error.with_context("max_bytes", std::to_string(options.max_bytes));
      return std::unexpected(std::move(error));
    }
    content.append(buffer.data(), bytes);
  }
}

}  // namespace ava::lsp
