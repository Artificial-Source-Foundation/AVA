#include "sys.h"
#include "ava/tools/secure_workspace.h"
#include "ava/core/ids.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/openat2.h>
#include <sys/syscall.h>
#endif

#ifndef O_PATH
#define O_PATH O_RDONLY
#endif

namespace ava::tools {
namespace {

constexpr unsigned int kResolveFlags =
#ifdef __linux__
    RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
#else
    0;
#endif

ava::core::Error workspace_error(ava::core::ErrorCategory category, std::string message, std::filesystem::path const& root, std::filesystem::path const& path,
                                 int error_number = 0)
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("workspace_root", root.string());
  if (!path.empty())
    error.with_context("path", path.string());
  if (error_number != 0)
    error.with_context("cause", std::generic_category().message(error_number));
  return error;
}

bool path_is_within(std::filesystem::path const& root, std::filesystem::path const& candidate)
{
  auto root_it = root.begin();
  auto candidate_it = candidate.begin();
  for (; root_it != root.end(); ++root_it, ++candidate_it)
    if (candidate_it == candidate.end() || *candidate_it != *root_it)
      return false;
  return true;
}

bool contains_nul(std::filesystem::path const& path)
{
  auto const text = path.native();
  return std::find(text.begin(), text.end(), '\0') != text.end();
}

ava::core::Result<SecureWorkspacePath> lexical_workspace_path(std::filesystem::path const& root, std::filesystem::path const& candidate)
{
  if (candidate.empty() || contains_nul(candidate))
    return std::unexpected(workspace_error(ava::core::ErrorCategory::InvalidArgument, "secure workspace path is empty or contains NUL", root, candidate));

  auto absolute = (candidate.is_absolute() ? candidate : root / candidate).lexically_normal();
  if (!absolute.is_absolute() || !path_is_within(root, absolute))
  {
    return std::unexpected(workspace_error(ava::core::ErrorCategory::PermissionDenied, "path is outside the descriptor-anchored workspace", root, absolute));
  }

  auto relative = absolute.lexically_relative(root);
  if (relative == ".")
    relative.clear();
  for (auto const& component : relative)
  {
    if (component.empty() || component == "." || component == "..")
      return std::unexpected(
          workspace_error(ava::core::ErrorCategory::PermissionDenied, "secure workspace path contains traversal or redundant components", root, absolute));
  }
  return SecureWorkspacePath{.absolute = std::move(absolute), .relative = std::move(relative)};
}

int fallback_open_beneath(int base_fd, std::filesystem::path const& relative, int flags)
{
  int current = ::dup(base_fd);
  if (current < 0)
    return -1;
  if (relative.empty())
  {
    if ((flags & O_DIRECTORY) != 0)
    {
      int const directory = ::openat(current, ".", flags | O_CLOEXEC | O_NOFOLLOW);
      int const saved = errno;
      ::close(current);
      errno = saved;
      return directory;
    }
    return current;
  }

  std::vector<std::string> components;
  for (auto const& component : relative) components.push_back(component.string());
  for (std::size_t index = 0; index < components.size(); ++index)
  {
    bool const final = index + 1 == components.size();
    int const open_flags = final ? flags | O_CLOEXEC | O_NOFOLLOW : O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW;
    int const next = ::openat(current, components[index].c_str(), open_flags);
    int const saved = errno;
    ::close(current);
    if (next < 0)
    {
      errno = saved;
      return -1;
    }
    current = next;
  }
  return current;
}

int open_beneath(int base_fd, std::filesystem::path const& relative, int flags)
{
  auto const text = relative.empty() ? std::string(".") : relative.generic_string();
#ifdef __linux__
  struct open_how how{};
  how.flags = static_cast<std::uint64_t>(flags | O_CLOEXEC);
  how.resolve = kResolveFlags;
  int const opened = static_cast<int>(::syscall(SYS_openat2, base_fd, text.c_str(), &how, sizeof(how)));
  if (opened >= 0)
    return opened;
  if (errno != ENOSYS && errno != EINVAL && errno != E2BIG)
    return -1;
#endif
  return fallback_open_beneath(base_fd, relative, flags);
}

ava::core::Error lookup_error(std::filesystem::path const& root, std::filesystem::path const& path, int error_number, std::string_view operation)
{
  auto const category = error_number == ELOOP || error_number == EXDEV
                            ? ava::core::ErrorCategory::PermissionDenied
                            : (error_number == ENOENT ? ava::core::ErrorCategory::NotFound : ava::core::ErrorCategory::Io);
  auto const message = error_number == ELOOP || error_number == EXDEV ? "secure workspace lookup rejected a symlink, magic link, or root escape"
                                                                      : std::string("secure workspace ") + std::string(operation) + " failed";
  return workspace_error(category, message, root, path, error_number);
}

SecureWorkspaceNodeType node_type(mode_t mode)
{
  if (S_ISREG(mode))
    return SecureWorkspaceNodeType::RegularFile;
  if (S_ISDIR(mode))
    return SecureWorkspaceNodeType::Directory;
  if (S_ISLNK(mode))
    return SecureWorkspaceNodeType::Symlink;
  return SecureWorkspaceNodeType::Other;
}

ava::core::Result<SecureWorkspaceHandle> open_typed(int root_fd, std::filesystem::path const& root, std::filesystem::path const& candidate, bool directory)
{
  auto identity = lexical_workspace_path(root, candidate);
  if (!identity)
    return std::unexpected(std::move(identity.error()));
  int const flags = directory ? O_RDONLY | O_DIRECTORY : O_RDONLY;
  int fd = open_beneath(root_fd, identity->relative, flags);
  if (fd < 0)
    return std::unexpected(lookup_error(root, identity->absolute, errno, directory ? "directory open" : "file open"));

  struct stat status{};
  if (::fstat(fd, &status) != 0)
  {
    int const error_number = errno;
    ::close(fd);
    return std::unexpected(lookup_error(root, identity->absolute, error_number, "fstat"));
  }
  bool const valid_type = directory ? S_ISDIR(status.st_mode) : S_ISREG(status.st_mode);
  if (!valid_type)
  {
    ::close(fd);
    return std::unexpected(workspace_error(ava::core::ErrorCategory::InvalidArgument,
                                           directory ? "secure workspace path is not a directory" : "secure workspace path is not a regular file", root,
                                           identity->absolute));
  }
  return SecureWorkspaceHandle(fd, static_cast<std::uintmax_t>(status.st_size), std::move(identity->absolute));
}

ava::core::Result<bool> visit_directory(int directory_fd, std::filesystem::path const& root, std::filesystem::path const& relative_directory, std::size_t depth,
                                        SecureWorkspaceWalkVisitor const& visitor)
{
  DIR* directory = ::fdopendir(directory_fd);
  if (directory == nullptr)
  {
    int const error_number = errno;
    ::close(directory_fd);
    return std::unexpected(lookup_error(root, root / relative_directory, error_number, "directory traversal"));
  }

  errno = 0;
  while (auto* entry = ::readdir(directory))
  {
    std::string const name(entry->d_name);
    if (name == "." || name == "..")
      continue;
    struct stat status{};
    if (::fstatat(::dirfd(directory), name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)
    {
      if (errno == ENOENT)
        continue;
      int const error_number = errno;
      ::closedir(directory);
      return std::unexpected(lookup_error(root, root / relative_directory / name, error_number, "tree fstat"));
    }

    auto const relative = relative_directory / name;
    SecureWorkspaceDirectoryEntry item{.name = name,
                                       .absolute_path = root / relative,
                                       .relative_path = relative,
                                       .type = node_type(status.st_mode),
                                       .size = S_ISREG(status.st_mode) ? static_cast<std::uintmax_t>(status.st_size) : 0,
                                       .depth = depth};
    auto action = visitor(item);
    if (!action)
    {
      ::closedir(directory);
      return std::unexpected(std::move(action.error()));
    }
    if (*action == SecureWorkspaceWalkAction::Stop)
    {
      ::closedir(directory);
      return false;
    }
    if (item.type == SecureWorkspaceNodeType::Directory && *action != SecureWorkspaceWalkAction::SkipDirectory)
    {
      int child = open_beneath(::dirfd(directory), std::filesystem::path(name), O_RDONLY | O_DIRECTORY);
      if (child < 0)
      {
        if (errno == ENOENT || errno == ELOOP)
          continue;
        int const error_number = errno;
        ::closedir(directory);
        return std::unexpected(lookup_error(root, item.absolute_path, error_number, "tree directory open"));
      }
      auto visited = visit_directory(child, root, relative, depth + 1, visitor);
      if (!visited)
      {
        ::closedir(directory);
        return std::unexpected(std::move(visited.error()));
      }
      if (!*visited)
      {
        ::closedir(directory);
        return false;
      }
    }
    errno = 0;
  }
  int const read_error = errno;
  ::closedir(directory);
  if (read_error != 0)
    return std::unexpected(lookup_error(root, root / relative_directory, read_error, "directory read"));
  return true;
}

ava::core::Error canceled_write_error(std::filesystem::path const& path)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "tool canceled");
  error.with_context("canceled", "true");
  error.with_context("operation", "write_file");
  error.with_context("path", path.string());
  return error;
}

}  // namespace

