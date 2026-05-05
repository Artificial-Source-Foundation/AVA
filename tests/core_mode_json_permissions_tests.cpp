#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/print_mode.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/context/context_loader.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/permissions/command_policy.h"
#include "ava/permissions/permission.h"
#include "ava/provider/openai_provider.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

namespace {

void test_mode_parsing()
{
  auto const build = ava::agent::parse_mode("build");
  auto const plan = ava::agent::parse_mode("plan");
  auto const bad = ava::agent::parse_mode("other");

  expect(build && *build == ava::agent::Mode::Build, "build mode parses");
  expect(plan && *plan == ava::agent::Mode::Plan, "plan mode parses");
  expect(!bad, "unknown mode fails");
  expect(ava::agent::toggle_mode(ava::agent::Mode::Build) == ava::agent::Mode::Plan, "build toggles to plan");
}

void test_json_escape_control_characters()
{
  auto const escaped = ava::session::json_escape(std::string("a\x01\b\f", 4));
  expect(escaped == "a\\u0001\\b\\f", "json_escape escapes all JSON control characters");
}

void test_core_json_top_level_lookup()
{
  std::string const document =
      "{\"data\":{\"type\":\"bad\"},\"items\":[{\"type\":\"array_bad\"}],"
      "\"text\":\"contains \\\"type\\\":\\\"string_bad\\\"\",\"type\":\"good\"}";
  auto const type = ava::core::json::string_field(document, "type");
  expect(type && *type == "good", "JSON string_field reads only top-level object keys");
  expect(!ava::core::json::string_field(document, "items.type"), "JSON lookup does not invent nested paths");

  std::string const arrays_first = "{\"items\":[{\"models\":[{\"id\":\"bad\"}]}],\"models\":[{\"id\":\"ok\"}]}";
  auto const models = ava::core::json::objects_in_array_field(arrays_first, "models");
  auto const model_id = models.empty() ? std::optional<std::string>{} : ava::core::json::string_field(models[0], "id");
  expect(models.size() == 1 && model_id && *model_id == "ok",
         "JSON array lookup ignores nested arrays before top-level field");
  auto const paths = ava::core::json::strings_in_array_field(
      "{\"items\":[\"bad\"],\"changed_paths\":[\"src/main.cpp\",\"a\\\\b\",{\"skip\":\"nested\"},[\"also skip\"]]}",
      "changed_paths");
  expect(paths.size() == 2 && paths[0] == "src/main.cpp" && paths[1] == "a\\b",
         "JSON string array lookup reads only top-level string array elements");

  auto const surrogate_pair = ava::core::json::string_field("{\"text\":\"\\uD834\\uDD1E\"}", "text");
  expect(surrogate_pair && *surrogate_pair == std::string("\xF0\x9D\x84\x9E"),
         "JSON string_field decodes UTF-16 surrogate pairs");
  auto const lone_high_surrogate = ava::core::json::string_field("{\"text\":\"\\uD834x\"}", "text");
  expect(lone_high_surrogate && *lone_high_surrogate == std::string("\xEF\xBF\xBDx"),
         "JSON string_field replaces lone high surrogates");
  auto const lone_low_surrogate = ava::core::json::string_field("{\"text\":\"\\uDD1E\"}", "text");
  expect(lone_low_surrogate && *lone_low_surrogate == std::string("\xEF\xBF\xBD"),
         "JSON string_field replaces lone low surrogates");
  auto const high_then_non_low = ava::core::json::string_field("{\"text\":\"\\uD834\\u0061\"}", "text");
  expect(high_then_non_low && *high_then_non_low == std::string("\xEF\xBF\xBD") + "a",
         "JSON string_field leaves non-low escape after replacing high surrogate");
  expect(!ava::core::json::string_field("{\"text\":\"\\u12xz\"}", "text"),
         "JSON string_field rejects malformed unicode escapes");
  expect(!ava::core::json::string_field("{\"text\":\"\\q\"}", "text"), "JSON string_field rejects invalid escapes");
}

void test_permission_command_policy_helpers()
{
  expect(ava::permissions::classify_command("git status --short").action == ava::permissions::PermissionAction::Allow,
         "permission command policy allows safe git status");
  expect(ava::permissions::classify_command("git diff --output=/tmp/out").action ==
             ava::permissions::PermissionAction::Ask,
         "permission command policy asks on path-carrying git output options");
  expect(ava::permissions::classify_command("rg --pre ./filter pattern src").action ==
             ava::permissions::PermissionAction::Deny,
         "permission command policy denies rg preprocessors");
  expect(ava::permissions::classify_command("bash -lc ls").action == ava::permissions::PermissionAction::Deny,
         "permission command policy denies arbitrary script shells");
  expect(ava::permissions::classify_command("sleep 0.1").action == ava::permissions::PermissionAction::Allow,
         "permission command policy allows simple numeric sleep");
}

void test_permission_defaults()
{
  auto const workspace = std::filesystem::current_path();

  auto const normal_edit = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::EditFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace / "src/main.cpp",
      .command = "",
  });
  expect(normal_edit.action == ava::permissions::PermissionAction::Allow &&
             normal_edit.risk == ava::permissions::PermissionRisk::Medium,
         "build mode allows workspace edits with medium mutation risk");

  auto const plan_source_edit = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::EditFile,
      .mode = ava::agent::Mode::Plan,
      .workspace_dir = workspace,
      .target_path = workspace / "src/main.cpp",
      .command = "",
  });
  expect(plan_source_edit.action == ava::permissions::PermissionAction::Deny &&
             plan_source_edit.risk == ava::permissions::PermissionRisk::High,
         "plan mode denies source edits with high semantic risk");

  auto const plan_doc_edit = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::EditFile,
      .mode = ava::agent::Mode::Plan,
      .workspace_dir = workspace,
      .target_path = workspace / "docs/versions/0.1.md",
      .command = "",
  });
  expect(plan_doc_edit.action == ava::permissions::PermissionAction::Allow, "plan mode allows planning markdown");

  auto const secret_read = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace / ".env",
      .command = "",
  });
  expect(secret_read.action == ava::permissions::PermissionAction::Deny &&
             secret_read.risk == ava::permissions::PermissionRisk::Critical,
         "secret files are denied with critical semantic risk");

  auto const npmrc_read = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace / ".npmrc",
      .command = "",
  });
  expect(npmrc_read.action == ava::permissions::PermissionAction::Deny, "common credential files are denied");

  auto const ssh_read = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace / ".ssh/work_key",
      .command = "",
  });
  expect(ssh_read.action == ava::permissions::PermissionAction::Deny, "credential directories are denied");

  auto const external_read = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace.parent_path() / "outside.txt",
      .command = "",
  });
  expect(external_read.action == ava::permissions::PermissionAction::Ask &&
             external_read.risk == ava::permissions::PermissionRisk::High,
         "external paths ask with high semantic risk");

  auto const workspace_lsp = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::LspQuery,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace / "src/main.cpp",
      .command = "",
  });
  expect(workspace_lsp.action == ava::permissions::PermissionAction::Allow, "workspace LSP diagnostics are allowed");
  expect(ava::permissions::to_string(ava::permissions::Operation::LspQuery) == "lsp.query",
         "LSP query operation string is stable");
  expect(ava::permissions::to_string(ava::permissions::PermissionRisk::Critical) == "critical",
         "permission risk string is stable");

  auto const secret_lsp = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::LspQuery,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace / ".env",
      .command = "",
  });
  expect(secret_lsp.action == ava::permissions::PermissionAction::Deny, "LSP diagnostics deny secret paths");

  auto const external_lsp = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::LspQuery,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace.parent_path() / "outside.cpp",
      .command = "",
  });
  expect(external_lsp.action == ava::permissions::PermissionAction::Ask, "external LSP diagnostic paths ask");

  auto const symlink_workspace = temp_root() / "symlink-workspace";
  auto const outside = temp_root() / "outside";
  std::filesystem::create_directories(symlink_workspace);
  std::filesystem::create_directories(outside);
  std::error_code symlink_error;
  auto const link = symlink_workspace / "link-outside";
  std::filesystem::remove(link, symlink_error);
  std::filesystem::create_directory_symlink(outside, link, symlink_error);
  if (!symlink_error) {
    auto const symlink_escape = ava::permissions::decide(ava::permissions::PermissionRequest{
        .operation = ava::permissions::Operation::ReadFile,
        .mode = ava::agent::Mode::Build,
        .workspace_dir = symlink_workspace,
        .target_path = link / "outside.txt",
        .command = "",
    });
    expect(symlink_escape.action == ava::permissions::PermissionAction::Ask, "symlink workspace escape asks");
  }
}

}  // namespace

void run_core_mode_tests()
{
  test_mode_parsing();
}

void run_core_json_permission_tests()
{
  test_json_escape_control_characters();
  test_core_json_top_level_lookup();
  test_permission_command_policy_helpers();
  test_permission_defaults();
}
