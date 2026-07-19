#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/command/command.h"
#include "ava/containment/containment.h"
#include "ava/agent/mode.h"
#include "ava/tools/bash_tool.h"
#include "ava/permissions/permission.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <grp.h>
#include <linux/landlock.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

namespace command = ava::command;
namespace containment = ava::containment;
namespace perms = ava::permissions;

bool landlock_available()
{
  return containment::probe_landlock_abi_version() >= containment::kRequiredLandlockAbiVersion;
}

bool python3_available()
{
  return std::filesystem::exists("/usr/bin/python3");
}

std::optional<std::string> environment_value(command::CommandEnvironment const& environment, std::string_view key)
{
  auto const found = std::ranges::find_if(environment.entries(), [key](command::EnvironmentVariable const& entry) { return entry.key == key; });
  if (found == environment.entries().end())
    return std::nullopt;
  return found->value;
}

std::optional<gid_t> private_primary_group_for_test()
{
  std::array<char, 16 * 1024> password_storage{};
  passwd account{};
  passwd* resolved_account = nullptr;
  if (::getpwuid_r(::geteuid(), &account, password_storage.data(), password_storage.size(), &resolved_account) != 0 || !resolved_account ||
      !resolved_account->pw_name || resolved_account->pw_name[0] == '\0')
  {
    return std::nullopt;
  }

  std::array<char, 16 * 1024> group_storage{};
  group primary_group{};
  group* resolved_group = nullptr;
  if (::getgrgid_r(resolved_account->pw_gid, &primary_group, group_storage.data(), group_storage.size(), &resolved_group) != 0 || !resolved_group ||
      !resolved_group->gr_name || std::string_view(resolved_group->gr_name) != resolved_account->pw_name)
  {
    return std::nullopt;
  }
  for (auto const* const* member = resolved_group->gr_mem; member && *member; ++member)
  {
    if (std::string_view(*member) != resolved_account->pw_name)
      return std::nullopt;
  }
  return resolved_group->gr_gid;
}

struct ContainmentFixture
{
  std::filesystem::path root;
  std::filesystem::path workspace;
  std::filesystem::path bin;
  std::filesystem::path build_dir;
  std::filesystem::path secret_file;
  std::filesystem::path ava_authority_root;

  explicit ContainmentFixture(std::string_view name, mode_t workspace_mode = S_IRWXU)
      : root(temp_root() / ("containment-" + std::string(name))),
        workspace(root / "workspace"),
        bin(root / "bin"),
        build_dir(workspace / "build"),
        secret_file(root / "secret" / "test-secret"),
        ava_authority_root(root / "ava-authority")
  {
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
    std::filesystem::create_directories(workspace);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(build_dir);
    std::filesystem::create_directories(secret_file.parent_path());
    std::filesystem::create_directories(ava_authority_root);
    ::chmod(temp_root().c_str(), S_IRWXU);
    ::chmod(root.c_str(), S_IRWXU);
    ::chmod(workspace.c_str(), workspace_mode);
    ::chmod(bin.c_str(), S_IRWXU);
    ::chmod(build_dir.c_str(), S_IRWXU);
    ::chmod(secret_file.parent_path().c_str(), S_IRWXU);
    ::chmod(ava_authority_root.c_str(), S_IRWXU);
    {
      std::ofstream f(secret_file, std::ios::binary | std::ios::trunc);
      f << "secret-data\n";
    }
    ::chmod(secret_file.c_str(), S_IRUSR | S_IWUSR);
  }

  bool write_executable(std::string_view name, std::string_view body)
  {
    auto path = bin / std::string(name);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << body;
    f.close();
    return ::chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR) == 0;
  }

  ava::tools::ToolContext make_context() { return ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}; }

  ava::tools::ToolContext make_allow_context()
  {
    return ava::tools::ToolContext{.workspace_dir = workspace,
                                   .mode = ava::agent::Mode::Build,
                                   .permission_resolver = [](perms::PermissionPrompt const&) -> ava::core::Result<perms::PermissionResolutionDecision> {
                                     return perms::PermissionResolution::Allow;
                                   }};
  }
};

struct RustupContainmentFixture
{
  std::filesystem::path root;
  std::filesystem::path workspace;
  std::filesystem::path trusted_home;
  std::filesystem::path cargo_bin;
  std::filesystem::path rustup_home;
  std::filesystem::path cargo_credentials;
  std::filesystem::path synthetic_root;
  std::filesystem::path outcome;

