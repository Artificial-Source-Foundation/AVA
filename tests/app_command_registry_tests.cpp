#include "ava/app/command_registry.h"
#include "ava/app/commands.h"
#include "ava/app/runtime.h"

#include "ava/permissions/permission.h"

#include "tests/support/app_runtime_support.h"
#include "tests/support/test_harness.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace {

using namespace ava::tests;

ava::app::RuntimeSession open_test_session(std::filesystem::path const& root, std::filesystem::path const& workspace)
{
  auto paths = app_test_paths(root);
  ava::app::RuntimeOpenOptions options;
  options.workspace_dir = workspace;
  options.current_dir = workspace;
  options.paths = paths;
  auto session = ava::app::open_runtime_session(options);
  expect(session.has_value(), "command registry test opens runtime session");
  return std::move(*session);
}

ava::permissions::PermissionResolver allow_all_permissions(
    std::vector<ava::permissions::Operation>* operations = nullptr)
{
  return [operations](ava::permissions::PermissionPrompt const& prompt)
             -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    if (operations) operations->push_back(prompt.operation);
    return ava::permissions::PermissionResolution::Allow;
  };
}

ava::app::CommandRegistryEntry const* find_entry(ava::app::CommandRegistry const& registry, std::string_view command)
{
  return ava::app::find_command_registry_entry(registry, command);
}

bool has_diagnostic(ava::app::CommandRegistry const& registry, std::string_view command, std::string_view message)
{
  return std::ranges::any_of(registry.diagnostics, [&](ava::app::CommandRegistryDiagnostic const& diagnostic) {
    return diagnostic.command == command && diagnostic.message.find(message) != std::string::npos;
  });
}

void test_prompt_commands_load_project_global_and_expand_arguments()
{
  auto const root = temp_root() / "command-registry-prompts";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  ScopedEnvVar home("HOME", (root / "home").string());
  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", paths.config_home.string());

  write_app_test_file(paths.ava_config_dir / "commands" / "review.md",
                      "---\ndescription: Global review\n---\nGlobal $1\n");
  write_app_test_file(
      workspace / ".ava" / "commands" / "review.md",
      "---\ndescription: Project review\nargument-hint: <topic>\n---\nProject $1 $2 $@ $ARGUMENTS ${@:2}\n");
  write_app_test_file(workspace / ".ava" / "commands" / "ship.md", "Ship $$ $1 ${@:2:1}\n");

  auto session = open_test_session(root, workspace);
  auto registry =
      ava::app::load_command_registry(session, ava::app::CommandRegistryOptions{.include_mcp_prompts = false});
  auto const* review = find_entry(registry, "/review");
  expect(review != nullptr && review->source == ava::app::UnifiedCommandSource::PromptProject &&
             review->description == "Project review" && review->hint == "<topic>",
         "command registry loads project prompt commands before global collisions");
  expect(has_diagnostic(registry, "/review", "command collision"),
         "command registry records deterministic prompt-command collision diagnostics");

  auto expanded = ava::app::run_command(session, ava::app::CommandRequest{.command = "/review \"one arg\" two"});
  expect(expanded && expanded->handled && expanded->prompt_message &&
             expanded->prompt_message->find("Project one arg two one arg two \"one arg\" two two") != std::string::npos,
         "prompt command invocation expands positional, all-argument, raw, and slice placeholders safely");
  auto literal = ava::app::run_command(session, ava::app::CommandRequest{.command = "/ship release notes extra"});
  expect(
      literal && literal->prompt_message && literal->prompt_message->find("Ship $ release notes") != std::string::npos,
      "prompt command invocation treats $$ as a literal dollar and supports bounded slices");
}

void test_skill_commands_are_registry_entries_and_permissioned_prompts()
{
  auto const root = temp_root() / "command-registry-skills";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  ScopedEnvVar home("HOME", (root / "home").string());
  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", paths.config_home.string());
  write_app_test_file(workspace / ".ava" / "skills" / "release" / "SKILL.md",
                      "---\nname: release\ndescription: Prepare release work\n---\nRelease skill body\n");

  auto session = open_test_session(root, workspace);
  auto registry = ava::app::load_command_registry(session);
  expect(find_entry(registry, "/skill:release") != nullptr && find_entry(registry, "/release") != nullptr,
         "command registry exposes skills as namespaced and unnamespaced command entries");

  std::vector<ava::permissions::Operation> operations;
  auto result = ava::app::run_command(
      session,
      ava::app::CommandRequest{.command = "/skill:release", .permission_resolver = allow_all_permissions(&operations)});
  expect(result && result->handled && result->prompt_message &&
             result->prompt_message->find("<skill_content name=\"release\">") != std::string::npos &&
             result->prompt_message->find("Release skill body") != std::string::npos,
         "skill command invocation returns normal prompt content");
  expect(std::ranges::find(operations, ava::permissions::Operation::SkillLoad) != operations.end(),
         "skill command invocation requests skill-load permission");
}

