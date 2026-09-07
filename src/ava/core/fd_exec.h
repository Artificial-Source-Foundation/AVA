#pragma once

// Descriptor-verified exec for macOS.
//
// Linux executes a verified open file description directly with fexecve(2),
// which is immune to pathname swaps between verification and exec. macOS has
// no fexecve(2)/execveat(2), and the kernel refuses to execute /dev/fd/N
// aliases (EACCES), so the closest equivalent is: resolve the fd with
// F_GETPATH, re-verify the full file identity at that path, and exec the path
// immediately. A same-uid attacker swapping the path in the residual window
// between the final check and execve is inherent to macOS (documented
// platform caveat); the identity check still fails closed for anything
// swapped before it.
//
// Only used on Apple builds; Linux call sites keep calling fexecve directly.

#ifdef __APPLE__

#include "ava/core/stat_time.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ava::core {

// Verify that the file at the fd's own path is the same file as the fd,
// then exec it. Never returns on success; returns -1 with errno set (EIO on
// identity mismatch) on failure, like the exec family.
inline int exec_verified_fd(int fd, char* const argv[], char* const environment[]) noexcept
{
  char exec_path[PATH_MAX];
  if (::fcntl(fd, F_GETPATH, exec_path) == -1)
    return -1;
  struct stat fd_status{};
  if (::fstat(fd, &fd_status) != 0)
    return -1;
  struct stat path_status{};
  if (::stat(exec_path, &path_status) != 0)
    return -1;
  timespec const fd_mtime = stat_modification_time(fd_status);
  timespec const path_mtime = stat_modification_time(path_status);
  timespec const fd_ctime = stat_change_time(fd_status);
  timespec const path_ctime = stat_change_time(path_status);
  if (fd_status.st_dev != path_status.st_dev || fd_status.st_ino != path_status.st_ino || fd_status.st_mode != path_status.st_mode ||
      fd_status.st_uid != path_status.st_uid || fd_status.st_gid != path_status.st_gid || fd_status.st_size != path_status.st_size ||
      fd_mtime.tv_sec != path_mtime.tv_sec || fd_mtime.tv_nsec != path_mtime.tv_nsec || fd_ctime.tv_sec != path_ctime.tv_sec ||
      fd_ctime.tv_nsec != path_ctime.tv_nsec)
  {
    errno = EIO;
    return -1;
  }
  ::execve(exec_path, argv, environment);
  return -1;
}

}  // namespace ava::core

#endif  // __APPLE__
