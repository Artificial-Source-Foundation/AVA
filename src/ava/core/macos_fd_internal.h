#ifndef AVA_CORE_MACOS_FD_INTERNAL_H
#define AVA_CORE_MACOS_FD_INTERNAL_H

#ifdef __APPLE__

#include <algorithm>
#include <array>
#include <cerrno>
#include <span>
#include <utility>
#include <libproc.h>
#include <unistd.h>

namespace ava::core::detail {

// Only call in the single-threaded child after fork, with signal handlers
// reset: no other code may create descriptors while this function closes
// them. proc_pidinfo's libSystem wrapper enters the kernel without heap
// allocation or userspace locks. Querying the child itself also finds sparse
// descriptors above a subsequently lowered soft limit.
//
// Return false on an unavailable/inconsistent query so the caller can retain
// its conservative descriptor-table fallback. No user code has run yet.
[[nodiscard]] inline auto close_macos_fds_except(std::span<int const> keep) noexcept -> bool
{
  std::array<proc_fdinfo, 256> descriptors{};
  for (std::size_t batch = 0; batch < 4096; ++batch)
  {
    int const bytes = ::proc_pidinfo(::getpid(), PROC_PIDLISTFDS, 0, descriptors.data(), static_cast<int>(sizeof(descriptors)));
    if (bytes <= 0 || std::cmp_greater(bytes, sizeof(descriptors)) || static_cast<std::size_t>(bytes) % sizeof(proc_fdinfo) != 0)
    {
      return false;
    }
    auto const count = static_cast<std::size_t>(bytes) / sizeof(proc_fdinfo);
    bool closed_any = false;
    for (auto const& entry : std::span{descriptors}.first(count))
    {
      int const descriptor = entry.proc_fd;
      if (descriptor < 0)
      {
        return false;
      }
      if (descriptor <= STDERR_FILENO || std::ranges::find(keep, descriptor) != keep.end())
      {
        continue;
      }
      if (::close(descriptor) != 0 && errno != EBADF)
      {
        return false;
      }
      closed_any = true;
    }
    if (count < descriptors.size())
    {
      return true;
    }
    // A full batch may conceal more descriptors. Closing its unneeded
    // entries lets the next query reach them without a dynamically allocated
    // snapshot or dependence on the process-wide descriptor limit.
    if (!closed_any)
    {
      return false;
    }
  }
  return false;
}

}  // namespace ava::core::detail

#endif

#endif