void test_plugin_commands_are_registry_entries()
{
  auto const root = temp_root() / "command-registry-plugins";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  auto const plugin_dir = workspace / ".ava" / "plugins" / "com.example.cmd";
  write_app_test_file(plugin_dir / "plugin.json", app_test_plugin_manifest_json("com.example.cmd", "Command Plugin"));

  auto session = open_test_session(root, workspace);
  auto enabled = ava::app::run_command(session, ava::app::CommandRequest{.command = "/plugins enable com.example.cmd"});
  expect(enabled && enabled->handled, "plugin command registry test enables a project plugin without execution");

  auto registry = ava::app::load_command_registry(session);
  auto const* entry = find_entry(registry, "/plugin:com.example.cmd:todo");
  expect(entry != nullptr && entry->source == ava::app::UnifiedCommandSource::PluginCommand && entry->enabled &&
             entry->plugin_id == "com.example.cmd" && entry->plugin_command_name == "todo",
         "command registry exposes enabled plugin command entries with source metadata");
}

void test_mcp_prompts_are_registry_entries_and_permissioned_prompts()
{
  if (std::string_view(AVA_FAKE_MCP_SERVER_PATH).empty()) return;

  auto const root = temp_root() / "command-registry-mcp";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  write_app_test_file(workspace / ".ava" / "mcp.json",
                      app_test_mcp_config_json("demo", "Demo MCP", AVA_FAKE_MCP_SERVER_PATH));

  auto session = open_test_session(root, workspace);
  std::vector<ava::permissions::Operation> operations;
  auto registry = ava::app::load_command_registry(
      session, ava::app::CommandRegistryOptions{.include_mcp_prompts = true,
                                                .permission_resolver = allow_all_permissions(&operations)});
  auto const* entry = find_entry(registry, "/mcp:demo:release-notes");
  expect(entry != nullptr && entry->source == ava::app::UnifiedCommandSource::McpPrompt &&
             entry->mcp_server_id == "demo" && entry->mcp_prompt_name == "release-notes" &&
             !entry->mcp_arguments.empty() && entry->mcp_arguments[0].name == "topic",
         "command registry exposes MCP prompts as command entries with argument metadata");

  auto result = ava::app::run_command(
      session, ava::app::CommandRequest{.command = "/mcp:demo:release-notes AVA",
                                        .permission_resolver = allow_all_permissions(&operations)});
  expect(result && result->handled && result->prompt_message &&
             result->prompt_message->find("MCP prompt for AVA") != std::string::npos,
         "MCP prompt command invocation returns prompt text from prompts/get");
  auto alias_result = ava::app::run_command(
      session, ava::app::CommandRequest{.command = "/release-notes AVA",
                                        .permission_resolver = allow_all_permissions(&operations)});
  expect(alias_result && alias_result->handled && alias_result->prompt_message &&
             alias_result->prompt_message->find("MCP prompt for AVA") != std::string::npos,
         "unnamespaced MCP prompt command entries are invokable");
  expect(std::ranges::find(operations, ava::permissions::Operation::McpServerLaunch) != operations.end() &&
             std::ranges::find(operations, ava::permissions::Operation::McpServerConnect) != operations.end() &&
             std::ranges::find(operations, ava::permissions::Operation::McpToolCall) != operations.end(),
         "MCP prompt command invocation stays behind MCP launch, connect, and call permissions");
}

}  // namespace

void run_app_command_registry_tests()
{
  test_prompt_commands_load_project_global_and_expand_arguments();
  test_skill_commands_are_registry_entries_and_permissioned_prompts();
  test_plugin_commands_are_registry_entries();
  test_mcp_prompts_are_registry_entries_and_permissioned_prompts();
}
