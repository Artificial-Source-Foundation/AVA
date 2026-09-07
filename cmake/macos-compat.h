// AVA macOS compatibility shim.
//
// This header is force-included (`-include`) in every translation unit on
// Apple builds. It only activates under `__APPLE__` and only fills in symbols
// that macOS/BSD libc omits, so Linux toolchains are unaffected.
//
// Background: the pinned aicxx submodules (ai-utils `utils/Signals.cxx`) and
// several AVA translation units use Linux/glibc facilities that do not exist
// on macOS:
//
// * `environ` -- not declared by macOS headers (but provided by the linker).
// * sigemptyset/sigfillset/sigaddset/sigdelset/sigismember -- function-like
//   macros on macOS, unusable with a `::` qualifier (real functions exist).
// * O_PATH -- Linux-only; O_RDONLY|O_NONBLOCK preserves the metadata-only,
//   never-blocks-on-FIFO semantics.
// * fdatasync(2) -- missing; fsync(2) is a superset.
// * SIGRTMIN/SIGRTMAX -- macOS has no POSIX real-time signals. The range is
//   defined empty (NSIG..NSIG); AVA itself never reserves RT signals, so the
//   loops over that range are no-ops on macOS.
// * SIGSTKFLT, SIGPOLL, SIGPWR -- Linux-only signal numbers used in a signal
//   name switch. They get distinct sentinel values above NSIG so the cases
//   compile but can never match a real macOS signal.
// * sigorset(3) -- glibc extension OR-ing two sigsets; implemented inline.

#pragma once

#ifdef __APPLE__

#include <signal.h>

// macOS declares no global `environ` symbol in its headers; the dynamic
// linker still provides it, so declare it here for the glibc-style
// `::environ` uses in AVA sources and test helpers.
extern char** environ;

// macOS implements several sigset operations as function-like macros (which
// cannot be called with a `::` qualifier), while real functions of the same
// names also exist in libSystem. Drop the macros so the qualified calls used
// across AVA resolve to the real functions. The shim is force-included before
// any other header, so the later guarded <signal.h> includes are no-ops and
// cannot resurrect the macros.
#ifdef sigemptyset
#undef sigemptyset
#endif
#ifdef sigfillset
#undef sigfillset
#endif
#ifdef sigaddset
#undef sigaddset
#endif
#ifdef sigdelset
#undef sigdelset
#endif
#ifdef sigismember
#undef sigismember
#endif

#include <fcntl.h>
// Linux O_PATH has no macOS equivalent. O_RDONLY is the established fallback,
// and O_NONBLOCK is folded in because Linux O_PATH opens are metadata-only by
// construction: they never block on FIFOs (cf. the explicit O_PATH|O_NONBLOCK
// opens already used in discovery.cpp). Every O_PATH fd in AVA is used
// for fstat/dirfd/verification only, except the duplicate made by
// reopen_resolved_object, which explicitly clears O_NONBLOCK again.
#ifndef O_PATH
#define O_PATH (O_RDONLY | O_NONBLOCK)
#endif

#include <unistd.h>
// macOS implements fsync(2) but not fdatasync(2); fsync is a superset
// (it also flushes metadata), so it is the correct portable substitute.
#ifndef fdatasync
#define fdatasync fsync
#endif

// --- Real-time signals: empty range on macOS (no RT signals). ---
#ifndef SIGRTMIN
#define SIGRTMIN 32
#endif
#ifndef SIGRTMAX
#define SIGRTMAX 32
#endif

// --- Linux-only signal numbers: unreachable sentinels. ---
#ifndef SIGSTKFLT
#define SIGSTKFLT 100
#endif
#ifndef SIGPOLL
#define SIGPOLL 101
#endif
#ifndef SIGPWR
#define SIGPWR 102
#endif

// --- glibc sigorset(): dest = left | right. ---
#ifndef AVA_HAVE_SIGORSET
#define AVA_HAVE_SIGORSET 1
static inline int sigorset(sigset_t* dest, sigset_t const* left, sigset_t const* right) __attribute__((unused));
static inline int sigorset(sigset_t* dest, sigset_t const* left, sigset_t const* right)
{
  if (dest == 0 || left == 0 || right == 0)
    return -1;
  sigemptyset(dest);
  for (int signum = 1; signum < NSIG; ++signum)
  {
    int in_left = sigismember(left, signum);
    int in_right = sigismember(right, signum);
    if (in_left < 0 || in_right < 0)
      return -1;
    if (in_left == 1 || in_right == 1)
      if (sigaddset(dest, signum) != 0)
        return -1;
  }
  return 0;
}
#endif

#endif  // __APPLE__
