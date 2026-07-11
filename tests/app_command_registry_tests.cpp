#include "sys.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/test_harness.h"
#include "ava/app/command_registry.h"
#include "ava/app/commands.h"
#include "ava/app/project_trust.h"
#include "ava/app/runtime.h"
#include "ava/permissions/permission.h"

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

ava::permissions::PermissionResolver allow_all_permissions(std::vector<ava::permissions::Operation>* operations = nullptr)
{
  return [operations](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    if (operations)
      operations->push_back(prompt.operation);
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
  auto trusted = ava::app::set_project_trust_decision(paths, workspace, true);
  expect(trusted.has_value(), trusted ? "command registry prompt test trusts project"
                                      : "command registry prompt test trusts project: " + trusted.error().format());

  write_app_test_file(paths.ava_config_dir / "commands" / "review.md", "---\ndescription: Global review\n---\nGlobal $1\n");
  write_app_test_file(workspace / ".ava" / "commands" / "review.md",
                      "---\ndescription: Project review\nargument-hint: <topic>\n---\nProject $1 $2 $@ $ARGUMENTS ${@:2}\n");
  write_app_test_file(workspace / ".ava" / "commands" / "ship.md", "Ship $$ $1 ${@:2:1}\n");
  write_app_test_file(workspace / ".ava" / "commands" / "defaults.md", "Default ${1:-release} ${2:-notes}\n");

  auto session = open_test_session(root, workspace);
  auto registry = ava::app::load_command_registry(session, ava::app::CommandRegistryOptions{.include_mcp_prompts = false});
  auto const* review = find_entry(registry, "/review");
  expect(review != nullptr && review->source == ava::app::UnifiedCommandSource::PromptProject && review->description == "Project review" &&
             review->hint == "<topic>",
         "command registry loads project prompt commands before global collisions");
  expect(has_diagnostic(registry, "/review", "command collision"), "command registry records deterministic prompt-command collision diagnostics");

  auto expanded = ava::app::run_command(session, ava::app::CommandRequest{.command = "/review \"one arg\" two"});
  expect(expanded && expanded->handled && expanded->prompt_message &&
             expanded->prompt_message->find("Project one arg two one arg two \"one arg\" two two") != std::string::npos,
         "prompt command invocation expands positional, all-argument, raw, and slice placeholders safely");
  auto literal = ava::app::run_command(session, ava::app::CommandRequest{.command = "/ship release notes extra"});
  expect(literal && literal->prompt_message && literal->prompt_message->find("Ship $ release notes") != std::string::npos,
         "prompt command invocation treats $$ as a literal dollar and supports bounded slices");
  auto defaulted = ava::app::run_command(session, ava::app::CommandRequest{.command = "/defaults"});
  expect(defaulted && defaulted->prompt_message && defaulted->prompt_message->find("Default release notes") != std::string::npos,
         "prompt command invocation applies positional defaults when arguments are missing");
  auto direct_defaults = ava::app::expand_prompt_command_template(
      "Defaults ${1:-seven} ${2:-fallback} ${3:-tail} ${0:-bad} ${x:-bad}", "\"\" custom");
  expect(direct_defaults && *direct_defaults == "Defaults seven custom tail ${0:-bad} ${x:-bad}",
         "prompt command expansion applies defaults for missing or empty args and leaves malformed defaults literal");
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
  auto trusted = ava::app::set_project_trust_decision(paths, workspace, true);
  expect(trusted.has_value(), trusted ? "command registry skill test trusts project"
                                      : "command registry skill test trusts project: " + trusted.error().format());
  write_app_test_file(workspace / ".ava" / "skills" / "release" / "SKILL.md",
                      "---\nname: release\ndescription: Prepare release work\n---\nRelease skill body\n");

  auto session = open_test_session(root, workspace);
  auto registry = ava::app::load_command_registry(session);
  expect(find_entry(registry, "/skill:release") != nullptr && find_entry(registry, "/release") != nullptr,
         "command registry exposes skills as namespaced and unnamespaced command entries");

  std::vector<ava::permissions::Operation> operations;
  auto result =
      ava::app::run_command(session, ava::app::CommandRequest{.command = "/skill:release", .permission_resolver = allow_all_permissions(&operations)});
  expect(result && result->handled && result->prompt_message && result->prompt_message->find("<skill_content name=\"release\">") != std::string::npos &&
             result->prompt_message->find("Release skill body") != std::string::npos,
         "skill command invocation returns normal prompt content");
  expect(std::ranges::find(operations, ava::permissions::Operation::SkillLoad) != operations.end(), "skill command invocation requests skill-load permission");
}

void test_plugin_commands_are_registry_entries()
{
  auto const root = temp_root() / "command-registry-plugins";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  auto trusted = ava::app::set_project_trust_decision(paths, workspace, true);
  expect(trusted.has_value(), trusted ? "command registry plugin test trusts project"
                                      : "command registry plugin test trusts project: " + trusted.error().format());
  auto const plugin_dir = workspace / ".ava" / "plugins" / "com.example.cmd";
  write_app_test_file(plugin_dir / "plugin.json", app_test_plugin_manifest_json("com.example.cmd", "Command Plugin"));

  auto session = open_test_session(root, workspace);
  auto enabled = ava::app::run_command(session, ava::app::CommandRequest{.command = "/plugins enable com.example.cmd"});
  expect(enabled && enabled->handled, "plugin command registry test enables a project plugin without execution");

  auto registry = ava::app::load_command_registry(session);
  auto const* entry = find_entry(registry, "/plugin:com.example.cmd:todo");
  expect(entry != nullptr && entry->source == ava::app::UnifiedCommandSource::PluginCommand && entry->enabled && entry->plugin_id == "com.example.cmd" &&
             entry->plugin_command_name == "todo",
         "command registry exposes enabled plugin command entries with source metadata");
}

void test_mcp_prompts_are_registry_entries_and_permissioned_prompts()
{
  if (std::string_view(AVA_FAKE_MCP_SERVER_PATH).empty())
    return;

  auto const root = temp_root() / "command-registry-mcp";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  auto trusted = ava::app::set_project_trust_decision(paths, workspace, true);
  expect(trusted.has_value(), trusted ? "command registry MCP test trusts project"
                                      : "command registry MCP test trusts project: " + trusted.error().format());
  write_app_test_file(workspace / ".ava" / "mcp.json", app_test_mcp_config_json("demo", "Demo MCP", AVA_FAKE_MCP_SERVER_PATH));

  auto session = open_test_session(root, workspace);
  std::vector<ava::permissions::Operation> operations;
  auto registry = ava::app::load_command_registry(
      session, ava::app::CommandRegistryOptions{.include_mcp_prompts = true, .permission_resolver = allow_all_permissions(&operations)});
  auto const* entry = find_entry(registry, "/mcp:demo:release-notes");
  expect(entry != nullptr && entry->source == ava::app::UnifiedCommandSource::McpPrompt && entry->mcp_server_id == "demo" &&
             entry->mcp_prompt_name == "release-notes" && !entry->mcp_arguments.empty() && entry->mcp_arguments[0].name == "topic",
         "command registry exposes MCP prompts as command entries with argument metadata");

  auto result = ava::app::run_command(
      session, ava::app::CommandRequest{.command = "/mcp:demo:release-notes AVA", .permission_resolver = allow_all_permissions(&operations)});
  expect(result && result->handled && result->prompt_message && result->prompt_message->find("MCP prompt for AVA") != std::string::npos,
         "MCP prompt command invocation returns prompt text from prompts/get");
  auto alias_result =
      ava::app::run_command(session, ava::app::CommandRequest{.command = "/release-notes AVA", .permission_resolver = allow_all_permissions(&operations)});
  expect(alias_result && alias_result->handled && alias_result->prompt_message && alias_result->prompt_message->find("MCP prompt for AVA") != std::string::npos,
         "unnamespaced MCP prompt command entries are invokable");
  expect(std::ranges::find(operations, ava::permissions::Operation::McpServerLaunch) != operations.end() &&
             std::ranges::find(operations, ava::permissions::Operation::McpServerConnect) != operations.end() &&
             std::ranges::find(operations, ava::permissions::Operation::McpToolCall) != operations.end(),
         "MCP prompt command invocation stays behind MCP launch, connect, and call permissions");
}

void test_project_trust_gates_project_resource_commands()
{
  auto const root = temp_root() / "command-registry-project-trust";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  ScopedEnvVar home("HOME", (root / "home").string());
  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", paths.config_home.string());
  write_app_test_file(paths.ava_config_dir / "commands" / "global.md", "Global command $1\n");
  write_app_test_file(paths.ava_config_dir / "APPEND_SYSTEM.md", "Global append instruction.\n");
  write_app_test_file(workspace / "AGENTS.md", "Project AGENTS context still loads while untrusted.\n");
  write_app_test_file(workspace / ".ava" / "SYSTEM.md", "Project system replacement.\n");
  write_app_test_file(workspace / ".ava" / "APPEND_SYSTEM.md", "Project append instruction.\n");
  write_app_test_file(workspace / ".ava" / "commands" / "local.md", "Local project command $1\n");
  write_app_test_file(workspace / ".ava" / "skills" / "local-skill" / "SKILL.md",
                      "---\nname: local-skill\ndescription: Local project skill\n---\nLocal skill body\n");
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.local" / "plugin.json",
                      "{\n"
                      "  \"schema_version\": 1,\n"
                      "  \"id\": \"com.example.local\",\n"
                      "  \"name\": \"Local Plugin\",\n"
                      "  \"version\": \"0.1.0\",\n"
                      "  \"api_version\": \"ava.plugin.v1\",\n"
                      "  \"description\": \"local plugin\",\n"
                      "  \"entrypoint\": {\"command\": \"node\", \"args\": [\"plugin.js\"]},\n"
                      "  \"capabilities\": [\"commands\"],\n"
                      "  \"contributes\": {\n"
                      "    \"commands\": [{\"name\": \"todo\", \"description\": \"Local todo\"}],\n"
                      "    \"prompts\": [{\"name\": \"review\", \"description\": \"Local review\", \"path\": \"prompts/review.md\"}],\n"
                      "    \"skills\": [{\"name\": \"triage\", \"description\": \"Local triage\", \"path\": \"skills/triage.md\"}]\n"
                      "  }\n"
                      "}");
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.local" / "prompts" / "review.md",
                      "Local plugin prompt\n");
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.local" / "skills" / "triage.md",
                      "Local plugin skill\n");

  auto session = open_test_session(root, workspace);
  expect(session.project_trust.decision == ava::app::ProjectTrustDecision::Unknown &&
             !ava::app::project_resources_trusted(session.project_trust),
         "runtime session defaults project resources to skipped without a trust decision");
  expect(session.system_prompt.find("Project AGENTS context still loads") != std::string::npos &&
             session.system_prompt.find("Global append instruction") != std::string::npos &&
             session.system_prompt.find("Project system replacement") == std::string::npos &&
             session.system_prompt.find("Project append instruction") == std::string::npos &&
             session.system_prompt.find("local-skill") == std::string::npos,
         "project AGENTS context loads while project system prompt files and skills remain gated");

  auto registry = ava::app::load_command_registry(session, ava::app::CommandRegistryOptions{.include_mcp_prompts = false});
  expect(find_entry(registry, "/global") != nullptr && find_entry(registry, "/local") == nullptr &&
             find_entry(registry, "/skill:local-skill") == nullptr,
         "untrusted sessions load global prompt commands but skip project prompt and skill commands");
  auto context_before = ava::app::run_command(session, ava::app::CommandRequest{.command = "/context"});
  expect(context_before && context_before->handled && !context_before->output.empty() &&
             context_before->output[0].find("project_trust=unknown project_resources=skipped") != std::string::npos &&
             context_before->output[0].find("system_prompt_sources=1") != std::string::npos &&
             context_before->output[0].find("append_system_prompt  global  APPEND_SYSTEM.md") != std::string::npos &&
             context_before->output[0].find("system_prompt  project  SYSTEM.md") == std::string::npos &&
             context_before->output[0].find("prompt_commands=1") != std::string::npos &&
             context_before->output[0].find("skills=0") != std::string::npos &&
             context_before->output[0].find("plugin_sources=0") != std::string::npos &&
             context_before->output[0].find("prompt_command  project  local") == std::string::npos,
         "untrusted /context reports skipped project resources without listing project freshness sources");
  auto plugins_before = ava::app::run_command(session, ava::app::CommandRequest{.command = "/plugins list"});
  expect(plugins_before && plugins_before->handled && !plugins_before->output.empty() &&
             plugins_before->output[0].find("com.example.local") == std::string::npos,
         "untrusted plugin commands do not discover project plugin manifests");

  auto trust = ava::app::run_command(session, ava::app::CommandRequest{.command = "/trust project"});
  expect(trust && trust->handled && !trust->output.empty() &&
             trust->output[0].find("trusted project resources") != std::string::npos &&
             trust->output[0].find("project_resources=enabled") != std::string::npos &&
             session.project_trust.decision == ava::app::ProjectTrustDecision::Trusted,
         "/trust project persists trust outside the workspace and reloads the runtime prompt state");
  expect(session.system_prompt.find("Project system replacement") != std::string::npos &&
             session.system_prompt.find("Project append instruction") != std::string::npos &&
             session.system_prompt.find("Global append instruction") == std::string::npos &&
             session.system_prompt.find("Implement changes directly") == std::string::npos &&
             session.system_prompt.find("local-skill") != std::string::npos,
         "trusted project resources replace/append the active system prompt after reload");

  registry = ava::app::load_command_registry(session, ava::app::CommandRegistryOptions{.include_mcp_prompts = false});
  expect(find_entry(registry, "/local") != nullptr && find_entry(registry, "/skill:local-skill") != nullptr,
         "trusted sessions expose project prompt and skill commands");
  auto local = ava::app::run_command(session, ava::app::CommandRequest{.command = "/local topic"});
  expect(local && local->handled && local->prompt_message &&
             local->prompt_message->find("Local project command topic") != std::string::npos,
         "trusted project prompt commands can be invoked");
  auto skill =
      ava::app::run_command(session, ava::app::CommandRequest{.command = "/skill:local-skill",
                                                              .permission_resolver = allow_all_permissions()});
  expect(skill && skill->handled && skill->prompt_message &&
             skill->prompt_message->find("Local skill body") != std::string::npos,
         "trusted project skill commands can be invoked");
  auto context_after = ava::app::run_command(session, ava::app::CommandRequest{.command = "/context"});
  expect(context_after && context_after->handled && !context_after->output.empty() &&
              context_after->output[0].find("project_trust=trusted project_resources=enabled") != std::string::npos &&
              context_after->output[0].find("system_prompt  project  SYSTEM.md") != std::string::npos &&
             context_after->output[0].find("append_system_prompt  project  APPEND_SYSTEM.md") != std::string::npos &&
             context_after->output[0].find("prompt_command  project  local") != std::string::npos &&
             context_after->output[0].find("skill  project  local-skill") != std::string::npos &&
              context_after->output[0].find("plugin_manifest  project  com.example.local/manifest") != std::string::npos,
          "trusted /context reports project prompt, skill, and plugin freshness sources");

  auto const global_append = paths.ava_config_dir / "APPEND_SYSTEM.md";
  auto const symlink_target = root / "outside-append.md";
  write_app_test_file(symlink_target, "outside append\n");
  std::error_code symlink_setup_error;
  std::filesystem::remove(global_append, symlink_setup_error);
  symlink_setup_error.clear();
  std::filesystem::create_symlink(symlink_target, global_append, symlink_setup_error);
  if (!symlink_setup_error)
  {
    auto denied_with_bad_prompt = ava::app::set_project_trust_decision(paths, workspace, false);
    expect(denied_with_bad_prompt.has_value(), denied_with_bad_prompt
                                                ? "test denies project trust for failed reload"
                                                : "test denies project trust for failed reload: " +
                                                      denied_with_bad_prompt.error().format());
    auto failed_reload = ava::app::run_command(session, ava::app::CommandRequest{.command = "/reload trust"});
    expect(failed_reload && failed_reload->handled && !failed_reload->output.empty() &&
               failed_reload->output[0].find("trust: error") != std::string::npos &&
               failed_reload->output[0].find("freshness source is not a regular file") != std::string::npos &&
               session.project_trust.decision == ava::app::ProjectTrustDecision::Trusted,
           "/reload trust keeps the old trust state if dependent prompt reload fails");
    expect(session.system_prompt.find("Project system replacement") != std::string::npos &&
               session.system_prompt.find("Project append instruction") != std::string::npos,
           "/reload trust keeps the old prompt state if dependent prompt reload fails");
    std::error_code restore_error;
    std::filesystem::remove(global_append, restore_error);
    write_app_test_file(global_append, "Global append instruction.\n");
  }
  else
  {
    write_app_test_file(global_append, "Global append instruction.\n");
  }

  auto denied = ava::app::set_project_trust_decision(paths, workspace, false);
  expect(denied.has_value(), denied ? "test denies project trust outside the active session"
                                    : "test denies project trust outside the active session: " + denied.error().format());
  auto reload_trust = ava::app::run_command(session, ava::app::CommandRequest{.command = "/reload trust"});
  expect(reload_trust && reload_trust->handled && !reload_trust->output.empty() &&
             reload_trust->output[0].find("Reload report:") != std::string::npos &&
             reload_trust->output[0].find("trust: loaded") != std::string::npos &&
             reload_trust->output[0].find("decision: denied") != std::string::npos &&
             reload_trust->output[0].find("project_resources: skipped") != std::string::npos &&
             session.project_trust.decision == ava::app::ProjectTrustDecision::Denied,
         "/reload trust applies external deny decisions and disables project resources");
  expect(session.system_prompt.find("Project system replacement") == std::string::npos &&
             session.system_prompt.find("Project append instruction") == std::string::npos &&
             session.system_prompt.find("local-skill") == std::string::npos,
         "/reload trust removes trusted project prompt content after denial");
  registry = ava::app::load_command_registry(session, ava::app::CommandRegistryOptions{.include_mcp_prompts = false});
  expect(find_entry(registry, "/local") == nullptr && find_entry(registry, "/skill:local-skill") == nullptr,
         "/reload trust removes project prompt and skill commands after denial");
}

}  // namespace

void run_app_command_registry_tests()
{
  test_prompt_commands_load_project_global_and_expand_arguments();
  test_skill_commands_are_registry_entries_and_permissioned_prompts();
  test_plugin_commands_are_registry_entries();
  test_mcp_prompts_are_registry_entries_and_permissioned_prompts();
  test_project_trust_gates_project_resource_commands();
}
