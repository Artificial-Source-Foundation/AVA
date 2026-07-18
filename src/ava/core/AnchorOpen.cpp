#include "sys.h"
#include "ava/core/AnchorOpen.h"
#include "ava/core/AnchorSet.h"
#include "ava/core/open_beneath.h"

#include <cerrno>
#include <string>
#include <system_error>
#include <utility>
#include <fcntl.h>
#include <unistd.h>
#include "debug.h"

namespace ava::core {
namespace {

// Build an error for a failed open, translating the errno into the category
// that matches its meaning for an anchor-resolved path: symlink escapes and
// loops are permission denials, a missing final component is NotFound, and
// everything else is a generic I/O failure. path is the candidate being
// opened; anchor_root is attached when the open was beneath a specific anchor
// (empty for external reads).
Error open_path_error(std::string const& message, int error_number, std::filesystem::path const& path, std::filesystem::path const& anchor_root = {})
{
  ErrorCategory const category = (error_number == ELOOP || error_number == EXDEV) ? ErrorCategory::PermissionDenied
                                                                                  : (error_number == ENOENT ? ErrorCategory::NotFound : ErrorCategory::Io);
  auto error = Error(category, message);
  if (!path.empty())
    error.with_context("path", path.string());
  if (!anchor_root.empty())
    error.with_context("anchor_root", anchor_root.string());
  if (error_number != 0)
    error.with_context("cause", std::generic_category().message(error_number));
  return error;
}

}  // namespace

AnchorOpen::AnchorOpen(int fd, std::filesystem::path root, std::filesystem::path absolute, std::filesystem::path relative)
    : fd_(fd), root_(std::move(root)), absolute_(std::move(absolute)), relative_(std::move(relative))
{
}

AnchorOpen::AnchorOpen(AnchorOpen&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)), root_(std::move(other.root_)), absolute_(std::move(other.absolute_)), relative_(std::move(other.relative_))
{
}

AnchorOpen& AnchorOpen::operator=(AnchorOpen&& other) noexcept
{
  if (this == &other)
    return *this;
  if (fd_ >= 0)
    ::close(fd_);
  fd_ = std::exchange(other.fd_, -1);
  root_ = std::move(other.root_);
  absolute_ = std::move(other.absolute_);
  relative_ = std::move(other.relative_);
  return *this;
}

AnchorOpen::~AnchorOpen()
{
  if (fd_ >= 0)
    ::close(fd_);
}

int AnchorOpen::fd() const noexcept
{
  return fd_;
}

std::filesystem::path const& AnchorOpen::root() const noexcept
{
  return root_;
}

std::filesystem::path const& AnchorOpen::absolute() const noexcept
{
  return absolute_;
}

std::filesystem::path const& AnchorOpen::relative() const noexcept
{
  return relative_;
}

Result<AnchorOpen> open_writable(AnchorSet const& anchors, std::filesystem::path const& candidate, int flags, mode_t mode)
{
  auto ref = anchors.find_anchor(candidate);
  if (!ref)
    return std::unexpected(std::move(ref.error()));
  auto const absolute = ref->root / ref->relative;
  int const fd = open_beneath(ref->fd, ref->relative, flags, mode);
  if (fd < 0)
    return std::unexpected(open_path_error("open_writable failed to open path beneath its anchor", errno, absolute, ref->root));
  return AnchorOpen(fd, std::move(ref->root), std::move(absolute), std::move(ref->relative));
}

Result<AnchorOpen> open_readable(AnchorSet const& anchors, std::filesystem::path const& candidate, int flags, mode_t mode)
{
  if ((flags & O_ACCMODE) != O_RDONLY)
    return std::unexpected(Error(ErrorCategory::InvalidArgument, "open_readable requires O_RDONLY access flags"));

  auto ref = anchors.find_anchor(candidate);
  if (ref)
  {
    auto const absolute = ref->root / ref->relative;
    int const fd = open_beneath(ref->fd, ref->relative, flags, mode);
    if (fd < 0)
      return std::unexpected(open_path_error("open_readable failed to open path beneath its anchor", errno, absolute, ref->root));
    return AnchorOpen(fd, std::move(ref->root), std::move(absolute), std::move(ref->relative));
  }

  // find_anchor returns PermissionDenied specifically when the candidate is
  // lexically outside all writable anchors (every other failure it reports is
  // a hard Io error such as an empty path or no anchors being available). That
  // case is an external read and is allowed; any other category is propagated.
  if (ref.error().category() != ErrorCategory::PermissionDenied)
    return std::unexpected(std::move(ref.error()));

  // Relative candidates resolve against the first anchor inside find_anchor and
  // therefore never reach this branch; only absolute candidates can be external.
  std::filesystem::path const absolute =
      candidate.is_absolute() ? candidate.lexically_normal() : (anchors.launch_workspace_root() / candidate).lexically_normal();

  /* Here: reject the path if it contains symlinks that enter a writable anchor.
   * A per-component readlinkat walk that lexically tests each resolved symlink
   * target against the anchor roots is required; openat2(RESOLVE_BENEATH) cannot
   * express "do not enter these subtrees" and RESOLVE_NO_SYMLINKS rejects all
   * symlinks (too strict — external->external symlinks must remain allowed).
   * Not implemented yet; must be added before reads through the anchor set are
   * relied on for confidentiality. */
  int const fd = ::open(absolute.c_str(), flags | O_CLOEXEC, mode);
  if (fd < 0)
    return std::unexpected(open_path_error("open_readable failed to open external path", errno, absolute, {}));
  return AnchorOpen(fd, {}, std::move(absolute), {});
}

}  // namespace ava::core