SecureWorkspaceHandle::SecureWorkspaceHandle(int fd, std::uintmax_t size, std::filesystem::path path) : fd_(fd), size_(size), path_(std::move(path))
{
}

SecureWorkspaceHandle::SecureWorkspaceHandle(SecureWorkspaceHandle&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)), size_(other.size_), path_(std::move(other.path_))
{
}

SecureWorkspaceHandle& SecureWorkspaceHandle::operator=(SecureWorkspaceHandle&& other) noexcept
{
  if (this == &other)
    return *this;
  if (fd_ >= 0)
    ::close(fd_);
  fd_ = std::exchange(other.fd_, -1);
  size_ = other.size_;
  path_ = std::move(other.path_);
  return *this;
}

SecureWorkspaceHandle::~SecureWorkspaceHandle()
{
  if (fd_ >= 0)
    ::close(fd_);
}

int SecureWorkspaceHandle::fd() const noexcept
{
  return fd_;
}

std::uintmax_t SecureWorkspaceHandle::size() const noexcept
{
  return size_;
}

std::filesystem::path const& SecureWorkspaceHandle::path() const noexcept
{
  return path_;
}

SecureWorkspace::StagedWrite::StagedWrite(int parent_fd, std::filesystem::path workspace_root, std::filesystem::path path, std::string temp_name,
                                          std::string target_name, std::size_t bytes_written)
    : parent_fd_(parent_fd),
      workspace_root_(std::move(workspace_root)),
      path_(std::move(path)),
      temp_name_(std::move(temp_name)),
      target_name_(std::move(target_name)),
      bytes_written_(bytes_written)
{
}

