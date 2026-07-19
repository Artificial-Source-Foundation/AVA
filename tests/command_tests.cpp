#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/command/command.h"

#include <algorithm>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace {

namespace command = ava::command;

static_assert(!std::default_initializable<command::CommandEnvironment>);
static_assert(!std::default_initializable<command::CommandPlan>);
static_assert(!std::default_initializable<command::CommandPreparation>);

struct CommandFixture
{
  std::filesystem::path root;
  std::filesystem::path workspace;
  std::filesystem::path trusted_home;
  std::filesystem::path synthetic_home;
  std::filesystem::path bin;

  explicit CommandFixture(std::string_view name)
      : root(temp_root() / ("command-foundation-" + std::string(name))),
        workspace(root / "workspace"),
        trusted_home(root / "trusted-home"),
        synthetic_home(root / "synthetic-child-home"),
        bin(root / "bin")
  {
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    // The shared test harness root may be group writable. Command planning
    // deliberately validates ancestors, so create and secure this fixture's
    // enclosing namespace before sealing any path under it.
    std::filesystem::create_directories(root.parent_path());
    ::chmod(root.parent_path().c_str(), S_IRWXU);
    for (auto const& directory : {workspace, trusted_home, synthetic_home, bin, workspace / "build", workspace / "tests", workspace / "nested"})
    {
      std::filesystem::create_directories(directory);
      ::chmod(directory.c_str(), S_IRWXU);
    }
    ::chmod(root.c_str(), S_IRWXU);
    executable_in(bin, "shell");
  }

  void executable(std::string_view name, std::string_view content = "#!/bin/sh\nexit 0\n") const { executable_in(bin, name, content); }

  void executable_in(std::filesystem::path const& directory, std::string_view name, std::string_view content = "#!/bin/sh\nexit 0\n") const
  {
    std::filesystem::create_directories(directory);
    ::chmod(directory.c_str(), S_IRWXU);
    auto const path = directory / name;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
    output.close();
    ::chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
  }

  command::CommandBuildOptions options(std::string profile_id = "command-test") const
  {
    return command::CommandBuildOptions{.workspace = workspace,
                                        .trusted_home = trusted_home,
                                        .startup_path = bin.string(),
                                        .shell = bin / "shell",
                                        .environment = command::CommandEnvironmentOptions{.profile_id = std::move(profile_id),
                                                                                          .user = "ava-test-user",
                                                                                          .logname = "ava-test-login",
                                                                                          .home = synthetic_home,
                                                                                          .xdg_config_home = root / "synthetic-xdg-config",
                                                                                          .xdg_cache_home = root / "synthetic-xdg-cache",
                                                                                          .xdg_data_home = root / "synthetic-xdg-data",
                                                                                          .xdg_state_home = root / "synthetic-xdg-state",
                                                                                          .tmpdir = root / "synthetic-tmp"},
                                        .workspace_script_recipes = {},
                                        .limits = {}};
  }
};

std::optional<std::string> environment_value(command::CommandEnvironment const& environment, std::string_view key)
{
  auto const found = std::ranges::find_if(environment.entries(), [key](command::EnvironmentVariable const& entry) { return entry.key == key; });
  if (found == environment.entries().end())
    return std::nullopt;
  return found->value;
}

bool is_standard_recipe(ava::core::Result<command::CommandPlan> const& plan)
{
  return plan && plan->classification().level == command::CommandLevel::Standard && plan->classification().recipe.has_value();
}

