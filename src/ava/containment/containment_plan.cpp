#include "sys.h"
#include "ava/command/discovery.h"
#include "ava/containment/containment.h"

#include <algorithm>
#include <array>
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

// Landlock access masks and validation are implemented in landlock.cpp.
// Forward declarations for the detail helpers used here.
namespace detail {
[[nodiscard]] std::uint64_t handled_access_fs_mask(std::uint32_t abi_version) noexcept;
[[nodiscard]] std::uint64_t writable_root_mask(std::uint32_t abi_version) noexcept;
[[nodiscard]] std::uint64_t read_execute_root_mask() noexcept;
[[nodiscard]] std::uint64_t read_execute_file_root_mask() noexcept;
[[nodiscard]] std::uint64_t read_only_root_mask() noexcept;
std::uint64_t device_file_mask() noexcept;
[[nodiscard]] ava::core::VoidResult validate_synthetic_directory_root(std::filesystem::path const& path);
[[nodiscard]] ava::core::VoidResult validate_writable_directory_root(std::filesystem::path const& path);
[[nodiscard]] ava::core::VoidResult validate_read_only_toolchain_directory_root(std::filesystem::path const& path);
[[nodiscard]] ava::core::VoidResult validate_device_root(std::filesystem::path const& path);
[[nodiscard]] bool path_is_within(std::filesystem::path const& child, std::filesystem::path const& parent);
void add_filesystem_rule(std::vector<ContainmentFilesystemRule>& rules, std::filesystem::path const& path, std::uint64_t mask);
}  // namespace detail

// Landlock application is in landlock.cpp.
[[nodiscard]] ava::core::VoidResult apply_landlock_in_child(DevelopmentContainmentPlan const& plan);
// Seccomp application is in seccomp_network.cpp.
[[nodiscard]] ava::core::VoidResult apply_seccomp_network_filter();
[[nodiscard]] ava::core::VoidResult apply_no_new_privs();

namespace {

[[nodiscard]] std::filesystem::path canonical_path(std::filesystem::path const& path)
{
  std::error_code error;
  auto resolved = std::filesystem::canonical(path, error);
  if (error)
    return std::filesystem::absolute(path).lexically_normal();
  return resolved;
}

[[nodiscard]] bool path_is_directory(std::filesystem::path const& path) noexcept
{
  struct stat status{};
  if (::lstat(path.c_str(), &status) != 0)
    return false;
  return S_ISDIR(status.st_mode) && !S_ISLNK(status.st_mode);
}

void add_system_roots(std::vector<ContainmentFilesystemRule>& rules)
{
  // System roots that provide executables, shared libraries, and headers.
  // These are read/execute only; the child cannot modify system files.
  for (auto const* root : {"/usr", "/lib", "/lib64", "/bin", "/sbin", "/etc"})
  {
    if (path_is_directory(root))
      detail::add_filesystem_rule(rules, root, detail::read_execute_root_mask());
  }
}

void add_device_files(std::vector<ContainmentFilesystemRule>& rules, ContainmentFilesystemScope& scope)
{
  // Exact device files needed for normal process/file work. Read-only.
  for (auto const* dev : {"/dev/null", "/dev/zero", "/dev/urandom", "/dev/random"})
  {
    struct stat status{};
    if (::lstat(dev, &status) == 0 && !S_ISLNK(status.st_mode))
    {
      detail::add_filesystem_rule(rules, dev, detail::device_file_mask());
      ++scope.device_file_count;
    }
  }
}

}  // namespace

std::uint32_t probe_landlock_abi_version() noexcept
{
  errno = 0;
  long const result = ::syscall(SYS_landlock_create_ruleset, nullptr, 0u, LANDLOCK_CREATE_RULESET_VERSION);
  if (result < 0)
    return 0;
  return static_cast<std::uint32_t>(result);
}

