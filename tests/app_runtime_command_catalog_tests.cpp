#include "sys.h"
#include "tests/app_runtime_test_declarations.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/test_harness.h"
#include "tests/support/tui_test_support.h"
#include "ava/app/command_catalog.h"
#include "ava/app/command_palette.h"
#include "ava/app/command_sessions.h"
#include "ava/app/commands.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_model.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/agent/mode.h"
#include "ava/tools/file_tools.h"
#include "ava/config/model_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_metadata.h"
#include "ava/permissions/permission.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace ava::tests::app_runtime_tests {

using namespace ava::tests;

void app_command_dispatcher_catalog_part(ava::app::runtime::Session* session, ava::config::XdgPaths const& paths, std::filesystem::path const& workspace,
                                         std::vector<ava::app::CommandHotkey> const& custom_hotkeys)
{
  auto const slash_items = ava::app::command_catalog_slash_items_1(*session, custom_hotkeys);
  auto const file_reference_items = ava::app::file_reference_items(*session);
  auto has_alias = [](ava::tui::SlashCommandItem const& item, std::string_view value) { return std::ranges::find(item.aliases, value) != item.aliases.end(); };
  auto has_file_reference = [&file_reference_items](std::string_view value) {
    return std::ranges::any_of(file_reference_items, [&](auto const& item) { return item.value == value; });
  };
  auto const* connect_item = tui_test_support::find_slash_command_item(slash_items, "/connect");
  auto const* hotkeys_item = tui_test_support::find_slash_command_item(slash_items, "/hotkeys");
  auto const* models_item = tui_test_support::find_slash_command_item(slash_items, "/models");
  auto const* providers_item = tui_test_support::find_slash_command_item(slash_items, "/providers");
  auto const* scoped_models_item = tui_test_support::find_slash_command_item(slash_items, "/scoped-models");
  auto const* details_item = tui_test_support::find_slash_command_item(slash_items, "/details");
  auto const* tool_item = tui_test_support::find_slash_command_item(slash_items, "/tool");
  auto const* diff_item = tui_test_support::find_slash_command_item(slash_items, "/diff");
  auto const* copy_item = tui_test_support::find_slash_command_item(slash_items, "/copy");
  auto const* export_item = tui_test_support::find_slash_command_item(slash_items, "/export");
  auto const* import_item = tui_test_support::find_slash_command_item(slash_items, "/import");
  expect(details_item != nullptr && details_item->hint == "[compact|rich|expanded]" && details_item->description.find("Rich") != std::string::npos,
         "slash catalog exposes explicit Compact, Rich, and Expanded tool-card modes");
  expect(tool_item != nullptr && tool_item->hint == "[query]" && has_alias(*tool_item, "/tools") &&
             tool_item->description.find("latest or matching tool details") != std::string::npos,
         "slash catalog exposes /tool for visible TUI tool-card inspection");
  expect(diff_item != nullptr && diff_item->hint == "[query]" && diff_item->description.find("latest or matching tool diff") != std::string::npos,
         "slash catalog exposes /diff for visible TUI tool-diff inspection");
  expect(copy_item != nullptr && copy_item->hint.empty() && copy_item->description.find("latest AVA message") != std::string::npos &&
             copy_item->description.find("tool") != std::string::npos && copy_item->description.find("permission details") != std::string::npos,
         "slash catalog exposes Pi-style /copy clipboard command without blocking exact-submit behavior");
  expect(scoped_models_item != nullptr && scoped_models_item->description.find("Ctrl+P cycling") != std::string::npos,
         "slash catalog exposes Pi-style /scoped-models model cycle selector entry point");
  expect(models_item != nullptr && models_item->hint == "[query]" && models_item->description.find("model selector") != std::string::npos,
         "slash catalog explains that exact /models opens the selector while an argument filters configured models");
  expect(providers_item != nullptr && providers_item->hint == "[query]" && providers_item->description.find("credential status") != std::string::npos,
         "slash catalog exposes provider capability and credential status discovery");
  auto const* sessions_item = tui_test_support::find_slash_command_item(slash_items, "/sessions");
  auto const* context_item = tui_test_support::find_slash_command_item(slash_items, "/context");
  auto const* read_item = tui_test_support::find_slash_command_item(slash_items, "/read");
  auto const* write_item = tui_test_support::find_slash_command_item(slash_items, "/write");
  auto const* glob_item = tui_test_support::find_slash_command_item(slash_items, "/glob");
  auto const* find_item = tui_test_support::find_slash_command_item(slash_items, "/find");
  auto const* ls_item = tui_test_support::find_slash_command_item(slash_items, "/ls");
  auto const* grep_item = tui_test_support::find_slash_command_item(slash_items, "/grep");
  auto const* mcp_item = tui_test_support::find_slash_command_item(slash_items, "/mcp");
  auto const* plugin_item = tui_test_support::find_slash_command_item(slash_items, "/plugin");
  auto const* permissions_item = tui_test_support::find_slash_command_item(slash_items, "/permissions");
  expect(
      !tui_test_support::has_slash_argument_completion(connect_item, 0, "openai") &&
          !tui_test_support::has_slash_argument_completion(connect_item, 1, "api-key") &&
          !tui_test_support::has_slash_argument_completion(connect_item, 1, "browser-oauth", {"openai"}) &&
          !tui_test_support::has_slash_argument_completion(connect_item, 1, "headless-oauth", {"openai"}) &&
          tui_test_support::has_slash_argument_completion(models_item, 0, "openai/gpt-5.5") &&
          !tui_test_support::has_slash_argument_completion(models_item, 0, "gpt-5.5") &&
          tui_test_support::has_slash_argument_completion(sessions_item, 0, session->store.session_id()) &&
          tui_test_support::has_slash_argument_completion(context_item, 0, (workspace / "AGENTS.md").generic_string()) &&
          tui_test_support::has_slash_argument_completion(read_item, 0, "src/main.cpp") &&
          tui_test_support::has_slash_argument_completion(write_item, 0, "src/main.cpp") &&
          tui_test_support::has_slash_argument_completion(glob_item, 0, "src/**") && tui_test_support::has_slash_argument_completion(find_item, 0, "src/**") &&
          tui_test_support::has_slash_argument_completion(ls_item, 0, "src/main.cpp") &&
          tui_test_support::has_slash_argument_completion(grep_item, 1, "src/**") &&
          tui_test_support::has_slash_argument_completion(export_item, 0, "markdown") &&
          tui_test_support::has_slash_argument_completion(export_item, 0, "html") && tui_test_support::has_slash_argument_completion(export_item, 0, "jsonl") &&
          tui_test_support::has_slash_argument_completion(import_item, 1, "--confirm") &&
          tui_test_support::has_slash_argument_completion(hotkeys_item, 0, "init") &&
          tui_test_support::has_slash_argument_completion(hotkeys_item, 0, "import") &&
          tui_test_support::has_slash_argument_completion(hotkeys_item, 0, "set") &&
          tui_test_support::has_slash_argument_completion(hotkeys_item, 0, "reset") &&
          tui_test_support::has_slash_argument_completion(hotkeys_item, 1, "submit", {"set"}) &&
          tui_test_support::has_slash_argument_completion(hotkeys_item, 1, "variant_cycle", {"set"}) &&
          tui_test_support::has_slash_argument_completion(hotkeys_item, 1, "submit", {"reset"}) &&
          tui_test_support::has_slash_argument_completion(hotkeys_item, 0, "validate") &&
          tui_test_support::has_slash_argument_completion(hotkeys_item, 1, "--force", {"init"}) &&
          tui_test_support::has_slash_argument_completion(hotkeys_item, 2, "--force", {"import"}) &&
          !tui_test_support::has_slash_argument_completion(read_item, 0, "my folder/space file.txt") &&
          !tui_test_support::has_slash_argument_completion(read_item, 0, "docs/reference-code/pi/reference-only.md") && has_file_reference("src/main.cpp") &&
          has_file_reference("my folder/space file.txt") && !has_file_reference("docs/reference-code/pi/reference-only.md") &&
          tui_test_support::has_slash_argument_completion(mcp_item, 1, "fs", {"inspect"}) &&
          tui_test_support::has_slash_argument_completion(plugin_item, 1, "com.example.project", {"run"}) &&
          tui_test_support::has_slash_argument_completion(plugin_item, 2, "todo", {"run", "com.example.project"}) &&
          tui_test_support::has_slash_argument_completion(permissions_item, 0, "list") &&
          tui_test_support::has_slash_argument_completion(permissions_item, 0, "audit") &&
          tui_test_support::has_slash_argument_completion(permissions_item, 0, "add") &&
          tui_test_support::has_slash_argument_completion(permissions_item, 1, "export", {"audit"}) &&
          tui_test_support::has_slash_argument_completion(permissions_item, 1, "summary", {"audit"}) &&
          tui_test_support::has_slash_argument_completion(permissions_item, 1, "show", {"audit"}) &&
          tui_test_support::has_slash_argument_completion(permissions_item, 1, "action=allow", {"add"}) &&
          tui_test_support::has_slash_argument_completion(permissions_item, 1, "operation=read", {"add"}) &&
          tui_test_support::has_slash_argument_completion(permissions_item, 1, "reason=", {"add"}),
      "command catalog argument completions keep /connect provider and method choices in the modal while "
      "populating model, session, context, export format, file path, file reference, MCP, plugin, and permission-rule metadata");
  auto disable_plugin = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins disable com.example.project"});
  expect(disable_plugin && disable_plugin->handled && !disable_plugin->output.empty() &&
             disable_plugin->output[0].find("Disabled project plugin com.example.project") != std::string::npos &&
             disable_plugin->output[0].find("No plugin process was stopped") != std::string::npos,
         "command dispatcher /plugins disable records state without stopping plugin processes");
  auto validate_plugin = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins validate .ava/plugins/com.example.project/plugin.json"});
  expect(validate_plugin && validate_plugin->handled && !validate_plugin->output.empty() &&
             validate_plugin->output[0].find("Valid plugin manifest") != std::string::npos &&
             validate_plugin->output[0].find("no entrypoint was executed") != std::string::npos,
         "command dispatcher /plugins validate parses manifests without executing entrypoints");
  auto plugin_failures = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins failures"});
  expect(plugin_failures && plugin_failures->handled && !plugin_failures->output.empty() &&
             plugin_failures->output[0].find("com.example.bad") != std::string::npos,
         "command dispatcher /plugins failures reports invalid discovered manifests");
  auto models = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/model"});
  expect(models && models->handled && !models->output.empty() && models->output[0].find("Models:") != std::string::npos &&
             models->output[0].find("current ") != std::string::npos && models->output[0].find("reasoning current") != std::string::npos &&
             models->output[0].find("reasoning levels: low, medium, high, xhigh") != std::string::npos &&
             models->output[0].find("reasoning params") != std::string::npos && models->output[0].find("reasoning.effort=<level>") != std::string::npos &&
             models->output[0].find("reasoning.summary=auto") != std::string::npos && models->output[0].find("reasoning format") != std::string::npos &&
             models->output[0].find("Shift+Tab or Ctrl+T cycles") != std::string::npos,
         "command dispatcher lists provider/model reasoning metadata and documents TUI reasoning cycling");
  auto filtered_models = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/models gpt-5.5"});
  expect(filtered_models && filtered_models->handled && !filtered_models->output.empty() &&
             filtered_models->output[0].find("filter gpt-5.5") != std::string::npos && filtered_models->output[0].find("gpt-5.5") != std::string::npos,
         "command dispatcher /models accepts backend-backed autocomplete query text without switching models");
  auto diagnostic_models = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/models diagnostic-local"});
  expect(diagnostic_models && diagnostic_models->handled && !diagnostic_models->output.empty() &&
             diagnostic_models->output[0].find("Diagnostic Local") != std::string::npos &&
             diagnostic_models->output[0].find("diagnostics:") != std::string::npos &&
             diagnostic_models->output[0].find("custom model missing context_window_tokens") != std::string::npos &&
             diagnostic_models->output[0].find("custom model api_family does not match provider profile") != std::string::npos &&
             diagnostic_models->output[0].find("custom model has unknown tool support") != std::string::npos &&
             diagnostic_models->output[0].find("reasoning model has no reasoning_levels") != std::string::npos,
         "command dispatcher /models reports actionable custom-model diagnostics");
  auto unavailable_models = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/models ghost"});
  expect(unavailable_models && unavailable_models->handled && !unavailable_models->output.empty() &&
             unavailable_models->output[0].find("Remote Missing") != std::string::npos &&
             unavailable_models->output[0].find("provider is not registered") != std::string::npos &&
             unavailable_models->output[0].find("custom model api_family is not recognized") != std::string::npos &&
             unavailable_models->output[0].find("selector is disabled") != std::string::npos,
         "command dispatcher /models reports unregistered provider diagnostics");
  {
    ScopedEnvVar openai_key("OPENAI_API_KEY", "providers-secret-openai-key");
    auto providers = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/providers"});
    expect(providers && providers->handled && !providers->output.empty() && providers->output[0].find("Providers:") != std::string::npos &&
               providers->output[0].find("openai  OpenAI") != std::string::npos && providers->output[0].find("anthropic  Anthropic") != std::string::npos &&
               providers->output[0].find("auth=api_key source=env:OPENAI_API_KEY") != std::string::npos &&
               providers->output[0].find("providers-secret-openai-key") == std::string::npos &&
               providers->output[0].find("oauth=interactive supported") != std::string::npos &&
               providers->output[0].find("interactive deferred pending official third-party flow") != std::string::npos &&
               providers->output[0].find("vercel  Vercel AI Gateway  unavailable: no runtime provider factory") != std::string::npos,
           "command dispatcher /providers lists provider runtime, auth, and OAuth status without printing secrets");
  }
  {
    ScopedEnvVar anthropic_oauth("ANTHROPIC_OAUTH_TOKEN", "providers-secret-anthropic-token");
    auto filtered_providers = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/providers anthropic"});
    expect(filtered_providers && filtered_providers->handled && !filtered_providers->output.empty() &&
               filtered_providers->output[0].find("filter anthropic") != std::string::npos &&
               filtered_providers->output[0].find("anthropic  Anthropic") != std::string::npos &&
               filtered_providers->output[0].find("auth=oauth source=env:ANTHROPIC_OAUTH_TOKEN") != std::string::npos &&
               filtered_providers->output[0].find("providers-secret-anthropic-token") == std::string::npos &&
               filtered_providers->output[0].find("openai  OpenAI") == std::string::npos,
           "command dispatcher /providers accepts query text without exposing unrelated provider rows or env token values");
  }

  auto context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context"});
  expect(context && context->handled && !context->output.empty() && context->output[0].find("Context freshness:") != std::string::npos &&
             context->output[0].find("mode=plan") != std::string::npos && context->output[0].find("model=openai/gpt-5.5") != std::string::npos &&
             context->output[0].find("prompt=override") != std::string::npos && context->output[0].find("plan.txt") != std::string::npos &&
             context->output[0].find("context_sources=1") != std::string::npos && context->output[0].find("prompt_commands=1") != std::string::npos &&
             context->output[0].find("skills=") != std::string::npos && context->output[0].find("plugin_sources=2") != std::string::npos &&
             context->output[0].find("workspace") != std::string::npos && context->output[0].find("AGENTS.md") != std::string::npos &&
             context->output[0].find("prompt_command  project  prompt-check") != std::string::npos &&
             context->output[0].find("skill  project  dispatcher-skill") != std::string::npos &&
             context->output[0].find("plugin_manifest  project  com.example.project/manifest") != std::string::npos &&
             context->output[0].find("plugin_prompt  project  com.example.project/review") == std::string::npos &&
             context->output[0].find("plugin_skill  project  com.example.project/triage") == std::string::npos &&
             context->output[0].find("loaded_bytes=") != std::string::npos && context->output[0].find("status=changed") != std::string::npos &&
             context->output[0].find("current_bytes=") != std::string::npos,
         "command dispatcher /context reports context freshness metadata without loading disabled plugin resources");
  auto prompt_context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context prompt"});
  expect(prompt_context && prompt_context->handled && !prompt_context->output.empty() &&
             prompt_context->output[0].find("prompt=override") != std::string::npos && prompt_context->output[0].find("plan.txt") != std::string::npos &&
             prompt_context->output[0].find("prompt_command  project  prompt-check") != std::string::npos &&
             prompt_context->output[0].find("plugin_prompt  project  com.example.project/review") == std::string::npos &&
             prompt_context->output[0].find("AGENTS.md") == std::string::npos,
         "command dispatcher /context can filter to prompt freshness metadata");
  auto prompt_command_context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context prompt-check"});
  expect(prompt_command_context && prompt_command_context->handled && !prompt_command_context->output.empty() &&
             prompt_command_context->output[0].find("prompt_command  project  prompt-check") != std::string::npos &&
             prompt_command_context->output[0].find("status=changed") != std::string::npos &&
             prompt_command_context->output[0].find("AGENTS.md") == std::string::npos,
         "command dispatcher /context can filter to prompt command freshness metadata");
  auto skill_context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context dispatcher-skill"});
  expect(skill_context && skill_context->handled && !skill_context->output.empty() &&
             skill_context->output[0].find("skill  project  dispatcher-skill") != std::string::npos &&
             skill_context->output[0].find("status=changed") != std::string::npos && skill_context->output[0].find("AGENTS.md") == std::string::npos,
         "command dispatcher /context can filter to loaded skill freshness metadata");
  auto plugin_prompt_context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context review"});
  expect(plugin_prompt_context && plugin_prompt_context->handled && !plugin_prompt_context->output.empty() &&
             plugin_prompt_context->output[0].find("No context sources matching: review") != std::string::npos,
         "command dispatcher /context does not report disabled plugin prompt resources loaded before plugin enablement");
  auto filtered_context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context AGENTS"});
  expect(filtered_context && filtered_context->handled && !filtered_context->output.empty() &&
             filtered_context->output[0].find("Context freshness:") != std::string::npos &&
             filtered_context->output[0].find("AGENTS.md") != std::string::npos && filtered_context->output[0].find("status=changed") != std::string::npos &&
             filtered_context->output[0].find("prompt=override") == std::string::npos,
         "command dispatcher /context accepts backend context-source query text");
  auto reload_prompts = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/reload prompts"});
  expect(reload_prompts && reload_prompts->handled && !reload_prompts->output.empty() &&
             reload_prompts->output[0].find("Reload report:") != std::string::npos && reload_prompts->output[0].find("prompts: loaded") != std::string::npos &&
             reload_prompts->output[0].find("context_sources: 1") != std::string::npos &&
             session->system_prompt().find("dispatcher context changed after session open") != std::string::npos,
         "command dispatcher /reload prompts re-reads prompt and context resources without restarting");
  write_app_test_file(paths.models_file, "{\n  \"scoped_model_cycle\": [\"openai/gpt-5.5\"]\n}\n");
  auto reload_models = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/reload models"});
  expect(reload_models && reload_models->handled && !reload_models->output.empty() && reload_models->output[0].find("models: loaded") != std::string::npos &&
             reload_models->output[0].find("scoped_cycle: configured") != std::string::npos &&
             reload_models->output[0].find("active_model: openai/gpt-5.5 (unchanged)") != std::string::npos && session->scoped_model_cycle() &&
             session->scoped_model_cycle()->size() == 1 && session->scoped_model_cycle()->front() == "openai/gpt-5.5" && session->model().model_id == "gpt-5.5",
         "command dispatcher /reload models refreshes model config without silently switching active model");
  auto reload_all = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/reload"});
  expect(reload_all && reload_all->handled && !reload_all->output.empty() && reload_all->output[0].find("display: loaded") != std::string::npos &&
             reload_all->output[0].find("models: loaded") != std::string::npos && reload_all->output[0].find("trust: loaded") != std::string::npos &&
             reload_all->output[0].find("compaction: validated") != std::string::npos &&
             reload_all->output[0].find("keybindings: tui-runtime") != std::string::npos &&
             reload_all->output[0].find("auth: restart-required") != std::string::npos &&
             reload_all->output[0].find("permissions: restart-required") != std::string::npos &&
             reload_all->output[0].find("lsp: restart-required") != std::string::npos &&
             reload_all->output[0].find("mcp: restart-required") != std::string::npos &&
             reload_all->output[0].find("plugins: restart-required") != std::string::npos,
         "command dispatcher /reload reports hot-reloaded and restart-required config domains");
  write_app_test_file(paths.compaction_file, "{\n  \"max_summary_bytes\": 0\n}\n");
  auto reload_compaction = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/reload compaction"});
  expect(reload_compaction && reload_compaction->handled && !reload_compaction->output.empty() &&
             reload_compaction->output[0].find("compaction: error") != std::string::npos &&
             reload_compaction->output[0].find("compaction max summary bytes must be greater than zero") != std::string::npos,
         "command dispatcher /reload compaction reports config validation errors without crashing");
  write_app_test_file(paths.compaction_file, "{\n  \"provider\": \"anthropic\", \"model\": \"unknown-summary-model\"\n}\n");
  auto reload_unknown_compaction = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/reload compaction"});
  expect(reload_unknown_compaction && reload_unknown_compaction->handled && !reload_unknown_compaction->output.empty() &&
             reload_unknown_compaction->output[0].find("compaction: error") != std::string::npos &&
             reload_unknown_compaction->output[0].find("compaction_provider: anthropic") != std::string::npos &&
             reload_unknown_compaction->output[0].find("compaction_model: unknown-summary-model") != std::string::npos,
         "command dispatcher /reload compaction performs runtime provider/model catalog validation");
  std::error_code remove_compaction_error;
  std::filesystem::remove(paths.compaction_file, remove_compaction_error);
  auto filtered_sessions = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions " + session->store.session_id()});
  expect(filtered_sessions && filtered_sessions->handled && !filtered_sessions->output.empty() &&
             filtered_sessions->output[0].find(session->store.session_id()) != std::string::npos,
         "command dispatcher /sessions accepts backend session-id query text");
  auto archive_active = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions archive " + session->store.session_id() + " --confirm"});
  auto active_metadata_after_archive_attempt = ava::session::load_session_metadata(session->store);
  expect(archive_active && archive_active->handled && !archive_active->output.empty() &&
             archive_active->output[0].find("Cannot archive the active session") != std::string::npos && active_metadata_after_archive_attempt &&
             !active_metadata_after_archive_attempt->archived,
         "slash /sessions archive refuses to archive the active runtime session");
  auto branch = ava::session::create_session_branch(
      ava::session::SessionBranchOptions{.workspace_dir = workspace,
                                         .root_dir = paths.sessions_dir,
                                         .source_session_id = session->store.session_id(),
                                         .branch_from_entry_id = {},
                                         .name = std::optional<std::string>("Review branch"),
                                         .labels = std::optional<std::vector<std::string>>(std::vector<std::string>{"review", "ui"}),
                                         .source_lease = &session->lease(),
                                         .mode = ava::session::SessionBranchMode::Fork,
                                         .actor = "test"});
  expect(branch.has_value(),
         branch ? "command dispatcher /sessions test creates a branch" : "command dispatcher /sessions test creates a branch: " + branch.error().format());
  auto tree_sessions = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions review"});
  expect(tree_sessions && tree_sessions->handled && !tree_sessions->output.empty() && tree_sessions->output[0].find("Sessions:") != std::string::npos &&
             tree_sessions->output[0].find("Review branch") != std::string::npos && tree_sessions->output[0].find("origin=fork") != std::string::npos &&
             tree_sessions->output[0].find("labels=review,ui") != std::string::npos && tree_sessions->output[0].find("parent=") != std::string::npos,
         "command dispatcher /sessions exposes tree branch names, labels, provenance, and parent links");
  if (branch)
  {
    branch->lease = ava::session::SessionLease{};
    auto const branch_session_id = branch->store.session_id();
    auto target_labels = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions labels " + branch_session_id + " triage selected"});
    auto branch_metadata_after_labels = ava::session::load_session_metadata(branch->store);
    auto target_label_status = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions labels " + branch_session_id});
    auto target_label_query = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions triage"});
    expect(target_labels && target_labels->handled && !target_labels->output.empty() &&
               target_labels->output[0].find("session " + branch_session_id + " labels set: triage,selected") != std::string::npos &&
               branch_metadata_after_labels && branch_metadata_after_labels->labels.size() == 2 && branch_metadata_after_labels->labels[0] == "triage" &&
               branch_metadata_after_labels->labels[1] == "selected" && target_label_status && target_label_status->handled &&
               !target_label_status->output.empty() &&
               target_label_status->output[0].find("session " + branch_session_id + " labels: triage,selected") != std::string::npos && target_label_query &&
               target_label_query->handled && !target_label_query->output.empty() &&
               target_label_query->output[0].find("labels=triage,selected") != std::string::npos && session->store.session_id() != branch_session_id,
           "slash /sessions labels updates a selected session without switching runtime sessions");

    auto archive_without_confirm = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions archive " + branch_session_id});
    auto metadata_before_archive = ava::session::load_session_metadata(branch->store);
    expect(archive_without_confirm && archive_without_confirm->handled && !archive_without_confirm->output.empty() &&
               archive_without_confirm->output[0].find("--confirm") != std::string::npos && metadata_before_archive && !metadata_before_archive->archived,
           "slash /sessions archive requires explicit confirmation before mutating metadata");

    auto archived = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions archive " + branch_session_id + " --confirm"});
    auto metadata_after_archive = ava::session::load_session_metadata(branch->store);
    auto hidden_archived = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions review"});
    auto visible_archived = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions --archived review"});
    expect(archived && archived->handled && !archived->output.empty() &&
               archived->output[0].find("session " + branch_session_id + " archived") != std::string::npos && metadata_after_archive &&
               metadata_after_archive->archived && hidden_archived && hidden_archived->handled && !hidden_archived->output.empty() &&
               hidden_archived->output[0].find("Review branch") == std::string::npos && visible_archived && visible_archived->handled &&
               !visible_archived->output.empty() && visible_archived->output[0].find("Review branch") != std::string::npos &&
               visible_archived->output[0].find("archived") != std::string::npos,
           "slash /sessions archive hides sessions from default lists while --archived includes them");

    auto unarchived = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions unarchive " + branch_session_id});
    auto metadata_after_unarchive = ava::session::load_session_metadata(branch->store);
    expect(unarchived && unarchived->handled && !unarchived->output.empty() &&
               unarchived->output[0].find("session " + branch_session_id + " unarchived") != std::string::npos && metadata_after_unarchive &&
               !metadata_after_unarchive->archived,
           "slash /sessions unarchive restores archived sessions to default views");
  }

  auto permissions_empty = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions"});
  expect(permissions_empty && permissions_empty->handled && !permissions_empty->output.empty() &&
             permissions_empty->output[0].find("Permission rules:") != std::string::npos &&
             permissions_empty->output[0].find("No persistent permission rules") != std::string::npos,
         "command dispatcher /permissions lists empty persistent permission rules with storage context");
  auto add_permission_rule =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions add action=allow operation=read path=src/main.cpp "
                                                                          "reason=\"trusted local read\""});
  expect(add_permission_rule && add_permission_rule->handled && !add_permission_rule->output.empty() &&
             add_permission_rule->output[0].find("added permission rule permrule_") != std::string::npos &&
             add_permission_rule->output[0].find("path=src/main.cpp") != std::string::npos &&
             add_permission_rule->output[0].find("trusted local read") != std::string::npos,
         "command dispatcher /permissions add stores a quoted persistent path rule");
  auto extract_rule_id = [](std::string const& text) {
    auto const start = text.find("permrule_");
    if (start == std::string::npos)
      return std::string{};
    auto end = start;
    while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
    return text.substr(start, end - start);
  };
  auto const permission_rule_id = add_permission_rule ? extract_rule_id(add_permission_rule->output[0]) : std::string{};
  expect(!permission_rule_id.empty(), "command dispatcher /permissions add exposes the created rule id");
  auto permissions_list = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions list trusted"});
  expect(permissions_list && permissions_list->handled && !permissions_list->output.empty() &&
             permissions_list->output[0].find(permission_rule_id) != std::string::npos &&
             permissions_list->output[0].find("built-in hard denies run first") != std::string::npos,
         "command dispatcher /permissions list filters rules and explains precedence");
  auto permissions_explain = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permission-rules explain " + permission_rule_id});
  expect(permissions_explain && permissions_explain->handled && !permissions_explain->output.empty() &&
             permissions_explain->output[0].find("Permission rule " + permission_rule_id) != std::string::npos &&
             permissions_explain->output[0].find("matching: exact operation") != std::string::npos &&
             permissions_explain->output[0].find("precedence: built-in hard denies run first") != std::string::npos,
         "command dispatcher /permissions explain reports rule matching and precedence diagnostics");
  auto permissions_diagnose = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/perms diagnose"});
  expect(permissions_diagnose && permissions_diagnose->handled && !permissions_diagnose->output.empty() &&
             permissions_diagnose->output[0].find("loaded rules: 1") != std::string::npos &&
             permissions_diagnose->output[0].find("outside the model-writable workspace") != std::string::npos,
         "command dispatcher /permissions diagnose reports storage and fail-closed behavior");
  auto append_permission_audit = ava::agent::append_permission_decision(
      session->owner_append_route(), ava::tools::PermissionAuditEvent{.permission_request_id = "permreq_runtime_deny",
                                                                      .operation = ava::permissions::Operation::RunCommand,
                                                                      .mode = ava::agent::Mode::Build,
                                                                      .tool_name = "bash",
                                                                      .action = ava::permissions::PermissionAction::Deny,
                                                                      .reason = "command can change external or destructive state",
                                                                      .risk = ava::permissions::PermissionRisk::High,
                                                                      .command = "git push origin main",
                                                                      .resolution = "deny",
                                                                      .resolution_source = "resolver",
                                                                      .resolution_reason = "remembered deny | rule",
                                                                      .actor = "tui",
                                                                      .rule_id = permission_rule_id});
  expect(append_permission_audit.has_value(), append_permission_audit
                                                  ? "command dispatcher test appends a permission audit entry"
                                                  : "command dispatcher test appends a permission audit entry: " + append_permission_audit.error().format());
  auto permissions_audit = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions audit permreq_runtime"});
  expect(permissions_audit && permissions_audit->handled && !permissions_audit->output.empty() &&
             permissions_audit->output[0].find("Permission audit:") != std::string::npos &&
             permissions_audit->output[0].find("permreq_runtime_deny") != std::string::npos &&
             permissions_audit->output[0].find("command=\"<redacted one-shot command>\"") != std::string::npos &&
             permissions_audit->output[0].find("source=resolver") != std::string::npos &&
             permissions_audit->output[0].find("git push origin main") == std::string::npos &&
             permissions_audit->output[0].find("remembered deny | rule") == std::string::npos,
         "command dispatcher /permissions audit filters persisted permission decisions");
  auto permissions_audit_summary = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions audit summary permreq_runtime"});
  expect(permissions_audit_summary && permissions_audit_summary->handled && !permissions_audit_summary->output.empty() &&
             permissions_audit_summary->output[0].find("Permission audit summary:") != std::string::npos &&
             permissions_audit_summary->output[0].find("filter: permreq_runtime") != std::string::npos &&
             permissions_audit_summary->output[0].find("entries: 1 matching") != std::string::npos &&
             permissions_audit_summary->output[0].find("denials: 1") != std::string::npos &&
             permissions_audit_summary->output[0].find("by action: deny=1") != std::string::npos &&
             permissions_audit_summary->output[0].find("by resolution: deny=1") != std::string::npos &&
             permissions_audit_summary->output[0].find("by source: resolver=1") != std::string::npos &&
             permissions_audit_summary->output[0].find("by risk: high=1") != std::string::npos &&
             permissions_audit_summary->output[0].find("by tool: bash=1") != std::string::npos &&
             permissions_audit_summary->output[0].find("/permissions audit show permreq_runtime_deny") != std::string::npos,
         "command dispatcher /permissions audit summary groups matching permission decisions for browsing");
  auto permissions_audit_export = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions audit export permreq_runtime"});
  expect(permissions_audit_export && permissions_audit_export->handled && !permissions_audit_export->output.empty() &&
             permissions_audit_export->output[0].find("Permission audit export:") != std::string::npos &&
             permissions_audit_export->output[0].find("format: markdown table") != std::string::npos &&
             permissions_audit_export->output[0].find("| timestamp | entry | request | action | resolution |") != std::string::npos &&
             permissions_audit_export->output[0].find("| deny |") != std::string::npos &&
             permissions_audit_export->output[0].find("<redacted one-shot command>") != std::string::npos &&
             permissions_audit_export->output[0].find("git push origin main") == std::string::npos &&
             permissions_audit_export->output[0].find("remembered deny") == std::string::npos,
         "command dispatcher /permissions audit export renders copyable markdown and escapes table cells");
  auto permissions_diagnose_denial = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions diagnose permreq_runtime"});
  expect(permissions_diagnose_denial && permissions_diagnose_denial->handled && !permissions_diagnose_denial->output.empty() &&
             permissions_diagnose_denial->output[0].find("Permission rule diagnostics:") != std::string::npos &&
             permissions_diagnose_denial->output[0].find("Recent permission denials:") != std::string::npos &&
             permissions_diagnose_denial->output[0].find("decision=deny") != std::string::npos &&
             permissions_diagnose_denial->output[0].find("source=resolver") != std::string::npos &&
             permissions_diagnose_denial->output[0].find("command=\"<redacted one-shot command>\"") != std::string::npos &&
             permissions_diagnose_denial->output[0].find("reason: command can change external or destructive state") != std::string::npos &&
             permissions_diagnose_denial->output[0].find("git push origin main") == std::string::npos &&
             permissions_diagnose_denial->output[0].find("remembered deny | rule") == std::string::npos &&
             permissions_diagnose_denial->output[0].find("next: /permissions explain " + permission_rule_id) != std::string::npos,
         "command dispatcher /permissions diagnose explains recent denied decisions with follow-up commands");
  auto permissions_audit_detail = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions audit show permreq_runtime"});
  expect(permissions_audit_detail && permissions_audit_detail->handled && !permissions_audit_detail->output.empty() &&
             permissions_audit_detail->output[0].find("Permission audit detail:") != std::string::npos &&
             permissions_audit_detail->output[0].find("selector: permreq_runtime") != std::string::npos &&
             permissions_audit_detail->output[0].find("matched entries: 1") != std::string::npos &&
             permissions_audit_detail->output[0].find("request: permreq_runtime_deny") != std::string::npos &&
             permissions_audit_detail->output[0].find("command: <redacted one-shot command>") != std::string::npos &&
             permissions_audit_detail->output[0].find("git push origin main") == std::string::npos &&
             permissions_audit_detail->output[0].find("remembered deny | rule") == std::string::npos &&
             permissions_audit_detail->output[0].find("/permissions audit export permreq_runtime") != std::string::npos &&
             permissions_audit_detail->output[0].find("/permissions explain " + permission_rule_id) != std::string::npos,
         "command dispatcher /permissions audit show drills into a permission request by id prefix");
  auto permissions_audit_empty = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions audit unmatched-query"});
  expect(permissions_audit_empty && permissions_audit_empty->handled && !permissions_audit_empty->output.empty() &&
             permissions_audit_empty->output[0].find("No permission audit entries match") != std::string::npos,
         "command dispatcher /permissions audit reports empty filtered results");
  auto slash_items_after_permission_rule = ava::app::command_catalog_slash_items_1(*session, custom_hotkeys);
  auto const permission_completion_available = std::ranges::any_of(slash_items_after_permission_rule, [&](auto const& item) {
    return item.command == "/permissions" && tui_test_support::has_slash_argument_completion(&item, 1, permission_rule_id, {"explain"}) &&
           tui_test_support::has_slash_argument_completion(&item, 1, permission_rule_id, {"remove"});
  });
  expect(permission_completion_available, "command catalog argument completions expose persistent permission rule ids for explain and remove");
  auto remove_permission_rule = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions remove " + permission_rule_id});
  expect(remove_permission_rule && remove_permission_rule->handled && !remove_permission_rule->output.empty() &&
             remove_permission_rule->output[0].find("removed permission rule " + permission_rule_id) != std::string::npos,
         "command dispatcher /permissions remove deletes persistent rules by id");
  auto permissions_after_remove = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions list"});
  expect(permissions_after_remove && permissions_after_remove->handled && !permissions_after_remove->output.empty() &&
             permissions_after_remove->output[0].find("No persistent permission rules") != std::string::npos,
         "command dispatcher /permissions list reflects removed persistent rules");
}

}  // namespace ava::tests::app_runtime_tests
