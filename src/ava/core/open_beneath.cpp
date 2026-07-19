#include "sys.h"
#include "ava/core/open_beneath.h"

#include <cerrno>
#include <cstdint>
#include <string>
#include <vector>
#include <fcntl.h>
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

// Component-walk fallback used when openat2(2) is unavailable (non-Linux, or
// kernels older than 5.6). It opens relative one component at a time and
// rejects any symlink component with ELOOP and any ".." component with EXDEV.
// This is deliberately stricter than the openat2(RESOLVE_BENEATH) path, which
// follows symlinks that stay within the anchor; the fallback is only reached
// on systems that lack openat2, where faithfully reproducing contained
// symlink-following in userspace is not safe.
int open_beneath_fallback(int anchor_fd, std::filesystem::path const& relative, int flags, mode_t mode)
{
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
#else
  return open_beneath_fallback(anchor_fd, relative, flags, mode);
#endif
}

}  // namespace ava::core