DevelopmentContainmentPlan prepare_development_containment(ava::command::CommandPreparation const& preparation, bool network_enabled)
{
  DevelopmentContainmentPlan plan;
  plan.network_allowed = network_enabled;

  // Probe kernel features without mutating parent state.
  plan.landlock_abi_version = probe_landlock_abi_version();
  if (plan.landlock_abi_version < kRequiredLandlockAbiVersion)
  {
    plan.availability = ContainmentAvailability::Unavailable;
    plan.profile_id = "ava-landlock-seccomp-v1";
    plan.unavailable_reason = "Landlock ABI " + std::to_string(plan.landlock_abi_version) + " is below the required version " +
                              std::to_string(kRequiredLandlockAbiVersion) + " (truncate/refer not mediated)";
    return plan;
  }

  if (!network_enabled && !seccomp_network_filter_supported())
  {
    plan.availability = ContainmentAvailability::Unavailable;
    plan.profile_id = "ava-landlock-seccomp-v1";
    plan.unavailable_reason = "seccomp network filter is not supported on this architecture or kernel";
    return plan;
  }

  auto const& command_plan = preparation.plan();

  // Build the filesystem rule list from the sealed preparation.
  std::vector<ContainmentFilesystemRule> rules;
  ContainmentFilesystemScope scope;

  // 1. Workspace: read/write/execute (the child builds and tests here).
  //    The workspace may be 0755/0750 (group read/execute) but must reject
  //    group/world write, symlink, or identity mismatch.
  auto const workspace = canonical_path(command_plan.workspace());
  if (auto valid = detail::validate_writable_directory_root(workspace); !valid)
  {
    plan.availability = ContainmentAvailability::Unavailable;
    plan.profile_id = "ava-landlock-seccomp-v1";
    plan.unavailable_reason = "workspace root failed containment validation";
    return plan;
  }
  detail::add_filesystem_rule(rules, workspace, detail::writable_root_mask(plan.landlock_abi_version));
  scope.workspace_writable = true;

  // 2. Synthetic environment roots: read/write (HOME, XDG, TMP). These must
  //    be exact current-user 0700 (synthetic, private, owner-only).
  auto const& env = preparation.environment();
  for (auto const* root : {&env.synthetic_roots().home, &env.synthetic_roots().xdg_config_home, &env.synthetic_roots().xdg_cache_home,
                           &env.synthetic_roots().xdg_data_home, &env.synthetic_roots().xdg_state_home, &env.synthetic_roots().tmpdir})
  {
    auto const canonical = canonical_path(root->canonical_path);
    if (canonical.empty())
      continue;
    if (auto valid = detail::validate_synthetic_directory_root(canonical); !valid)
    {
      plan.availability = ContainmentAvailability::Unavailable;
      plan.profile_id = "ava-landlock-seccomp-v1";
      plan.unavailable_reason = "synthetic environment root failed containment validation";
      return plan;
    }
    detail::add_filesystem_rule(rules, canonical, detail::writable_root_mask(plan.landlock_abi_version));
  }
  scope.synthetic_environment_writable = true;

  // 3. Optional sealed rustup state: read/execute only. This root is
  // identity-bound at planning time and revalidated here before it becomes a
  // Landlock allow rule; no CARGO_HOME or broader trusted-home rule exists.
  if (auto const& rustup_home = command_plan.rustup_home_metadata())
  {
    auto fresh = ava::command::detail::path_metadata_is_fresh(*rustup_home);
    if (!fresh || !*fresh || canonical_path(rustup_home->canonical_path) != rustup_home->canonical_path ||
        detail::path_is_within(rustup_home->canonical_path, command_plan.workspace()) ||
        detail::path_is_within(command_plan.workspace(), rustup_home->canonical_path))
    {
      plan.availability = ContainmentAvailability::Unavailable;
      plan.profile_id = "ava-landlock-seccomp-v1";
      plan.unavailable_reason = "sealed rustup home changed or overlaps the workspace before containment preparation";
      return plan;
    }
    if (auto valid = detail::validate_read_only_toolchain_directory_root(rustup_home->canonical_path); !valid)
    {
      plan.availability = ContainmentAvailability::Unavailable;
      plan.profile_id = "ava-landlock-seccomp-v1";
      plan.unavailable_reason = "sealed rustup home failed containment validation";
      return plan;
    }
    detail::add_filesystem_rule(rules, rustup_home->canonical_path, detail::read_execute_root_mask());
    ++scope.read_only_root_count;
  }

  // 4. System roots: read/execute (executables, libraries, headers, config).
  add_system_roots(rules);

  // 5. Sealed PATH entries: read/execute (toolchain directories).
  for (auto const& entry : command_plan.path_entries())
  {
    auto const canonical = canonical_path(entry.directory);
    if (canonical.empty() || !path_is_directory(canonical))
      continue;
    detail::add_filesystem_rule(rules, canonical, detail::read_execute_root_mask());
    ++scope.read_only_root_count;
  }

  // 6. Resolved executable directory: read/execute (finding the binary).
  if (command_plan.resolved_executable())
  {
    auto const& exec_path = command_plan.resolved_executable()->executable.canonical_path;
    if (!exec_path.empty())
    {
      auto const exec_dir = exec_path.parent_path();
      if (!exec_dir.empty() && path_is_directory(exec_dir))
      {
        detail::add_filesystem_rule(rules, exec_dir, detail::read_execute_root_mask());
        ++scope.read_only_root_count;
      }
    }
  }

  // 7. Shebang interpreter directories: read/execute.
  if (command_plan.resolved_executable())
  {
    for (auto const& interpreter : command_plan.resolved_executable()->shebang_interpreters)
    {
      auto const& interp_path = interpreter.interpreter.canonical_path;
      if (interp_path.empty())
        continue;
      auto const interp_dir = interp_path.parent_path();
      if (!interp_dir.empty() && path_is_directory(interp_dir))
      {
        detail::add_filesystem_rule(rules, interp_dir, detail::read_execute_root_mask());
        ++scope.read_only_root_count;
      }
    }
  }

  // 8. Recipe path arguments: directories get writable access; regular files
  //    get file-valid read/execute only (never READ_DIR). Redundant rules
  //    already covered by the writable workspace are omitted to avoid
  //    stale or conflicting masks.
  if (command_plan.classification().recipe)
  {
    for (auto const& path_arg : command_plan.classification().recipe->path_arguments)
    {
      if (path_arg.canonical_path.empty())
        continue;
      // Omit recipe paths already beneath the writable workspace.
      if (detail::path_is_within(path_arg.canonical_path, workspace))
        continue;
      if (path_is_directory(path_arg.canonical_path))
      {
        if (auto valid = detail::validate_writable_directory_root(path_arg.canonical_path); valid)
          detail::add_filesystem_rule(rules, path_arg.canonical_path, detail::writable_root_mask(plan.landlock_abi_version));
      }
      else
      {
        // Regular file: use file-valid rights only (READ_FILE | EXECUTE),
        // never READ_DIR which Landlock rejects on non-directory paths.
        detail::add_filesystem_rule(rules, path_arg.canonical_path, detail::read_execute_file_root_mask());
      }
    }
  }

  // 9. Device files: read-only (/dev/null, /dev/zero, etc.).
  add_device_files(rules, scope);

  // Assemble the plan.
  plan.availability = ContainmentAvailability::Available;
  plan.profile_id = "ava-landlock-seccomp-v1";
  plan.handled_access_fs = detail::handled_access_fs_mask(plan.landlock_abi_version);
  plan.filesystem_rules = std::move(rules);
  plan.filesystem_scope = scope;
  // The filter is planned (not yet installed). Installation happens in the
  // child and is verified by the parent before exec.
  plan.network_filter_planned = !network_enabled;
  return plan;
}