  explicit RustupContainmentFixture(std::string_view name)
      : root(temp_root() / ("rustup-containment-" + std::string(name))),
        workspace(root / "workspace"),
        trusted_home(root / "trusted-home"),
        cargo_bin(trusted_home / ".cargo" / "bin"),
        rustup_home(trusted_home / ".rustup"),
        cargo_credentials(trusted_home / ".cargo" / "credentials"),
        synthetic_root(root / "synthetic"),
        outcome(workspace / "rustup-outcome")
  {
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
    std::filesystem::create_directories(root.parent_path());
    ::chmod(root.parent_path().c_str(), S_IRWXU);
    for (auto const& directory : {workspace, trusted_home, cargo_bin, rustup_home, synthetic_root / "home", synthetic_root / "config", synthetic_root / "cache",
                                  synthetic_root / "data", synthetic_root / "state", synthetic_root / "tmp"})
    {
      std::filesystem::create_directories(directory);
      ::chmod(directory.c_str(), S_IRWXU);
    }
    ::chmod(root.c_str(), S_IRWXU);
    {
      std::ofstream settings(rustup_home / "settings-marker", std::ios::binary | std::ios::trunc);
      settings << "rustup-settings-marker\n";
    }
    {
      std::ofstream credentials(cargo_credentials, std::ios::binary | std::ios::trunc);
      credentials << "cargo-secret-sentinel\n";
    }
    ::chmod((rustup_home / "settings-marker").c_str(), S_IRUSR | S_IWUSR);
    ::chmod(cargo_credentials.c_str(), S_IRUSR | S_IWUSR);
  }

  bool write_rustup_proxy()
  {
    auto const rustup = cargo_bin / "rustup";
    std::ofstream output(rustup, std::ios::binary | std::ios::trunc);
    output << "#!/bin/sh\n"
              "out='"
           << outcome.string()
           << "'\n"
              ": > \"$out\"\n"
              "if [ -n \"$RUSTUP_HOME\" ] && IFS= read -r marker < \"$RUSTUP_HOME/settings-marker\"; then echo RUSTUP_READ_OK >> \"$out\"; else echo "
              "RUSTUP_READ_BAD >> \"$out\"; fi\n"
              "if [ \"${CARGO_HOME+x}\" = x ]; then echo CARGO_HOME_BAD >> \"$out\"; else echo CARGO_HOME_ABSENT >> \"$out\"; fi\n"
              "if printf blocked > \"$RUSTUP_HOME/write-marker\" 2>/dev/null; then echo RUSTUP_WRITE_BAD >> \"$out\"; else echo RUSTUP_WRITE_BLOCKED >> "
              "\"$out\"; fi\n"
              "if IFS= read -r secret < '"
           << cargo_credentials.string()
           << "'; then echo CARGO_SECRET_BAD >> \"$out\"; else echo CARGO_SECRET_BLOCKED >> \"$out\"; fi\n"
              "exit 0\n";
    output.close();
    if (::chmod(rustup.c_str(), S_IRUSR | S_IWUSR | S_IXUSR) != 0)
      return false;
    std::error_code link_error;
    std::filesystem::create_symlink("rustup", cargo_bin / "cargo", link_error);
    return !link_error;
  }

  command::CommandBuildOptions options() const
  {
    return command::CommandBuildOptions{.workspace = workspace,
                                        .trusted_home = trusted_home,
                                        .startup_path = cargo_bin.string(),
                                        .shell = "/bin/sh",
                                        .ava_authority_roots = {},
                                        .environment = command::CommandEnvironmentOptions{.profile_id = "rustup-containment-test",
                                                                                          .user = "ava-test-user",
                                                                                          .logname = "ava-test-user",
                                                                                          .home = synthetic_root / "home",
                                                                                          .xdg_config_home = synthetic_root / "config",
                                                                                          .xdg_cache_home = synthetic_root / "cache",
                                                                                          .xdg_data_home = synthetic_root / "data",
                                                                                          .xdg_state_home = synthetic_root / "state",
                                                                                          .tmpdir = synthetic_root / "tmp"},
                                        .workspace_script_recipes = {},
                                        .limits = {}};
  }
};

// ---------------------------------------------------------------------------
// Unit-level tests: these run without Landlock and are never skipped.
// ---------------------------------------------------------------------------

void test_containment_summary_redacts_paths()
{
  ContainmentFixture fix("summary");
  auto plan = containment::DevelopmentContainmentPlan{};
  plan.availability = containment::ContainmentAvailability::Available;
  plan.profile_id = "ava-landlock-seccomp-v1";
  plan.network_allowed = false;
  plan.network_filter_planned = true;
  plan.filesystem_scope.workspace_writable = true;
  auto summary = containment::containment_summary(plan);
  expect(summary.find("available") != std::string::npos, "containment summary shows availability");
  expect(summary.find("\"network_allowed\":false") != std::string::npos, "containment summary shows network denied");
  expect(summary.find("\"network_filter\":\"planned\"") != std::string::npos, "containment summary says planned before fork");
  expect(summary.find(fix.workspace.string()) == std::string::npos, "containment summary does not leak workspace paths");
  expect(summary.find(fix.secret_file.string()) == std::string::npos, "containment summary does not leak secret paths");
}

