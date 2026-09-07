#include "sys.h"
#include "ava/core/open_beneath.h"

#include <cerrno>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/openat2.h>
#include <sys/syscall.h>
#endif

#ifndef O_PATH
#define O_PATH O_RDONLY
#endif

namespace ava::core {
namespace {

#ifndef __APPLE__
#ifndef __linux__
// Maximum symlink expansions while resolving one anchored path (matches the
// SYMLOOP_MAX convention used by resolve_external_path in AnchorOpen.cpp).
constexpr int kMaxBeneathSymlinkFollows = 40;

// Verify that fd refers to anchor_fd itself or something beneath it. macOS
// cannot use a non-directory fd as dirfd for a ".." walk (unlike Linux O_PATH
// fds), so compare the canonical vnode paths instead: resolve both fds with
// F_GETPATH and require a lexical parent/child relationship. This closes the
// TOCTOU window of the userspace symlink expansion above: a concurrently
// swapped path fails closed here because the final descriptor no longer
// resolves beneath the anchor.
bool beneath_anchor(int anchor_fd, int fd) noexcept
{
  char anchor_path[PATH_MAX];
  char child_path[PATH_MAX];
  if (::fcntl(anchor_fd, F_GETPATH, anchor_path) != 0)
    return false;
  if (::fcntl(fd, F_GETPATH, child_path) != 0)
    return false;
  std::string_view const anchor(anchor_path);
  std::string_view const child(child_path);
  if (child == anchor)
    return true;
  if (anchor == "/")
    return true;
  return child.size() > anchor.size() && child.compare(0, anchor.size(), anchor) == 0 && child[anchor.size()] == '/';
}

// Component-walk with contained symlink following for systems without
// openat2(2). Symlinks are expanded lexically: absolute targets and ".."
// components fail (they escape the anchor by definition), expansions are
// capped, and the final descriptor is verified to still sit at or beneath the
// anchor before it is returned.
int open_beneath_follow(int anchor_fd, std::filesystem::path const& relative, int flags, mode_t mode)
{
  int current = ::dup(anchor_fd);
  if (current < 0)
    return -1;

  if (relative.empty() || relative == ".")
  {
    if ((flags & O_DIRECTORY) != 0)
    {
      int const directory = ::openat(current, ".", flags | O_CLOEXEC | O_NOFOLLOW);
      int const saved = errno;
      ::close(current);
      errno = saved;
      return directory;
    }
    int const fd_flags = ::fcntl(current, F_GETFD);
    if (fd_flags >= 0)
      static_cast<void>(::fcntl(current, F_SETFD, fd_flags | FD_CLOEXEC));
    return current;
  }

  std::vector<std::string> components;
  for (auto const& component : relative) components.push_back(component.string());

  int followed = 0;
  std::size_t index = 0;
  // Depth below the anchor, mirroring kernel path resolution: ".." pops back
  // toward the anchor (and fails only at depth 0), so in-anchor ".." segments
  // from symlink targets resolve exactly as openat2(RESOLVE_BENEATH) would.
  int depth = 0;
  while (index < components.size())
  {
    auto const& name = components[index];
    if (name.empty() || name == ".")
    {
      ++index;
      continue;
    }
    if (name == "..")
    {
      if (depth == 0)
      {
        ::close(current);
        errno = EXDEV;
        return -1;
      }
      int const parent = ::openat(current, "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
      int const saved = errno;
      ::close(current);
      if (parent < 0)
      {
        errno = saved;
        return -1;
      }
      current = parent;
      --depth;
      ++index;
      continue;
    }
    bool const final = index + 1 == components.size();
    int const open_flags = final ? (flags | O_CLOEXEC | O_NOFOLLOW) : (O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int const next = (open_flags & O_CREAT) != 0 ? ::openat(current, name.c_str(), open_flags, mode) : ::openat(current, name.c_str(), open_flags);
    if (next >= 0)
    {
      ::close(current);
      current = next;
      if (!final)
        ++depth;
      ++index;
      continue;
    }
    int const saved = errno;
    if (saved != ELOOP && saved != ENOTDIR)
    {
      ::close(current);
      errno = saved;
      return -1;
    }
    if (final && (flags & O_NOFOLLOW) != 0)
    {
      ::close(current);
      errno = saved;
      return -1;
    }
    // A symlink: on macOS the final-component open fails with ELOOP while an
    // intermediate O_DIRECTORY open fails with ENOTDIR (a link is not a
    // directory). Confirm either case with fstatat so a genuine non-directory
    // or other failure still propagates, then expand the target lexically.
    struct stat link_status{};
    if (::fstatat(current, name.c_str(), &link_status, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISLNK(link_status.st_mode))
    {
      ::close(current);
      errno = saved;
      return -1;
    }
    std::string target(256, '\0');
    for (;;)
    {
      ssize_t const length = ::readlinkat(current, name.c_str(), target.data(), target.size());
      if (length < 0)
      {
        int const read_error = errno;
        ::close(current);
        errno = read_error;
        return -1;
      }
      if (static_cast<std::size_t>(length) < target.size())
      {
        target.resize(static_cast<std::size_t>(length));
        break;
      }
      if (target.size() > 1024 * 1024)
      {
        ::close(current);
        errno = ENAMETOOLONG;
        return -1;
      }
      target.resize(2 * target.size());
    }
    std::filesystem::path const target_path(target);
    if (target_path.is_absolute())
    {
      ::close(current);
      errno = EXDEV;
      return -1;
    }
    if (++followed > kMaxBeneathSymlinkFollows)
    {
      ::close(current);
      errno = ELOOP;
      return -1;
    }
    std::vector<std::string> expanded;
    for (auto const& component : target_path) expanded.push_back(component.string());
    auto const position = components.begin() + static_cast<std::ptrdiff_t>(index);
    components.erase(position, position + 1);
    components.insert(components.begin() + static_cast<std::ptrdiff_t>(index), expanded.begin(), expanded.end());
    // Re-process at the same index: the spliced components are walked next.
  }

  if (!beneath_anchor(anchor_fd, current))
  {
    ::close(current);
    errno = EXDEV;
    return -1;
  }
  return current;
}
#endif  // not __linux__

// Component-walk fallback used when openat2(2) is unavailable (non-Linux, or
// kernels older than 5.6). On Linux it opens relative one component at a time
// and rejects any symlink component with ELOOP and any ".." component with
// EXDEV. This is deliberately stricter than the openat2(RESOLVE_BENEATH) path,
// which follows symlinks that stay within the anchor. Off Linux the fallback
// instead follows contained symlinks in userspace with post-open anchor
// verification (open_beneath_follow above), which is the closest faithful
// equivalent where the kernel offers no RESOLVE_BENEATH.
int open_beneath_fallback(int anchor_fd, std::filesystem::path const& relative, int flags, mode_t mode)
{
#ifndef __linux__
  return open_beneath_follow(anchor_fd, relative, flags, mode);
#endif
  int current = ::dup(anchor_fd);
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
    // The duplicated anchor descriptor is not close-on-exec by default; make
    // it so for consistency with the openat2 path, which forces O_CLOEXEC.
    int const fd_flags = ::fcntl(current, F_GETFD);
    if (fd_flags >= 0)
      static_cast<void>(::fcntl(current, F_SETFD, fd_flags | FD_CLOEXEC));
    return current;
  }

  std::vector<std::string> components;
  for (auto const& component : relative)
    components.push_back(component.string());
  for (std::size_t index = 0; index < components.size(); ++index)
  {
    auto const& name = components[index];
    if (name.empty() || name == ".")
      continue;
    if (name == "..")
    {
      ::close(current);
      errno = EXDEV;
      return -1;
    }
    bool const final = index + 1 == components.size();
    int const open_flags =
        final ? (flags | O_CLOEXEC | O_NOFOLLOW) : (O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int const next = (open_flags & O_CREAT) ? ::openat(current, name.c_str(), open_flags, mode) : ::openat(current, name.c_str(), open_flags);
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

#endif  // not __APPLE__

}  // namespace

int open_beneath(int anchor_fd, std::filesystem::path const& relative, int flags, mode_t mode)
{
  auto const text = relative.empty() ? std::string(".") : relative.generic_string();
#ifdef __linux__
  // openat2(RESOLVE_BENEATH) follows symlinks but guarantees the resolution
  // cannot escape anchor_fd. EAGAIN is returned when the kernel could not
  // prove a ".." component stayed contained (a race or attack); retry a few
  // times before propagating the error.
  for (int attempt = 0; attempt < 8; ++attempt)
  {
    struct open_how how{};
    how.flags = static_cast<std::uint64_t>(flags | O_CLOEXEC);
    how.mode = static_cast<std::uint64_t>(mode);
    how.resolve = RESOLVE_BENEATH;
    int const opened = static_cast<int>(::syscall(SYS_openat2, anchor_fd, text.c_str(), &how, sizeof(how)));
    if (opened >= 0)
      return opened;
    if (errno != EAGAIN)
      break;
  }
  // Fall back only when the kernel does not support openat2 at all, or rejects
  // the request/structure. Any other error (escape, missing path, etc.) is
  // real and is propagated.
  if (errno == ENOSYS || errno == EINVAL || errno == E2BIG)
    return open_beneath_fallback(anchor_fd, relative, flags, mode);
  return -1;
#elif defined(__APPLE__)
  int const open_flags = flags | O_CLOEXEC | O_RESOLVE_BENEATH;
  if ((flags & O_CREAT) != 0)
    return ::openat(anchor_fd, text.c_str(), open_flags, mode);
  return ::openat(anchor_fd, text.c_str(), open_flags);
#else
  return open_beneath_fallback(anchor_fd, relative, flags, mode);
#endif
}

}  // namespace ava::core
