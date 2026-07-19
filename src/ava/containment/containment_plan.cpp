#include "sys.h"
#include "ava/containment/containment.h"

#include <algorithm>
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
[[nodiscard]] std::uint64_t handled_access_fs_mask() noexcept;
[[nodiscard]] std::uint64_t writable_root_mask() noexcept;
[[nodiscard]] std::uint64_t read_execute_root_mask() noexcept;
[[nodiscard]] std::uint64_t read_only_root_mask() noexcept;
std::uint64_t device_file_mask() noexcept;
[[nodiscard]] ava::core::VoidResult validate_directory_root(std::filesystem::path const& path);
[[nodiscard]] ava::core::VoidResult validate_device_root(std::filesystem::path const& path);
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

[[nodiscard]] ava::core::VoidResult validate_writable_root(std::filesystem::path const& path)
{
  return detail::validate_directory_root(path);
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
    plan.unavailable_reason = "seccomp network filter is not supported on this architecture";
    return plan;
  }

  auto const& command_plan = preparation.plan();

  // Build the filesystem rule list from the sealed preparation.
  std::vector<ContainmentFilesystemRule> rules;
  ContainmentFilesystemScope scope;

  // 1. Workspace: read/write/execute (the child builds and tests here).
  auto const workspace = canonical_path(command_plan.workspace());
  if (auto valid = validate_writable_root(workspace); !valid)
  {
    plan.availability = ContainmentAvailability::Unavailable;
    plan.profile_id = "ava-landlock-seccomp-v1";
    plan.unavailable_reason = "workspace root failed containment validation";
    return plan;
  }
  detail::add_filesystem_rule(rules, workspace, detail::writable_root_mask());
  scope.workspace_writable = true;

  // 2. Synthetic environment roots: read/write (HOME, XDG, TMP).
  auto const& env = preparation.environment();
  for (auto const* root : {&env.synthetic_roots().home, &env.synthetic_roots().xdg_config_home, &env.synthetic_roots().xdg_cache_home,
                           &env.synthetic_roots().xdg_data_home, &env.synthetic_roots().xdg_state_home, &env.synthetic_roots().tmpdir})
  {
    auto const canonical = canonical_path(root->canonical_path);
    if (canonical.empty())
      continue;
    if (auto valid = validate_writable_root(canonical); !valid)
    {
      plan.availability = ContainmentAvailability::Unavailable;
      plan.profile_id = "ava-landlock-seccomp-v1";
      plan.unavailable_reason = "synthetic environment root failed containment validation";
      return plan;
    }
    detail::add_filesystem_rule(rules, canonical, detail::writable_root_mask());
  }
  scope.synthetic_environment_writable = true;

  // 3. System roots: read/execute (executables, libraries, headers, config).
  add_system_roots(rules);

  // 4. Sealed PATH entries: read/execute (toolchain directories).
  for (auto const& entry : command_plan.path_entries())
  {
    auto const canonical = canonical_path(entry.directory);
    if (canonical.empty() || !path_is_directory(canonical))
      continue;
    detail::add_filesystem_rule(rules, canonical, detail::read_execute_root_mask());
    ++scope.read_only_root_count;
  }

  // 5. Resolved executable directory: read/execute (finding the binary).
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

  // 6. Shebang interpreter directories: read/execute.
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

  // 7. Recipe path arguments: read/write (build/test directories need write
  //    access for artifacts). Only directories are added; file arguments get
  //    read/execute.
  if (command_plan.classification().recipe)
  {
    for (auto const& path_arg : command_plan.classification().recipe->path_arguments)
    {
      if (path_arg.canonical_path.empty())
        continue;
      if (path_is_directory(path_arg.canonical_path))
      {
        if (auto valid = validate_writable_root(path_arg.canonical_path); valid)
          detail::add_filesystem_rule(rules, path_arg.canonical_path, detail::writable_root_mask());
      }
      else
      {
        detail::add_filesystem_rule(rules, path_arg.canonical_path, detail::read_execute_root_mask());
      }
    }
  }

  // 8. Device files: read-only (/dev/null, /dev/zero, etc.).
  add_device_files(rules, scope);

  // Assemble the plan.
  plan.availability = ContainmentAvailability::Available;
  plan.profile_id = "ava-landlock-seccomp-v1";
  plan.handled_access_fs = detail::handled_access_fs_mask();
  plan.filesystem_rules = std::move(rules);
  plan.filesystem_scope = scope;
  plan.network_filter_installed = !network_enabled;
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
  if (plan.network_filter_installed)
  {
    if (auto result = apply_seccomp_network_filter(); !result)
      return result;
  }

  return {};
}

void close_inherited_fds_except(int keep_fd_a, int keep_fd_b) noexcept
{
  long const open_max = ::sysconf(_SC_OPEN_MAX);
  int const max_fd = open_max > 0 ? static_cast<int>(open_max) : 1024;
  for (int fd = STDERR_FILENO + 1; fd < max_fd; ++fd)
  {
    if (fd == keep_fd_a || fd == keep_fd_b)
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
  summary += ",\"network_filter\":";
  summary += plan.network_filter_installed ? "installed" : "not_installed";
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