ava::core::VoidResult apply_containment_in_child(DevelopmentContainmentPlan const& plan)
{
  if (plan.availability != ContainmentAvailability::Available)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "containment plan is not available"));

  // Set no_new_privs first. The kernel requires PR_SET_NO_NEW_PRIVS before
  // both landlock_restrict_self and seccomp filter installation. It is
  // inherited across execve and prevents privilege escalation through setuid.
  if (auto result = apply_no_new_privs(); !result)
    return result;

  // Install Landlock filesystem rules. Landlock is enforced as long as the
  // ruleset is properly installed and persists across execve.
  if (auto result = apply_landlock_in_child(plan); !result)
    return result;

  // Install the seccomp network filter only when network is denied.
  // For network-enabled commands, no_new_privs is still set but the seccomp
  // network filter is omitted; filesystem containment is retained.
  if (plan.network_filter_planned)
  {
    if (auto result = apply_seccomp_network_filter(); !result)
      return result;
  }

  return {};
}

void close_inherited_fds_except(int keep_fd_a, int keep_fd_b, int keep_fd_c) noexcept
{
  // Use close_range(2) where available for O(1) bulk closure, then fall back
  // to a bounded loop. The handshake fds and the approved executable
  // descriptor are never closed before descriptor exec.
  int const lo = STDERR_FILENO + 1;
  auto const close_range_available = [](unsigned int low, unsigned int high) { return ::syscall(SYS_close_range, low, high, 0u) == 0; };

  std::array<int, 3> keep_fds{keep_fd_a, keep_fd_b, keep_fd_c};
  std::ranges::sort(keep_fds);
  auto const unique_end = std::unique(keep_fds.begin(), keep_fds.end());
  unsigned int next = static_cast<unsigned int>(lo);
  for (auto keep_iterator = keep_fds.begin(); keep_iterator != unique_end; ++keep_iterator)
  {
    auto const keep_fd = *keep_iterator;
    if (keep_fd < lo)
      continue;
    auto const keep = static_cast<unsigned int>(keep_fd);
    if (next < keep)
      static_cast<void>(close_range_available(next, keep - 1));
    next = keep + 1;
  }
  // Close everything above the final kept descriptor (including very high
  // descriptors beyond the sysconf bound). keep_fd is an int, so next cannot
  // wrap here.
  static_cast<void>(close_range_available(next, ~0u));

  // Bounded fallback: iterate and close any remaining non-keep fds. This
  // catches cases where close_range is unavailable (ENOSYS) or a segment
  // failed. The bound prevents scanning millions of entries.
  long const open_max = ::sysconf(_SC_OPEN_MAX);
  int const max_fd = open_max > 0 ? static_cast<int>(open_max) : 1024;
  for (int fd = lo; fd < max_fd; ++fd)
  {
    if (fd == keep_fd_a || fd == keep_fd_b || fd == keep_fd_c)
      continue;
    static_cast<void>(::close(fd));
  }
}

