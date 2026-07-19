#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/command/command.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <sys/stat.h>

namespace {

namespace command = ava::command;

struct CommandFixture
{
  std::filesystem::path root;
  std::filesystem::path workspace;
  std::filesystem::path home;
  std::filesystem::path bin;

  explicit CommandFixture(std::string_view name)
      : root(temp_root() / ("command-foundation-" + std::string(name))), workspace(root / "workspace"), home(root / "home"), bin(root / "bin")
  {
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    std::filesystem::create_directories(workspace);
    std::filesystem::create_directories(home);
    std::filesystem::create_directories(bin);
    chmod_tree();
  }

  void chmod_tree() const
  {
    ::chmod(root.c_str(), S_IRWXU);
    ::chmod(workspace.c_str(), S_IRWXU);
    ::chmod(home.c_str(), S_IRWXU);
    ::chmod(bin.c_str(), S_IRWXU);
  }

  void executable(std::string_view name, std::string_view content = "#!/bin/sh\nexit 0\n") const
  {
    auto const path = bin / name;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
    output.close();
    ::chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
  }

  command::CommandBuildOptions options(std::string profile_id = "command-test") const
  {
    return command::CommandBuildOptions{.workspace = workspace,
                                        .startup_path = bin.string(),
                                        .environment = command::CommandEnvironmentOptions{.profile_id = std::move(profile_id),
                                                                                          .user = "ava-test-user",
                                                                                          .logname = "ava-test-login",
                                                                                          .home = home,
                                                                                          .xdg_config_home = root / "xdg-config",
                                                                                          .xdg_cache_home = root / "xdg-cache",
                                                                                          .xdg_data_home = root / "xdg-data",
                                                                                          .xdg_state_home = root / "xdg-state",
                                                                                          .tmpdir = root / "tmp"},
                                        .workspace_script_recipes = {},
                                        .limits = {}};
  }
};

void test_command_intent_parser_and_argv_identity()
{
  auto structured = command::CommandIntent::structured({"echo", "", "two words", "--literal=;"});
  expect(structured && structured->lane() == command::CommandIntentLane::StructuredArgv &&
             structured->argv() == std::vector<std::string>({"echo", "", "two words", "--literal=;"}),
         "structured command intent preserves exact argv boundaries including empty arguments");

  auto compatibility = command::CommandIntent::compatibility("echo \"two words\" ''");
  expect(compatibility && compatibility->lane() == command::CommandIntentLane::Compatibility &&
             compatibility->argv() == std::vector<std::string>({"echo", "two words", ""}),
         "compatibility command parser preserves explicitly quoted empty arguments without losing argv identity");
}

void test_command_raw_shell_detection_and_mode_defaults()
{
  CommandFixture fixture("raw-shell");
  auto raw = command::CommandIntent::compatibility("echo safe; whoami");
  auto glob = command::CommandIntent::compatibility("echo *.cpp");
  auto quoted_glob = command::CommandIntent::compatibility("echo '*.cpp'");
  expect(raw && raw->lane() == command::CommandIntentLane::RawShell && raw->source_text() == "echo safe; whoami" && glob &&
             glob->lane() == command::CommandIntentLane::RawShell && quoted_glob && quoted_glob->lane() == command::CommandIntentLane::Compatibility &&
             quoted_glob->argv() == std::vector<std::string>({"echo", "*.cpp"}),
         "unquoted compatibility shell metacharacters select raw shell while quoted literals preserve direct argv");

  auto plan = command::seal_command_plan(*raw, fixture.options());
  expect(plan && plan->execution_domain() == command::CommandExecutionDomain::RawShell && plan->classification().level == command::CommandLevel::Critical &&
             plan->classification().ask_candidate && plan->classification().max_interactive_scope == command::InteractiveScope::Once,
         "raw-shell commands remain critical ASK candidates rather than hard-denied commands");

  command::CommandRuntimeOptions const defaults;
  expect(!command::command_mode_is_enabled(defaults) && !command::command_mode_is_prompt_only(defaults),
         "command foundation defaults to legacy mode without consulting arbitrary environment variables");
  expect(command::command_mode_is_prompt_only(command::CommandRuntimeOptions{.mode = command::CommandRuntimeMode::PromptOnly}) &&
             command::command_mode_is_enabled(command::CommandRuntimeOptions{.mode = command::CommandRuntimeMode::Enabled}),
         "explicit runtime options select prompt-only and enabled command modes");
}

void test_command_intent_control_and_size_bounds()
{
  auto control = command::CommandIntent::compatibility(std::string("echo ") + char(1));
  auto unterminated = command::CommandIntent::compatibility("echo 'unterminated");
  auto empty_executable = command::CommandIntent::structured({"", "argument"});
  auto empty_cwd = command::CommandIntent::structured({"tool"}, std::optional<std::filesystem::path>{std::filesystem::path{}});
  command::CommandLimits tight_limits;
  tight_limits.max_argument_bytes = 4;
  auto overlong = command::CommandIntent::structured({"tool", "oversized"}, std::nullopt, tight_limits);

  expect(!control && !unterminated && !empty_executable && !empty_cwd && !overlong,
         "command intents reject control bytes, unterminated syntax, ambiguous empty executable/cwd encoding, and bounded-field overflow");
}

void test_command_cwd_containment()
{
  CommandFixture fixture("cwd");
  fixture.executable("pwd");
  auto const outside = fixture.root / "outside";
  std::filesystem::create_directories(outside);
  ::chmod(outside.c_str(), S_IRWXU);

  auto escaped_cwd = command::CommandIntent::structured({"pwd"}, std::filesystem::path("../outside"));
  auto escaped_plan = command::seal_command_plan(*escaped_cwd, fixture.options());
  expect(!escaped_plan, "command plan rejects a canonical cwd that escapes its non-symlink workspace");

  auto nested = fixture.workspace / "nested";
  std::filesystem::create_directories(nested);
  ::chmod(nested.c_str(), S_IRWXU);
  auto inside_cwd = command::CommandIntent::structured({"pwd"}, std::filesystem::path("nested"));
  auto inside_plan = command::seal_command_plan(*inside_cwd, fixture.options());
  expect(inside_plan && inside_plan->cwd() == std::filesystem::canonical(nested), "command plan seals a canonical cwd inside its workspace");
}

void test_command_executable_origin_and_bare_absolute_equivalence()
{
  CommandFixture fixture("origin");
  auto const venv_bin = fixture.workspace / ".venv" / "bin";
  std::filesystem::create_directories(venv_bin);
  ::chmod((fixture.workspace / ".venv").c_str(), S_IRWXU);
  ::chmod(venv_bin.c_str(), S_IRWXU);
  auto const executable = venv_bin / "workspace-tool";
  {
    std::ofstream output(executable, std::ios::binary | std::ios::trunc);
    output << "#!/bin/sh\nexit 0\n";
  }
  ::chmod(executable.c_str(), S_IRUSR | S_IWUSR | S_IXUSR);

  auto options = fixture.options("origin-profile");
  options.startup_path = venv_bin.string();
  auto bare = command::CommandIntent::structured({"workspace-tool"});
  auto absolute = command::CommandIntent::structured({executable.string()});
  auto bare_plan = command::seal_command_plan(*bare, options);
  auto absolute_plan = command::seal_command_plan(*absolute, options);

  expect(bare_plan && absolute_plan && bare_plan->resolved_executable() && absolute_plan->resolved_executable() &&
             bare_plan->resolved_executable()->executable == absolute_plan->resolved_executable()->executable &&
             bare_plan->resolved_executable()->origin == command::ExecutableOrigin::Workspace &&
             absolute_plan->resolved_executable()->origin == command::ExecutableOrigin::Workspace,
         "bare and absolute executable requests resolve to the same canonical workspace executable and origin");
}

void test_command_path_safety_and_environment_positive_list()
{
  CommandFixture fixture("path-environment");
  fixture.executable("pwd");
  ::chmod(fixture.bin.c_str(), S_IRWXU | S_IWGRP | S_IWOTH);
  auto intent = command::CommandIntent::structured({"pwd"});
  auto unsafe_plan = command::seal_command_plan(*intent, fixture.options());
  expect(!unsafe_plan, "group/world-writable startup PATH directories are rejected instead of searched");
  ::chmod(fixture.bin.c_str(), S_IRWXU);

  ScopedEnvVar provider_secret("OPENAI_API_KEY", "provider-secret-sentinel");
  ScopedEnvVar cloud_secret("AWS_SECRET_ACCESS_KEY", "cloud-secret-sentinel");
  ScopedEnvVar loader_injection("LD_PRELOAD", "loader-sentinel");
  ScopedEnvVar git_injection("GIT_CONFIG_COUNT", "git-sentinel");
  ScopedEnvVar ava_injection("AVA_PRIVATE_TOKEN", "ava-sentinel");
  auto prepared = command::prepare_command(*intent, fixture.options("environment-profile"));
  std::vector<std::string> keys;
  if (prepared)
  {
    for (auto const& entry : prepared->environment.entries()) keys.push_back(entry.key);
  }
  auto const contains = [&keys](std::string_view key) { return std::ranges::find(keys, key) != keys.end(); };
  expect(prepared &&
             keys == std::vector<std::string>({"LANG", "LC_ALL", "LC_CTYPE", "TZ", "USER", "LOGNAME", "PATH", "HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME",
                                               "XDG_DATA_HOME", "XDG_STATE_HOME", "TMPDIR"}) &&
             !contains("OPENAI_API_KEY") && !contains("AWS_SECRET_ACCESS_KEY") && !contains("LD_PRELOAD") && !contains("GIT_CONFIG_COUNT") &&
             !contains("AVA_PRIVATE_TOKEN"),
         "environment profile is an ordered positive-list and excludes provider, cloud, loader, Git, and arbitrary AVA secret sentinels");
  expect(prepared && prepared->plan.display_json().find("provider-secret-sentinel") == std::string::npos &&
             prepared->plan.display_json().find("environment-profile") != std::string::npos,
         "plan display JSON stores the environment profile id but never environment values");
}

void test_command_fingerprint_and_freshness()
{
  CommandFixture fixture("fingerprint-freshness");
  fixture.executable("pwd");
  fixture.executable("fresh", "#!/bin/sh\nexit 0\n");
  auto first = command::CommandIntent::structured({"pwd"});
  auto second = command::CommandIntent::structured({"pwd", "-P"});
  auto first_plan = command::seal_command_plan(*first, fixture.options("fingerprint-profile"));
  auto second_plan = command::seal_command_plan(*second, fixture.options("fingerprint-profile"));
  expect(first_plan && second_plan && first_plan->fingerprint() != second_plan->fingerprint(),
         "length-prefixed plan fingerprints change when exact planned argv fields change");

  auto fresh = command::CommandIntent::structured({"fresh"});
  auto fresh_plan = command::seal_command_plan(*fresh, fixture.options("freshness-profile"));
  auto before = command::plan_is_fresh(*fresh_plan);
  {
    std::ofstream append(fixture.bin / "fresh", std::ios::binary | std::ios::app);
    append << "# changed\n";
  }
  auto after = command::plan_is_fresh(*fresh_plan);
  expect(before && *before && after && !*after, "plan_is_fresh detects executable metadata changes before any future execution");
}

void test_command_level_recipes_and_no_cross_family_widening()
{
  CommandFixture fixture("classification");
  for (auto const name : std::array{"cmake", "ctest", "ninja", "make", "cargo", "npm", "pytest", "python"}) fixture.executable(name);
  auto options = fixture.options("classification-profile");

  auto cmake = command::CommandIntent::structured({"cmake", "--build", "build"});
  auto npm_install = command::CommandIntent::structured({"npm", "install"});
  auto python_inline = command::CommandIntent::structured({"python", "-c", "print(1)"});
  auto npm_script = command::CommandIntent::structured({"npm", "run", "test"});
  auto cmake_helper = command::CommandIntent::structured({"cmake", "-E", "echo", "not-a-build"});
  auto ninja_target = command::CommandIntent::structured({"ninja", "unbounded-target"});
  auto ctest = command::CommandIntent::structured({"ctest", "--test-dir", "build"});
  auto ninja = command::CommandIntent::structured({"ninja", "-C", "build"});
  auto make = command::CommandIntent::structured({"make"});
  auto cargo = command::CommandIntent::structured({"cargo", "test"});
  auto pytest = command::CommandIntent::structured({"pytest", "tests"});

  auto const workspace_script_directory = fixture.workspace / "scripts";
  std::filesystem::create_directories(workspace_script_directory);
  ::chmod(workspace_script_directory.c_str(), S_IRWXU);
  auto const workspace_script = workspace_script_directory / "exact-script";
  {
    std::ofstream output(workspace_script, std::ios::binary | std::ios::trunc);
    output << "#!/bin/sh\nexit 0\n";
  }
  ::chmod(workspace_script.c_str(), S_IRUSR | S_IWUSR | S_IXUSR);
  options.startup_path = fixture.bin.string() + ":" + workspace_script_directory.string();
  options.workspace_script_recipes = {command::WorkspaceScriptRecipe{.script = "scripts/exact-script", .argv_tail = {"--fixed"}}};
  auto exact_workspace_script = command::CommandIntent::structured({"exact-script", "--fixed"});
  auto widened_workspace_script = command::CommandIntent::structured({"exact-script", "--other"});

  auto cmake_plan = command::seal_command_plan(*cmake, options);
  auto install_plan = command::seal_command_plan(*npm_install, options);
  auto inline_plan = command::seal_command_plan(*python_inline, options);
  auto script_plan = command::seal_command_plan(*npm_script, options);
  auto helper_plan = command::seal_command_plan(*cmake_helper, options);
  auto ninja_plan = command::seal_command_plan(*ninja_target, options);
  auto ctest_plan = command::seal_command_plan(*ctest, options);
  auto ninja_recipe_plan = command::seal_command_plan(*ninja, options);
  auto make_plan = command::seal_command_plan(*make, options);
  auto cargo_plan = command::seal_command_plan(*cargo, options);
  auto pytest_plan = command::seal_command_plan(*pytest, options);
  auto workspace_script_plan = command::seal_command_plan(*exact_workspace_script, options);
  auto widened_workspace_script_plan = command::seal_command_plan(*widened_workspace_script, options);

  expect(cmake_plan && cmake_plan->classification().level == command::CommandLevel::Standard &&
             cmake_plan->classification().family == command::CommandFamily::CmakeBuild && cmake_plan->classification().recipe &&
             cmake_plan->classification().capabilities.executes_mutable_project_code && cmake_plan->classification().capabilities.requires_containment &&
             cmake_plan->classification().max_interactive_scope == command::InteractiveScope::Workspace,
         "exact CMake build recipe is Standard, records its typed family, and requires containment for mutable project code");
  expect(install_plan && install_plan->classification().level == command::CommandLevel::Sensitive &&
             install_plan->classification().family == command::CommandFamily::InstallOrUpdate &&
             install_plan->classification().max_interactive_scope == command::InteractiveScope::Workspace,
         "install and update commands are Sensitive workspace-scope ASK candidates");
  expect(inline_plan && inline_plan->classification().level == command::CommandLevel::Critical &&
             inline_plan->classification().family == command::CommandFamily::InterpreterInline && inline_plan->classification().ask_candidate &&
             inline_plan->classification().max_interactive_scope == command::InteractiveScope::Once,
         "interpreter-inline commands are Critical once-scope ASK candidates rather than unconditional denies");
  expect(script_plan && script_plan->classification().level == command::CommandLevel::Standard &&
             script_plan->classification().family == command::CommandFamily::PackageManagerScript && script_plan->classification().recipe,
         "package-manager run-script matching requires an exact typed recipe");
  expect(
      ctest_plan && ctest_plan->classification().recipe && ctest_plan->classification().recipe->recipe == command::CommandRecipe::Ctest && ninja_recipe_plan &&
          ninja_recipe_plan->classification().recipe && ninja_recipe_plan->classification().recipe->recipe == command::CommandRecipe::Ninja && make_plan &&
          make_plan->classification().recipe && make_plan->classification().recipe->recipe == command::CommandRecipe::Make && cargo_plan &&
          cargo_plan->classification().recipe && cargo_plan->classification().recipe->recipe == command::CommandRecipe::CargoTest && pytest_plan &&
          pytest_plan->classification().recipe && pytest_plan->classification().recipe->recipe == command::CommandRecipe::Pytest && workspace_script_plan &&
          workspace_script_plan->classification().recipe && workspace_script_plan->classification().recipe->recipe == command::CommandRecipe::WorkspaceScript,
      "each initial project-code family requires and records its own exact typed Standard recipe");
  expect(
      helper_plan && helper_plan->classification().level == command::CommandLevel::Critical &&
          helper_plan->classification().family == command::CommandFamily::UnknownWrapper && !helper_plan->classification().recipe && ninja_plan &&
          ninja_plan->classification().level == command::CommandLevel::Critical &&
          ninja_plan->classification().family == command::CommandFamily::UnknownWrapper && widened_workspace_script_plan &&
          widened_workspace_script_plan->classification().level == command::CommandLevel::Critical &&
          widened_workspace_script_plan->classification().family == command::CommandFamily::UnknownWrapper,
      "CMake, Ninja, and configured workspace executable names alone do not widen cross-family helpers, targets, or argument variants into Standard recipes");
}

}  // namespace

void run_command_tests()
{
  test_command_intent_parser_and_argv_identity();
  test_command_raw_shell_detection_and_mode_defaults();
  test_command_intent_control_and_size_bounds();
  test_command_cwd_containment();
  test_command_executable_origin_and_bare_absolute_equivalence();
  test_command_path_safety_and_environment_positive_list();
  test_command_fingerprint_and_freshness();
  test_command_level_recipes_and_no_cross_family_widening();
}
