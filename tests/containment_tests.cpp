#include "sys.h"
#include "tests/support/test_harness.h"
#include "tests/support/test_timeout.h"
#include "ava/command/command.h"
#include "ava/containment/containment.h"
#include "ava/agent/mode.h"
#include "ava/tools/bash_tool.h"
#include "ava/permissions/permission.h"
#include "ava/core/AnchorSet.h"
#include "ava/core/trusted_home.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <grp.h>
#ifdef __linux__
#include <linux/landlock.h>
#endif
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

namespace command = ava::command;
namespace containment = ava::containment;
namespace perms = ava::permissions;

bool group_has_member_other_than(group const& resolved, std::string_view account_name)
{
#ifdef __APPLE__
  auto const* member_bytes = reinterpret_cast<unsigned char const*>(resolved.gr_mem);
  while (member_bytes)
  {
    char* member = nullptr;
    std::memcpy(&member, member_bytes, sizeof(member));
    if (!member)
      break;
    if (std::string_view(member) != account_name)
      return true;
    member_bytes += sizeof(member);
  }
#else
  for (auto const* const* member = resolved.gr_mem; member && *member; ++member)
    if (std::string_view(*member) != account_name)
      return true;
#endif
  return false;
}

bool landlock_available()
{
  return containment::probe_landlock_abi_version() >= containment::kRequiredLandlockAbiVersion;
}

