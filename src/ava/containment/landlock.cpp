#include "sys.h"
#include "ava/containment/containment.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <linux/landlock.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace ava::containment {
namespace {

// All filesystem access rights that AVA must mediate. This includes every
// supported mutate right (write, truncate, remove, make_*, refer) plus the
// read/execute rights that are restricted to specific allow roots.
[[nodiscard]] std::uint64_t all_handled_access_fs() noexcept
{
  return LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR |
         LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_MAKE_CHAR | LANDLOCK_ACCESS_FS_MAKE_DIR |
         LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SOCK | LANDLOCK_ACCESS_FS_MAKE_FIFO | LANDLOCK_ACCESS_FS_MAKE_BLOCK |
         LANDLOCK_ACCESS_FS_MAKE_SYM | LANDLOCK_ACCESS_FS_REFER | LANDLOCK_ACCESS_FS_TRUNCATE;
}

// Full read/write/create/execute mask for writable roots (workspace, synthetic
// environment). The child can read, write, create, remove, and execute files
// beneath these roots.
[[nodiscard]] std::uint64_t read_write_execute_mask() noexcept
{
  return all_handled_access_fs();
}

// Read/execute mask for system roots and toolchain directories. The child can
// read files and list directories but not modify them.
[[nodiscard]] std::uint64_t read_execute_mask() noexcept
{
  return LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR;
}

// Read-only mask for device files.
[[nodiscard]] std::uint64_t read_only_mask() noexcept
{
  return LANDLOCK_ACCESS_FS_READ_FILE;
}

ava::core::Error landlock_error(std::string message)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, std::move(message));
  error.with_context("cause", std::strerror(errno));
  return error;
}

[[nodiscard]] int landlock_create_ruleset_fd(std::uint64_t handled_access_fs)
{
  struct landlock_ruleset_attr attr{};
  attr.handled_access_fs = handled_access_fs;
  attr.handled_access_net = 0;
  return static_cast<int>(::syscall(SYS_landlock_create_ruleset, &attr, sizeof(attr), 0));
}

[[nodiscard]] ava::core::VoidResult add_path_rule(int ruleset_fd, std::filesystem::path const& path, std::uint64_t access_mask)
{
  // Open the path with O_PATH | O_CLOEXEC so we get a descriptor without
  // requiring read/write permission on the target. O_NOFOLLOW prevents
  // symlink substitution: the rule is anchored to the exact directory.
  int const parent_fd = ::open(path.c_str(), O_PATH | O_CLOEXEC | O_NOFOLLOW);
  if (parent_fd < 0)
    return std::unexpected(landlock_error("failed to open Landlock rule parent path"));

  struct landlock_path_beneath_attr path_attr{};
  path_attr.allowed_access = access_mask;
  path_attr.parent_fd = parent_fd;

  int const result = static_cast<int>(::syscall(SYS_landlock_add_rule, ruleset_fd, LANDLOCK_RULE_PATH_BENEATH, &path_attr, 0));
  ::close(parent_fd);
  if (result < 0)
    return std::unexpected(landlock_error("failed to add Landlock filesystem rule"));
  return {};
}

}  // namespace

ava::core::VoidResult apply_landlock_in_child(DevelopmentContainmentPlan const& plan)
{
  if (plan.filesystem_rules.empty())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "containment plan has no filesystem rules"));

  int const ruleset_fd = landlock_create_ruleset_fd(plan.handled_access_fs);
  if (ruleset_fd < 0)
  {
    if (errno == EINVAL)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied,
                                              "Landlock ruleset creation rejected the handled access mask; kernel ABI may be insufficient"));
    return std::unexpected(landlock_error("failed to create Landlock ruleset"));
  }

  // Add each validated rule. If any rule fails, the ruleset is closed and the
  // child must not exec: fail closed before user code starts.
  for (auto const& rule : plan.filesystem_rules)
  {
    auto added = add_path_rule(ruleset_fd, rule.canonical_path, rule.access_mask);
    if (!added)
    {
      ::close(ruleset_fd);
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "Landlock rule installation failed; containment is not active"));
    }
  }

  int const restrict_result = static_cast<int>(::syscall(SYS_landlock_restrict_self, ruleset_fd, 0));
  ::close(ruleset_fd);
  if (restrict_result < 0)
    return std::unexpected(landlock_error("failed to enforce Landlock ruleset"));

  return {};
}

namespace {

// Validate that a path is an existing non-symlink directory owned by the
// current user with no group/other permissions. This is the same validation
// used by command planning for authority roots.
[[nodiscard]] ava::core::VoidResult validate_allow_root(std::filesystem::path const& path)
{
  struct stat status{};
  if (::lstat(path.c_str(), &status) != 0)
    return std::unexpected(landlock_error("failed to inspect Landlock allow root"));
  if (!S_ISDIR(status.st_mode))
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "Landlock allow root is not a directory"));
  if (S_ISLNK(status.st_mode))
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "Landlock allow root is a symlink"));
  if (status.st_uid != ::geteuid())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "Landlock allow root is not owned by the current user"));
  if ((status.st_mode & (S_IRWXG | S_IRWXO)) != 0)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "Landlock allow root is not owner-only"));
  return {};
}

// Validate that a path is an existing non-symlink file (regular or device).
[[nodiscard]] ava::core::VoidResult validate_device_file(std::filesystem::path const& path)
{
  struct stat status{};
  if (::lstat(path.c_str(), &status) != 0)
    return std::unexpected(landlock_error("failed to inspect Landlock device file"));
  if (S_ISLNK(status.st_mode))
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "Landlock device file is a symlink"));
  return {};
}

[[nodiscard]] std::filesystem::path canonicalize(std::filesystem::path const& path)
{
  std::error_code error;
  auto resolved = std::filesystem::canonical(path, error);
  if (error)
    return std::filesystem::absolute(path).lexically_normal();
  return resolved;
}

void add_rule(std::vector<ContainmentFilesystemRule>& rules, std::filesystem::path const& path, std::uint64_t mask)
{
  auto canonical = canonicalize(path);
  for (auto const& existing : rules)
  {
    if (existing.canonical_path == canonical)
      return;
  }
  rules.push_back({.canonical_path = std::move(canonical), .access_mask = mask});
}

}  // namespace

// These accessors are used by containment_plan.cpp to build the rule list.
// They are in an anonymous detail namespace within this translation unit.
namespace detail {

std::uint64_t handled_access_fs_mask() noexcept
{
  return all_handled_access_fs();
}

std::uint64_t writable_root_mask() noexcept
{
  return read_write_execute_mask();
}

std::uint64_t read_execute_root_mask() noexcept
{
  return read_execute_mask();
}

std::uint64_t read_only_root_mask() noexcept
{
  return read_only_mask();
}

std::uint64_t device_file_mask() noexcept
{
  // /dev/null needs WRITE_FILE for output redirection; /dev/zero and
  // /dev/urandom need READ_FILE. All device files get both.
  return LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_WRITE_FILE;
}

ava::core::VoidResult validate_directory_root(std::filesystem::path const& path)
{
  return validate_allow_root(path);
}

ava::core::VoidResult validate_device_root(std::filesystem::path const& path)
{
  return validate_device_file(path);
}

void add_filesystem_rule(std::vector<ContainmentFilesystemRule>& rules, std::filesystem::path const& path, std::uint64_t mask)
{
  add_rule(rules, path, mask);
}

}  // namespace detail

}  // namespace ava::containment