void test_compatibility_parser_is_lossless_or_raw_shell()
{
  auto quoted_backslash = command::CommandIntent::compatibility("echo 'a\\b'");
  auto ordinary_double_quote = command::CommandIntent::compatibility("echo \"a\\q\"");
  auto expanded_double_quote = command::CommandIntent::compatibility("echo \"$HOME\"");
  auto backtick = command::CommandIntent::compatibility("echo `id`");
  auto assignment = command::CommandIntent::compatibility("NAME=value echo safe");
  auto substitution = command::CommandIntent::compatibility("echo $(id)");
  auto unmatched_quote = command::CommandIntent::compatibility("echo 'unterminated");
  auto unmatched_escape = command::CommandIntent::compatibility("echo trailing\\");

  expect(quoted_backslash && quoted_backslash->lane() == command::CommandIntentLane::Compatibility &&
             quoted_backslash->argv() == std::vector<std::string>({"echo", "a\\b"}) && ordinary_double_quote &&
             ordinary_double_quote->lane() == command::CommandIntentLane::Compatibility &&
             ordinary_double_quote->argv() == std::vector<std::string>({"echo", "a\\q"}),
         "compatibility parsing preserves literal single-quote backslashes and supported double-quote escapes exactly");
  expect(expanded_double_quote && expanded_double_quote->lane() == command::CommandIntentLane::RawShell && backtick &&
             backtick->lane() == command::CommandIntentLane::RawShell && assignment && assignment->lane() == command::CommandIntentLane::RawShell &&
             substitution && substitution->lane() == command::CommandIntentLane::RawShell && !unmatched_quote && !unmatched_escape,
         "compatibility parsing routes expansion, leading assignment, unsupported shell constructs, and non-lossless syntax to critical raw-shell handling");
}

void test_intent_bounds_and_path_bounds_do_not_underflow()
{
  command::CommandLimits request_limit;
  request_limit.max_request_bytes = 4;
  auto too_large = command::CommandIntent::structured({"aa", "bbb"}, std::nullopt, request_limit);
  command::CommandLimits exact_limit;
  exact_limit.max_request_bytes = 4;
  auto exact = command::CommandIntent::structured({"aa", "bb"}, std::nullopt, exact_limit);

  CommandFixture fixture("bounds");
  fixture.executable("pwd");
  auto intent = command::CommandIntent::structured({"pwd"});
  auto fits = fixture.options();
  fits.environment.home = "/a";
  fits.environment.xdg_config_home = "/b";
  fits.environment.xdg_cache_home = "/c";
  fits.environment.xdg_data_home = "/d";
  fits.environment.xdg_state_home = "/e";
  fits.environment.tmpdir = "/f";
  fits.limits.max_path_bytes = fixture.bin.string().size();
  auto does_not_fit = fits;
  does_not_fit.limits.max_path_bytes = fixture.bin.string().size() - 1;
  auto fitting_plan = command::seal_command_plan(*intent, fits);
  auto oversized_path_plan = command::seal_command_plan(*intent, does_not_fit);

  expect(!too_large && exact && fitting_plan && !oversized_path_plan,
         "bounded argv and PATH accumulation reject overflows without unsigned-underflow bypasses while accepting exact boundaries");
}

void test_workspace_spoofs_are_not_inspection_recipes()
{
  CommandFixture fixture("origin-spoof");
  fixture.executable("ls");
  auto const venv_bin = fixture.workspace / ".venv" / "bin";
  fixture.executable_in(venv_bin, "ls");
  fixture.executable_in(venv_bin, "cmake");
  ::chmod((fixture.workspace / ".venv").c_str(), S_IRWXU);
  std::filesystem::create_directories(fixture.workspace / "build");

  auto options = fixture.options("origin-profile");
  options.startup_path = venv_bin.string() + ":" + fixture.bin.string();
  auto bare_ls = command::seal_command_plan(*command::CommandIntent::structured({"ls"}), options);
  auto absolute_ls = command::seal_command_plan(*command::CommandIntent::structured({(venv_bin / "ls").string()}), options);
  auto workspace_cmake = command::seal_command_plan(*command::CommandIntent::structured({"cmake", "--build", "build"}), options);
  options.startup_path = fixture.bin.string();
  auto trusted_ls = command::seal_command_plan(*command::CommandIntent::structured({"ls"}), options);

  expect(bare_ls && absolute_ls && bare_ls->resolved_executable() && absolute_ls->resolved_executable() &&
             bare_ls->resolved_executable()->origin == command::ExecutableOrigin::Workspace &&
             absolute_ls->resolved_executable()->origin == command::ExecutableOrigin::Workspace &&
             bare_ls->classification().level == command::CommandLevel::Critical && absolute_ls->classification().level == command::CommandLevel::Critical &&
             workspace_cmake && workspace_cmake->classification().level == command::CommandLevel::Standard &&
             workspace_cmake->classification().capabilities.executes_mutable_project_code &&
             workspace_cmake->classification().capabilities.requires_containment && trusted_ls &&
             trusted_ls->classification().level == command::CommandLevel::Standard && !trusted_ls->classification().capabilities.requires_containment,
         "bare and absolute workspace same-name spoof executables never gain uncontained inspection status, while project-code recipes retain containment");
}