bool python3_available()
{
  return std::filesystem::exists("/usr/bin/python3");
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

#ifdef __APPLE__
  group* resolved_group = ::getgrgid(resolved_account->pw_gid);
  if (!resolved_group || !resolved_group->gr_name || std::string_view(resolved_group->gr_name) != resolved_account->pw_name)
    return std::nullopt;
#else
  std::array<char, 16 * 1024> group_storage{};
  group primary_group{};
  group* resolved_group = nullptr;
  if (::getgrgid_r(resolved_account->pw_gid, &primary_group, group_storage.data(), group_storage.size(), &resolved_group) != 0 || !resolved_group ||
      !resolved_group->gr_name || std::string_view(resolved_group->gr_name) != resolved_account->pw_name)
  {
    return std::nullopt;
  }
#endif
  if (group_has_member_other_than(*resolved_group, resolved_account->pw_name))
    return std::nullopt;
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
  std::filesystem::path spill;
  std::shared_ptr<ava::core::AnchorSet> anchors;

  explicit ContainmentFixture(std::string_view name, mode_t workspace_mode = S_IRWXU)
      : root(temp_root() / ("containment-" + std::string(name))),
        workspace(root / "workspace"),
        bin(root / "bin"),
        build_dir(workspace / "build"),
        secret_file(root / "secret" / "test-secret"),
        ava_authority_root(root / "ava-authority"),
        spill(root / "spill")
  {
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
    std::filesystem::create_directories(workspace);
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(build_dir);
    std::filesystem::create_directories(secret_file.parent_path());
    std::filesystem::create_directories(ava_authority_root);
    std::filesystem::create_directories(spill);
    ::chmod(temp_root().c_str(), S_IRWXU);
    ::chmod(root.c_str(), S_IRWXU);
    ::chmod(workspace.c_str(), workspace_mode);
    ::chmod(bin.c_str(), S_IRWXU);
    ::chmod(build_dir.c_str(), S_IRWXU);
    ::chmod(secret_file.parent_path().c_str(), S_IRWXU);
    ::chmod(ava_authority_root.c_str(), S_IRWXU);
    ::chmod(spill.c_str(), S_IRWXU);
    {
      std::ofstream f(secret_file, std::ios::binary | std::ios::trunc);
      f << "secret-data\n";
    }
    ::chmod(secret_file.c_str(), S_IRUSR | S_IWUSR);
    auto opened = ava::core::AnchorSet::open({workspace, spill});
    if (!opened)
      throw std::runtime_error("failed to open containment test AnchorSet");
    anchors = *opened;
  }

  bool write_executable(std::string_view name, std::string_view body)
  {
    auto path = bin / std::string(name);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << body;
    f.close();
    return ::chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR) == 0;
  }

  ava::tools::ToolContext make_context()
  {
    return ava::tools::ToolContext{
        .workspace_dir = workspace, .spill_dir = spill, .mode = ava::agent::Mode::Build, .anchor_set = anchors, .ava_authority_roots = {ava_authority_root}};
  }

  ava::tools::ToolContext make_allow_context()
  {
    return ava::tools::ToolContext{.workspace_dir = workspace,
                                   .spill_dir = spill,
                                   .mode = ava::agent::Mode::Build,
                                   .permission_resolver = [](perms::PermissionPrompt const&) -> ava::core::Result<perms::PermissionResolutionDecision> {
                                     return perms::PermissionResolution::Allow;
                                   },
                                   .anchor_set = anchors,
                                   .ava_authority_roots = {ava_authority_root}};
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
#ifdef __APPLE__
  // macOS has no pipe2(2); pipe() plus FD_CLOEXEC is equivalent.
  bool report_pipe_created = ::pipe(report.data()) == 0;
  if (report_pipe_created && (::fcntl(report[0], F_SETFD, FD_CLOEXEC) != 0 || ::fcntl(report[1], F_SETFD, FD_CLOEXEC) != 0))
  {
    ::close(report[0]);
    ::close(report[1]);
    report = {-1, -1};
    report_pipe_created = false;
  }
#else
  bool const report_pipe_created = ::pipe2(report.data(), O_CLOEXEC) == 0;
#endif
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
    bool const reported = write_all_to_descriptor_for_test(report[1], child_state.data(), sizeof(child_state));
    static_cast<void>(::close(report[1]));
    _exit(reported ? 0 : 2);
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
  bool const report_read = report_pipe_created && read_exact_from_descriptor_for_test(report[0], observed.data(), sizeof(observed));
  if (report[0] >= 0)
    static_cast<void>(::close(report[0]));
  int status = 0;
  if (child > 0)
    static_cast<void>(::waitpid(child, &status, 0));
  expect(report_read, "containment fd closure report transfers to the parent");
  expect(report_pipe_created && child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 && observed[0] >= 0 && observed[1] >= 0 && observed[2] >= 0 &&
             observed[3] == -1,
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
  ctx.spill_dir = fix.spill;
  ctx.anchor_set = fix.anchors;
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
  ctx.spill_dir = fix.spill;
  ctx.anchor_set = fix.anchors;
  auto result = ava::tools::run_bash(ctx, "curl https://example.com", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->exit_code == 0 && prompts == 1, "sensitive network-enabled command remains Ask");
  expect(network_allowed_in_metadata, "sensitive network-enabled command metadata states network permitted after approval");
}

#ifdef __APPLE__
void test_macos_native_security_contract()
{
  ContainmentFixture fix("macos-command-security");
  expect(fix.write_executable("cmake", "#!/bin/sh\nprintf 'ran\\n' >> build/approval-count\nprintf 'approved-command\\n'\n"),
         "macOS approval fixture creates a normal build command");
  ScopedEnvVar const path_guard("PATH", fix.bin.string() + ":/usr/bin:/bin");
  auto context = fix.make_context();
  std::vector<perms::PermissionPrompt> prompts;
  context.permission_resolver = [&prompts](perms::PermissionPrompt const& prompt) -> ava::core::Result<perms::PermissionResolutionDecision> {
    prompts.push_back(prompt);
    return prompts.size() == 2 ? perms::PermissionResolution::Deny : perms::PermissionResolution::Allow;
  };
  auto const options = ava::tools::BashOptions{.timeout = std::chrono::milliseconds(2000)};
  auto first = ava::tools::run_bash(context, "cmake --build build", options);
  auto denied = ava::tools::run_bash(context, "cmake --build build", options);
  auto third = ava::tools::run_bash(context, "cmake --build build", options);
  std::ifstream marker(fix.build_dir / "approval-count");
  std::string const executions{std::istreambuf_iterator<char>(marker), std::istreambuf_iterator<char>()};
  expect(first && third && first->exit_code == 0 && third->exit_code == 0 && first->output == "approved-command\n" && third->output == "approved-command\n" &&
             !first->containment_applied && !third->containment_applied,
         "macOS approved native commands execute successfully and never claim containment");
  expect(prompts.size() == 3 && !denied && denied.error().category() == ava::core::ErrorCategory::PermissionDenied && executions == "ran\nran\n",
         "macOS asks again after every approval and denial prevents execution");
  bool const all_one_shot =
      prompts.size() == 3 && std::ranges::all_of(prompts, [](perms::PermissionPrompt const& prompt) {
        if (!prompt.command_metadata)
          return false;
        auto const& metadata = *prompt.command_metadata;
        return prompt.risk == perms::PermissionRisk::Critical && metadata.level == command::CommandLevel::Critical && metadata.executor_identity_verified &&
               metadata.containment_status == perms::CommandContainmentStatus::Unavailable && !metadata.containment_available &&
               metadata.containment_profile_id == "ava-macos-uncontained-v1" && metadata.backend_maximum_scope == command::InteractiveScope::Once &&
               metadata.effective_allowed_scopes == std::vector{command::InteractiveScope::Once} && metadata.global_recipe_key.empty() &&
               metadata.workspace_recipe_key.empty() && !perms::command_permission_allows_reusable_grant(metadata) &&
               !perms::command_prompt_allows_persistent_allow(prompt) &&
               prompt.reason == "Command not executed: macOS native containment is unavailable and this command requires one-shot user approval.";
      });
  expect(all_one_shot, "macOS containment-required commands become truthful CriticalAsk prompts with no persistent or session Allow authority");
  expect(containment::probe_landlock_abi_version() == 0 && !containment::seccomp_network_filter_supported(),
         "native macOS reports Linux containment capabilities unavailable");
  expect(fix.write_executable("cmake.next", "#!/bin/sh\nprintf 'replacement-must-not-run' > build/replacement-marker\n"),
         "macOS replacement regression prepares an inert marker command");
  bool replaced_during_approval = false;
  context.permission_resolver = [&fix, &replaced_during_approval](perms::PermissionPrompt const&) -> ava::core::Result<perms::PermissionResolutionDecision> {
    std::error_code error;
    std::filesystem::rename(fix.bin / "cmake.next", fix.bin / "cmake", error);
    replaced_during_approval = !error;
    return perms::PermissionResolution::Allow;
  };
  auto replaced = ava::tools::run_bash(context, "cmake --build build", options);
  expect(replaced_during_approval && !replaced && !std::filesystem::exists(fix.build_dir / "replacement-marker"),
         "macOS executable identity and path revalidation fail closed on replacement between approval and execution");
}
#endif

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
  ctx.spill_dir = fix.spill;
  ctx.anchor_set = fix.anchors;
  auto result = ava::tools::run_bash(ctx, "printf raw; true", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(!result && prompts == 1 && containment_not_required, "critical/raw command remains Ask once with no containment claim in metadata");
}

void test_home_as_workspace_rejected()
{
  // Reject a workspace that equals or is an ancestor of the trusted home.
  // Ordinary projects nested under the trusted home are allowed.
  //
  // The boundary rule operates on CommandBuildOptions::trusted_home. Obtain that
  // path from ava::core::cached_trusted_account() -- the process-wide trusted
  // home, and the single place HOME is read -- rather than reading $HOME here.
  // To exercise the rule we need a directory whose ancestor chain is owner-safe
  // so it passes the sealing ancestor-safety validation; /tmp-based temp space
  // does not qualify (its ancestors are world-writable). Create a per-process
  // scratch directory under the trusted home to stand in for it; when the
  // trusted home is mounted read-only (some CI containers) fall back to a
  // scratch directory under the build tree, which is writable and owner-safe.
  // Because the stand-in is always a directory we create, cleanup is uniform and
  // never touches a real home directory.
  std::error_code ec;
  std::filesystem::path trusted_home;
  auto const boundary_name = "ava-test-home-boundary-" + std::to_string(::getpid());

  {
    auto candidate = ava::core::cached_trusted_account().home / boundary_name;
    std::filesystem::create_directories(candidate, ec);
    if (!ec)
      trusted_home = std::move(candidate);
  }
#ifdef AVA_TESTS_BINARY_DIR
  // The trusted home may be mounted read-only (some CI containers); fall back to
  // a scratch directory under the build tree, which is writable and whose
  // ancestor chain is owner-safe.
  if (trusted_home.empty())
  {
    auto candidate = std::filesystem::path(AVA_TESTS_BINARY_DIR) / boundary_name;
    std::filesystem::create_directories(candidate, ec);
    if (!ec)
      trusted_home = std::move(candidate);
  }
#endif
  if (trusted_home.empty())
  {
    ava::tests::request_skip("no writable trusted-home stand-in with a safe ancestor chain available");
    return;
  }
  // create_directories applies the process umask to 0777. The trusted-home
  // validator intentionally rejects group-writable roots, so make this fixture
  // deterministic even when the invoking shell uses umask 0002.
  if (::chmod(trusted_home.c_str(), S_IRWXU) != 0)
  {
    std::filesystem::remove_all(trusted_home, ec);
    ava::tests::request_skip("cannot secure the trusted-home stand-in to mode 0700");
    return;
  }

  auto project_under_home = trusted_home / "ava-test-project-boundary";
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
  auto home_anchors = ava::core::AnchorSet::open({trusted_home, synth_base});
  expect(home_anchors.has_value(), "home boundary test opens its shared AnchorSet");
  if (!home_anchors)
    return;
  command::CommandBuildOptions opts{.workspace = trusted_home,
                                    .anchor_set = *home_anchors,
                                    .trusted_home = trusted_home,
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
  expect(!plan && plan.error().category() == ava::core::ErrorCategory::PermissionDenied &&
             plan.error().message() == "command workspace must not equal or contain the trusted real home directory",
         "sealing rejects workspace specifically at the trusted-home boundary");

  // Positive: a project nested under the trusted home is allowed.
  auto project_anchors = ava::core::AnchorSet::open({project_under_home, synth_base});
  expect(project_anchors.has_value(), "nested project boundary test opens its shared AnchorSet");
  if (!project_anchors)
    return;
  opts.workspace = project_under_home;
  opts.anchor_set = *project_anchors;
  auto nested_intent = command::CommandIntent::compatibility("ls", limits);
  auto nested_plan = command::seal_command_plan(*nested_intent, opts);
  expect(nested_plan.has_value(), "sealing allows ordinary projects nested under home");

  std::filesystem::remove_all(trusted_home, ec);
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
  auto anchors = ava::core::AnchorSet::open({fix.ava_authority_root, synth_root});
  expect(anchors.has_value(), "authority overlap test opens its shared AnchorSet");
  if (!anchors)
    return;
  command::CommandBuildOptions opts{.workspace = fix.ava_authority_root,
                                    .anchor_set = *anchors,
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
    ava::tests::request_skip("kernel lacks required Landlock ABI or seccomp support");
  }
}

void test_replaced_external_path_rule_fails_child_application()
{
  if (!landlock_available())
  {
    ava::tests::request_skip("Landlock ABI insufficient");
    return;
  }

  ContainmentFixture fix("replaced-external-path-rule");
  expect(fix.write_executable("cmake", "#!/bin/sh\nexit 0\n"), "replacement-race fixture creates a known contained command");

  auto const trusted_home = fix.root / "trusted-home";
  auto const synthetic_root = fix.root / "synthetic-environment";
  std::vector<std::filesystem::path> synthetic_directories;
  for (auto const* name : {"home", "config", "cache", "data", "state", "tmp"})
  {
    auto const directory = synthetic_root / name;
    std::filesystem::create_directories(directory);
    ::chmod(directory.c_str(), S_IRWXU);
    synthetic_directories.push_back(directory);
  }
  std::filesystem::create_directories(trusted_home);
  ::chmod(trusted_home.c_str(), S_IRWXU);
  ::chmod(synthetic_root.c_str(), S_IRWXU);

  std::vector<std::filesystem::path> anchor_roots{fix.workspace};
  anchor_roots.insert(anchor_roots.end(), synthetic_directories.begin(), synthetic_directories.end());
  auto anchors = ava::core::AnchorSet::open(anchor_roots);
  expect(anchors.has_value() && !(*anchors)->contains_lexical(fix.bin), "replacement-race PATH directory is sealed as an external path");
  if (!anchors)
    return;

  command::CommandBuildOptions options{.workspace = fix.workspace,
                                       .anchor_set = *anchors,
                                       .trusted_home = trusted_home,
                                       .discover_host_user_tools = false,
                                       .startup_path = fix.bin.string(),
                                       .shell = "/bin/sh",
                                       .ava_authority_roots = {fix.ava_authority_root},
                                       .environment = command::CommandEnvironmentOptions{.profile_id = "containment-test-environment",
                                                                                         .user = "test-user",
                                                                                         .logname = "test-user",
                                                                                         .home = synthetic_directories[0],
                                                                                         .xdg_config_home = synthetic_directories[1],
                                                                                         .xdg_cache_home = synthetic_directories[2],
                                                                                         .xdg_data_home = synthetic_directories[3],
                                                                                         .xdg_state_home = synthetic_directories[4],
                                                                                         .tmpdir = synthetic_directories[5]},
                                       .workspace_script_recipes = {},
                                       .limits = {}};
  auto intent = command::CommandIntent::structured({"cmake", "--build", "build"});
  auto preparation = intent ? command::prepare_command(*intent, options)
                            : ava::core::Result<command::CommandPreparation>{
                                  std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "no contained command intent"))};
  expect(preparation.has_value(), "replacement-race command preparation succeeds");
  if (!preparation)
    return;

  auto const path_entry = std::ranges::find_if(preparation->plan().path_entries(),
                                               [&](command::CommandPathEntry const& entry) { return entry.directory == fix.bin.lexically_normal(); });
  auto plan = containment::prepare_development_containment(*preparation, false);
  auto const target_rule = std::ranges::find_if(
      plan.filesystem_rules, [&](containment::ContainmentFilesystemRule const& rule) { return rule.logical_path == fix.bin.lexically_normal(); });
  expect(path_entry != preparation->plan().path_entries().end() && plan.availability == containment::ContainmentAvailability::Available &&
             target_rule != plan.filesystem_rules.end() && target_rule->identity_bound,
         "containment plan includes an identity-bound rule for the external sealed PATH directory");
  if (path_entry == preparation->plan().path_entries().end() || plan.availability != containment::ContainmentAvailability::Available ||
      target_rule == plan.filesystem_rules.end() || !target_rule->identity_bound)
    return;

  auto const approved_directory = fix.root / "approved-path-original";
  std::error_code replacement_error;
  std::filesystem::rename(fix.bin, approved_directory, replacement_error);
  bool const approved_directory_moved = !replacement_error;
  if (approved_directory_moved)
  {
    std::filesystem::create_directory(fix.bin, replacement_error);
    if (!replacement_error && ::chmod(fix.bin.c_str(), S_IRWXU) != 0)
      replacement_error = std::error_code(errno, std::generic_category());
  }
  if (replacement_error)
  {
    if (approved_directory_moved)
    {
      std::error_code cleanup_error;
      std::filesystem::remove_all(fix.bin, cleanup_error);
      cleanup_error.clear();
      std::filesystem::rename(approved_directory, fix.bin, cleanup_error);
    }
    expect(false, "replacement-race fixture replaces the approved PATH directory");
    return;
  }

  pid_t const child = ::fork();
  if (child == 0)
  {
    auto const applied = containment::apply_containment_in_child(plan);
    _exit(applied ? 91 : 90);
  }

  int child_status = 0;
  bool child_exited = false;
  if (child > 0)
  {
    auto const deadline = ava::tests::now_plus_seconds(2);
    while (std::chrono::steady_clock::now() < deadline)
    {
      pid_t const waited = ::waitpid(child, &child_status, WNOHANG);
      if (waited == child)
      {
        child_exited = true;
        break;
      }
      if (waited < 0 && errno != EINTR)
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!child_exited)
    {
      static_cast<void>(::kill(child, SIGKILL));
      static_cast<void>(::waitpid(child, &child_status, 0));
    }
  }

  std::error_code restore_error;
  std::filesystem::remove_all(fix.bin, restore_error);
  bool const replacement_removed = !restore_error;
  restore_error.clear();
  std::filesystem::rename(approved_directory, fix.bin, restore_error);
  bool const restored = !restore_error;

  expect(child > 0 && child_exited && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 90,
         "child containment application rejects a replaced external PATH rule before accepting or executing the command");
  expect(replacement_removed && restored, "replacement-race fixture restores the approved PATH directory");
}

