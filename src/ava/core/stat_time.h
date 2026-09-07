#pragma once

// Portable access to stat(2) timestamps.
//
// macOS names the timespec members st_atimespec/st_mtimespec/st_ctimespec
// while Linux (and POSIX.1-2008) names them st_atim/st_mtim/st_ctim. These
// inline accessors keep every use site portable without repeating #ifdefs.

#include <ctime>
#include <sys/stat.h>

namespace ava::core {

inline timespec stat_access_time(struct stat const& status) noexcept
{
#ifdef __APPLE__
  return status.st_atimespec;
#else
  return status.st_atim;
#endif
}

inline timespec stat_modification_time(struct stat const& status) noexcept
{
#ifdef __APPLE__
  return status.st_mtimespec;
#else
  return status.st_mtim;
#endif
}

inline timespec stat_change_time(struct stat const& status) noexcept
{
#ifdef __APPLE__
  return status.st_ctimespec;
#else
  return status.st_ctim;
#endif
}

}  // namespace ava::core
