#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/app/headless_policy.h"
#include "ava/agent/mode.h"
#include "ava/permissions/permission.h"
#include "ava/permissions/permission_rules.h"

#include <filesystem>
#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace ava::tests::app_runtime_tests {

using namespace ava::tests;

void test_command_classification()
{
  expect(ava::permissions::classify_command("git status --short").action == ava::permissions::PermissionAction::Allow,
         "git status is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("git diff").action == ava::permissions::PermissionAction::Allow,
         "git diff is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("git log --oneline").action == ava::permissions::PermissionAction::Allow,
         "git log is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("pwd").action == ava::permissions::PermissionAction::Allow, "pwd remains allowed as inert local inspection");
  expect(ava::permissions::classify_command("ls src").action == ava::permissions::PermissionAction::Allow, "ls remains allowed for safe relative paths");
  expect(ava::permissions::classify_command("rm -rf build").action == ava::permissions::PermissionAction::Deny, "rm -rf is denied");
  expect(ava::permissions::classify_command("git push origin main").action == ava::permissions::PermissionAction::Ask, "git push asks");
  expect(ava::permissions::classify_command("git diff --output=/tmp/ava-owned").action == ava::permissions::PermissionAction::Ask,
         "git diff output paths are not auto-allowed");
  expect(ava::permissions::classify_command("git diff --output out.diff").action == ava::permissions::PermissionAction::Ask,
         "git diff output option is not auto-allowed");
  expect(ava::permissions::classify_command("git diff --no-index empty .ssh/work_key").action == ava::permissions::PermissionAction::Ask,
         "relative credential paths are not auto-allowed");
  expect(ava::permissions::classify_command("cmake --build build").action == ava::permissions::PermissionAction::Ask,
         "cmake build requires explicit approval because repository build rules can execute code");
  expect(ava::permissions::classify_command("cmake --build build --target test").action == ava::permissions::PermissionAction::Ask,
         "cmake build target variants cannot bypass explicit approval");
  expect(ava::permissions::classify_command("cmake --build=build").action == ava::permissions::PermissionAction::Ask,
         "cmake equals-form build variants cannot bypass explicit approval");
  expect(ava::permissions::classify_command("/usr/bin/cmake --build build").action == ava::permissions::PermissionAction::Ask,
         "path-qualified cmake builds cannot bypass explicit approval");
  expect(ava::permissions::classify_command("cmake --build-and-test source build --build-generator Ninja").action == ava::permissions::PermissionAction::Ask,
         "cmake build-and-test variants cannot bypass explicit approval");
  expect(ava::permissions::classify_command("cmake --workflow --preset=ci").action == ava::permissions::PermissionAction::Ask,
         "cmake workflow variants cannot bypass explicit approval");
  expect(ava::permissions::classify_command("ctest --test-dir build").action == ava::permissions::PermissionAction::Ask,
         "ctest requires explicit approval because repository tests can execute code");
  expect(ava::permissions::classify_command("ctest -S dashboard.cmake").action == ava::permissions::PermissionAction::Ask,
         "ctest script variants cannot bypass explicit approval");
  expect(ava::permissions::classify_command("/usr/bin/ctest --test-dir build").action == ava::permissions::PermissionAction::Ask,
         "path-qualified ctest cannot bypass explicit approval");
  expect(ava::permissions::classify_command("cmake -S . -B build").action == ava::permissions::PermissionAction::Ask,
         "cmake configure remains an explicit-approval command");
  expect(ava::permissions::classify_command("rg hello src").action == ava::permissions::PermissionAction::Allow,
         "rg is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("rg --pre ./filter hello src").action == ava::permissions::PermissionAction::Deny,
         "rg preprocessors remain denied because they execute commands");
  expect(ava::permissions::decide(ava::permissions::PermissionRequest{.operation = ava::permissions::Operation::RunCommand,
                                                                      .mode = ava::agent::Mode::Plan,
                                                                      .workspace_dir = std::filesystem::current_path(),
                                                                      .target_path = {},
                                                                      .command = "git status --short"})
                 .action == ava::permissions::PermissionAction::Allow,
         "run-command decisions preserve safe command allows");
  expect(ava::permissions::classify_command("cmake -E cat ~/.config/ava/auth.json").action == ava::permissions::PermissionAction::Deny,
         "cmake -E helper access is denied");
  expect(ava::permissions::classify_command("cmake -P docs/plan.md").action == ava::permissions::PermissionAction::Deny, "cmake -P script execution is denied");
  expect(ava::permissions::classify_command("cmake --workflow -P docs/plan.md").action == ava::permissions::PermissionAction::Deny,
         "cmake workflow recognition does not override script execution denial");
  expect(ava::permissions::classify_command("cmake -E copy docs/plan.md src/new.cpp").action == ava::permissions::PermissionAction::Deny,
         "cmake -E copy mutation is denied");
  expect(ava::permissions::classify_command("python3 scripts/run.py").action == ava::permissions::PermissionAction::Deny, "interpreters are denied");
  expect(ava::permissions::classify_command("bash -lc ls").action == ava::permissions::PermissionAction::Deny, "shell interpreters remain denied");
}

void test_repository_build_test_headless_decision_matrix()
{
  ava::permissions::PermissionPrompt const prompt{.permission_request_id = "permreq_build_test",
                                                  .operation = ava::permissions::Operation::RunCommand,
                                                  .mode = ava::agent::Mode::Build,
                                                  .workspace_dir = std::filesystem::current_path(),
                                                  .target_path = {},
                                                  .command = "ctest --test-dir build",
                                                  .tool_name = "bash",
                                                  .reason = "repository test execution requires explicit approval",
                                                  .risk = ava::permissions::PermissionRisk::High};
  auto headless = ava::app::build_headless_permission_resolver(ava::app::HeadlessPermissionPolicyOptions{});
  auto const resolved = headless(prompt);
  expect(resolved && *resolved == ava::permissions::PermissionResolution::Deny,
         "headless mode fails closed for repository test execution that asks interactively");

  auto build_prompt = prompt;
  build_prompt.command = "cmake --build build";
  build_prompt.reason = "repository build execution requires explicit approval";
  auto const build_resolved = headless(build_prompt);
  expect(build_resolved && *build_resolved == ava::permissions::PermissionResolution::Deny,
         "headless mode fails closed for repository build execution that asks interactively");
}

}  // namespace ava::tests::app_runtime_tests
