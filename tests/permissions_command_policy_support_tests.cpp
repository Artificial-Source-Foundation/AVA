#include <string>

#include "ava/permissions/command_policy_support.h"
#include "tests/support/test_harness.h"

namespace {

void test_command_policy_support_parses_argv()
{
  auto parsed = ava::permissions::detail::parse_command_argv("git diff \"src/main.cpp\" docs\\ plan.md");
  expect(parsed.ok && parsed.argv.size() == 4 && parsed.argv[0] == "git" && parsed.argv[2] == "src/main.cpp" &&
             parsed.argv[3] == "docs plan.md",
         "permission command policy support parses safe quotes and escapes");

  auto empty = ava::permissions::detail::parse_command_argv("   ");
  expect(!empty.ok && empty.reason.find("empty command") != std::string::npos,
         "permission command policy support rejects empty commands");
}

void test_command_policy_support_rejects_unsafe_syntax()
{
  auto meta = ava::permissions::detail::parse_command_argv("git status; rm -rf build");
  expect(!meta.ok && meta.reason.find("metacharacters") != std::string::npos,
         "permission command policy support rejects shell metacharacters");

  auto control = ava::permissions::detail::parse_command_argv(std::string("git\ndiff", 8));
  expect(!control.ok && control.reason.find("control byte") != std::string::npos,
         "permission command policy support rejects control bytes");

  auto unterminated = ava::permissions::detail::parse_command_argv("git diff \"src");
  expect(!unterminated.ok && unterminated.reason.find("unterminated") != std::string::npos,
         "permission command policy support rejects unterminated quotes");
}

void test_command_policy_support_path_safety()
{
  expect(ava::permissions::detail::is_safe_relative_path_arg("src/main.cpp") &&
             ava::permissions::detail::is_safe_relative_path_arg("-n") &&
             ava::permissions::detail::is_safe_relative_path_arg(".env.example"),
         "permission command policy support accepts safe relative paths and options");
  expect(!ava::permissions::detail::is_safe_relative_path_arg("../outside") &&
             !ava::permissions::detail::is_safe_relative_path_arg("/tmp/outside") &&
             !ava::permissions::detail::is_safe_relative_path_arg(".ssh/id_ed25519") &&
             !ava::permissions::detail::is_safe_relative_path_arg(".env.local") &&
             !ava::permissions::detail::is_safe_relative_path_arg(".docker/config.json"),
         "permission command policy support rejects path traversal, absolute paths, and likely secret paths");
}

}  // namespace

void run_permissions_command_policy_support_tests()
{
  test_command_policy_support_parses_argv();
  test_command_policy_support_rejects_unsafe_syntax();
  test_command_policy_support_path_safety();
}
