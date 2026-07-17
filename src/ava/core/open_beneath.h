#pragma once

#include <filesystem>

namespace ava::core {

// Open a path relative to a trusted anchor directory descriptor, refusing to
// let the resolution escape the anchor. anchor_fd must be an open descriptor
// for a directory the caller already trusts (typically opened once at startup,
// before any untrusted input is processed); relative is resolved against it.
//
// Symbolic links are followed as long as the resolved path stays beneath the
// anchor. Any component that would escape the anchor (a ".." that climbs above
// it, an absolute symlink, or a symlink whose target lies outside the anchor)
// fails the call. flags are the usual open(2) flags; O_CLOEXEC is forced on
// the returned descriptor.
//
// Returns the opened descriptor on success, or -1 on failure with errno set.
// EXDEV indicates an escape from the anchor; ELOOP, ENOENT, etc. carry their
// usual meaning. On Linux 5.6+ this is implemented with openat2(RESOLVE_BENEATH);
// on systems without openat2 a component walk is used that rejects every
// symlink component, which is stricter than the openat2 path.
int open_beneath(int anchor_fd, std::filesystem::path const& relative, int flags);

}  // namespace ava::core