void test_contained_command_writes_workspace()
{
  if (!landlock_available())
  {
    ava::tests::request_skip("Landlock ABI insufficient");
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
    ava::tests::request_skip("Landlock ABI insufficient");
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
    ava::tests::request_skip("Landlock ABI insufficient");
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
    ava::tests::request_skip("Landlock ABI insufficient");
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
    ava::tests::request_skip("Landlock ABI insufficient");
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
    ava::tests::request_skip("Landlock ABI insufficient");
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
    ava::tests::request_skip("Landlock ABI or python3 insufficient");
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
    ava::tests::request_skip("Landlock ABI or python3 insufficient");
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
    ava::tests::request_skip("Landlock ABI or python3 insufficient");
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
    ava::tests::request_skip("Landlock ABI insufficient");
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
    ava::tests::request_skip("Landlock ABI insufficient");
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
    ava::tests::request_skip("Landlock ABI insufficient");
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
  ctx.spill_dir = fix.spill;
  ctx.anchor_set = fix.anchors;
  auto result =
      ava::tools::run_bash(ctx, "cmake --build " + fix.build_dir.generic_string(), ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(result && result->canceled, "contained descriptor-executed command observes cancellation and reports a canceled process result");
}

void test_contained_direct_file_recipe()
{
  if (!landlock_available())
  {
    ava::tests::request_skip("Landlock ABI insufficient");
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

#ifdef __APPLE__
void run_macos_command_security_tests()
{
  test_macos_native_security_contract();
  test_containment_fd_closure_keeps_bound_executable();
}
#endif

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
  if (ava::tests::skip_requested())
    return;
  test_authority_overlap_rejected();
  if (ava::tests::skip_requested())
    return;

  // Feature probe: skip remaining Landlock-dependent tests if unavailable.
  test_feature_probe();
  if (ava::tests::skip_requested())
    return;

  test_replaced_external_path_rule_fails_child_application();
  test_contained_command_writes_workspace();
  test_contained_command_writes_private_primary_group_workspace();
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