void test_recipe_paths_are_canonical_sealed_and_fresh()
{
  CommandFixture fixture("recipe-paths");
  for (auto const name : {"ls", "cmake", "ctest", "pytest"}) fixture.executable(name);
  auto const outside = fixture.root / "outside";
  std::filesystem::create_directories(outside);
  ::chmod(outside.c_str(), S_IRWXU);

  struct RecipeCase
  {
    std::string executable;
    std::vector<std::string> prefix;
  };
  std::vector<RecipeCase> const cases{{"ls", {}}, {"cmake", {"--build"}}, {"ctest", {"--test-dir"}}, {"pytest", {}}};
  bool all_escaped_downgraded = true;
  bool all_replacements_stale = true;
  bool all_paths_bound = true;
  for (auto const& recipe : cases)
  {
    auto const target = fixture.workspace / (recipe.executable + "-target");
    auto const escape = fixture.workspace / (recipe.executable + "-escape");
    std::filesystem::create_directories(target);
    ::chmod(target.c_str(), S_IRWXU);
    std::filesystem::create_directory_symlink(outside, escape);

    std::vector<std::string> escaped_argv{recipe.executable};
    escaped_argv.insert(escaped_argv.end(), recipe.prefix.begin(), recipe.prefix.end());
    escaped_argv.push_back(escape.filename().string());
    auto escaped = command::seal_command_plan(*command::CommandIntent::structured(std::move(escaped_argv)), fixture.options());
    all_escaped_downgraded = all_escaped_downgraded && escaped && escaped->classification().level == command::CommandLevel::Critical;

    std::vector<std::string> valid_argv{recipe.executable};
    valid_argv.insert(valid_argv.end(), recipe.prefix.begin(), recipe.prefix.end());
    valid_argv.push_back(target.filename().string());
    auto plan = command::seal_command_plan(*command::CommandIntent::structured(std::move(valid_argv)), fixture.options());
    auto before =
        plan ? command::plan_is_fresh(*plan) : ava::core::Result<bool>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "no plan"))};
    all_paths_bound = all_paths_bound && is_standard_recipe(plan) && plan->classification().recipe->path_arguments.size() == 1 &&
                      plan->classification().recipe->path_arguments.front().canonical_path == std::filesystem::canonical(target) &&
                      plan->classification().recipe->canonical_argv.back() == std::filesystem::canonical(target).string() && before && *before;

    std::filesystem::remove_all(target);
    std::filesystem::create_directories(target);
    ::chmod(target.c_str(), S_IRWXU);
    auto after =
        plan ? command::plan_is_fresh(*plan) : ava::core::Result<bool>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "no plan"))};
    all_replacements_stale = all_replacements_stale && after && !*after;
  }
  expect(all_escaped_downgraded && all_paths_bound && all_replacements_stale,
         "ls, build, ctest, and pytest path arguments reject symlink escapes, bind canonical identities, and detect replacement before execution");
}