bool containment_is_available(DevelopmentContainmentPlan const& plan) noexcept
{
  return plan.availability == ContainmentAvailability::Available;
}

std::string containment_summary(DevelopmentContainmentPlan const& plan)
{
  std::string summary = "{\"profile\":\"";
  summary += plan.profile_id;
  summary += "\",\"availability\":\"";
  summary += plan.availability == ContainmentAvailability::Available ? "available" : "unavailable";
  summary += "\",\"landlock_abi\":";
  summary += std::to_string(plan.landlock_abi_version);
  summary += ",\"network_allowed\":";
  summary += plan.network_allowed ? "true" : "false";
  // Before fork, the filter is "planned", not "installed". The parent only
  // reports "installed" after verifying the child's successful handshake.
  summary += ",\"network_filter\":\"";
  summary += plan.network_filter_planned ? "planned" : "not_required";
  summary += "\"";
  summary += ",\"filesystem\":{\"workspace_writable\":";
  summary += plan.filesystem_scope.workspace_writable ? "true" : "false";
  summary += ",\"synthetic_environment_writable\":";
  summary += plan.filesystem_scope.synthetic_environment_writable ? "true" : "false";
  summary += ",\"read_only_roots\":";
  summary += std::to_string(plan.filesystem_scope.read_only_root_count);
  summary += ",\"device_files\":";
  summary += std::to_string(plan.filesystem_scope.device_file_count);
  summary += "}}";
  return summary;
}

}  // namespace ava::containment