SecureWorkspace::StagedWrite::StagedWrite(StagedWrite&& other) noexcept
    : parent_fd_(std::exchange(other.parent_fd_, -1)),
      workspace_root_(std::move(other.workspace_root_)),
      path_(std::move(other.path_)),
      temp_name_(std::move(other.temp_name_)),
      target_name_(std::move(other.target_name_)),
      bytes_written_(other.bytes_written_),
      target_changed_(other.target_changed_)
{
  other.temp_name_.clear();
}

SecureWorkspace::StagedWrite& SecureWorkspace::StagedWrite::operator=(StagedWrite&& other) noexcept
{
  if (this == &other)
    return *this;
  cleanup();
  parent_fd_ = std::exchange(other.parent_fd_, -1);
  workspace_root_ = std::move(other.workspace_root_);
  path_ = std::move(other.path_);
  temp_name_ = std::move(other.temp_name_);
  target_name_ = std::move(other.target_name_);
  bytes_written_ = other.bytes_written_;
  target_changed_ = other.target_changed_;
  other.temp_name_.clear();
  return *this;
}

SecureWorkspace::StagedWrite::~StagedWrite()
{
  cleanup();
}

void SecureWorkspace::StagedWrite::cleanup() noexcept
{
  if (parent_fd_ < 0)
    return;
  if (!temp_name_.empty())
    static_cast<void>(::unlinkat(parent_fd_, temp_name_.c_str(), 0));
  ::close(parent_fd_);
  parent_fd_ = -1;
  temp_name_.clear();
}