void test_sealed_freshness_and_symlink_provenance()
{
  CommandFixture fixture("freshness");
  fixture.executable("fresh");
  auto fresh = command::seal_command_plan(*command::CommandIntent::structured({"fresh"}), fixture.options());
  auto before =
      fresh ? command::plan_is_fresh(*fresh) : ava::core::Result<bool>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "no plan"))};
  {
    std::ofstream append(fixture.bin / "fresh", std::ios::binary | std::ios::app);
    append << "# changed\n";
  }
  auto after =
      fresh ? command::plan_is_fresh(*fresh) : ava::core::Result<bool>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "no plan"))};

  fixture.executable("gone");
  auto gone = command::seal_command_plan(*command::CommandIntent::structured({"gone"}), fixture.options());
  std::filesystem::remove(fixture.bin / "gone");
  auto missing =
      gone ? command::plan_is_fresh(*gone) : ava::core::Result<bool>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "no plan"))};

  fixture.executable("target-one");
  fixture.executable("target-two");
  std::filesystem::create_symlink(fixture.bin / "target-one", fixture.bin / "linked");
  auto linked = command::seal_command_plan(*command::CommandIntent::structured({"linked"}), fixture.options());
  auto linked_before =
      linked ? command::plan_is_fresh(*linked) : ava::core::Result<bool>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "no plan"))};
  std::filesystem::remove(fixture.bin / "linked");
  std::filesystem::create_symlink(fixture.bin / "target-two", fixture.bin / "linked");
  auto linked_after =
      linked ? command::plan_is_fresh(*linked) : ava::core::Result<bool>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "no plan"))};

  expect(fresh && before && *before && after && !*after && gone && !missing && linked && linked->resolved_executable() &&
             linked->resolved_executable()->executable.requested_path_is_symlink && linked_before && *linked_before && linked_after && !*linked_after,
         "freshness checks executable metadata and symlink provenance, returning an error for inaccessible identities rather than silently treating them as "
         "stale");
}

void test_workspace_cwd_path_and_interpreter_freshness()
{
  CommandFixture fixture("all-freshness");
  fixture.executable("pwd");
  auto workspace_plan = command::seal_command_plan(*command::CommandIntent::structured({"pwd"}), fixture.options());
  ::chmod(fixture.workspace.c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
  auto workspace_changed = workspace_plan ? command::plan_is_fresh(*workspace_plan)
                                          : ava::core::Result<bool>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "no plan"))};

  CommandFixture cwd_fixture("cwd-freshness");
  cwd_fixture.executable("pwd");
  auto cwd_plan = command::seal_command_plan(*command::CommandIntent::structured({"pwd"}, "nested"), cwd_fixture.options());
  ::chmod((cwd_fixture.workspace / "nested").c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
  auto cwd_changed =
      cwd_plan ? command::plan_is_fresh(*cwd_plan) : ava::core::Result<bool>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "no plan"))};

  CommandFixture path_fixture("path-freshness");
  path_fixture.executable("pwd");
  auto path_plan = command::seal_command_plan(*command::CommandIntent::structured({"pwd"}), path_fixture.options());
  ::chmod(path_fixture.bin.c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
  auto path_changed =
      path_plan ? command::plan_is_fresh(*path_plan) : ava::core::Result<bool>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "no plan"))};

  CommandFixture interpreter_fixture("interpreter-freshness");
  interpreter_fixture.executable("interpreter");
  interpreter_fixture.executable("script", "#!" + (interpreter_fixture.bin / "interpreter").string() + "\nexit 0\n");
  auto script_plan = command::seal_command_plan(*command::CommandIntent::structured({"script"}), interpreter_fixture.options());
  {
    std::ofstream append(interpreter_fixture.bin / "interpreter", std::ios::binary | std::ios::app);
    append << "# changed\n";
  }
  auto interpreter_changed = script_plan ? command::plan_is_fresh(*script_plan)
                                         : ava::core::Result<bool>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "no plan"))};

  expect(workspace_changed && !*workspace_changed && cwd_changed && !*cwd_changed && path_changed && !*path_changed && script_plan &&
             script_plan->resolved_executable()->shebang_interpreters.size() == 2 && interpreter_changed && !*interpreter_changed,
         "workspace, cwd, PATH entries, executables, and every shebang interpreter participate in freshness validation");
}