void test_containment_fd_closure_keeps_bound_executable()
{
  std::array<int, 2> report{-1, -1};
  bool const report_pipe_created = ::pipe2(report.data(), O_CLOEXEC) == 0;
  int const handshake_fd = report_pipe_created ? ::open("/dev/null", O_RDONLY | O_CLOEXEC) : -1;
  int const executable_fd = report_pipe_created ? ::open("/dev/null", O_RDONLY | O_CLOEXEC) : -1;
  int const inherited_fd = report_pipe_created ? ::open("/dev/null", O_RDONLY | O_CLOEXEC) : -1;
  std::array<int, 4> child_state{-1, -1, -1, -1};
  pid_t const child = report_pipe_created && handshake_fd >= 0 && executable_fd >= 0 && inherited_fd >= 0 ? ::fork() : -1;
  if (child == 0)
  {
    static_cast<void>(::close(report[0]));
    containment::close_inherited_fds_except(report[1], handshake_fd, executable_fd);
    child_state = {::fcntl(report[1], F_GETFD), ::fcntl(handshake_fd, F_GETFD), ::fcntl(executable_fd, F_GETFD), ::fcntl(inherited_fd, F_GETFD)};
    static_cast<void>(::write(report[1], child_state.data(), sizeof(child_state)));
    _exit(0);
  }
  if (report[1] >= 0)
    static_cast<void>(::close(report[1]));
  if (handshake_fd >= 0)
    static_cast<void>(::close(handshake_fd));
  if (executable_fd >= 0)
    static_cast<void>(::close(executable_fd));
  if (inherited_fd >= 0)
    static_cast<void>(::close(inherited_fd));

  std::array<int, 4> observed{-1, -1, -1, -1};
  auto const bytes = report_pipe_created ? ::read(report[0], observed.data(), sizeof(observed)) : -1;
  if (report[0] >= 0)
    static_cast<void>(::close(report[0]));
  int status = 0;
  if (child > 0)
    static_cast<void>(::waitpid(child, &status, 0));
  expect(report_pipe_created && child > 0 && bytes == static_cast<ssize_t>(sizeof(observed)) && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
             observed[0] >= 0 && observed[1] >= 0 && observed[2] >= 0 && observed[3] == -1,
         "containment fd closure preserves handshake and approved-executable descriptors while closing inherited descriptors");
}