ava::core::VoidResult SecureWorkspace::StagedWrite::commit()
{
  if (parent_fd_ < 0 || temp_name_.empty())
  {
    auto error = workspace_error(ava::core::ErrorCategory::InvalidArgument, "secure workspace staged write is no longer active", workspace_root_, path_);
    return std::unexpected(std::move(error));
  }
  if (::renameat(parent_fd_, temp_name_.c_str(), parent_fd_, target_name_.c_str()) != 0)
    return std::unexpected(lookup_error(workspace_root_, path_, errno, "staged write commit"));

  target_changed_ = true;
  temp_name_.clear();
  if (::fsync(parent_fd_) != 0)
  {
    int const error_number = errno;
    ::close(parent_fd_);
    parent_fd_ = -1;
    auto error = lookup_error(workspace_root_, path_, error_number, "write parent sync");
    error.with_context("target_changed", "true");
    return std::unexpected(std::move(error));
  }
  ::close(parent_fd_);
  parent_fd_ = -1;
  return {};
}

std::filesystem::path const& SecureWorkspace::StagedWrite::path() const noexcept
{
  return path_;
}

std::size_t SecureWorkspace::StagedWrite::bytes_written() const noexcept
{
  return bytes_written_;
}

bool SecureWorkspace::StagedWrite::target_changed() const noexcept
{
  return target_changed_;
}

SecureWorkspace::SecureWorkspace(int root_fd, std::filesystem::path root) : root_fd_(root_fd), root_(std::move(root))
{
}

SecureWorkspace::SecureWorkspace(SecureWorkspace&& other) noexcept : root_fd_(std::exchange(other.root_fd_, -1)), root_(std::move(other.root_))
{
}

SecureWorkspace& SecureWorkspace::operator=(SecureWorkspace&& other) noexcept
{
  if (this == &other)
    return *this;
  if (root_fd_ >= 0)
    ::close(root_fd_);
  root_fd_ = std::exchange(other.root_fd_, -1);
  root_ = std::move(other.root_);
  return *this;
}

SecureWorkspace::~SecureWorkspace()
{
  if (root_fd_ >= 0)
    ::close(root_fd_);
}

