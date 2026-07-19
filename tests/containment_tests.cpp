#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/command/command.h"
#include "ava/containment/containment.h"
#include "ava/agent/mode.h"
#include "ava/tools/bash_tool.h"
#include "ava/permissions/permission.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <signal.h>
#include <sys/stat.h>
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
  expect(result && result->canceled, "contained command observes cancellation and reports a canceled process result");
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
  test_contained_command_writes_0755_workspace();
  test_contained_command_reads_system_libs();
  test_contained_command_cannot_read_secret();
  test_contained_command_cannot_write_ava_authority();
  test_contained_command_cannot_open_outbound_network();
  test_contained_command_cannot_open_unix_socket();
  test_contained_command_cannot_connect_abstract();
  test_contained_command_cannot_escape_via_symlink();
  test_standard_contained_no_resolver_executes();
  test_contained_command_cancel();
  test_contained_direct_file_recipe();
}
