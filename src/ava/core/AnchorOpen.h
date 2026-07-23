#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/core/result.h"

#include <filesystem>
#include <sys/types.h>

namespace ava::core {

class AnchorSet;

// A file descriptor opened through an AnchorSet together with the anchor
// identity that located it. The descriptor is owned: the destructor calls
// close(2), and move transfers ownership; copy is disabled.
//
// For a path opened beneath a writable anchor, root() is that anchor's
// normalized root path and relative() is the candidate relative to it, so
// root() / relative() reconstructs the normalized absolute candidate. For a
// path opened outside all anchors (reads only), root() and relative() are
// empty and absolute() is the normalized candidate.
class AnchorOpen
{
 public:
  AnchorOpen() = default;
  AnchorOpen(AnchorOpen const&) = delete;
  AnchorOpen& operator=(AnchorOpen const&) = delete;
  AnchorOpen(AnchorOpen&& other) noexcept;
  AnchorOpen& operator=(AnchorOpen&& other) noexcept;
  ~AnchorOpen();

  [[nodiscard]] int fd() const noexcept;
  [[nodiscard]] std::filesystem::path const& root() const noexcept;
  [[nodiscard]] std::filesystem::path const& absolute() const noexcept;
  [[nodiscard]] std::filesystem::path const& relative() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  friend Result<AnchorOpen> open_writable(AnchorSet const&, std::filesystem::path const&, int, mode_t);
  friend Result<AnchorOpen> open_readable(AnchorSet const&, std::filesystem::path const&, int, mode_t);
  AnchorOpen(int fd, std::filesystem::path root, std::filesystem::path absolute, std::filesystem::path relative);

  int fd_ = -1;
  std::filesystem::path root_;
  std::filesystem::path absolute_;
  std::filesystem::path relative_;
};

// Open a candidate path the caller intends to write to. The candidate must
// lexically fall inside a writable anchor: the lexical permission layer is
// expected to guarantee this before calling, so a candidate outside all anchors
// is treated as a hard error here. The path is opened beneath the matching
// anchor with open_beneath, so any symlink that would escape the anchor is
// rejected with PermissionDenied. flags are the usual open(2) flags and may
// carry any access mode or creation options; O_CLOEXEC is forced on. mode is
// the creation mode used when O_CREAT is present (ignored otherwise).
[[nodiscard]] Result<AnchorOpen> open_writable(AnchorSet const& anchors, std::filesystem::path const& candidate, int flags, mode_t mode = 0);

// Open a candidate path the caller intends to read from. flags must request
// O_RDONLY access; O_WRONLY and O_RDWR are rejected with InvalidArgument. Paths
// inside a writable anchor are opened beneath that anchor with open_beneath
// (escaping symlinks rejected). Paths outside all anchors are resolved with
// held descriptors and may follow symlinks only while resolution remains
// physically outside every writable anchor. The final readable descriptor is
// reopened from the exact inspected identity; inspection failures fail closed.
// absolute() retains the normalized logical candidate rather than its physical
// resolution.
[[nodiscard]] Result<AnchorOpen> open_readable(AnchorSet const& anchors, std::filesystem::path const& candidate, int flags, mode_t mode = 0);

}  // namespace ava::core