ava::core::Result<std::shared_ptr<SecureWorkspace>> SecureWorkspace::open(std::filesystem::path const& root)
{
  std::error_code absolute_error;
  auto absolute = std::filesystem::absolute(root, absolute_error).lexically_normal();
  if (absolute_error || !absolute.is_absolute())
    return std::unexpected(workspace_error(ava::core::ErrorCategory::Io, "failed to resolve secure workspace root", root, root, absolute_error.value()));
  std::error_code canonical_error;
  auto canonical = std::filesystem::canonical(absolute, canonical_error);
  if (canonical_error || canonical != absolute)
  {
    auto error = workspace_error(ava::core::ErrorCategory::PermissionDenied, "secure workspace root must be an existing canonical non-symlink directory",
                                 absolute, absolute, canonical_error.value());
    return std::unexpected(std::move(error));
  }

  int slash = ::open("/", O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (slash < 0)
    return std::unexpected(lookup_error(absolute, absolute, errno, "root anchor open"));
  int anchored = absolute == "/" ? ::dup(slash) : open_beneath(slash, absolute.relative_path(), O_PATH | O_DIRECTORY);
  int const open_error = errno;
  ::close(slash);
  if (anchored < 0)
    return std::unexpected(lookup_error(absolute, absolute, open_error, "root anchor open"));

  return std::shared_ptr<SecureWorkspace>(new SecureWorkspace(anchored, std::move(absolute)));
}

std::filesystem::path const& SecureWorkspace::root() const noexcept
{
  return root_;
}

ava::core::Result<SecureWorkspacePath> SecureWorkspace::resolve(std::filesystem::path const& candidate, SecureWorkspaceResolveMode mode) const
{
  auto identity = lexical_workspace_path(root_, candidate);
  if (!identity)
    return std::unexpected(std::move(identity.error()));

  if (identity->relative.empty())
  {
    identity->exists = true;
    identity->directory = true;
    return identity;
  }

  int fd = open_beneath(root_fd_, identity->relative, O_PATH);
  if (fd >= 0)
  {
    struct stat status{};
    if (::fstat(fd, &status) != 0)
    {
      int const error_number = errno;
      ::close(fd);
      return std::unexpected(lookup_error(root_, identity->absolute, error_number, "identity fstat"));
    }
    ::close(fd);
    if (S_ISLNK(status.st_mode))
      return std::unexpected(
          workspace_error(ava::core::ErrorCategory::PermissionDenied, "secure workspace identity rejects symlinks", root_, identity->absolute));
    identity->exists = true;
    identity->regular_file = S_ISREG(status.st_mode);
    identity->directory = S_ISDIR(status.st_mode);
    return identity;
  }

  int const whole_error = errno;
  if (mode == SecureWorkspaceResolveMode::Existing || whole_error != ENOENT)
    return std::unexpected(lookup_error(root_, identity->absolute, whole_error, "identity resolution"));

  int current = ::dup(root_fd_);
  if (current < 0)
    return std::unexpected(lookup_error(root_, identity->absolute, errno, "identity root duplication"));
  for (auto const& component : identity->relative)
  {
    auto const name = component.string();
    int next = ::openat(current, name.c_str(), O_PATH | O_CLOEXEC | O_NOFOLLOW);
    int const open_error = errno;
    if (next < 0)
    {
      ::close(current);
      if (open_error == ENOENT)
        return identity;
      return std::unexpected(lookup_error(root_, identity->absolute, open_error, "identity component resolution"));
    }
    struct stat status{};
    if (::fstat(next, &status) != 0 || S_ISLNK(status.st_mode))
    {
      int const status_error = errno;
      ::close(next);
      ::close(current);
      return std::unexpected(workspace_error(ava::core::ErrorCategory::PermissionDenied, "secure workspace identity rejects a symlinked component", root_,
                                             identity->absolute, status_error));
    }
    ::close(current);
    current = next;
  }
  ::close(current);
  return identity;
}

ava::core::Result<SecureWorkspaceHandle> SecureWorkspace::open_regular_file(std::filesystem::path const& candidate) const
{
  return open_typed(root_fd_, root_, candidate, false);
}

ava::core::Result<SecureWorkspaceHandle> SecureWorkspace::open_directory(std::filesystem::path const& candidate) const
{
  return open_typed(root_fd_, root_, candidate, true);
}

ava::core::Result<std::vector<SecureWorkspaceDirectoryEntry>> SecureWorkspace::list_directory(std::filesystem::path const& candidate) const
{
  auto opened = open_directory(candidate);
  if (!opened)
    return std::unexpected(std::move(opened.error()));
  int duplicate = ::dup(opened->fd());
  if (duplicate < 0)
    return std::unexpected(lookup_error(root_, opened->path(), errno, "directory duplication"));
  DIR* directory = ::fdopendir(duplicate);
  if (directory == nullptr)
  {
    int const error_number = errno;
    ::close(duplicate);
    return std::unexpected(lookup_error(root_, opened->path(), error_number, "directory listing"));
  }

  auto identity = lexical_workspace_path(root_, opened->path());
  if (!identity)
  {
    ::closedir(directory);
    return std::unexpected(std::move(identity.error()));
  }
  std::vector<SecureWorkspaceDirectoryEntry> entries;
  errno = 0;
  while (auto* entry = ::readdir(directory))
  {
    std::string const name(entry->d_name);
    if (name == "." || name == "..")
      continue;
    struct stat status{};
    if (::fstatat(::dirfd(directory), name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)
    {
      if (errno == ENOENT)
        continue;
      int const error_number = errno;
      ::closedir(directory);
      return std::unexpected(lookup_error(root_, opened->path() / name, error_number, "directory entry fstat"));
    }
    auto const relative = identity->relative / name;
    entries.push_back(SecureWorkspaceDirectoryEntry{.name = name,
                                                    .absolute_path = root_ / relative,
                                                    .relative_path = relative,
                                                    .type = node_type(status.st_mode),
                                                    .size = S_ISREG(status.st_mode) ? static_cast<std::uintmax_t>(status.st_size) : 0,
                                                    .depth = 0});
    errno = 0;
  }
  int const read_error = errno;
  ::closedir(directory);
  if (read_error != 0)
    return std::unexpected(lookup_error(root_, opened->path(), read_error, "directory listing"));
  return entries;
}

ava::core::VoidResult SecureWorkspace::visit_tree(SecureWorkspaceWalkVisitor const& visitor) const
{
  if (!visitor)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "secure workspace tree visitor is required"));
  int root_directory = open_beneath(root_fd_, {}, O_RDONLY | O_DIRECTORY);
  if (root_directory < 0)
    return std::unexpected(lookup_error(root_, root_, errno, "tree root open"));
  auto visited = visit_directory(root_directory, root_, {}, 0, visitor);
  if (!visited)
    return std::unexpected(std::move(visited.error()));
  return {};
}