void test_raw_shell_binds_configured_shell_and_rejects_env_shebang()
{
  CommandFixture fixture("raw-shell");
  auto raw = command::CommandIntent::compatibility("echo safe; whoami");
  auto raw_plan = command::seal_command_plan(*raw, fixture.options());
  auto default_shell_options = fixture.options();
  default_shell_options.shell = "/bin/sh";
  auto default_shell_plan = command::seal_command_plan(*raw, default_shell_options);
  auto invalid_shell_options = fixture.options();
  invalid_shell_options.shell = "shell";
  auto invalid_shell_plan = command::seal_command_plan(*raw, invalid_shell_options);
  fixture.executable("env-indirect", "#!/usr/bin/env python\nprint('no')\n");
  auto env_indirect = command::seal_command_plan(*command::CommandIntent::structured({"env-indirect"}), fixture.options());

  expect(raw_plan && raw_plan->execution_domain() == command::CommandExecutionDomain::RawShell && raw_plan->resolved_executable() &&
             raw_plan->resolved_executable()->executable.canonical_path == std::filesystem::canonical(fixture.bin / "shell") &&
             raw_plan->classification().level == command::CommandLevel::Critical && default_shell_plan && default_shell_plan->resolved_executable() &&
             default_shell_plan->resolved_executable()->executable.canonical_path == std::filesystem::canonical("/bin/sh") && !invalid_shell_plan &&
             !env_indirect,
         "raw-shell plans bind the exact absolute configured shell and reject unsupported /usr/bin/env shebang indirection");
}

void test_environment_is_synthetic_and_digest_bound()
{
  CommandFixture fixture("environment");
  auto const user_bin = fixture.trusted_home / ".local" / "bin";
  fixture.executable_in(user_bin, "user-tool");
  ::chmod(user_bin.parent_path().c_str(), S_IRWXU);
  auto prepared = command::prepare_command(*command::CommandIntent::structured({"user-tool"}), fixture.options("environment-profile"));
  auto changed_environment_options = fixture.options("environment-profile");
  changed_environment_options.environment.tmpdir = fixture.root / "different-synthetic-tmp";
  auto changed_environment = command::seal_command_plan(*command::CommandIntent::structured({"user-tool"}), changed_environment_options);
  auto overlapping_root_options = fixture.options("environment-profile");
  overlapping_root_options.environment.home = fixture.trusted_home;
  auto overlapping_root = command::seal_command_plan(*command::CommandIntent::structured({"user-tool"}), overlapping_root_options);

  auto home = prepared ? environment_value(prepared->environment(), "HOME") : std::nullopt;
  auto path = prepared ? environment_value(prepared->environment(), "PATH") : std::nullopt;
  expect(prepared && prepared->plan().resolved_executable() && prepared->plan().resolved_executable()->origin == command::ExecutableOrigin::User && home &&
             *home == fixture.synthetic_home.string() && *home != fixture.trusted_home.string() && path && path->find(user_bin.string()) != std::string::npos &&
             prepared->plan().environment_digest().starts_with("sha256:ava-command-environment-v1:") &&
             prepared->plan().display_json().find(fixture.synthetic_home.string()) == std::string::npos && changed_environment &&
             changed_environment->environment_digest() != prepared->plan().environment_digest() &&
             changed_environment->fingerprint() != prepared->plan().fingerprint() && !overlapping_root,
         "trusted host toolchain discovery is separate from synthetic child roots and a versioned SHA-256 environment digest binds exact environment content "
         "to plans");
}

