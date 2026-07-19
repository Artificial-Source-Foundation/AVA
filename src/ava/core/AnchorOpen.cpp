#include "sys.h"
#include "ava/core/AnchorOpen.h"
#include "ava/core/AnchorSet.h"
#include "ava/core/open_beneath.h"

#include <cerrno>
#include <deque>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "debug.h"

#ifndef O_PATH
#define O_PATH O_RDONLY
#endif

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

// Walk an absolute path component by component, checking each symlink's
// resolved target against the anchor set. Returns a Configuration error if
// any symlink in the path enters a writable anchor; returns nullopt if no
// symlink enters an anchor (or if the walk cannot proceed due to a missing
// or inaccessible component, in which case the subsequent open() will report
// the real error).
//
// This is the "enter-anchor" check for open_readable's external branch: the
// candidate is lexically outside all anchors, but a symlink along the path
// may redirect resolution into an anchor. Each symlink's target is resolved
// (relative targets against the link's parent directory, absolute targets
// from root), normalized, and checked with find_anchor. Chained symlinks
// (external -> external -> anchor) are caught by prepending the target's
// components to the worklist and continuing the walk.
std::optional<Error> external_path_enters_anchor(AnchorSet const& anchors, std::filesystem::path const& absolute)
{
  // Open root as the starting directory descriptor.
  int dir_fd = ::open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
  if (dir_fd < 0)
    return std::nullopt;

  // Worklist of path components still to process. Symlink targets are
  // prepended here so the walk continues from the resolved target.
  std::deque<std::string> remaining;
  for (auto const& c : absolute)
  {
    std::string s = c.string();
    if (!s.empty() && s != "/")
      remaining.push_back(s);
  }

  // Tracks the directory that dir_fd points to, used for resolving
  // relative symlink targets and for error reporting.
  std::filesystem::path current_path = "/";
  int symlink_count = 0;

  while (!remaining.empty())
  {
    std::string comp = remaining.front();
    remaining.pop_front();

    if (comp.empty() || comp == ".")
      continue;

    if (comp == "..")
    {
      int parent = ::openat(dir_fd, "..", O_PATH | O_DIRECTORY | O_CLOEXEC);
      if (parent >= 0)
      {
        ::close(dir_fd);
        dir_fd = parent;
        current_path = current_path.parent_path();
        if (current_path.empty())
          current_path = "/";
      }
      continue;
    }

    struct stat st;
    if (::fstatat(dir_fd, comp.c_str(), &st, AT_SYMLINK_NOFOLLOW) < 0)
      break;  // Component missing or inaccessible; let open() report it.

    if (S_ISLNK(st.st_mode))
    {
      if (++symlink_count > 40)
        break;  // ELOOP-like protection against symlink cycles.

      char buf[4096];
      ssize_t len = ::readlinkat(dir_fd, comp.c_str(), buf, sizeof(buf) - 1);
      if (len < 0)
        break;
      buf[len] = '\0';
      std::filesystem::path target(buf);
      std::filesystem::path link_path = current_path / comp;

      // Resolve the target: absolute targets from root, relative targets
      // against the link's parent directory (current_path).
      std::filesystem::path resolved;
      if (target.is_absolute())
      {
        resolved = target;
        // Reset to root for the continued walk.
        int root_fd = ::open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
        if (root_fd >= 0)
        {
          ::close(dir_fd);
          dir_fd = root_fd;
        }
        current_path = "/";
      }
      else
        resolved = current_path / target;

      // Check if the resolved target enters any writable anchor.
      std::filesystem::path normalized = resolved.lexically_normal();
      auto ref = anchors.find_anchor(normalized);
      if (ref.has_value())
      {
        ::close(dir_fd);
        auto error = Error(ErrorCategory::Configuration,
                           "external path contains a symlink that enters a writable anchor");
        error.with_context("path", absolute.string());
        error.with_context("symlink", link_path.string());
        error.with_context("target", normalized.string());
        error.with_context("anchor_root", ref->anchor().root.string());
        return error;
      }

      // The symlink target is outside all anchors. Prepend its components
      // to the worklist so the walk continues from the target, catching
      // chained symlinks (external -> external -> anchor).
      std::vector<std::string> target_components;
      for (auto const& c : target)
      {
        std::string s = c.string();
        if (!s.empty() && s != "/")
          target_components.push_back(s);
      }
      for (auto it = target_components.rbegin(); it != target_components.rend(); ++it)
        remaining.push_front(*it);

      // dir_fd and current_path stay at the symlink's parent (relative
      // targets) or root (absolute targets). The prepended components will
      // be processed relative to that position.
      continue;
    }

    if (S_ISDIR(st.st_mode))
    {
      int next = ::openat(dir_fd, comp.c_str(), O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      if (next < 0)
        break;
      ::close(dir_fd);
      dir_fd = next;
      current_path = current_path / comp;
      continue;
    }

    // Regular file (or other type). This should be the final component;
    // if not, open() will report ENOTDIR.
    break;
  }

  ::close(dir_fd);
  return std::nullopt;
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
  auto const& anchor = ref->anchor();
  auto const absolute = ref->absolute();
  int const fd = open_beneath(anchor.fd, ref->relative(), flags, mode);
  if (fd < 0)
    return std::unexpected(open_path_error("open_writable failed to open path beneath its anchor", errno, absolute, anchor.root));
  return AnchorOpen(fd, anchor.root, std::move(absolute), ref->relative());
}

Result<AnchorOpen> open_readable(AnchorSet const& anchors, std::filesystem::path const& candidate, int flags, mode_t mode)
{
  if ((flags & O_ACCMODE) != O_RDONLY)
    return std::unexpected(Error(ErrorCategory::InvalidArgument, "open_readable requires O_RDONLY access flags"));

  auto ref = anchors.find_anchor(candidate);
  if (ref)
  {
    auto const& anchor = ref->anchor();
    int const fd = open_beneath(anchor.fd, ref->relative(), flags, mode);
    if (fd < 0)
      return std::unexpected(open_path_error("open_readable failed to open path beneath its anchor", errno, ref->absolute(), anchor.root));
    return AnchorOpen(fd, anchor.root, ref->absolute(), ref->relative());
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

  // Reject the path if it contains symlinks that enter a writable anchor.
  // The candidate is lexically outside all anchors, but a symlink along the
  // path may redirect resolution into an anchor. This is a configuration
  // issue (the symlink's existence crosses the anchor safety boundary) rather
  // than a permission violation, so it is reported as Configuration.
  if (auto enters = external_path_enters_anchor(anchors, absolute))
    return std::unexpected(std::move(*enters));

  int const fd = ::open(absolute.c_str(), flags | O_CLOEXEC, mode);
  if (fd < 0)
    return std::unexpected(open_path_error("open_readable failed to open external path", errno, absolute, {}));
  return AnchorOpen(fd, {}, std::move(absolute), {});
}

}  // namespace ava::core