void test_unavailable_probe_forces_ask()
{
  ContainmentFixture fix("unavailable-ask");
  fix.write_executable("cmake", "#!/bin/sh\nexit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  int prompts = 0;
  bool critical_risk = false;
  ava::tools::ToolContext ctx{
      .workspace_dir = fix.workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&prompts, &critical_risk](perms::PermissionPrompt const& p) -> ava::core::Result<perms::PermissionResolutionDecision> {
        ++prompts;
        if (p.command_metadata && !p.command_metadata->containment_available)
        {
          critical_risk = (p.risk == perms::PermissionRisk::Critical);
          return perms::PermissionResolution::Allow;
        }
        return perms::PermissionResolution::Deny;
      }};
  auto result =
      ava::tools::run_bash(ctx, "cmake --build " + fix.build_dir.generic_string(), ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  if (landlock_available())
  {
    expect(result && result->exit_code == 0, "when Landlock is available, standard mutable command auto-allows without resolver");
  }
  else
  {
    // Unavailable containment must downgrade to Critical risk Ask, not
    // ordinary High risk Ask. After explicit one-shot approval it runs
    // uncontained, but metadata must state unavailable/uncontained.
    expect(result && prompts == 1, "when containment is unavailable, standard mutable command downgrades to Ask");
    expect(critical_risk, "unavailable containment downgrades to Critical risk, not ordinary High");
    if (result)
    {
      expect(!result->containment_applied, "unavailable containment reports not applied in result");
    }
  }
}

void test_sensitive_network_enabled_remains_ask()
{
  ContainmentFixture fix("sensitive-network");
  fix.write_executable("curl", "#!/bin/sh\necho curl-ran\nexit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  int prompts = 0;
  bool network_allowed_in_metadata = false;
  ava::tools::ToolContext ctx{.workspace_dir = fix.workspace,
                              .mode = ava::agent::Mode::Build,
                              .permission_resolver = [&prompts, &network_allowed_in_metadata](
                                                         perms::PermissionPrompt const& p) -> ava::core::Result<perms::PermissionResolutionDecision> {
                                ++prompts;
                                if (p.command_metadata)
                                {
                                  network_allowed_in_metadata = p.command_metadata->containment_network_allowed;
                                }
                                return perms::PermissionResolution::Allow;
                              }};
  auto result = ava::tools::run_bash(ctx, "curl https://example.com", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0 && prompts == 1, "sensitive network-enabled command remains Ask");
  expect(network_allowed_in_metadata, "sensitive network-enabled command metadata states network permitted after approval");
}

void test_critical_raw_remains_ask_no_containment()
{
  ContainmentFixture fix("critical-raw");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  int prompts = 0;
  bool containment_not_required = false;
  ava::tools::ToolContext ctx{
      .workspace_dir = fix.workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&prompts, &containment_not_required](perms::PermissionPrompt const& p) -> ava::core::Result<perms::PermissionResolutionDecision> {
        ++prompts;
        if (p.command_metadata)
        {
          containment_not_required = p.command_metadata->containment_status == perms::CommandContainmentStatus::NotRequired ||
                                     p.command_metadata->containment_status == perms::CommandContainmentStatus::Unavailable;
        }
        return perms::PermissionResolution::Deny;
      }};
  auto result = ava::tools::run_bash(ctx, "printf raw; true", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(!result && prompts == 1 && containment_not_required, "critical/raw command remains Ask once with no containment claim in metadata");
}

void test_home_as_workspace_rejected()
{
  // Reject a workspace that equals or is an ancestor of the trusted real home.
  // Ordinary projects nested under home are allowed.
  char const* const home_env = std::getenv("HOME");
  if (!home_env || home_env[0] == '\0')
  {
    ava::test::request_skip("cannot determine trusted home for boundary test");
    return;
  }
  std::filesystem::path const home(home_env);
  // Create a project directory under home for the positive case.
  std::error_code ec;
  auto project_under_home = std::filesystem::path(home) / "ava-test-project-boundary";
  std::filesystem::create_directories(project_under_home, ec);
  ::chmod(project_under_home.c_str(), S_IRWXU);
  // Synthetic roots must be disjoint from workspace and trusted_home; use
  // temp_root() so they never overlap.
  auto synth_base = temp_root() / "ava-boundary-synth";
  std::filesystem::create_directories(synth_base, ec);
  ::chmod(synth_base.c_str(), S_IRWXU);
  for (auto const* d : {"home", "cfg", "cache", "data", "state", "tmp"})
  {
    auto p = synth_base / d;
    std::filesystem::create_directories(p, ec);
    ::chmod(p.c_str(), S_IRWXU);
  }
  command::CommandLimits limits;
  command::CommandBuildOptions opts{.workspace = home,
                                    .trusted_home = home,
                                    .startup_path = "/usr/bin:/bin",
                                    .shell = "/bin/sh",
                                    .ava_authority_roots = {},
                                    .environment = command::CommandEnvironmentOptions{.profile_id = "test",
                                                                                      .user = "test",
                                                                                      .logname = "test",
                                                                                      .home = synth_base / "home",
                                                                                      .xdg_config_home = synth_base / "cfg",
                                                                                      .xdg_cache_home = synth_base / "cache",
                                                                                      .xdg_data_home = synth_base / "data",
                                                                                      .xdg_state_home = synth_base / "state",
                                                                                      .tmpdir = synth_base / "tmp"},
                                    .workspace_script_recipes = {},
                                    .limits = limits};
  auto intent = command::CommandIntent::compatibility("ls", limits);
  auto plan = command::seal_command_plan(*intent, opts);
  expect(!plan, "sealing rejects workspace that equals the trusted real home");

  // Positive: a project nested under home is allowed.
  opts.workspace = project_under_home;
  auto nested_intent = command::CommandIntent::compatibility("ls", limits);
  auto nested_plan = command::seal_command_plan(*nested_intent, opts);
  expect(nested_plan.has_value(), "sealing allows ordinary projects nested under home");

  std::filesystem::remove_all(project_under_home, ec);
  std::filesystem::remove_all(synth_base, ec);
}

void test_authority_overlap_rejected()
{
  ContainmentFixture fix("authority-overlap");
  // Reject workspace that overlaps with an AVA authority root.
  command::CommandLimits limits;
  auto synth_root = fix.root / "synth";
  std::error_code ec;
  for (auto const* d : {"home", "cfg", "cache", "data", "state", "tmp"}) std::filesystem::create_directories(synth_root / d, ec);
  command::CommandBuildOptions opts{.workspace = fix.ava_authority_root,
                                    .trusted_home = fix.root / "trusted-home",
                                    .startup_path = "/usr/bin:/bin",
                                    .shell = "/bin/sh",
                                    .ava_authority_roots = {fix.ava_authority_root},
                                    .environment = command::CommandEnvironmentOptions{.profile_id = "test",
                                                                                      .user = "test",
                                                                                      .logname = "test",
                                                                                      .home = synth_root / "home",
                                                                                      .xdg_config_home = synth_root / "cfg",
                                                                                      .xdg_cache_home = synth_root / "cache",
                                                                                      .xdg_data_home = synth_root / "data",
                                                                                      .xdg_state_home = synth_root / "state",
                                                                                      .tmpdir = synth_root / "tmp"},
                                    .workspace_script_recipes = {},
                                    .limits = limits};
  std::filesystem::create_directories(opts.trusted_home, ec);
  ::chmod(opts.trusted_home.c_str(), S_IRWXU);
  auto intent = command::CommandIntent::compatibility("ls", limits);
  auto plan = command::seal_command_plan(*intent, opts);
  expect(!plan, "sealing rejects workspace that overlaps with an AVA authority root");
}

// ---------------------------------------------------------------------------
// Feature probe and Landlock-dependent tests.
// ---------------------------------------------------------------------------

void test_feature_probe()
{
  auto const abi = containment::probe_landlock_abi_version();
  auto const seccomp_ok = containment::seccomp_network_filter_supported();
  if (abi >= containment::kRequiredLandlockAbiVersion && seccomp_ok)
  {
    expect(abi >= 3, "Landlock ABI version meets the minimum requirement for truncate/refer mediation");
    expect(seccomp_ok, "seccomp network filter is supported on this architecture");
  }
  else
  {
    ava::test::request_skip("kernel lacks required Landlock ABI or seccomp support");
  }
}

void test_contained_command_writes_workspace()
{
  if (!landlock_available())
  {
    ava::test::request_skip("Landlock ABI insufficient");
    return;
  }
  ContainmentFixture fix("writes-workspace");
  fix.write_executable("cmake", "#!/bin/sh\necho cmake-ran > " + (fix.build_dir / "marker.txt").string() + "\nexit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  auto result = ava::tools::run_bash(fix.make_context(), "cmake --build " + fix.build_dir.generic_string(),
                                     ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0, "contained standard project command succeeds under development containment");
  expect(std::filesystem::exists(fix.build_dir / "marker.txt"), "contained command writes to workspace build directory");
}

void test_contained_command_writes_private_primary_group_workspace()
{
  if (!landlock_available())
  {
    ava::test::request_skip("Landlock ABI insufficient");
    return;
  }
  auto const private_group = private_primary_group_for_test();
  if (!private_group)
    return;

  ContainmentFixture fix("writes-private-primary-group", S_IRWXU | S_IRWXG);
  if (::chown(fix.workspace.c_str(), ::geteuid(), *private_group) != 0)
  {
    expect(false, "private-primary-group containment fixture changes workspace group");
    return;
  }
  fix.write_executable("cmake", "#!/bin/sh\necho cmake-ran-private-group > " + (fix.build_dir / "marker.txt").string() + "\nexit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  auto result = ava::tools::run_bash(fix.make_context(), "cmake --build " + fix.build_dir.generic_string(),
                                     ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0 && std::filesystem::exists(fix.build_dir / "marker.txt"),
         "contained standard project command succeeds under a private-primary-group 0770 workspace");
  if (result)
  {
    expect(result->containment_applied, "private-primary-group workspace containment is applied and reported in result");
  }
}

void test_contained_command_writes_0755_workspace()
{
  if (!landlock_available())
  {
    ava::test::request_skip("Landlock ABI insufficient");
    return;
  }
  // A writable workspace may be current-user-owned 0755 (group read/execute)
  // but must still pass containment validation.
  ContainmentFixture fix("writes-0755", S_IRWXU | S_IRGRP | S_IXGRP);
  fix.write_executable("cmake", "#!/bin/sh\necho cmake-ran-0755 > " + (fix.build_dir / "marker.txt").string() + "\nexit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  auto result = ava::tools::run_bash(fix.make_context(), "cmake --build " + fix.build_dir.generic_string(),
                                     ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0, "contained standard project command succeeds under 0755 workspace");
  expect(std::filesystem::exists(fix.build_dir / "marker.txt"), "contained command writes to 0755 workspace build directory");
  if (result)
  {
    expect(result->containment_applied, "0755 workspace containment is applied and reported in result");
  }
}

void test_contained_command_reads_system_libs()
{
  if (!landlock_available())
  {
    ava::test::request_skip("Landlock ABI insufficient");
    return;
  }
  ContainmentFixture fix("reads-system");
  fix.write_executable("cmake", "#!/bin/sh\ncat /etc/passwd > /dev/null 2>&1 && echo SYS_READ_OK || echo SYS_READ_FAIL\nexit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  auto result = ava::tools::run_bash(fix.make_context(), "cmake --build " + fix.build_dir.generic_string(),
                                     ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0 && result->output.find("SYS_READ_OK") != std::string::npos,
         "contained command can read system files under allowed system roots");
}

void test_contained_command_cannot_read_secret()
{
  if (!landlock_available())
  {
    ava::test::request_skip("Landlock ABI insufficient");
    return;
  }
  ContainmentFixture fix("cannot-read-secret");
  fix.write_executable("cmake", "#!/bin/sh\ncat " + fix.secret_file.string() + " > /dev/null 2>&1 && echo SECRET_READ_BAD || echo SECRET_BLOCKED\nexit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  auto result = ava::tools::run_bash(fix.make_context(), "cmake --build " + fix.build_dir.generic_string(),
                                     ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0 && result->output.find("SECRET_BLOCKED") != std::string::npos,
         "contained command cannot read a test secret file outside the workspace");
}

void test_contained_rustup_cargo_reads_only_sealed_rustup_home()
{
  if (!landlock_available())
  {
    ava::test::request_skip("Landlock ABI insufficient");
    return;
  }

  RustupContainmentFixture fix("cargo-proxy");
  bool const proxy_written = fix.write_rustup_proxy();
  auto const intent = command::CommandIntent::structured({"cargo", "check"});
  auto preparation = proxy_written ? command::prepare_command(*intent, fix.options())
                                   : ava::core::Result<command::CommandPreparation>{
                                         std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create fake rustup proxy"))};
  auto containment_plan = preparation ? containment::prepare_development_containment(*preparation, false) : containment::DevelopmentContainmentPlan{};
  auto const permission =
      preparation ? perms::decide(perms::command_permission_metadata(preparation->plan(), {.available = containment::containment_is_available(containment_plan),
                                                                                           .profile_id = containment_plan.profile_id,
                                                                                           .network_allowed = containment_plan.network_allowed}))
                  : perms::PermissionDecision{.action = perms::PermissionAction::Ask, .reason = {}};

  auto const read_execute_mask = LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR;
  auto const rustup_rule = preparation ? std::ranges::find_if(containment_plan.filesystem_rules,
                                                              [&preparation](containment::ContainmentFilesystemRule const& rule) {
                                                                return rule.canonical_path == preparation->plan().rustup_home_metadata()->canonical_path;
                                                              })
                                       : containment_plan.filesystem_rules.end();
  std::size_t expected_read_only_roots = 0;
  if (preparation && preparation->plan().resolved_executable())
  {
    expected_read_only_roots = preparation->plan().path_entries().size() + 1U + preparation->plan().resolved_executable()->shebang_interpreters.size() + 1U;
  }

  int child_status = -1;
  if (preparation && containment::containment_is_available(containment_plan) && rustup_rule != containment_plan.filesystem_rules.end())
  {
    pid_t const child = ::fork();
    if (child == 0)
    {
      if (auto applied = containment::apply_containment_in_child(containment_plan); !applied)
        _exit(90);
      if (::chdir(preparation->plan().cwd().c_str()) != 0)
        _exit(91);
      std::vector<std::string> environment_strings;
      environment_strings.reserve(preparation->environment().entries().size());
      for (auto const& entry : preparation->environment().entries()) environment_strings.push_back(entry.key + "=" + entry.value);
      std::vector<char*> environment;
      environment.reserve(environment_strings.size() + 1U);
      for (auto& entry : environment_strings) environment.push_back(entry.data());
      environment.push_back(nullptr);
      std::array<char*, 3> argv{const_cast<char*>("cargo"), const_cast<char*>("check"), nullptr};
      auto const executable = preparation->plan().resolved_executable()->executable.canonical_path.string();
      ::execve(executable.c_str(), argv.data(), environment.data());
      _exit(92);
    }
    if (child > 0)
      static_cast<void>(::waitpid(child, &child_status, 0));
  }

  std::ifstream outcome_input(fix.outcome, std::ios::binary);
  std::string outcome((std::istreambuf_iterator<char>(outcome_input)), std::istreambuf_iterator<char>());
  expect(proxy_written && preparation && preparation->plan().rustup_home_metadata() && preparation->environment().rustup_home_metadata() &&
             environment_value(preparation->environment(), "RUSTUP_HOME") == std::optional<std::string>{fix.rustup_home.string()} &&
             !environment_value(preparation->environment(), "CARGO_HOME") && preparation->plan().classification().level == command::CommandLevel::Standard &&
             permission.action == perms::PermissionAction::Allow && containment::containment_is_available(containment_plan) &&
             !containment_plan.network_allowed && containment_plan.network_filter_planned && rustup_rule != containment_plan.filesystem_rules.end() &&
             rustup_rule->access_mask == read_execute_mask && containment_plan.filesystem_scope.read_only_root_count == expected_read_only_roots &&
             WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0 && outcome.find("RUSTUP_READ_OK") != std::string::npos &&
             outcome.find("CARGO_HOME_ABSENT") != std::string::npos && outcome.find("RUSTUP_WRITE_BLOCKED") != std::string::npos &&
             outcome.find("CARGO_SECRET_BLOCKED") != std::string::npos && !std::filesystem::exists(fix.rustup_home / "write-marker"),
         "contained symlinked cargo/rustup is Standard with no prompt, receives only read/execute access to sealed RUSTUP_HOME, reads its marker, cannot write "
         "there or read .cargo credentials, and remains network denied");
}

void test_contained_command_cannot_write_ava_authority()
{
  if (!landlock_available())
  {
    ava::test::request_skip("Landlock ABI insufficient");
    return;
  }
  ContainmentFixture fix("cannot-write-authority");
  auto const authority_marker = fix.ava_authority_root / "marker.txt";
  fix.write_executable("cmake", "#!/bin/sh\necho bad > " + authority_marker.string() + " 2>/dev/null && echo AUTH_WRITE_BAD || echo AUTH_BLOCKED\nexit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  auto result = ava::tools::run_bash(fix.make_context(), "cmake --build " + fix.build_dir.generic_string(),
                                     ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0 && result->output.find("AUTH_BLOCKED") != std::string::npos && !std::filesystem::exists(authority_marker),
         "contained command cannot write to AVA authority test root");
}

void test_contained_command_cannot_open_outbound_network()
{
  if (!landlock_available() || !python3_available())
  {
    ava::test::request_skip("Landlock ABI or python3 insufficient");
    return;
  }
  ContainmentFixture fix("network-denied");
  fix.write_executable("cmake",
                       "#!/bin/sh\n"
                       "python3 -c 'import socket; s=socket.socket(socket.AF_INET,socket.SOCK_STREAM)' 2>&1\n"
                       "if [ $? -ne 0 ]; then echo NETWORK_BLOCKED; else echo NETWORK_OK; fi\n"
                       "exit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  auto result = ava::tools::run_bash(fix.make_context(), "cmake --build " + fix.build_dir.generic_string(),
                                     ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0 && result->output.find("NETWORK_BLOCKED") != std::string::npos,
         "contained command with network denied cannot create outbound IP socket");
}

void test_contained_command_cannot_open_unix_socket()
{
  if (!landlock_available() || !python3_available())
  {
    ava::test::request_skip("Landlock ABI or python3 insufficient");
    return;
  }
  // Network-denied profile must block ALL socket creation, including
  // AF_UNIX/abstract sockets, not just AF_INET/AF_INET6.
  ContainmentFixture fix("unix-denied");
  fix.write_executable("cmake",
                       "#!/bin/sh\n"
                       "python3 -c 'import socket; s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)' 2>&1\n"
                       "if [ $? -ne 0 ]; then echo UNIX_BLOCKED; else echo UNIX_OK; fi\n"
                       "exit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  auto result = ava::tools::run_bash(fix.make_context(), "cmake --build " + fix.build_dir.generic_string(),
                                     ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0 && result->output.find("UNIX_BLOCKED") != std::string::npos,
         "contained command with network denied cannot create AF_UNIX socket");
}

void test_contained_command_cannot_connect_abstract()
{
  if (!landlock_available() || !python3_available())
  {
    ava::test::request_skip("Landlock ABI or python3 insufficient");
    return;
  }
  // The seccomp filter blocks connect() so even an abstract socket connect
  // cannot succeed.
  ContainmentFixture fix("abstract-denied");
  fix.write_executable("cmake",
                       "#!/bin/sh\n"
                       "python3 -c 'import socket; s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); s.connect(\"\\x00ava-test-abstract\")' 2>&1\n"
                       "if [ $? -ne 0 ]; then echo CONNECT_BLOCKED; else echo CONNECT_OK; fi\n"
                       "exit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  auto result = ava::tools::run_bash(fix.make_context(), "cmake --build " + fix.build_dir.generic_string(),
                                     ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0 && result->output.find("CONNECT_BLOCKED") != std::string::npos,
         "contained command with network denied cannot connect to abstract socket");
}

void test_contained_command_cannot_escape_via_symlink()
{
  if (!landlock_available())
  {
    ava::test::request_skip("Landlock ABI insufficient");
    return;
  }
  ContainmentFixture fix("symlink-escape");
  auto const link = fix.workspace / "escape-link";
  std::error_code ec;
  std::filesystem::create_directory_symlink(fix.secret_file.parent_path(), link, ec);
  fix.write_executable("cmake",
                       "#!/bin/sh\ncat " + link.string() + "/test-secret > /dev/null 2>&1 && echo SYMLINK_ESCAPE_BAD || echo SYMLINK_BLOCKED\nexit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  auto result = ava::tools::run_bash(fix.make_context(), "cmake --build " + fix.build_dir.generic_string(),
                                     ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0 && result->output.find("SYMLINK_BLOCKED") != std::string::npos,
         "contained command cannot escape workspace via symlink to external directory");
}

void test_standard_contained_no_resolver_executes()
{
  if (!landlock_available())
  {
    ava::test::request_skip("Landlock ABI insufficient");
    return;
  }
  ContainmentFixture fix("no-resolver");
  fix.write_executable("cmake", "#!/bin/sh\necho auto-ran > " + (fix.build_dir / "auto.txt").string() + "\nexit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  auto result = ava::tools::run_bash(fix.make_context(), "cmake --build " + fix.build_dir.generic_string(),
                                     ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0 && std::filesystem::exists(fix.build_dir / "auto.txt"),
         "standard contained command executes automatically without a resolver");
  if (result)
  {
    expect(result->containment_applied, "contained command reports containment applied in result");
    expect(!result->containment_profile_id.empty(), "contained command reports containment profile in result");
    expect(result->containment_network_mode == "denied", "contained command reports network denied mode in result");
  }
}

void test_contained_command_cancel()
{
  if (!landlock_available())
  {
    ava::test::request_skip("Landlock ABI insufficient");
    return;
  }
  ContainmentFixture fix("cancel");
  fix.write_executable("cmake", "#!/bin/sh\nwhile true; do sleep 0.1; done\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  int cancel_checks = 0;
  ava::tools::ToolContext ctx{.workspace_dir = fix.workspace,
                              .mode = ava::agent::Mode::Build,
                              .permission_resolver = [](perms::PermissionPrompt const&) -> ava::core::Result<perms::PermissionResolutionDecision> {
                                return perms::PermissionResolution::Allow;
                              },
                              .cancel_requested =
                                  [&cancel_checks] {
                                    ++cancel_checks;
                                    return cancel_checks >= 3;
                                  }};
  auto result =
      ava::tools::run_bash(ctx, "cmake --build " + fix.build_dir.generic_string(), ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->canceled, "contained descriptor-executed command observes cancellation and reports a canceled process result");
}

void test_contained_direct_file_recipe()
{
  if (!landlock_available())
  {
    ava::test::request_skip("Landlock ABI insufficient");
    return;
  }
  // Regular-file Landlock rules must use only file-valid rights (READ_FILE
  // and EXECUTE), never READ_DIR. A recipe with a file path argument must
  // not fail rule installation due to an invalid READ_DIR on a file.
  // Using cmake --build <file> classifies as CmakeBuild with the file as a
  // recipe path argument. Since the file is within the writable workspace,
  // the redundant file rule is omitted, and the file is accessible through
  // the workspace writable rule.
  ContainmentFixture fix("direct-file");
  auto const test_file = fix.workspace / "test_input.txt";
  {
    std::ofstream f(test_file, std::ios::binary | std::ios::trunc);
    f << "test-content\n";
  }
  ::chmod(test_file.c_str(), S_IRUSR | S_IWUSR);
  fix.write_executable("cmake", "#!/bin/sh\ncat " + test_file.string() + " > /dev/null 2>&1 && echo FILE_READ_OK || echo FILE_READ_FAIL\nexit 0\n");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  auto result = ava::tools::run_bash(fix.make_context(), "cmake --build " + test_file.generic_string(),
                                     ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0, "contained command with direct-file recipe argument installs containment and executes");
  if (result)
  {
    expect(result->containment_applied, "direct-file recipe containment is applied");
  }
}

}  // namespace

void run_containment_tests()
{
  // Unit-level tests run first and are never skipped, ensuring
  // unavailable-policy coverage even when Landlock is absent.
  test_containment_summary_redacts_paths();
  test_containment_fd_closure_keeps_bound_executable();
  test_unavailable_probe_forces_ask();
  test_sensitive_network_enabled_remains_ask();
  test_critical_raw_remains_ask_no_containment();
  test_home_as_workspace_rejected();
  if (ava::test::skip_requested())
    return;
  test_authority_overlap_rejected();
  if (ava::test::skip_requested())
    return;

  // Feature probe: skip remaining Landlock-dependent tests if unavailable.
  test_feature_probe();
  if (ava::test::skip_requested())
    return;

  test_contained_command_writes_workspace();
  test_contained_command_writes_private_primary_group_workspace();
  test_contained_command_writes_0755_workspace();
  test_contained_command_reads_system_libs();
  test_contained_command_cannot_read_secret();
  test_contained_rustup_cargo_reads_only_sealed_rustup_home();
  test_contained_command_cannot_write_ava_authority();
  test_contained_command_cannot_open_outbound_network();
  test_contained_command_cannot_open_unix_socket();
  test_contained_command_cannot_connect_abstract();
  test_contained_command_cannot_escape_via_symlink();
  test_standard_contained_no_resolver_executes();
  test_contained_command_cancel();
  test_contained_direct_file_recipe();
}