void test_capabilities_scopes_and_standard_recipes()
{
  CommandFixture fixture("policy");
  for (auto const name : {"git", "cmake", "ctest", "pytest", "npm", "python"}) fixture.executable(name);
  auto options = fixture.options();
  auto pull = command::seal_command_plan(*command::CommandIntent::structured({"git", "pull"}), options);
  auto fetch = command::seal_command_plan(*command::CommandIntent::structured({"git", "fetch"}), options);
  auto clone = command::seal_command_plan(*command::CommandIntent::structured({"git", "clone", "origin"}), options);
  auto submodule = command::seal_command_plan(*command::CommandIntent::structured({"git", "submodule", "update"}), options);
  auto force_push = command::seal_command_plan(*command::CommandIntent::structured({"git", "push", "--force-with-lease"}), options);
  auto cmake = command::seal_command_plan(*command::CommandIntent::structured({"cmake", "--build", "build"}), options);
  auto ctest = command::seal_command_plan(*command::CommandIntent::structured({"ctest", "--test-dir", "build"}), options);
  auto pytest = command::seal_command_plan(*command::CommandIntent::structured({"pytest", "tests"}), options);
  auto install = command::seal_command_plan(*command::CommandIntent::structured({"npm", "install"}), options);
  auto inline_python = command::seal_command_plan(*command::CommandIntent::structured({"python", "-c", "print(1)"}), options);

  auto const remote_mutating = [](ava::core::Result<command::CommandPlan> const& plan) {
    return plan && plan->classification().level == command::CommandLevel::Sensitive && plan->classification().capabilities.network_enabled &&
           plan->classification().capabilities.mutates_workspace && plan->classification().max_interactive_scope == command::InteractiveScope::Workspace;
  };
  expect(remote_mutating(pull) && remote_mutating(fetch) && remote_mutating(clone) && remote_mutating(submodule) && force_push &&
             force_push->classification().level == command::CommandLevel::Critical && force_push->classification().capabilities.network_enabled &&
             force_push->classification().capabilities.destructive_or_privileged &&
             force_push->classification().max_interactive_scope == command::InteractiveScope::Once && is_standard_recipe(cmake) && is_standard_recipe(ctest) &&
             is_standard_recipe(pytest) && cmake->classification().capabilities.executes_mutable_project_code &&
             cmake->classification().capabilities.requires_containment && install &&
             install->classification().max_interactive_scope == command::InteractiveScope::Workspace && inline_python &&
             inline_python->classification().max_interactive_scope == command::InteractiveScope::Once &&
             command::to_string(command::InteractiveScope::Session) == "session",
         "git network/mutation capabilities are exact, force-push is critical, Standard/Sensitive cap at workspace, Critical caps at once, and Session is "
         "available without global prompting");
}

void test_unsafe_executables_and_ancestors_are_rejected_without_blocking()
{
  CommandFixture fixture("unsafe-metadata");
  fixture.executable("unsafe");
  ::chmod((fixture.bin / "unsafe").c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IWGRP);
  auto writable_file = command::seal_command_plan(*command::CommandIntent::structured({"unsafe"}), fixture.options());

  fixture.executable("hard-target");
  std::filesystem::create_hard_link(fixture.bin / "hard-target", fixture.bin / "hard-linked");
  auto hard_link = command::seal_command_plan(*command::CommandIntent::structured({"hard-linked"}), fixture.options());

  auto const unsafe_dir = fixture.root / "unsafe-ancestor";
  fixture.executable_in(unsafe_dir, "ancestor-tool");
  ::chmod(unsafe_dir.c_str(), S_IRWXU | S_IWGRP);
  auto ancestor_options = fixture.options();
  ancestor_options.startup_path = unsafe_dir.string();
  auto unsafe_ancestor = command::seal_command_plan(*command::CommandIntent::structured({"ancestor-tool"}), ancestor_options);

  auto const fifo = fixture.bin / "fifo-tool";
  ::mkfifo(fifo.c_str(), S_IRUSR | S_IWUSR);
  auto fifo_plan = command::seal_command_plan(*command::CommandIntent::structured({"fifo-tool"}), fixture.options());

  expect(!writable_file && !hard_link && !unsafe_ancestor && !fifo_plan,
         "unsafe executable modes, link counts, writable ancestors, and FIFO candidates are rejected through nonblocking metadata inspection");
}

}  // namespace

void run_command_tests()
{
  test_compatibility_parser_is_lossless_or_raw_shell();
  test_intent_bounds_and_path_bounds_do_not_underflow();
  test_workspace_spoofs_are_not_inspection_recipes();
  test_recipe_paths_are_canonical_sealed_and_fresh();
  test_sealed_freshness_and_symlink_provenance();
  test_workspace_cwd_path_and_interpreter_freshness();
  test_raw_shell_binds_configured_shell_and_rejects_env_shebang();
  test_environment_is_synthetic_and_digest_bound();
  test_capabilities_scopes_and_standard_recipes();
  test_unsafe_executables_and_ancestors_are_rejected_without_blocking();
}