ava::core::Result<SecureWorkspace::StagedWrite> SecureWorkspace::stage_write(std::filesystem::path const& candidate, std::string_view content,
                                                                             std::function<bool()> const& cancel_requested) const
{
  auto identity = lexical_workspace_path(root_, candidate);
  if (!identity)
    return std::unexpected(std::move(identity.error()));
  if (identity->relative.empty() || identity->relative.filename().empty())
    return std::unexpected(
        workspace_error(ava::core::ErrorCategory::InvalidArgument, "secure workspace write target must name a file", root_, identity->absolute));

  int parent = ::openat(root_fd_, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (parent < 0)
    return std::unexpected(lookup_error(root_, identity->absolute, errno, "write root open"));
  for (auto const& component : identity->relative.parent_path())
  {
    auto const name = component.string();
    int next = open_beneath(parent, std::filesystem::path(name), O_RDONLY | O_DIRECTORY);
    if (next < 0 && errno == ENOENT)
    {
      if (::mkdirat(parent, name.c_str(), 0700) != 0 && errno != EEXIST)
      {
        int const error_number = errno;
        ::close(parent);
        return std::unexpected(lookup_error(root_, identity->absolute.parent_path(), error_number, "parent creation"));
      }
      next = open_beneath(parent, std::filesystem::path(name), O_RDONLY | O_DIRECTORY);
    }
    if (next < 0)
    {
      int const error_number = errno;
      ::close(parent);
      return std::unexpected(lookup_error(root_, identity->absolute.parent_path(), error_number, "write parent open"));
    }
    ::close(parent);
    parent = next;
  }

  auto const target_name = identity->relative.filename().string();
  struct stat existing{};
  bool const target_exists = ::fstatat(parent, target_name.c_str(), &existing, AT_SYMLINK_NOFOLLOW) == 0;
  if (!target_exists && errno != ENOENT)
  {
    int const error_number = errno;
    ::close(parent);
    return std::unexpected(lookup_error(root_, identity->absolute, error_number, "write target fstat"));
  }
  if (target_exists && (S_ISLNK(existing.st_mode) || !S_ISREG(existing.st_mode)))
  {
    ::close(parent);
    return std::unexpected(
        workspace_error(S_ISLNK(existing.st_mode) ? ava::core::ErrorCategory::PermissionDenied : ava::core::ErrorCategory::InvalidArgument,
                        S_ISLNK(existing.st_mode) ? "secure workspace writes do not follow symlinks" : "secure workspace write target is not a regular file",
                        root_, identity->absolute));
  }

  std::string temp_name;
  int temp = -1;
  for (int attempt = 0; attempt < 8 && temp < 0; ++attempt)
  {
    temp_name = "." + target_name + ".ava-write-" + ava::core::make_id("tmp") + ".tmp";
    temp = ::openat(parent, temp_name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (temp < 0 && errno != EEXIST)
      break;
  }
  if (temp < 0)
  {
    int const error_number = errno;
    ::close(parent);
    return std::unexpected(lookup_error(root_, identity->absolute, error_number, "temporary write open"));
  }

  auto cleanup = [&] {
    if (temp >= 0)
    {
      ::close(temp);
      temp = -1;
    }
    if (!temp_name.empty())
      static_cast<void>(::unlinkat(parent, temp_name.c_str(), 0));
  };
  std::size_t offset = 0;
  while (offset < content.size())
  {
    if (cancel_requested && cancel_requested())
    {
      cleanup();
      ::close(parent);
      return std::unexpected(canceled_write_error(identity->absolute));
    }
    auto const count = ::write(temp, content.data() + offset, content.size() - offset);
    if (count < 0)
    {
      if (errno == EINTR)
        continue;
      int const error_number = errno;
      cleanup();
      ::close(parent);
      return std::unexpected(lookup_error(root_, identity->absolute, error_number, "temporary write"));
    }
    offset += static_cast<std::size_t>(count);
  }
  mode_t const permissions = target_exists ? existing.st_mode & 07777 : 0600;
  if (::fchmod(temp, permissions) != 0 || ::fsync(temp) != 0)
  {
    int const error_number = errno;
    cleanup();
    ::close(parent);
    return std::unexpected(lookup_error(root_, identity->absolute, error_number, "temporary write sync"));
  }
  if (::close(temp) != 0)
  {
    int const error_number = errno;
    temp = -1;
    static_cast<void>(::unlinkat(parent, temp_name.c_str(), 0));
    ::close(parent);
    return std::unexpected(lookup_error(root_, identity->absolute, error_number, "temporary write close"));
  }
  temp = -1;
  if (cancel_requested && cancel_requested())
  {
    static_cast<void>(::unlinkat(parent, temp_name.c_str(), 0));
    ::close(parent);
    return std::unexpected(canceled_write_error(identity->absolute));
  }

  return StagedWrite(parent, root_, std::move(identity->absolute), std::move(temp_name), target_name, content.size());
}

ava::core::Result<SecureWorkspaceWriteResult> SecureWorkspace::write_file(std::filesystem::path const& candidate, std::string_view content,
                                                                          std::function<bool()> const& cancel_requested) const
{
  auto staged = stage_write(candidate, content, cancel_requested);
  if (!staged)
    return std::unexpected(std::move(staged.error()));
  if (cancel_requested && cancel_requested())
    return std::unexpected(canceled_write_error(staged->path()));
  if (auto committed = staged->commit(); !committed)
    return std::unexpected(std::move(committed.error()));
  return SecureWorkspaceWriteResult{.path = staged->path(), .bytes_written = staged->bytes_written()};
}

}  // namespace ava::tools
