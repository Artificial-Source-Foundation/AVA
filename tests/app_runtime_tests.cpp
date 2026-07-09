#include "sys.h"
#include "ava/app/command_catalog.h"
#include "ava/app/command_palette.h"
#include "ava/app/clipboard_image.h"
#include "ava/app/commands.h"
#include "ava/app/connect_openai.h"
#include "ava/app/display_settings.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/onboarding.h"
#include "ava/app/print_mode.h"
#include "ava/app/project_trust.h"
#include "ava/app/reasoning_controls.h"
#include "ava/app/rpc/serialization.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime_retry.h"

#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_dispatcher.h"

#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"

#include "ava/tui/composer.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/terminal.h"
#include "ava/tui/theme.h"

#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"

#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/attachments.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_metadata.h"
#include "ava/session/session_store.h"
#include "ava/session/stats.h"

#include "ava/permissions/permission.h"
#include "ava/permissions/permission_rules.h"

#include "ava/provider/openai_provider.h"

#include "ava/context/context_loader.h"

#include "ava/core/ids.h"
#include "ava/core/json.h"

#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdlib>
#include <cwchar>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace {

using namespace ava::tests;

std::string app_tiny_png_bytes()
{
  std::string bytes;
  bytes.push_back(static_cast<char>(0x89));
  bytes += "PNG\r\n";
  bytes.push_back(static_cast<char>(0x1A));
  bytes += "\nava-runtime-image";
  return bytes;
}

void test_command_classification()
{
  expect(ava::permissions::classify_command("git status --short").action == ava::permissions::PermissionAction::Allow,
         "git status is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("git diff").action == ava::permissions::PermissionAction::Allow,
         "git diff is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("git log --oneline").action == ava::permissions::PermissionAction::Allow,
         "git log is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("pwd").action == ava::permissions::PermissionAction::Allow,
         "pwd remains allowed as inert local inspection");
  expect(ava::permissions::classify_command("ls src").action == ava::permissions::PermissionAction::Allow,
         "ls remains allowed for safe relative paths");
  expect(ava::permissions::classify_command("rm -rf build").action == ava::permissions::PermissionAction::Deny,
         "rm -rf is denied");
  expect(ava::permissions::classify_command("git push origin main").action == ava::permissions::PermissionAction::Ask,
         "git push asks");
  expect(ava::permissions::classify_command("git diff --output=/tmp/ava-owned").action ==
             ava::permissions::PermissionAction::Ask,
         "git diff output paths are not auto-allowed");
  expect(ava::permissions::classify_command("git diff --output out.diff").action ==
             ava::permissions::PermissionAction::Ask,
         "git diff output option is not auto-allowed");
  expect(ava::permissions::classify_command("git diff --no-index empty .ssh/work_key").action ==
             ava::permissions::PermissionAction::Ask,
         "relative credential paths are not auto-allowed");
  expect(ava::permissions::classify_command("cmake --build build").action == ava::permissions::PermissionAction::Allow,
         "cmake build is allowed for non-TTY line shell verification");
  expect(
      ava::permissions::classify_command("ctest --test-dir build").action == ava::permissions::PermissionAction::Allow,
      "ctest is allowed for non-TTY line shell verification");
  expect(ava::permissions::classify_command("rg hello src").action == ava::permissions::PermissionAction::Allow,
         "rg is allowed for non-TTY line shell inspection");
  expect(ava::permissions::classify_command("rg --pre ./filter hello src").action ==
             ava::permissions::PermissionAction::Deny,
         "rg preprocessors remain denied because they execute commands");
  expect(
      ava::permissions::decide(ava::permissions::PermissionRequest{.operation = ava::permissions::Operation::RunCommand,
                                                                   .mode = ava::agent::Mode::Plan,
                                                                   .workspace_dir = std::filesystem::current_path(),
                                                                   .target_path = {},
                                                                   .command = "git status --short"})
              .action == ava::permissions::PermissionAction::Allow,
      "run-command decisions preserve safe command allows");
  expect(ava::permissions::classify_command("cmake -E cat ~/.config/ava/auth.json").action ==
             ava::permissions::PermissionAction::Deny,
         "cmake -E helper access is denied");
  expect(ava::permissions::classify_command("cmake -P docs/plan.md").action == ava::permissions::PermissionAction::Deny,
         "cmake -P script execution is denied");
  expect(ava::permissions::classify_command("cmake -E copy docs/plan.md src/new.cpp").action ==
             ava::permissions::PermissionAction::Deny,
         "cmake -E copy mutation is denied");
  expect(
      ava::permissions::classify_command("python3 scripts/run.py").action == ava::permissions::PermissionAction::Deny,
      "interpreters are denied");
  expect(ava::permissions::classify_command("bash -lc ls").action == ava::permissions::PermissionAction::Deny,
         "shell interpreters remain denied");
}

void test_app_event_serialization()
{
  ava::app::RuntimeEvent session_event;
  session_event.type = ava::app::RuntimeEventType::SessionStart;
  session_event.timestamp = "2026-04-29T00:00:00Z";
  session_event.session_id = "session_1";
  session_event.mode = ava::agent::Mode::Plan;
  session_event.provider_id = "openai";
  session_event.model_id = "gpt-5.5";
  auto const jsonl = ava::app::serialize_event_jsonl(session_event);
  expect(jsonl ==
             "{\"type\":\"session_start\",\"timestamp\":\"2026-04-29T00:00:00Z\","
             "\"session_id\":\"session_1\",\"mode\":\"plan\",\"provider\":\"openai\","
             "\"model\":\"gpt-5.5\"}\n",
         "runtime event JSONL serialization is deterministic");

  ava::app::RuntimeEvent message_event;
  message_event.type = ava::app::RuntimeEventType::UserMessage;
  message_event.timestamp = "2026-04-29T00:00:01Z";
  message_event.session_id = "session_1";
  message_event.text = "hello\n\"ava\"";
  auto const message_jsonl = ava::app::serialize_event_jsonl(message_event);
  expect(message_jsonl.find("hello\\n\\\"ava\\\"") != std::string::npos, "runtime event JSONL escapes message text");
  expect(message_jsonl.ends_with('\n') &&
             message_jsonl.substr(0, message_jsonl.size() - 1).find('\n') == std::string::npos,
         "runtime event JSONL contains one terminating newline only");
}

void test_app_runtime_open_session_and_context_prompt()
{
  auto const root = temp_root() / "app-runtime-open";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const current = workspace / "src";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(current);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "workspace runtime instructions\n";
  }
  {
    std::ofstream file(current / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "nested runtime instructions\n";
  }
  {
    std::ofstream file(paths.global_agents_file, std::ios::binary | std::ios::trunc);
    file << "global runtime instructions\n";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = current;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime session opens with selected model, prompt, and context");
  if (!session) return;

  expect(session->created && session->mode == ava::agent::Mode::Plan && session->model.model_id == "gpt-5.5",
         "runtime session records created state, mode, and model");
  expect(session->context_sources.size() == 3, "runtime session records workspace and global context metadata");
  expect(session->system_prompt.find("Plan before changing files") != std::string::npos &&
             session->system_prompt.find("workspace runtime instructions") != std::string::npos &&
             session->system_prompt.find("nested runtime instructions") != std::string::npos &&
             session->system_prompt.find("global runtime instructions") != std::string::npos,
         "runtime session system prompt combines selected prompt and formatted context");
  auto entries = session->store.load();
  expect(entries && entries->size() == 1 && (*entries)[0].type == ava::session::EntryType::SessionStart &&
             (*entries)[0].data_json.find("\"context_sources\":3") != std::string::npos,
         "runtime session appends session_start on creation");

  auto const session_id = session->store.session_id();
  ava::app::RuntimeOpenOptions reopen_options;
  reopen_options.workspace_dir = workspace;
  reopen_options.current_dir = current;
  reopen_options.requested_session_id = session_id.substr(0, 12);
  reopen_options.mode = ava::agent::Mode::Plan;
  reopen_options.paths = paths;
  auto reopened = ava::app::open_runtime_session(reopen_options);
  expect(reopened && !reopened->created && reopened->store.session_id() == session_id,
         "runtime session resolves requested session id prefixes without creating a new session");
  if (reopened) {
    auto reopened_entries = reopened->store.load();
    expect(reopened_entries && reopened_entries->size() == 1,
           "runtime reopened session does not append another session_start");
  }
}

void test_app_runtime_no_session_mode()
{
  auto const root = temp_root() / "app-runtime-no-session";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  open_options.sessionless = true;

  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value() && session->sessionless && session->store.is_ephemeral(),
         "runtime opens no-session mode with an ephemeral store");
  if (!session)
    return;

  auto entries = session->store.load();
  expect(entries && entries->size() == 1 && (*entries)[0].type == ava::session::EntryType::SessionStart,
         "runtime no-session mode records session_start in memory");
  expect(!std::filesystem::exists(session->store.session_path()),
         "runtime no-session mode does not create a resumable JSONL file");

  auto listed = ava::session::SessionStore::list_sessions(workspace, paths.sessions_dir);
  expect(listed && listed->empty(), "runtime no-session mode does not appear in persisted session listings");

  auto requested_conflict = open_options;
  requested_conflict.requested_session_id = session->store.session_id();
  auto requested_result = ava::app::open_runtime_session(requested_conflict);
  expect(!requested_result && requested_result.error().message().find("no-session") != std::string::npos,
         "runtime rejects no-session with requested session resume");

  auto continue_conflict = open_options;
  continue_conflict.continue_last_session = true;
  auto continue_result = ava::app::open_runtime_session(continue_conflict);
  expect(!continue_result && continue_result.error().message().find("no-session") != std::string::npos,
         "runtime rejects no-session with continue");
}

void test_app_runtime_session_startup_options()
{
  auto const root = temp_root() / "app-runtime-session-startup-options";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions named_options;
  named_options.workspace_dir = workspace;
  named_options.current_dir = workspace;
  named_options.mode = ava::agent::Mode::Build;
  named_options.paths = paths;
  named_options.initial_session_name = "named startup";

  auto named = ava::app::open_runtime_session(named_options);
  expect(named.has_value() && named->created, "runtime opens a named startup session");
  if (!named)
    return;

  auto named_metadata = ava::session::load_session_metadata(named->store);
  expect(named_metadata && named_metadata->name == "named startup" && named_metadata->actor == "cli",
         "runtime startup --name records session metadata");
  auto named_entries = named->store.load();
  expect(named_entries && named_entries->size() == 2 && (*named_entries)[0].type == ava::session::EntryType::SessionStart &&
             (*named_entries)[1].type == ava::session::EntryType::SessionMetadata,
         "runtime startup --name appends metadata after session_start");

  auto custom_paths = paths;
  custom_paths.sessions_dir = root / "custom-sessions";
  ava::app::RuntimeOpenOptions custom_options;
  custom_options.workspace_dir = workspace;
  custom_options.current_dir = workspace;
  custom_options.paths = custom_paths;
  auto custom = ava::app::open_runtime_session(custom_options);
  expect(custom.has_value() && custom->store.session_path().string().find(custom_paths.sessions_dir.string()) == 0,
         "runtime opens sessions under a custom session directory");
  auto default_sessions = ava::session::SessionStore::list_sessions(workspace, paths.sessions_dir);
  auto custom_sessions = ava::session::SessionStore::list_sessions(workspace, custom_paths.sessions_dir);
  expect(default_sessions && default_sessions->size() == 1 && default_sessions->front().session_id == named->store.session_id(),
         "runtime custom session directory leaves default session listing unchanged");
  expect(custom_sessions && custom_sessions->size() == 1 && custom_sessions->front().session_id == custom->store.session_id(),
         "runtime custom session directory has its own session listing");

  ava::app::RuntimeOpenOptions fork_options;
  fork_options.workspace_dir = workspace;
  fork_options.current_dir = workspace;
  fork_options.paths = paths;
  fork_options.fork_session_id = named->store.session_id().substr(0, 12);
  fork_options.initial_session_name = "forked startup";
  auto forked = ava::app::open_runtime_session(fork_options);
  expect(forked.has_value() && forked->created && forked->store.session_id() != named->store.session_id(),
         "runtime --fork creates a new session from a source prefix");
  if (forked) {
    auto fork_metadata = ava::session::load_session_metadata(forked->store);
    expect(fork_metadata && fork_metadata->name == "forked startup" && fork_metadata->parent_session_id == named->store.session_id() &&
               fork_metadata->source_session_id == named->store.session_id() && fork_metadata->branch_origin == "fork" &&
               fork_metadata->actor == "cli" && !fork_metadata->branch_from_entry_id.empty(),
           "runtime --fork records branch metadata and startup name");
    auto fork_entries = forked->store.load();
    if (fork_entries && named_entries && named_entries->size() > 1) {
      auto const start_count = std::ranges::count_if(*fork_entries, [](auto const& entry) {
        return entry.type == ava::session::EntryType::SessionStart;
      });
      expect(start_count == 1 && fork_entries->size() == 3 && fork_entries->back().type == ava::session::EntryType::SessionMetadata,
             "runtime --fork copies source history and does not append an extra session_start");
      expect(fork_metadata && fork_metadata->branch_from_entry_id == (*named_entries)[1].id,
             "runtime --fork records the latest copied source entry as the branch point");
    }
  }

  auto fork_requested_conflict = fork_options;
  fork_requested_conflict.requested_session_id = named->store.session_id();
  auto fork_requested_result = ava::app::open_runtime_session(fork_requested_conflict);
  expect(!fork_requested_result && fork_requested_result.error().message().find("fork") != std::string::npos,
         "runtime rejects --fork with requested session resume");

  auto fork_continue_conflict = fork_options;
  fork_continue_conflict.continue_last_session = true;
  auto fork_continue_result = ava::app::open_runtime_session(fork_continue_conflict);
  expect(!fork_continue_result && fork_continue_result.error().message().find("fork") != std::string::npos,
         "runtime rejects --fork with continue");

  auto fork_no_session_conflict = fork_options;
  fork_no_session_conflict.sessionless = true;
  auto fork_no_session_result = ava::app::open_runtime_session(fork_no_session_conflict);
  expect(!fork_no_session_result && fork_no_session_result.error().message().find("no-session") != std::string::npos,
         "runtime rejects --fork with no-session");
}

void test_app_runtime_cli_prompt_overrides()
{
  auto const root = temp_root() / "app-runtime-cli-prompt-overrides";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  write_app_test_file(workspace / "AGENTS.md", "workspace context should remain after cli prompt override\n");
  write_app_test_file(paths.prompts_dir / "openai" / "gpt-5.5" / "plan.txt", "provider prompt override should be replaced\n");
  write_app_test_file(paths.ava_config_dir / "SYSTEM.md", "global system prompt should be replaced\n");
  write_app_test_file(paths.ava_config_dir / "APPEND_SYSTEM.md", "global append prompt should be replaced\n");
  write_app_test_file(workspace / ".ava" / "SYSTEM.md", "project system prompt should be replaced\n");
  write_app_test_file(workspace / ".ava" / "APPEND_SYSTEM.md", "project append prompt should be replaced\n");
  auto trusted = ava::app::set_project_trust_decision(paths, workspace, true);
  expect(trusted.has_value(), trusted ? "cli prompt override test trusts project resources"
                                      : "cli prompt override test trusts project resources: " + trusted.error().format());

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  open_options.prompt_overrides.system_prompt = "cli system prompt";
  open_options.prompt_overrides.append_system_prompts = {"cli append prompt one", "cli append prompt two"};
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime session opens with cli prompt overrides");
  if (!session) return;

  expect(session->prompt.from_override && !session->prompt.source_path && session->prompt.text == "cli system prompt",
         "cli --system-prompt is recorded as the effective prompt override");
  expect(session->prompt_overrides.system_prompt && *session->prompt_overrides.system_prompt == "cli system prompt" &&
             session->prompt_overrides.append_system_prompts.size() == 2,
         "runtime session retains cli prompt overrides for reloads");
  expect(session->system_prompt.find("cli system prompt") != std::string::npos &&
             session->system_prompt.find("cli append prompt one") != std::string::npos &&
             session->system_prompt.find("cli append prompt two") != std::string::npos &&
             session->system_prompt.find("workspace context should remain") != std::string::npos &&
             session->system_prompt.find("provider prompt override should be replaced") == std::string::npos &&
             session->system_prompt.find("global system prompt should be replaced") == std::string::npos &&
             session->system_prompt.find("project system prompt should be replaced") == std::string::npos &&
             session->system_prompt.find("global append prompt should be replaced") == std::string::npos &&
             session->system_prompt.find("project append prompt should be replaced") == std::string::npos,
         "cli prompt overrides replace selected system/append prompt resources while preserving context");

  auto const system_source_count = std::ranges::count_if(session->freshness_sources, [](auto const& source) {
    return source.kind == ava::app::RuntimeFreshnessSourceKind::SystemPrompt;
  });
  auto const append_source_count = std::ranges::count_if(session->freshness_sources, [](auto const& source) {
    return source.kind == ava::app::RuntimeFreshnessSourceKind::AppendSystemPrompt;
  });
  expect(system_source_count == 1 && append_source_count == 2,
         "cli prompt overrides are tracked as system prompt freshness sources");

  auto context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context --system-prompt"});
  expect(context && context->handled && !context->output.empty() &&
             context->output[0].find("system_prompt_sources=3") != std::string::npos &&
             context->output[0].find("system_prompt  cli  --system-prompt  <inline>") != std::string::npos &&
             context->output[0].find("status=inline") != std::string::npos &&
             context->output[0].find("SYSTEM.md") == std::string::npos,
         "context freshness reports cli system prompt overrides as inline sources");
  auto append_context =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context --append-system-prompt"});
  expect(append_context && append_context->handled && !append_context->output.empty() &&
             append_context->output[0].find("append_system_prompt  cli  --append-system-prompt/1  <inline>") !=
                 std::string::npos &&
             append_context->output[0].find("append_system_prompt  cli  --append-system-prompt/2  <inline>") !=
                 std::string::npos &&
             append_context->output[0].find("APPEND_SYSTEM.md") == std::string::npos,
         "context freshness reports repeated cli append prompt overrides as inline sources");

  auto switched_mode = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/mode"});
  expect(switched_mode && switched_mode->handled && session->mode == ava::agent::Mode::Build &&
             session->system_prompt.find("cli system prompt") != std::string::npos &&
             session->system_prompt.find("cli append prompt two") != std::string::npos &&
             session->system_prompt.find("Implement changes directly") == std::string::npos,
         "mode reloads preserve cli system prompt overrides");
}

void test_app_run_prompt_emits_events()
{
  auto const root = temp_root() / "app-runtime-run";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "runtime run context\n";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime run test opens session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"runtime answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  std::vector<ava::app::RuntimeEvent> events;
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  run_options.event_sink = [&events](ava::app::RuntimeEvent const& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };
  auto result = ava::app::run_prompt(*session, "hello runtime", provider, transport, run_options);
  expect(result && result->final_text == "runtime answer", "runtime run_prompt returns agent loop result");
  expect(events.size() == 4 && events[0].type == ava::app::RuntimeEventType::SessionStart &&
             events[1].type == ava::app::RuntimeEventType::UserMessage &&
             events[2].type == ava::app::RuntimeEventType::AssistantMessage &&
             events[3].type == ava::app::RuntimeEventType::Done,
         "runtime run_prompt emits session, user, assistant, and done events");
  expect(events.size() == 4 && events[2].text == "runtime answer" && events[3].provider_iterations == 1,
         "runtime run_prompt events include final text and completion counters");
  expect(
      transport.requests().size() == 1 && transport.requests()[0].body.find("runtime run context") != std::string::npos,
      "runtime run_prompt sends context-augmented system prompt to provider");
  auto entries = session->store.load();
  expect(entries && entries->size() == 3 && (*entries)[1].type == ava::session::EntryType::UserMessage &&
             (*entries)[2].type == ava::session::EntryType::AssistantMessage,
         "runtime run_prompt persists user and assistant entries in the runtime session");
}

void test_app_run_prompt_expands_file_references()
{
  auto const root = temp_root() / "app-runtime-file-reference";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace / "src");
  std::filesystem::create_directories(workspace / "my folder");
  {
    std::ofstream file(workspace / "src" / "reference.cpp", std::ios::binary | std::ios::trunc);
    file << "int referenced_symbol() { return 42; }\n";
  }
  {
    std::ofstream file(workspace / "my folder" / "reference file.cpp", std::ios::binary | std::ios::trunc);
    file << "int spaced_reference_symbol() { return 24; }\n";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime file reference test opens session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"reference answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  std::vector<ava::app::RuntimeEvent> events;
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  run_options.event_sink = [&events](ava::app::RuntimeEvent const& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };
  auto result = ava::app::run_prompt(*session, "review @src/reference.cpp and @\"my folder/reference file.cpp\"",
                                     provider, transport, run_options);
  expect(result && result->final_text == "reference answer", "runtime file reference prompt succeeds");
  expect(events.size() >= 2 && events[1].type == ava::app::RuntimeEventType::UserMessage &&
             events[1].text.find("Referenced files:") != std::string::npos &&
             events[1].text.find("int referenced_symbol()") != std::string::npos &&
             events[1].text.find("int spaced_reference_symbol()") != std::string::npos,
         "runtime user_message event contains expanded plain and quoted file reference content");
  expect(transport.requests().size() == 1 &&
             transport.requests()[0].body.find("review @src/reference.cpp") != std::string::npos &&
             transport.requests()[0].body.find("--- src/reference.cpp ---") != std::string::npos &&
             transport.requests()[0].body.find("int referenced_symbol()") != std::string::npos &&
             transport.requests()[0].body.find("--- my folder/reference file.cpp ---") != std::string::npos &&
             transport.requests()[0].body.find("int spaced_reference_symbol()") != std::string::npos,
         "runtime provider request receives bounded plain and quoted referenced file content");
  auto entries = session->store.load();
  auto const expanded_user_entry =
      entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
        return entry.type == ava::session::EntryType::UserMessage &&
               entry.data_json.find("int referenced_symbol()") != std::string::npos &&
               entry.data_json.find("int spaced_reference_symbol()") != std::string::npos;
      });
  expect(expanded_user_entry,
         "runtime session persists expanded plain and quoted file reference content in the user entry");
}

void test_app_run_prompt_sends_imported_image_attachment()
{
  auto const root = temp_root() / "app-runtime-image-attachment";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const image_path = workspace / "screen.png";
  write_app_test_file(image_path, app_tiny_png_bytes());

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime image attachment test opens session");
  if (!session) return;

  auto imported = ava::session::import_image_attachment(session->store, image_path);
  expect(imported.has_value(), "runtime image attachment test imports local image into session storage");
  if (!imported) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"image answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  run_options.image_attachments = {*imported};

  auto result = ava::app::run_prompt(*session, "describe this image", provider, transport, run_options);
  expect(result && result->final_text == "image answer", "runtime run_prompt accepts imported image attachments");
  expect(transport.requests().size() == 1 &&
             transport.requests()[0].body.find("\"type\":\"input_image\"") != std::string::npos &&
             transport.requests()[0].body.find("data:image/png;base64,") != std::string::npos,
         "runtime provider request includes a verified image data URL payload");
  auto entries = session->store.load();
  auto const persisted_metadata =
      entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
        return entry.type == ava::session::EntryType::UserMessage &&
               entry.data_json.find("\"attachments\"") != std::string::npos &&
               entry.data_json.find("\"mime_type\":\"image/png\"") != std::string::npos &&
               entry.data_json.find("data_base64") == std::string::npos;
      });
  expect(persisted_metadata, "runtime persists image attachment metadata without inline image bytes");
}

void test_app_clipboard_image_file_override_imports_attachment()
{
  auto const root = temp_root() / "app-runtime-clipboard-image";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const image_path = workspace / "clipboard.png";
  write_app_test_file(image_path, app_tiny_png_bytes());

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime clipboard image test opens session");
  if (!session) return;

  ScopedEnvVar clipboard_file("AVA_CLIPBOARD_IMAGE_FILE", image_path.string());
  auto imported = ava::app::import_clipboard_image_attachment(session->store);
  expect(imported && imported->has_value() && (*imported)->mime_type == "image/png" &&
             (*imported)->byte_size == app_tiny_png_bytes().size(),
         "runtime clipboard image override imports supported image bytes into session storage");
  if (!imported || !*imported) return;

  auto loaded = ava::session::load_image_attachment(session->store, **imported);
  expect(loaded && loaded->bytes == app_tiny_png_bytes(),
         "runtime clipboard image override writes reusable session-owned attachment bytes");
}

void test_app_run_prompt_emits_provider_retry_events_when_enabled()
{
  auto const root = temp_root() / "app-runtime-provider-retry";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime provider retry test opens session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{
           .status_code = 429, .headers = {{"Retry-After", "0"}}, .body = "{\"error\":{\"message\":\"rate limited\"}}"},
       sse_response(final_text_sse("retried answer"))});
  std::vector<ava::app::RuntimeEvent> events;
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  run_options.enable_transport_retries = true;
  run_options.event_sink = [&events](ava::app::RuntimeEvent const& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };
  bool runtime_retry_cancel = false;
  run_options.cancel_requested = [&runtime_retry_cancel] { return runtime_retry_cancel; };

  auto result = ava::app::run_prompt(*session, "retry runtime", provider, transport, run_options);
  expect(result && result->final_text == "retried answer" && transport.requests().size() == 2,
         "runtime run_prompt retries transient provider transport failures when enabled");
  expect(std::ranges::any_of(events,
                             [](ava::app::RuntimeEvent const& event) {
                               return event.type == ava::app::RuntimeEventType::Retry &&
                                      event.trigger == "provider_transport" && event.reason == "rate_limited" &&
                                      event.attempt == 2 && event.max_attempts == 3 && event.delay_ms == 0 &&
                                      event.text == "HTTP status 429";
                             }),
         "runtime run_prompt emits provider retry metadata through the shared event sink");
  events.clear();
  auto retry_options = ava::app::runtime::runtime_retry_options(*session, run_options);
  expect(retry_options.on_retry != nullptr, "runtime retry options expose provider retry event mapping");
  runtime_retry_cancel = true;
  expect(retry_options.cancel_requested && retry_options.cancel_requested(),
         "runtime retry options preserve the active run cancellation callback");
  runtime_retry_cancel = false;
  if (retry_options.on_retry) {
    auto emitted_tick = retry_options.on_retry(ava::provider::RetryOptions::Event{.attempt = 2,
                                                                                  .max_attempts = 3,
                                                                                  .delay_ms = 1000,
                                                                                  .remaining_ms = 500,
                                                                                  .reason = "rate_limited",
                                                                                  .status_code = 429,
                                                                                  .streaming = true,
                                                                                  .countdown_tick = true});
    expect(emitted_tick.has_value() && events.size() == 1 && events[0].type == ava::app::RuntimeEventType::RetryTick &&
               events[0].trigger == "provider_transport" && events[0].remaining_ms == 500 &&
               events[0].delay_ms == 1000 && events[0].status == "streaming",
           "runtime retry options map provider countdown ticks to explicit backend retry_tick events");
  }
}

void test_app_run_prompt_emits_tool_progress_and_session_spill()
{
  auto const root = temp_root() / "app-runtime-tool-progress";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime tool progress test opens session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.function_call.added\",\"item_id\":"
                                                   "\"call_bash\",\"name\":\"bash\"}\n\n"
                                                   "data: {\"type\":\"response.function_call_arguments.delta\","
                                                   "\"item_id\":\"call_bash\",\"delta\":\"{\\\"command\\\":"
                                                   "\\\"pwd\\\",\\\"max_bytes\\\":4}\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"tool done\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});
  std::vector<ava::app::RuntimeEvent> events;
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  run_options.event_sink = [&events](ava::app::RuntimeEvent const& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };

  auto result = ava::app::run_prompt(*session, "run pwd", provider, transport, run_options);
  auto const spill_dir = session->store.session_path().parent_path() / "spill";
  bool has_spill_file = false;
  std::error_code iter_error;
  for (std::filesystem::directory_iterator it(spill_dir, iter_error), end; !iter_error && it != end;
       it.increment(iter_error)) {
    has_spill_file = true;
    expect(it->path().parent_path() == spill_dir, "runtime spill file stays under the session-local spill directory");
    break;
  }
  expect(result && result->final_text == "tool done" &&
             std::ranges::any_of(events,
                                 [](ava::app::RuntimeEvent const& event) {
                                   return event.type == ava::app::RuntimeEventType::ToolProgress &&
                                          event.call_id == "call_bash" && event.tool_name == "bash" &&
                                          !event.text.empty();
                                 }),
         "runtime run_prompt emits additive tool_progress events from tool callbacks");
  expect(std::ranges::any_of(events,
                             [](ava::app::RuntimeEvent const& event) {
                               return event.type == ava::app::RuntimeEventType::ToolStart &&
                                      event.call_id == "call_bash" && event.tool_name == "bash" &&
                                      event.tool_arguments_json.find("\"command\":\"pwd\"") != std::string::npos;
                             }) &&
             std::ranges::any_of(events,
                                 [](ava::app::RuntimeEvent const& event) {
                                   return event.type == ava::app::RuntimeEventType::ToolResult &&
                                          event.call_id == "call_bash" && event.tool_name == "bash" &&
                                          event.truncated && event.total_bytes > 0 && event.output_lines > 0 &&
                                          event.total_lines > 0 && !event.spill_path.empty() &&
                                          event.tool_result_json.find("\"spill_file\"") != std::string::npos;
                                 }),
         "runtime run_prompt emits semantic tool args, result, and spill metadata for frontend adapters");
  expect(has_spill_file, "runtime run_prompt configures session-local spill files for truncated tool output");
}

void test_app_first_run_auth_onboarding()
{
  auto const root = temp_root() / "app-first-run-onboarding";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const paths = app_test_paths(root);
  auto const workspace = root / "workspace";
  auto const home = root / "home";
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  auto const home_text = home.string();
  auto const config_home_text = paths.config_home.string();
  auto const state_home_text = paths.state_home.string();
  auto const data_home_text = paths.data_home.string();
  setenv("HOME", home_text.c_str(), 1);
  setenv("XDG_CONFIG_HOME", config_home_text.c_str(), 1);
  setenv("XDG_STATE_HOME", state_home_text.c_str(), 1);
  setenv("XDG_DATA_HOME", data_home_text.c_str(), 1);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "first-run onboarding test opens session");
  if (!session) return;

  unsetenv("OPENAI_API_KEY");
  auto missing = ava::app::first_run_auth_onboarding_message(*session);
  expect(missing && missing->find("Provider auth is not configured for `openai`") != std::string::npos &&
             missing->find("Connect with /connect or /login") != std::string::npos &&
             missing->find("ava connect openai --headless-oauth") != std::string::npos &&
             missing->find("OPENAI_API_KEY") != std::string::npos && missing->find(paths.auth_file.string()) != std::string::npos,
         "first-run onboarding explains missing OpenAI auth with TUI, CLI, env, and auth-file paths");

  auto required = ava::app::provider_auth_required_message(*session, "\nslash tool commands still work offline.");
  expect(required.find("Auth is required for provider `openai`") != std::string::npos &&
             required.find("Connect with /connect or /login") != std::string::npos &&
             required.find("slash tool commands still work offline") != std::string::npos,
         "provider auth failure reuses onboarding guidance before a prompt runs");

  setenv("OPENAI_API_KEY", "test-openai-key", 1);
  auto env_ready = ava::app::first_run_auth_onboarding_message(*session);
  expect(!env_ready, "first-run onboarding stays hidden when provider auth comes from the environment");
  unsetenv("OPENAI_API_KEY");

  auto stored = ava::config::store_provider_credential(
      paths, ava::config::ProviderCredential{.provider_id = "openai",
                                             .access_token = "stored-openai-key",
                                             .credential_type = "api_key",
                                             .source = "test"});
  expect(stored.has_value(), "first-run onboarding test stores OpenAI credential");
  auto stored_ready = ava::app::first_run_auth_onboarding_message(*session);
  expect(!stored_ready, "first-run onboarding stays hidden when provider auth is stored");
}

void test_app_run_prompt_event_sink_failure_cancels_before_next_provider_call()
{
  auto const root = temp_root() / "app-runtime-event-sink-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "event sink cancel data";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime event sink failure test opens session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.function_call.added\",\"item_id\":"
                                                   "\"call_read\",\"name\":\"read_file\"}\n\n"
                                                   "data: "
                                                   "{\"type\":\"response.function_call_arguments.delta\","
                                                   "\"item_id\":\"call_read\",\"delta\":\"{\\\"path\\\":"
                                                   "\\\"note.txt\\\"}\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"should not request\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  run_options.event_sink = [](ava::app::RuntimeEvent const& event) {
    if (event.type == ava::app::RuntimeEventType::ToolStart) {
      return ava::core::VoidResult{
          std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "event sink failed"))};
    }
    return ava::core::VoidResult{};
  };

  auto result = ava::app::run_prompt(*session, "read with failing sink", provider, transport, run_options);
  expect(!result && result.error().category() == ava::core::ErrorCategory::Io &&
             result.error().message() == "event sink failed",
         "runtime returns the event sink write failure");
  expect(transport.requests().size() == 1, "event sink failure cancels before the next provider request");
}

void test_app_command_dispatcher()
{
  auto const root = temp_root() / "app-command-dispatcher";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace / "src");
  std::filesystem::create_directories(paths.ava_config_dir);
  write_app_test_file(paths.models_file,
                      "{\n"
                      "  \"models\": [\n"
                      "    {\"provider\":\"openai\",\"id\":\"diagnostic-local\",\"name\":\"Diagnostic Local\",\"supports_reasoning\":true},\n"
                      "    {\"provider\":\"ghost\",\"id\":\"remote-model\",\"name\":\"Remote Missing\",\"family\":\"remote\","
                      "\"context_window_tokens\":4096,\"api_family\":\"chat_completions\",\"input_modalities\":[\"text\"],"
                      "\"supports_tools\":false,\"supports_streaming\":false,\"supports_reasoning\":false,\"reports_usage\":false}\n"
                      "  ]\n"
                      "}\n");
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "dispatcher context\n";
  }
  {
    std::filesystem::create_directories(paths.prompts_dir / "openai" / "gpt-5");
    std::ofstream file(paths.prompts_dir / "openai" / "gpt-5" / "plan.txt", std::ios::binary | std::ios::trunc);
    file << "dispatcher plan prompt\n";
  }
  write_app_test_file(workspace / ".ava" / "skills" / "dispatcher-skill" / "SKILL.md",
                      "---\n"
                      "name: dispatcher-skill\n"
                      "description: Dispatcher skill\n"
                      "---\n"
                      "Use the dispatcher skill.\n");
  write_app_test_file(workspace / ".ava" / "commands" / "prompt-check.md",
                      "---\n"
                      "description: Check prompt command freshness\n"
                      "argument-hint: \"[topic]\"\n"
                      "---\n"
                      "Check prompt command freshness for $ARGUMENTS.\n");
  {
    std::ofstream file(workspace / "src" / "main.cpp", std::ios::binary | std::ios::trunc);
    file << "int main() { return 0; }\n";
  }
  {
    std::filesystem::create_directories(workspace / "my folder");
    std::ofstream file(workspace / "my folder" / "space file.txt", std::ios::binary | std::ios::trunc);
    file << "space path\n";
  }
  {
    std::filesystem::create_directories(workspace / "docs" / "reference-code" / "pi");
    std::ofstream file(workspace / "docs" / "reference-code" / "pi" / "reference-only.md", std::ios::binary | std::ios::trunc);
    file << "reference code stays out of normal path completion\n";
  }
  write_app_test_file(paths.ava_config_dir / "plugins" / "com.example.global" / "plugin.json",
                      app_test_plugin_manifest_json("com.example.global", "Global Plugin"));
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.project" / "plugin.json",
                      "{\n"
                      "  \"schema_version\": 1,\n"
                      "  \"id\": \"com.example.project\",\n"
                      "  \"name\": \"Project Plugin\",\n"
                      "  \"version\": \"0.1.0\",\n"
                      "  \"api_version\": \"ava.plugin.v1\",\n"
                      "  \"description\": \"test plugin\",\n"
                      "  \"entrypoint\": {\"command\": \"node\", \"args\": [\"plugin.js\", \"--safe\"]},\n"
                      "  \"capabilities\": [\"tools\", \"commands\"],\n"
                      "  \"contributes\": {\n"
                      "    \"tools\": [{\"name\": \"todo_add\", \"description\": \"Add todo\", \"input_schema\": {\"type\": \"object\", \"additionalProperties\": false}}],\n"
                      "    \"commands\": [{\"name\": \"todo\", \"description\": \"Show todos\"}],\n"
                      "    \"prompts\": [{\"name\": \"review\", \"description\": \"Review prompt\", \"path\": \"prompts/review.md\"}],\n"
                      "    \"skills\": [{\"name\": \"triage\", \"description\": \"Triage skill\", \"path\": \"skills/triage.md\"}]\n"
                      "  }\n"
                      "}");
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.project" / "prompts" / "review.md",
                      "Review todos from the project plugin.\n");
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.project" / "skills" / "triage.md",
                      "Triage todos from the project plugin.\n");
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.bad" / "plugin.json", "{not-json");
  write_app_test_file(workspace / ".ava" / "mcp.json",
                      app_test_mcp_config_json("fs", "Filesystem Server", AVA_FAKE_MCP_SERVER_PATH));
  auto trusted = ava::app::set_project_trust_decision(paths, workspace, true);
  expect(trusted.has_value(), trusted ? "command dispatcher test trusts project resources"
                                      : "command dispatcher test trusts project resources: " + trusted.error().format());

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "command dispatcher test opens runtime session");
  if (!session) return;
  auto const plan_system_prompt = session->system_prompt;
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "dispatcher context changed after session open\n";
  }
  write_app_test_file(workspace / ".ava" / "skills" / "dispatcher-skill" / "SKILL.md",
                      "---\n"
                      "name: dispatcher-skill\n"
                      "description: Dispatcher skill\n"
                      "---\n"
                      "Use the changed dispatcher skill.\n");
  write_app_test_file(workspace / ".ava" / "commands" / "prompt-check.md",
                      "---\n"
                      "description: Check changed prompt command freshness\n"
                      "argument-hint: \"[topic]\"\n"
                      "---\n"
                      "Check changed prompt command freshness for $ARGUMENTS.\n");
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.project" / "prompts" / "review.md",
                      "Review todos from the changed project plugin.\n");
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.project" / "skills" / "triage.md",
                      "Triage todos from the changed project plugin.\n");

  expect(
          ava::app::is_backend_command("/model") && ava::app::is_backend_command("/models") &&
          ava::app::is_backend_command("/hotkeys") && ava::app::is_backend_command("/keybindings") &&
          ava::app::is_backend_command("/theme") && ava::app::is_backend_command("/details") &&
          ava::app::is_backend_command("/tool") && ava::app::is_backend_command("/tools write") &&
          ava::app::is_backend_command("/diff") && ava::app::is_backend_command("/copy") &&
          ava::app::is_backend_command("/find src/*.cpp") && ava::app::is_backend_command("/ls src") &&
          ava::app::is_backend_command("/thinking") && ava::app::is_backend_command("/status") &&
          ava::app::is_backend_command("/reload") &&
          ava::app::is_backend_command("/plugins") && ava::app::is_backend_command("/permissions") &&
          ava::app::is_backend_command("/permission-rules") && ava::app::is_backend_command("!pwd") &&
          ava::app::is_backend_command("!!pwd"),
      "command catalog classifies display toggles, status aliases, disabled aliases, hotkeys, and shell helpers as backend commands");

  std::vector<ava::app::CommandHotkey> const custom_hotkeys = {
      ava::app::CommandHotkey{.action = "submit", .description = "Submit custom", .keys = "Ctrl+M"},
      ava::app::CommandHotkey{.action = "variant_cycle", .description = "Cycle variants", .keys = "Ctrl+T"}};
  auto hotkeys =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/hotkeys", .hotkeys = custom_hotkeys});
  expect(hotkeys && hotkeys->handled && !hotkeys->output.empty() &&
             hotkeys->output[0].find("Ctrl+M") != std::string::npos &&
             hotkeys->output[0].find("variant_cycle") != std::string::npos &&
             hotkeys->output[0].find("$XDG_CONFIG_HOME/ava/keybinds.json") != std::string::npos &&
             hotkeys->output[0].find("/reload keybindings") != std::string::npos,
         "command dispatcher /hotkeys reports effective keybind metadata");
  auto keybindings = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings", .hotkeys = custom_hotkeys});
  expect(keybindings && keybindings->handled && !keybindings->output.empty() &&
             keybindings->output[0].find("Keybindings:") != std::string::npos &&
             keybindings->output[0].find("Ctrl+M") != std::string::npos &&
             keybindings->output[0].find("/keybindings init") != std::string::npos &&
             keybindings->output[0].find("/keybindings import <path>") != std::string::npos &&
             keybindings->output[0].find("/keybindings set <action>") != std::string::npos &&
             keybindings->output[0].find("/keybindings reset <action>") != std::string::npos &&
             keybindings->output[0].find("/keybindings validate") != std::string::npos,
         "command dispatcher /keybindings aliases the effective keybinding discovery surface");
  auto keybindings_validate_missing =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings validate"});
  expect(keybindings_validate_missing && keybindings_validate_missing->handled &&
             !keybindings_validate_missing->output.empty() &&
             keybindings_validate_missing->output[0].find("No keybindings file found") != std::string::npos &&
             keybindings_validate_missing->output[0].find("/keybindings init") != std::string::npos,
         "command dispatcher /keybindings validate reports missing config without failing closed");
  auto keybindings_init = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings init"});
  auto const keybinds_file = paths.ava_config_dir / "keybinds.json";
  auto const initialized_keybinds = ava::tui::load_key_bindings(keybinds_file);
  expect(keybindings_init && keybindings_init->handled && !keybindings_init->output.empty() &&
             keybindings_init->output[0].find("Created keybindings starter file") != std::string::npos &&
             keybindings_init->output[0].find(keybinds_file.string()) != std::string::npos &&
             initialized_keybinds &&
             ava::tui::key_matches_action(*initialized_keybinds, ava::tui::TuiAction::Submit, ava::tui::Key::Enter),
         "command dispatcher /keybindings init writes a validated starter file to the runtime config dir");
  auto keybindings_validate =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings validate"});
  expect(keybindings_validate && keybindings_validate->handled && !keybindings_validate->output.empty() &&
             keybindings_validate->output[0].find("keybindings file is valid") != std::string::npos &&
             keybindings_validate->output[0].find(keybinds_file.string()) != std::string::npos &&
             keybindings_validate->output[0].find("/reload keybindings") != std::string::npos,
         "command dispatcher /keybindings validate checks the configured keybind file without reloading");
  {
    std::ofstream output(keybinds_file, std::ios::binary | std::ios::trunc);
    output << "{\"submit\":\"Ctrl+P\",\"model_cycle_forward\":\"Ctrl+P\"}\n";
  }
  auto invalid_keybindings_validate =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings validate"});
  expect(invalid_keybindings_validate && invalid_keybindings_validate->handled &&
             !invalid_keybindings_validate->output.empty() &&
             invalid_keybindings_validate->output[0].find("keybindings file is invalid") != std::string::npos &&
             invalid_keybindings_validate->output[0].find("conflicting TUI keybinding") != std::string::npos &&
             invalid_keybindings_validate->output[0].find("Ctrl+P") != std::string::npos &&
             invalid_keybindings_validate->output[0].find(keybinds_file.string()) != std::string::npos,
         "command dispatcher /keybindings validate surfaces parser diagnostics with path context");
  auto unsupported_keybindings_validate =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings validate --bad"});
  expect(unsupported_keybindings_validate && unsupported_keybindings_validate->handled &&
             !unsupported_keybindings_validate->output.empty() &&
             unsupported_keybindings_validate->output[0].find("unsupported keybindings validate option") != std::string::npos,
         "command dispatcher /keybindings validate reports unsupported options");
  auto keybindings_init_existing =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings init"});
  expect(keybindings_init_existing && keybindings_init_existing->handled && !keybindings_init_existing->output.empty() &&
             keybindings_init_existing->output[0].find("already exists") != std::string::npos &&
             keybindings_init_existing->output[0].find("--force") != std::string::npos,
         "command dispatcher /keybindings init refuses accidental overwrite");
  auto keybindings_init_force =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings init --force"});
  expect(keybindings_init_force && keybindings_init_force->handled && !keybindings_init_force->output.empty() &&
             keybindings_init_force->output[0].find("Replaced keybindings starter file") != std::string::npos,
         "command dispatcher /keybindings init --force replaces the starter file explicitly");
  auto read_keybinds_file = [&] {
    std::ifstream input(keybinds_file, std::ios::binary);
    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
  };
  auto const import_source = workspace / "import-keybinds.json";
  auto const valid_import_content =
      std::string("{\"tui.editor.cursorLeft\":[\"Left\",\"Alt+H\"],\"app.tools.expand\":\"Ctrl+O\"}\n");
  write_app_test_file(import_source, valid_import_content);
  auto keybindings_import_existing =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings import import-keybinds.json"});
  expect(keybindings_import_existing && keybindings_import_existing->handled &&
             !keybindings_import_existing->output.empty() &&
             keybindings_import_existing->output[0].find("keybindings file already exists") != std::string::npos &&
             keybindings_import_existing->output[0].find("/keybindings import <path> --force") != std::string::npos,
         "command dispatcher /keybindings import refuses accidental overwrite");
  auto keybindings_import_force = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/keybindings import import-keybinds.json --force"});
  auto const imported_keybinds = ava::tui::load_key_bindings(keybinds_file);
  auto const installed_import_content = read_keybinds_file();
  expect(keybindings_import_force && keybindings_import_force->handled &&
             !keybindings_import_force->output.empty() &&
             keybindings_import_force->output[0].find("Imported keybindings file") != std::string::npos &&
             keybindings_import_force->output[0].find(import_source.string()) != std::string::npos &&
             keybindings_import_force->output[0].find(keybinds_file.string()) != std::string::npos &&
             keybindings_import_force->output[0].find("/reload keybindings") != std::string::npos &&
             installed_import_content == valid_import_content && imported_keybinds &&
             ava::tui::key_matches_action(*imported_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::AltH) &&
             ava::tui::key_matches_action(*imported_keybinds, ava::tui::TuiAction::DetailsToggle, ava::tui::Key::CtrlO),
         "command dispatcher /keybindings import --force validates and installs a relative source file");
  auto keybindings_set =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings set cursor_left Alt+H"});
  auto const set_keybinds = ava::tui::load_key_bindings(keybinds_file);
  auto const set_content = read_keybinds_file();
  expect(keybindings_set && keybindings_set->handled && !keybindings_set->output.empty() &&
             keybindings_set->output[0].find("Set keybinding") != std::string::npos &&
             keybindings_set->output[0].find("action: cursor_left") != std::string::npos &&
             keybindings_set->output[0].find("keys: Alt+H") != std::string::npos &&
             keybindings_set->output[0].find("/reload keybindings") != std::string::npos &&
             set_content.find("\"tui.editor.cursorLeft\": \"Alt+H\"") != std::string::npos &&
             set_content.find("\"cursor_left\"") == std::string::npos && set_keybinds &&
             ava::tui::key_matches_action(*set_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::AltH) &&
             !ava::tui::key_matches_action(*set_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::ArrowLeft),
         "command dispatcher /keybindings set validates, canonicalizes, and edits one action in keybinds.json");
  auto keybindings_set_multi =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings set cursor_left Left,Alt+H"});
  auto const set_multi_keybinds = ava::tui::load_key_bindings(keybinds_file);
  auto const set_multi_content = read_keybinds_file();
  expect(keybindings_set_multi && keybindings_set_multi->handled && !keybindings_set_multi->output.empty() &&
             keybindings_set_multi->output[0].find("keys: Left, Alt+H") != std::string::npos &&
             set_multi_content.find("\"tui.editor.cursorLeft\": [\"Left\", \"Alt+H\"]") != std::string::npos &&
             set_multi_keybinds &&
             ava::tui::key_matches_action(*set_multi_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::ArrowLeft) &&
             ava::tui::key_matches_action(*set_multi_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::AltH),
         "command dispatcher /keybindings set accepts comma-separated key lists");
  auto const before_failed_set_content = read_keybinds_file();
  auto keybindings_set_conflict =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings set app.tools.expand Alt+H"});
  expect(keybindings_set_conflict && keybindings_set_conflict->handled &&
             !keybindings_set_conflict->output.empty() &&
             keybindings_set_conflict->output[0].find("keybindings assignment is invalid") != std::string::npos &&
             keybindings_set_conflict->output[0].find("conflicting TUI keybinding") != std::string::npos &&
             keybindings_set_conflict->output[0].find("Target was not changed") != std::string::npos &&
             read_keybinds_file() == before_failed_set_content,
         "command dispatcher /keybindings set validates the whole config before writing conflicts");
  auto keybindings_set_unknown_action =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings set no_such_action Alt+H"});
  expect(keybindings_set_unknown_action && keybindings_set_unknown_action->handled &&
             !keybindings_set_unknown_action->output.empty() &&
             keybindings_set_unknown_action->output[0].find("unknown TUI keybinding action") != std::string::npos,
         "command dispatcher /keybindings set reports unknown actions");
  auto keybindings_set_unknown_key =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings set cursor_left Hyper+H"});
  expect(keybindings_set_unknown_key && keybindings_set_unknown_key->handled &&
             !keybindings_set_unknown_key->output.empty() &&
             keybindings_set_unknown_key->output[0].find("unknown TUI key binding") != std::string::npos,
         "command dispatcher /keybindings set reports unknown keys");
  auto keybindings_reset =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings reset cursor_left"});
  auto const reset_keybinds = ava::tui::load_key_bindings(keybinds_file);
  auto const reset_content = read_keybinds_file();
  expect(keybindings_reset && keybindings_reset->handled && !keybindings_reset->output.empty() &&
             keybindings_reset->output[0].find("Reset keybinding override") != std::string::npos &&
             keybindings_reset->output[0].find("action: cursor_left") != std::string::npos &&
             keybindings_reset->output[0].find("/reload keybindings") != std::string::npos &&
             reset_content.find("tui.editor.cursorLeft") == std::string::npos &&
             reset_content.find("cursor_left") == std::string::npos && reset_keybinds &&
             ava::tui::key_matches_action(*reset_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::ArrowLeft) &&
             !ava::tui::key_matches_action(*reset_keybinds, ava::tui::TuiAction::CursorLeft, ava::tui::Key::AltH),
         "command dispatcher /keybindings reset removes equivalent action aliases and restores default bindings");
  auto keybindings_reset_missing =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings reset cursor_left"});
  expect(keybindings_reset_missing && keybindings_reset_missing->handled &&
             !keybindings_reset_missing->output.empty() &&
             keybindings_reset_missing->output[0].find("No keybinding override found") != std::string::npos &&
             keybindings_reset_missing->output[0].find("Target was not changed") != std::string::npos,
         "command dispatcher /keybindings reset reports absent overrides without rewriting");
  auto keybindings_reset_unknown_action =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings reset no_such_action"});
  expect(keybindings_reset_unknown_action && keybindings_reset_unknown_action->handled &&
             !keybindings_reset_unknown_action->output.empty() &&
             keybindings_reset_unknown_action->output[0].find("unknown TUI keybinding action") != std::string::npos,
         "command dispatcher /keybindings reset reports unknown actions");
  auto const invalid_import_source = workspace / "bad-keybinds.json";
  write_app_test_file(invalid_import_source, "{\"submit\":\"Ctrl+P\",\"model_cycle_forward\":\"Ctrl+P\"}\n");
  auto const before_invalid_import_content = read_keybinds_file();
  auto keybindings_import_invalid = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/keybindings import bad-keybinds.json --force"});
  expect(keybindings_import_invalid && keybindings_import_invalid->handled &&
             !keybindings_import_invalid->output.empty() &&
             keybindings_import_invalid->output[0].find("keybindings import source is invalid") != std::string::npos &&
             keybindings_import_invalid->output[0].find("conflicting TUI keybinding") != std::string::npos &&
             keybindings_import_invalid->output[0].find("Target was not changed") != std::string::npos &&
             read_keybinds_file() == before_invalid_import_content,
         "command dispatcher /keybindings import validates before replacing the target file");
  auto keybindings_import_missing = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/keybindings import missing-keybinds.json --force"});
  expect(keybindings_import_missing && keybindings_import_missing->handled &&
             !keybindings_import_missing->output.empty() &&
             keybindings_import_missing->output[0].find("keybindings import source does not exist") != std::string::npos,
         "command dispatcher /keybindings import reports missing source files");
  auto unsupported_keybindings_import = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/keybindings import import-keybinds.json --bad"});
  expect(unsupported_keybindings_import && unsupported_keybindings_import->handled &&
             !unsupported_keybindings_import->output.empty() &&
             unsupported_keybindings_import->output[0].find("unsupported keybindings import option") != std::string::npos,
         "command dispatcher /keybindings import reports unsupported options");
  auto unsupported_keybindings_init =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/keybindings init --bad"});
  expect(unsupported_keybindings_init && unsupported_keybindings_init->handled &&
             !unsupported_keybindings_init->output.empty() &&
             unsupported_keybindings_init->output[0].find("unsupported keybindings init option") != std::string::npos,
         "command dispatcher /keybindings init reports unsupported options");
  {
    ScopedEnvVar no_color_guard("NO_COLOR", "");
    ScopedEnvVar theme_env_guard("AVA_TUI_THEME", "");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "");
    ava::tui::set_tui_config_theme(std::nullopt);
    auto theme_status = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/theme"});
    expect(theme_status && theme_status->handled && !theme_status->output.empty() &&
               theme_status->output[0].find("TUI theme:") != std::string::npos &&
               theme_status->output[0].find("usage: /theme [dark|light|plain|custom-name|reset]") != std::string::npos,
           "command dispatcher /theme reports current config, active theme, and usage");
    auto theme_light = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/theme light"});
    auto loaded_theme = ava::app::load_tui_display_settings(paths);
    auto active_theme = ava::tui::active_tui_theme();
    expect(theme_light && theme_light->handled && !theme_light->output.empty() &&
               theme_light->output[0].find("Stored TUI theme light") != std::string::npos &&
               loaded_theme && loaded_theme->theme && *loaded_theme->theme == "light" &&
               active_theme.kind == ava::tui::TuiThemeKind::Light && active_theme.badge == "display.json",
           "command dispatcher /theme light persists display.json and applies the active TUI theme override");
    write_app_test_file(paths.ava_config_dir / "themes" / "sunrise.json",
                        "{\n"
                        "  \"name\": \"sunrise\",\n"
                        "  \"vars\": {\"primary\": \"#0066cc\", \"paper\": 255},\n"
                        "  \"colors\": {\n"
                        "    \"text\": \"\",\n"
                        "    \"muted\": 242,\n"
                        "    \"success\": 34,\n"
                        "    \"warning\": \"#ffaa00\",\n"
                        "    \"error\": \"#ff0000\",\n"
                        "    \"accent\": \"primary\",\n"
                        "    \"screenBg\": \"paper\",\n"
                        "    \"composerBg\": 236\n"
                        "  }\n"
                        "}\n");
    write_app_test_file(paths.ava_config_dir / "themes" / "broken.json",
                        "{\n"
                        "  \"name\": \"broken\",\n"
                        "  \"colors\": {\"text\":\"\",\"muted\":242,\"success\":34,\"warning\":220,\"error\":196,\"accent\":39,\"screenBg\":235}\n"
                        "}\n");
    auto custom_theme = ava::app::load_tui_custom_theme(paths, "sunrise");
    auto invalid_custom_theme_file = ava::app::load_tui_custom_theme_file(paths.ava_config_dir / "themes" / "broken.json");
    expect(custom_theme && custom_theme->name == "sunrise" && custom_theme->palette.text == -1 &&
               custom_theme->palette.screen_bg == 255 && custom_theme->palette.composer_bg == 236 &&
               !invalid_custom_theme_file && invalid_custom_theme_file.error().format().find("composerBg") != std::string::npos,
           "display settings load valid AVA custom themes and reject incomplete custom theme files with token context");
    auto theme_custom = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/theme sunrise"});
    loaded_theme = ava::app::load_tui_display_settings(paths);
    active_theme = ava::tui::active_tui_theme();
    expect(theme_custom && theme_custom->handled && !theme_custom->output.empty() &&
               theme_custom->output[0].find("Stored TUI theme sunrise") != std::string::npos &&
               loaded_theme && loaded_theme->theme && *loaded_theme->theme == "sunrise" && loaded_theme->custom_theme &&
               active_theme.kind == ava::tui::TuiThemeKind::Custom && active_theme.name == "sunrise" &&
               active_theme.badge == "display.json" && active_theme.palette &&
               active_theme.palette->composer_bg == 236,
           "command dispatcher /theme <custom> persists display.json and applies a valid custom TUI theme");
    auto custom_watch = ava::app::load_tui_display_settings_watch_state(paths);
    expect(custom_watch && custom_watch->theme && *custom_watch->theme == "sunrise" && custom_watch->custom_theme_revision,
           "display settings watch state records the selected custom theme revision");
    write_app_test_file(paths.ava_config_dir / "themes" / "sunrise.json",
                        "{\n"
                        "  \"name\": \"sunrise\",\n"
                        "  \"vars\": {\"primary\": \"#0066cc\", \"paper\": 255},\n"
                        "  \"colors\": {\n"
                        "    \"text\": \"\",\n"
                        "    \"muted\": 242,\n"
                        "    \"success\": 34,\n"
                        "    \"warning\": \"#ffaa00\",\n"
                        "    \"error\": \"#ff0000\",\n"
                        "    \"accent\": \"primary\",\n"
                        "    \"screenBg\": \"paper\",\n"
                        "    \"composerBg\": 237\n"
                        "  }\n"
                        "}\n");
    auto changed_custom_watch = ava::app::load_tui_display_settings_watch_state(paths);
    expect(custom_watch && changed_custom_watch &&
               ava::app::tui_display_settings_watch_state_changed(*custom_watch, *changed_custom_watch) &&
               changed_custom_watch->custom_theme_revision != custom_watch->custom_theme_revision,
           "display settings watch state detects selected custom theme file edits by content revision");
    write_app_test_file(paths.ava_config_dir / "display.json", "{\n  \"theme\": \"plain\"\n}\n");
    auto changed_display_watch = ava::app::load_tui_display_settings_watch_state(paths);
    expect(changed_custom_watch && changed_display_watch &&
               ava::app::tui_display_settings_watch_state_changed(*changed_custom_watch, *changed_display_watch) &&
               changed_display_watch->theme && *changed_display_watch->theme == "plain" &&
               !changed_display_watch->custom_theme_revision,
           "display settings watch state detects display.json edits and clears custom theme tracking");
    auto invalid_theme = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/theme sepia"});
    expect(invalid_theme && invalid_theme->handled && !invalid_theme->output.empty() &&
               invalid_theme->output[0].find("unsupported theme: sepia") != std::string::npos &&
               invalid_theme->output[0].find("dark|light|plain|custom-name|reset") != std::string::npos,
           "command dispatcher /theme rejects unsupported theme names with usage");
    auto reset_theme = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/theme reset"});
    auto reset_loaded_theme = ava::app::load_tui_display_settings(paths);
    active_theme = ava::tui::active_tui_theme();
    expect(reset_theme && reset_theme->handled && !reset_theme->output.empty() &&
               reset_theme->output[0].find("Reset TUI theme") != std::string::npos && reset_loaded_theme &&
               !reset_loaded_theme->theme && active_theme.kind == ava::tui::TuiThemeKind::Dark &&
               active_theme.badge == "built-in",
           "command dispatcher /theme reset clears the persisted TUI theme and returns to the built-in default");
  }
  auto details = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/details"});
  expect(details && details->handled && !details->output.empty() &&
             details->output[0].find("TUI display toggle") != std::string::npos,
         "command dispatcher recognizes /details without inventing backend tool metadata");
  auto tool = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/tools write"});
  expect(tool && tool->handled && !tool->output.empty() &&
             tool->output[0].find("/tool [query] to show the latest or matching expanded tool card") != std::string::npos &&
             tool->output[0].find("/copy tool [query] to copy details") != std::string::npos,
         "command dispatcher recognizes /tool and /tools as TUI tool-card inspection commands");
  auto diff = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/diff src/main.cpp"});
  expect(diff && diff->handled && !diff->output.empty() &&
             diff->output[0].find("/diff [query] to show the latest or matching unified diff") != std::string::npos &&
             diff->output[0].find("/copy diff [query] to copy it") != std::string::npos,
         "command dispatcher recognizes filtered /diff as a TUI transcript inspection command");
  auto copy = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/copy tool"});
  expect(copy && copy->handled && !copy->output.empty() &&
             copy->output[0].find("/copy for the latest AVA message") != std::string::npos &&
             copy->output[0].find("/copy tool [query] for tool details") != std::string::npos &&
             copy->output[0].find("/copy diff [query] for unified diffs") != std::string::npos &&
             copy->output[0].find("/copy permission [query] for permission audit details") != std::string::npos,
         "command dispatcher recognizes /copy as a TUI clipboard command");
  auto copy_diff = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/copy diff src/main.cpp"});
  expect(copy_diff && copy_diff->handled && !copy_diff->output.empty() &&
             copy_diff->output[0].find("/copy diff [query] for unified diffs") != std::string::npos,
         "command dispatcher recognizes filtered /copy diff as a TUI clipboard command");
  auto copy_permission = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/copy permission git push"});
  expect(copy_permission && copy_permission->handled && !copy_permission->output.empty() &&
             copy_permission->output[0].find("/copy permission [query] for permission audit details") != std::string::npos,
         "command dispatcher recognizes filtered /copy permission as a TUI clipboard command");
  auto unsupported_copy = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/copy branch"});
  expect(unsupported_copy && unsupported_copy->handled && !unsupported_copy->output.empty() &&
             unsupported_copy->output[0].find("unsupported copy target: branch") != std::string::npos &&
             unsupported_copy->output[0].find("supported: tool [query], diff [query], permission [query]") != std::string::npos,
         "command dispatcher reports unsupported copy targets");
  auto thinking = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/thinking"});
  expect(thinking && thinking->handled && !thinking->output.empty() &&
             thinking->output[0].find("TUI display toggle") != std::string::npos &&
             thinking->output[0].find("does not change provider reasoning mode") != std::string::npos,
         "command dispatcher recognizes /thinking as display-only instead of changing backend reasoning mode");
  auto reload = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/reload"});
  expect(reload && reload->handled && !reload->output.empty() &&
             reload->output[0].find("Keybindings reload live inside the interactive TUI") != std::string::npos,
         "command dispatcher recognizes /reload while leaving live keybinding reload to the TUI runtime");
  {
    ScopedEnvVar no_color_guard("NO_COLOR", "");
    ScopedEnvVar theme_env_guard("AVA_TUI_THEME", "");
    ScopedEnvVar colorfgbg_guard("COLORFGBG", "");
    ava::tui::set_tui_config_theme(std::nullopt);
    write_app_test_file(paths.ava_config_dir / "display.json", "{\n  \"theme\": \"plain\"\n}\n");
    auto reload_theme = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/reload theme"});
    auto active_theme = ava::tui::active_tui_theme();
    expect(reload_theme && reload_theme->handled && !reload_theme->output.empty() &&
               reload_theme->output[0].find("TUI display settings reloaded") != std::string::npos &&
               reload_theme->output[0].find("configured: plain") != std::string::npos &&
               active_theme.kind == ava::tui::TuiThemeKind::Plain && active_theme.badge == "display.json",
           "command dispatcher /reload theme applies externally edited display.json without restarting");
    ava::tui::set_tui_config_theme(std::nullopt);
  }
  auto unsupported_reload = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/reload prompts"});
  expect(unsupported_reload && unsupported_reload->handled && !unsupported_reload->output.empty() &&
             unsupported_reload->output[0].find("unsupported reload target: prompts") != std::string::npos &&
             unsupported_reload->output[0].find("supported: keybindings, theme") != std::string::npos,
         "command dispatcher reports unsupported reload targets");
  auto help = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/help", .hotkeys = custom_hotkeys});
  expect(help && help->handled && !help->output.empty() && help->output[0].find("/hotkeys") != std::string::npos &&
             help->output[0].find("/keybindings") != std::string::npos &&
             help->output[0].find("/connect") != std::string::npos &&
             help->output[0].find("/plugins") != std::string::npos &&
             help->output[0].find("!<command>") != std::string::npos &&
             help->output[0].find("!!<command>") != std::string::npos &&
             help->output[0].find("Unavailable commands") != std::string::npos &&
             help->output[0].find("Ctrl+M") != std::string::npos,
         "command dispatcher /help includes catalog commands and effective hotkeys");

  auto bang_shell = ava::app::run_command(*session, ava::app::CommandRequest{.command = "!pwd"});
  expect(bang_shell && bang_shell->handled && bang_shell->tool_timeline.size() == 2 &&
             bang_shell->tool_timeline[0].name == "bash" &&
             bang_shell->tool_timeline[0].argument_summary == "pwd" &&
             bang_shell->tool_timeline[1].status == ava::agent::ToolTimelineStatus::Success &&
             !bang_shell->output.empty() && bang_shell->output[0].find("exit: 0") != std::string::npos,
         "Pi-style ! shell helper runs through the permissioned bash command path");
  auto hidden_bang_shell = ava::app::run_command(*session, ava::app::CommandRequest{.command = "!! pwd"});
  expect(hidden_bang_shell && hidden_bang_shell->handled && hidden_bang_shell->tool_timeline.size() == 2 &&
             hidden_bang_shell->tool_timeline[0].name == "bash" &&
             hidden_bang_shell->tool_timeline[0].argument_summary == "pwd" &&
             hidden_bang_shell->tool_timeline[1].status == ava::agent::ToolTimelineStatus::Success,
         "Pi-style !! shell helper is accepted as the hidden-output bash helper without bypassing permissions");
  auto missing_bang_shell = ava::app::run_command(*session, ava::app::CommandRequest{.command = "!"});
  expect(missing_bang_shell && missing_bang_shell->handled && !missing_bang_shell->output.empty() &&
             missing_bang_shell->output[0].find("!<command> or !!<command>") != std::string::npos,
         "empty shell helper reports the expected usage");
  auto find_alias = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/find src/*.cpp"});
  expect(find_alias && find_alias->handled && find_alias->tool_timeline.size() == 2 &&
             find_alias->tool_timeline[0].name == "find" &&
             find_alias->tool_timeline[1].result_json.find("\"tool\":\"glob\"") != std::string::npos &&
             !find_alias->output.empty() && find_alias->output[0].find("src/main.cpp") != std::string::npos,
         "Pi-style /find alias runs through the native glob tool path");
  auto ls_alias = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/ls src"});
  expect(ls_alias && ls_alias->handled && ls_alias->tool_timeline.size() == 2 &&
             ls_alias->tool_timeline[0].name == "ls" &&
             ls_alias->tool_timeline[1].result_json.find("\"tool\":\"list_directory\"") != std::string::npos &&
             !ls_alias->output.empty() && ls_alias->output[0].find("main.cpp") != std::string::npos,
         "Pi-style /ls alias runs through the native list_directory tool path");
  auto missing_find_alias = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/find"});
  expect(missing_find_alias && missing_find_alias->handled && !missing_find_alias->output.empty() &&
             missing_find_alias->output[0].find("/find <pattern>") != std::string::npos,
         "empty /find alias reports Pi-style usage instead of the native /glob name");

  auto plugins_usage = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins"});
  expect(plugins_usage && plugins_usage->handled && !plugins_usage->output.empty() &&
             plugins_usage->output[0].find("usage: /plugins") != std::string::npos,
         "command dispatcher /plugins without a subcommand reports usage");
  auto plugins = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins list"});
  expect(plugins && plugins->handled && !plugins->output.empty() &&
             plugins->output[0].find("com.example.global") != std::string::npos &&
             plugins->output[0].find("com.example.project") != std::string::npos &&
             plugins->output[0].find("Failures: 1") != std::string::npos,
         "command dispatcher /plugins list reports discovered plugins and diagnostics failures");
  auto inspect_plugin =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins inspect com.example.project"});
  expect(inspect_plugin && inspect_plugin->handled && !inspect_plugin->output.empty() &&
             inspect_plugin->output[0].find("entrypoint: node plugin.js --safe (not executed)") != std::string::npos &&
             inspect_plugin->output[0].find("no plugin process is started yet") != std::string::npos,
         "command dispatcher /plugins inspect shows manifest details without executing entrypoints");
  auto enable_plugin =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins enable com.example.project"});
  expect(enable_plugin && enable_plugin->handled && !enable_plugin->output.empty() &&
             enable_plugin->output[0].find("Enabled project plugin com.example.project") != std::string::npos &&
             enable_plugin->output[0].find("No plugin process was started") != std::string::npos,
         "command dispatcher /plugins enable records state without starting plugin processes");
  auto plugins_after_enable = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins list"});
  expect(plugins_after_enable && plugins_after_enable->handled && !plugins_after_enable->output.empty() &&
             plugins_after_enable->output[0].find("com.example.project  enabled") != std::string::npos,
         "command dispatcher /plugins list reflects enablement state");
  auto const slash_items = ava::app::command_catalog_slash_items(*session, custom_hotkeys);
  auto const file_reference_items = ava::app::file_reference_items(*session);
  auto find_slash_item = [&slash_items](std::string_view command) -> ava::tui::SlashCommandItem const* {
    for (auto const& item : slash_items) {
      if (item.command == command) return &item;
    }
    return nullptr;
  };
  auto has_completion = [](ava::tui::SlashCommandItem const* item, std::size_t argument_index, std::string_view value,
                           std::vector<std::string> previous_args = {}) {
    return item != nullptr && std::ranges::any_of(item->argument_completions, [&](auto const& completion) {
             return completion.argument_index == argument_index && completion.value == value &&
                    completion.required_previous_args == previous_args;
           });
  };
  auto has_alias = [](ava::tui::SlashCommandItem const& item, std::string_view value) {
    return std::ranges::find(item.aliases, value) != item.aliases.end();
  };
  auto has_file_reference = [&file_reference_items](std::string_view value) {
    return std::ranges::any_of(file_reference_items, [&](auto const& item) { return item.value == value; });
  };
  auto const* connect_item = find_slash_item("/connect");
  auto const* hotkeys_item = find_slash_item("/hotkeys");
  auto const* models_item = find_slash_item("/models");
  auto const* scoped_models_item = find_slash_item("/scoped-models");
  auto const* tool_item = find_slash_item("/tool");
  auto const* diff_item = find_slash_item("/diff");
  auto const* copy_item = find_slash_item("/copy");
  auto const* export_item = find_slash_item("/export");
  expect(tool_item != nullptr && tool_item->hint == "[query]" && has_alias(*tool_item, "/tools") &&
             tool_item->description.find("latest or matching tool details") != std::string::npos,
         "slash catalog exposes /tool for visible TUI tool-card inspection");
  expect(diff_item != nullptr && diff_item->hint == "[query]" &&
             diff_item->description.find("latest or matching tool diff") != std::string::npos,
         "slash catalog exposes /diff for visible TUI tool-diff inspection");
  expect(copy_item != nullptr && copy_item->hint.empty() && copy_item->description.find("latest AVA message") != std::string::npos &&
             copy_item->description.find("tool") != std::string::npos && copy_item->description.find("permission details") != std::string::npos,
         "slash catalog exposes Pi-style /copy clipboard command without blocking exact-submit behavior");
  expect(scoped_models_item != nullptr &&
             scoped_models_item->description.find("Ctrl+P cycling") != std::string::npos,
         "slash catalog exposes Pi-style /scoped-models model cycle selector entry point");
  auto const* sessions_item = find_slash_item("/sessions");
  auto const* context_item = find_slash_item("/context");
  auto const* read_item = find_slash_item("/read");
  auto const* write_item = find_slash_item("/write");
  auto const* glob_item = find_slash_item("/glob");
  auto const* find_item = find_slash_item("/find");
  auto const* ls_item = find_slash_item("/ls");
  auto const* grep_item = find_slash_item("/grep");
  auto const* mcp_item = find_slash_item("/mcp");
  auto const* plugin_item = find_slash_item("/plugin");
  auto const* permissions_item = find_slash_item("/permissions");
  expect(!has_completion(connect_item, 0, "openai") && !has_completion(connect_item, 1, "api-key") &&
             !has_completion(connect_item, 1, "browser-oauth", {"openai"}) &&
             !has_completion(connect_item, 1, "headless-oauth", {"openai"}) &&
             has_completion(models_item, 0, "openai/gpt-5.5") &&
             has_completion(sessions_item, 0, session->store.session_id()) &&
             has_completion(context_item, 0, (workspace / "AGENTS.md").generic_string()) &&
             has_completion(read_item, 0, "src/main.cpp") && has_completion(write_item, 0, "src/main.cpp") &&
             has_completion(glob_item, 0, "src/**") && has_completion(find_item, 0, "src/**") &&
             has_completion(ls_item, 0, "src/main.cpp") && has_completion(grep_item, 1, "src/**") &&
             has_completion(export_item, 0, "markdown") && has_completion(export_item, 0, "html") &&
             has_completion(hotkeys_item, 0, "init") &&
             has_completion(hotkeys_item, 0, "import") &&
             has_completion(hotkeys_item, 0, "set") &&
             has_completion(hotkeys_item, 0, "reset") &&
             has_completion(hotkeys_item, 1, "submit", {"set"}) &&
             has_completion(hotkeys_item, 1, "variant_cycle", {"set"}) &&
             has_completion(hotkeys_item, 1, "submit", {"reset"}) &&
             has_completion(hotkeys_item, 0, "validate") &&
             has_completion(hotkeys_item, 1, "--force", {"init"}) &&
             has_completion(hotkeys_item, 2, "--force", {"import"}) &&
             !has_completion(read_item, 0, "my folder/space file.txt") &&
             !has_completion(read_item, 0, "docs/reference-code/pi/reference-only.md") &&
             has_file_reference("src/main.cpp") && has_file_reference("my folder/space file.txt") &&
             !has_file_reference("docs/reference-code/pi/reference-only.md") &&
             has_completion(mcp_item, 1, "fs", {"inspect"}) &&
             has_completion(plugin_item, 1, "com.example.project", {"run"}) &&
             has_completion(plugin_item, 2, "todo", {"run", "com.example.project"}) &&
             has_completion(permissions_item, 0, "list") && has_completion(permissions_item, 0, "audit") &&
             has_completion(permissions_item, 0, "add") &&
             has_completion(permissions_item, 1, "export", {"audit"}) &&
             has_completion(permissions_item, 1, "summary", {"audit"}) &&
             has_completion(permissions_item, 1, "show", {"audit"}) &&
             has_completion(permissions_item, 1, "action=allow", {"add"}) &&
             has_completion(permissions_item, 1, "operation=read", {"add"}) &&
             has_completion(permissions_item, 1, "reason=", {"add"}),
         "command catalog argument completions keep /connect provider and method choices in the modal while "
         "populating model, session, context, export format, file path, file reference, MCP, plugin, and permission-rule metadata");
  auto disable_plugin =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins disable com.example.project"});
  expect(disable_plugin && disable_plugin->handled && !disable_plugin->output.empty() &&
             disable_plugin->output[0].find("Disabled project plugin com.example.project") != std::string::npos &&
             disable_plugin->output[0].find("No plugin process was stopped") != std::string::npos,
         "command dispatcher /plugins disable records state without stopping plugin processes");
  auto validate_plugin = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/plugins validate .ava/plugins/com.example.project/plugin.json"});
  expect(validate_plugin && validate_plugin->handled && !validate_plugin->output.empty() &&
             validate_plugin->output[0].find("Valid plugin manifest") != std::string::npos &&
             validate_plugin->output[0].find("no entrypoint was executed") != std::string::npos,
         "command dispatcher /plugins validate parses manifests without executing entrypoints");
  auto plugin_failures = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins failures"});
  expect(plugin_failures && plugin_failures->handled && !plugin_failures->output.empty() &&
             plugin_failures->output[0].find("com.example.bad") != std::string::npos,
         "command dispatcher /plugins failures reports invalid discovered manifests");
  auto models = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/model"});
  expect(models && models->handled && !models->output.empty() &&
             models->output[0].find("Models:") != std::string::npos &&
             models->output[0].find("current ") != std::string::npos &&
             models->output[0].find("reasoning current") != std::string::npos &&
             models->output[0].find("reasoning levels: low, medium, high, xhigh") != std::string::npos &&
             models->output[0].find("reasoning params") != std::string::npos &&
             models->output[0].find("reasoning.effort=<level>") != std::string::npos &&
             models->output[0].find("reasoning.summary=auto") != std::string::npos &&
             models->output[0].find("reasoning format") != std::string::npos &&
             models->output[0].find("Shift+Tab or Ctrl+T cycles") != std::string::npos,
         "command dispatcher lists provider/model reasoning metadata and documents TUI reasoning cycling");
  auto filtered_models = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/models gpt-5.5"});
  expect(filtered_models && filtered_models->handled && !filtered_models->output.empty() &&
             filtered_models->output[0].find("filter gpt-5.5") != std::string::npos &&
             filtered_models->output[0].find("gpt-5.5") != std::string::npos,
         "command dispatcher /models accepts backend-backed autocomplete query text without switching models");
  auto diagnostic_models = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/models diagnostic-local"});
  expect(diagnostic_models && diagnostic_models->handled && !diagnostic_models->output.empty() &&
             diagnostic_models->output[0].find("Diagnostic Local") != std::string::npos &&
             diagnostic_models->output[0].find("diagnostics:") != std::string::npos &&
             diagnostic_models->output[0].find("custom model missing context_window_tokens") != std::string::npos &&
             diagnostic_models->output[0].find("custom model has unknown tool support") != std::string::npos &&
             diagnostic_models->output[0].find("reasoning model has no reasoning_levels") != std::string::npos,
         "command dispatcher /models reports actionable custom-model diagnostics");
  auto unavailable_models = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/models ghost"});
  expect(unavailable_models && unavailable_models->handled && !unavailable_models->output.empty() &&
             unavailable_models->output[0].find("Remote Missing") != std::string::npos &&
             unavailable_models->output[0].find("provider is not registered") != std::string::npos &&
             unavailable_models->output[0].find("selector is disabled") != std::string::npos,
         "command dispatcher /models reports unregistered provider diagnostics");

  auto context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context"});
  expect(context && context->handled && !context->output.empty() &&
             context->output[0].find("Context freshness:") != std::string::npos &&
             context->output[0].find("mode=plan") != std::string::npos &&
             context->output[0].find("model=openai/gpt-5.5") != std::string::npos &&
             context->output[0].find("prompt=override") != std::string::npos &&
             context->output[0].find("plan.txt") != std::string::npos &&
             context->output[0].find("context_sources=1") != std::string::npos &&
             context->output[0].find("prompt_commands=1") != std::string::npos &&
             context->output[0].find("skills=") != std::string::npos &&
             context->output[0].find("plugin_sources=4") != std::string::npos &&
             context->output[0].find("workspace") != std::string::npos &&
             context->output[0].find("AGENTS.md") != std::string::npos &&
             context->output[0].find("prompt_command  project  prompt-check") != std::string::npos &&
             context->output[0].find("skill  project  dispatcher-skill") != std::string::npos &&
             context->output[0].find("plugin_manifest  project  com.example.project/manifest") != std::string::npos &&
             context->output[0].find("plugin_prompt  project  com.example.project/review") != std::string::npos &&
             context->output[0].find("plugin_skill  project  com.example.project/triage") != std::string::npos &&
             context->output[0].find("loaded_bytes=") != std::string::npos &&
             context->output[0].find("status=changed") != std::string::npos &&
             context->output[0].find("current_bytes=") != std::string::npos,
         "command dispatcher /context reports context freshness metadata");
  auto prompt_context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context prompt"});
  expect(prompt_context && prompt_context->handled && !prompt_context->output.empty() &&
             prompt_context->output[0].find("prompt=override") != std::string::npos &&
             prompt_context->output[0].find("plan.txt") != std::string::npos &&
             prompt_context->output[0].find("prompt_command  project  prompt-check") != std::string::npos &&
             prompt_context->output[0].find("plugin_prompt  project  com.example.project/review") != std::string::npos &&
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
             skill_context->output[0].find("status=changed") != std::string::npos &&
             skill_context->output[0].find("AGENTS.md") == std::string::npos,
         "command dispatcher /context can filter to loaded skill freshness metadata");
  auto plugin_prompt_context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context review"});
  expect(plugin_prompt_context && plugin_prompt_context->handled && !plugin_prompt_context->output.empty() &&
             plugin_prompt_context->output[0].find("plugin_prompt  project  com.example.project/review") != std::string::npos &&
             plugin_prompt_context->output[0].find("status=changed") != std::string::npos &&
             plugin_prompt_context->output[0].find("AGENTS.md") == std::string::npos,
         "command dispatcher /context can filter to plugin prompt resource freshness metadata");
  auto filtered_context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context AGENTS"});
  expect(filtered_context && filtered_context->handled && !filtered_context->output.empty() &&
             filtered_context->output[0].find("Context freshness:") != std::string::npos &&
             filtered_context->output[0].find("AGENTS.md") != std::string::npos &&
             filtered_context->output[0].find("status=changed") != std::string::npos &&
             filtered_context->output[0].find("prompt=override") == std::string::npos,
         "command dispatcher /context accepts backend context-source query text");
  auto filtered_sessions =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions " + session->store.session_id()});
  expect(filtered_sessions && filtered_sessions->handled && !filtered_sessions->output.empty() &&
             filtered_sessions->output[0].find(session->store.session_id()) != std::string::npos,
         "command dispatcher /sessions accepts backend session-id query text");
  auto archive_active = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/sessions archive " + session->store.session_id() + " --confirm"});
  auto active_metadata_after_archive_attempt = ava::session::load_session_metadata(session->store);
  expect(archive_active && archive_active->handled && !archive_active->output.empty() &&
             archive_active->output[0].find("Cannot archive the active session") != std::string::npos &&
             active_metadata_after_archive_attempt && !active_metadata_after_archive_attempt->archived,
         "slash /sessions archive refuses to archive the active runtime session");
  auto branch = ava::session::create_session_branch(ava::session::SessionBranchOptions{
      .workspace_dir = workspace,
      .root_dir = paths.sessions_dir,
      .source_session_id = session->store.session_id(),
      .branch_from_entry_id = {},
      .name = std::optional<std::string>("Review branch"),
      .labels = std::optional<std::vector<std::string>>(std::vector<std::string>{"review", "ui"}),
      .mode = ava::session::SessionBranchMode::Fork,
      .actor = "test"});
  expect(branch.has_value(), branch ? "command dispatcher /sessions test creates a branch"
                                    : "command dispatcher /sessions test creates a branch: " + branch.error().format());
  auto tree_sessions = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions review"});
  expect(tree_sessions && tree_sessions->handled && !tree_sessions->output.empty() &&
             tree_sessions->output[0].find("Sessions:") != std::string::npos &&
             tree_sessions->output[0].find("Review branch") != std::string::npos &&
             tree_sessions->output[0].find("origin=fork") != std::string::npos &&
             tree_sessions->output[0].find("labels=review,ui") != std::string::npos &&
             tree_sessions->output[0].find("parent=") != std::string::npos,
         "command dispatcher /sessions exposes tree branch names, labels, provenance, and parent links");
  if (branch)
  {
    auto const branch_session_id = branch->store.session_id();
    auto target_labels =
        ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions labels " + branch_session_id + " triage selected"});
    auto branch_metadata_after_labels = ava::session::load_session_metadata(branch->store);
    auto target_label_status =
        ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions labels " + branch_session_id});
    auto target_label_query = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions triage"});
    expect(target_labels && target_labels->handled && !target_labels->output.empty() &&
               target_labels->output[0].find("session " + branch_session_id + " labels set: triage,selected") != std::string::npos &&
               branch_metadata_after_labels && branch_metadata_after_labels->labels.size() == 2 &&
               branch_metadata_after_labels->labels[0] == "triage" && branch_metadata_after_labels->labels[1] == "selected" &&
               target_label_status && target_label_status->handled && !target_label_status->output.empty() &&
               target_label_status->output[0].find("session " + branch_session_id + " labels: triage,selected") != std::string::npos &&
               target_label_query && target_label_query->handled && !target_label_query->output.empty() &&
               target_label_query->output[0].find("labels=triage,selected") != std::string::npos &&
               session->store.session_id() != branch_session_id,
           "slash /sessions labels updates a selected session without switching runtime sessions");

    auto archive_without_confirm =
        ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions archive " + branch_session_id});
    auto metadata_before_archive = ava::session::load_session_metadata(branch->store);
    expect(archive_without_confirm && archive_without_confirm->handled && !archive_without_confirm->output.empty() &&
               archive_without_confirm->output[0].find("--confirm") != std::string::npos &&
               metadata_before_archive && !metadata_before_archive->archived,
           "slash /sessions archive requires explicit confirmation before mutating metadata");

    auto archived = ava::app::run_command(
        *session, ava::app::CommandRequest{.command = "/sessions archive " + branch_session_id + " --confirm"});
    auto metadata_after_archive = ava::session::load_session_metadata(branch->store);
    auto hidden_archived = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions review"});
    auto visible_archived = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions --archived review"});
    expect(archived && archived->handled && !archived->output.empty() &&
               archived->output[0].find("session " + branch_session_id + " archived") != std::string::npos &&
               metadata_after_archive && metadata_after_archive->archived &&
               hidden_archived && hidden_archived->handled && !hidden_archived->output.empty() &&
               hidden_archived->output[0].find("Review branch") == std::string::npos &&
               visible_archived && visible_archived->handled && !visible_archived->output.empty() &&
               visible_archived->output[0].find("Review branch") != std::string::npos &&
               visible_archived->output[0].find("archived") != std::string::npos,
           "slash /sessions archive hides sessions from default lists while --archived includes them");

    auto unarchived =
        ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions unarchive " + branch_session_id});
    auto metadata_after_unarchive = ava::session::load_session_metadata(branch->store);
    expect(unarchived && unarchived->handled && !unarchived->output.empty() &&
               unarchived->output[0].find("session " + branch_session_id + " unarchived") != std::string::npos &&
               metadata_after_unarchive && !metadata_after_unarchive->archived,
           "slash /sessions unarchive restores archived sessions to default views");
  }

  auto permissions_empty = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions"});
  expect(permissions_empty && permissions_empty->handled && !permissions_empty->output.empty() &&
             permissions_empty->output[0].find("Permission rules:") != std::string::npos &&
             permissions_empty->output[0].find("No persistent permission rules") != std::string::npos,
         "command dispatcher /permissions lists empty persistent permission rules with storage context");
  auto add_permission_rule = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/permissions add action=allow operation=read path=src/main.cpp "
                                                    "reason=\"trusted local read\""});
  expect(add_permission_rule && add_permission_rule->handled && !add_permission_rule->output.empty() &&
             add_permission_rule->output[0].find("added permission rule permrule_") != std::string::npos &&
             add_permission_rule->output[0].find("path=src/main.cpp") != std::string::npos &&
             add_permission_rule->output[0].find("trusted local read") != std::string::npos,
         "command dispatcher /permissions add stores a quoted persistent path rule");
  auto extract_rule_id = [](std::string const& text) {
    auto const start = text.find("permrule_");
    if (start == std::string::npos) return std::string{};
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
  auto permissions_explain =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permission-rules explain " + permission_rule_id});
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
  auto append_permission_audit = append_permission_audit_for_test(
      session->store,
      ava::tools::PermissionAuditEvent{.permission_request_id = "permreq_runtime_deny",
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
  expect(append_permission_audit.has_value(),
         append_permission_audit ? "command dispatcher test appends a permission audit entry"
                                 : "command dispatcher test appends a permission audit entry: " +
                                       append_permission_audit.error().format());
  auto permissions_audit =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions audit git push"});
  expect(permissions_audit && permissions_audit->handled && !permissions_audit->output.empty() &&
             permissions_audit->output[0].find("Permission audit:") != std::string::npos &&
             permissions_audit->output[0].find("permreq_runtime_deny") != std::string::npos &&
             permissions_audit->output[0].find("git push origin main") != std::string::npos &&
             permissions_audit->output[0].find("source=resolver") != std::string::npos &&
             permissions_audit->output[0].find("remembered deny | rule") != std::string::npos,
         "command dispatcher /permissions audit filters persisted permission decisions");
  auto permissions_audit_summary =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions audit summary git push"});
  expect(permissions_audit_summary && permissions_audit_summary->handled &&
             !permissions_audit_summary->output.empty() &&
             permissions_audit_summary->output[0].find("Permission audit summary:") != std::string::npos &&
             permissions_audit_summary->output[0].find("filter: git push") != std::string::npos &&
             permissions_audit_summary->output[0].find("entries: 1 matching") != std::string::npos &&
             permissions_audit_summary->output[0].find("denials: 1") != std::string::npos &&
             permissions_audit_summary->output[0].find("by action: deny=1") != std::string::npos &&
             permissions_audit_summary->output[0].find("by resolution: deny=1") != std::string::npos &&
             permissions_audit_summary->output[0].find("by source: resolver=1") != std::string::npos &&
             permissions_audit_summary->output[0].find("by risk: high=1") != std::string::npos &&
             permissions_audit_summary->output[0].find("by tool: bash=1") != std::string::npos &&
             permissions_audit_summary->output[0].find("/permissions audit show permreq_runtime_deny") !=
                 std::string::npos,
         "command dispatcher /permissions audit summary groups matching permission decisions for browsing");
  auto permissions_audit_export =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions audit export git push"});
  expect(permissions_audit_export && permissions_audit_export->handled && !permissions_audit_export->output.empty() &&
             permissions_audit_export->output[0].find("Permission audit export:") != std::string::npos &&
             permissions_audit_export->output[0].find("format: markdown table") != std::string::npos &&
             permissions_audit_export->output[0].find("| timestamp | entry | request | action | resolution |") !=
                 std::string::npos &&
             permissions_audit_export->output[0].find("| deny |") != std::string::npos &&
             permissions_audit_export->output[0].find("git push origin main") != std::string::npos &&
             permissions_audit_export->output[0].find("remembered deny \\| rule") != std::string::npos,
         "command dispatcher /permissions audit export renders copyable markdown and escapes table cells");
  auto permissions_diagnose_denial =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions diagnose git push"});
  expect(permissions_diagnose_denial && permissions_diagnose_denial->handled &&
             !permissions_diagnose_denial->output.empty() &&
             permissions_diagnose_denial->output[0].find("Permission rule diagnostics:") != std::string::npos &&
             permissions_diagnose_denial->output[0].find("Recent permission denials:") != std::string::npos &&
             permissions_diagnose_denial->output[0].find("decision=deny") != std::string::npos &&
             permissions_diagnose_denial->output[0].find("source=resolver") != std::string::npos &&
             permissions_diagnose_denial->output[0].find("git push origin main") != std::string::npos &&
             permissions_diagnose_denial->output[0].find("reason: command can change external or destructive state") !=
                 std::string::npos &&
             permissions_diagnose_denial->output[0].find("resolution reason: remembered deny | rule") !=
                 std::string::npos &&
             permissions_diagnose_denial->output[0].find("next: /permissions explain " + permission_rule_id) !=
                 std::string::npos,
         "command dispatcher /permissions diagnose explains recent denied decisions with follow-up commands");
  auto permissions_audit_detail =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions audit show permreq_runtime"});
  expect(permissions_audit_detail && permissions_audit_detail->handled && !permissions_audit_detail->output.empty() &&
             permissions_audit_detail->output[0].find("Permission audit detail:") != std::string::npos &&
             permissions_audit_detail->output[0].find("selector: permreq_runtime") != std::string::npos &&
             permissions_audit_detail->output[0].find("matched entries: 1") != std::string::npos &&
             permissions_audit_detail->output[0].find("request: permreq_runtime_deny") != std::string::npos &&
             permissions_audit_detail->output[0].find("command: git push origin main") != std::string::npos &&
             permissions_audit_detail->output[0].find("resolution reason: remembered deny | rule") !=
                 std::string::npos &&
             permissions_audit_detail->output[0].find("/permissions audit export permreq_runtime") != std::string::npos &&
             permissions_audit_detail->output[0].find("/permissions explain " + permission_rule_id) != std::string::npos,
         "command dispatcher /permissions audit show drills into a permission request by id prefix");
  auto permissions_audit_empty =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions audit unmatched-query"});
  expect(permissions_audit_empty && permissions_audit_empty->handled && !permissions_audit_empty->output.empty() &&
             permissions_audit_empty->output[0].find("No permission audit entries match") != std::string::npos,
         "command dispatcher /permissions audit reports empty filtered results");
  auto slash_items_after_permission_rule = ava::app::command_catalog_slash_items(*session, custom_hotkeys);
  auto const permission_completion_available =
      std::ranges::any_of(slash_items_after_permission_rule, [&](auto const& item) {
        return item.command == "/permissions" && has_completion(&item, 1, permission_rule_id, {"explain"}) &&
               has_completion(&item, 1, permission_rule_id, {"remove"});
      });
  expect(permission_completion_available,
         "command catalog argument completions expose persistent permission rule ids for explain and remove");
  auto remove_permission_rule =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions remove " + permission_rule_id});
  expect(remove_permission_rule && remove_permission_rule->handled && !remove_permission_rule->output.empty() &&
             remove_permission_rule->output[0].find("removed permission rule " + permission_rule_id) != std::string::npos,
         "command dispatcher /permissions remove deletes persistent rules by id");
  auto permissions_after_remove = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/permissions list"});
  expect(permissions_after_remove && permissions_after_remove->handled && !permissions_after_remove->output.empty() &&
             permissions_after_remove->output[0].find("No persistent permission rules") != std::string::npos,
         "command dispatcher /permissions list reflects removed persistent rules");

  auto mode = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/mode"});
  expect(mode && mode->handled && session->mode == ava::agent::Mode::Build && !mode->output.empty() &&
             mode->output[0].find("build") != std::string::npos,
         "command dispatcher /mode toggles runtime mode");
  expect(session->system_prompt != plan_system_prompt &&
             session->system_prompt.find("Implement changes directly") != std::string::npos &&
             session->system_prompt.find("dispatcher context changed after session open") != std::string::npos,
         "command dispatcher /mode rebuilds the mode-specific system prompt with context");

  bool saw_secret_prompt = false;
  auto connect = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/login moonshot api-key",
                                         .question_resolver = [&](ava::agent::QuestionPrompt const& prompt) {
                                           saw_secret_prompt = prompt.modal && prompt.secret && prompt.allow_custom &&
                                                               prompt.question.find("moonshot") != std::string::npos;
                                           return ava::agent::QuestionAnswer{.selected_options = {},
                                                                             .custom_text = "slash-moonshot-api-key"};
                                         }});
  expect(connect && connect->handled && saw_secret_prompt && !connect->output.empty() &&
             connect->output[0].find("Stored moonshot API key credential") != std::string::npos,
         "command dispatcher /login alias stores provider API key credentials via masked prompt");
  ava::tests::FakeTransport credential_transport({});
  auto slash_moonshot = ava::config::provider_credential_for_request(session->paths, "moonshot", credential_transport);
  expect(slash_moonshot && slash_moonshot->has_value() && (*slash_moonshot)->access_token == "slash-moonshot-api-key" &&
             (*slash_moonshot)->credential_type == "api_key",
         "slash provider connect writes loadable provider credential");

  std::size_t connect_prompt_count = 0;
  auto connect_modal = ava::app::run_command(
      *session,
      ava::app::CommandRequest{
          .command = "/connect", .question_resolver = [&](ava::agent::QuestionPrompt const& prompt) {
            if (connect_prompt_count == 0) {
              expect(prompt.modal && prompt.searchable && prompt.allow_custom && prompt.question == "Select provider",
                     "slash /connect opens provider selection as searchable modal");
              ++connect_prompt_count;
              return ava::agent::QuestionAnswer{.selected_options = {"anthropic"}, .custom_text = ""};
            }
            if (connect_prompt_count == 1) {
              expect(prompt.modal && prompt.secret && prompt.question.find("anthropic") != std::string::npos,
                     "slash /connect opens secret prompt as masked modal");
              ++connect_prompt_count;
              return ava::agent::QuestionAnswer{.selected_options = {}, .custom_text = "slash-api-key"};
            }
            expect(false, "slash /connect should not prompt for a non-OpenAI credential type");
            ++connect_prompt_count;
            return ava::agent::QuestionAnswer{};
          }});
  expect(connect_modal && connect_modal->handled && connect_prompt_count == 2 && !connect_modal->output.empty() &&
             connect_modal->output[0].find("Stored anthropic API key credential") != std::string::npos,
         "command dispatcher /connect walks provider and secret modals for API-key-only providers");
  auto slash_anthropic =
      ava::config::provider_credential_for_request(session->paths, "anthropic", credential_transport);
  expect(slash_anthropic && slash_anthropic->has_value() && (*slash_anthropic)->access_token == "slash-api-key" &&
             (*slash_anthropic)->credential_type == "api_key",
         "slash provider connect modal writes loadable API key credential");

  std::size_t openai_connect_prompt_count = 0;
  auto connect_openai_modal = ava::app::run_command(
      *session,
      ava::app::CommandRequest{
          .command = "/connect", .question_resolver = [&](ava::agent::QuestionPrompt const& prompt) {
            if (openai_connect_prompt_count == 0) {
              bool saw_active_openai = false;
              std::size_t kimi_moonshot_count = 0;
              bool saw_split_kimi = false;
              bool saw_manual_token_text = false;
              for (auto const& option : prompt.options) {
                saw_active_openai = saw_active_openai || option.label == "OpenAI ✓";
                if (option.label.find("Kimi / Moonshot") != std::string::npos) ++kimi_moonshot_count;
                saw_split_kimi =
                    saw_split_kimi || option.label.starts_with("Kimi -") || option.label.starts_with("Moonshot -");
                saw_manual_token_text = saw_manual_token_text || option.label.find("token") != std::string::npos;
              }
              expect(prompt.modal && prompt.searchable && prompt.allow_custom && prompt.question == "Select provider" &&
                         saw_active_openai && kimi_moonshot_count == 1 && !saw_split_kimi && !saw_manual_token_text,
                     "slash /connect opens provider modal with active OpenAI and merged API-key-only providers");
              ++openai_connect_prompt_count;
              return ava::agent::QuestionAnswer{.selected_options = {"openai"}, .custom_text = ""};
            }
            if (openai_connect_prompt_count == 1) {
              bool saw_browser = false;
              bool saw_headless = false;
              bool saw_api_key = false;
              bool saw_previous = false;
              for (auto const& option : prompt.options) {
                saw_browser = saw_browser || option.value == "openai_browser_oauth";
                saw_headless = saw_headless || option.value == "openai_headless_oauth";
                saw_api_key = saw_api_key || option.value == "api_key";
                saw_previous = saw_previous || option.value == "back";
              }
              expect(prompt.modal && !prompt.searchable && !prompt.secret && prompt.question == "Choose login method" &&
                         saw_browser && saw_headless && saw_api_key && saw_previous,
                     "slash /connect OpenAI method modal lists browser, headless, API key, and previous options");
              ++openai_connect_prompt_count;
              return ava::agent::QuestionAnswer{.selected_options = {"api_key"}, .custom_text = ""};
            }
            expect(openai_connect_prompt_count == 2 && prompt.modal && prompt.secret &&
                       prompt.question.find("openai") != std::string::npos,
                   "slash /connect OpenAI API key choice opens masked secret modal");
            ++openai_connect_prompt_count;
            return ava::agent::QuestionAnswer{.selected_options = {}, .custom_text = "slash-openai-modal-api-key"};
          }});
  expect(connect_openai_modal && connect_openai_modal->handled && openai_connect_prompt_count == 3 &&
             !connect_openai_modal->output.empty() &&
             connect_openai_modal->output[0].find("Stored openai API key credential") != std::string::npos,
         "command dispatcher /connect OpenAI walks provider, method, and secret modals");
  auto slash_openai_from_modal = ava::config::load_openai_credential(session->paths);
  expect(slash_openai_from_modal && slash_openai_from_modal->has_value() &&
             (*slash_openai_from_modal)->type == ava::config::OpenAICredentialType::ApiKey &&
             (*slash_openai_from_modal)->access_token == "slash-openai-modal-api-key",
         "slash OpenAI connect modal writes loadable OpenAI credential");

  std::size_t back_connect_prompt_count = 0;
  auto connect_back_modal = ava::app::run_command(
      *session, ava::app::CommandRequest{
                    .command = "/connect", .question_resolver = [&](ava::agent::QuestionPrompt const& prompt) {
                      if (back_connect_prompt_count == 0) {
                        ++back_connect_prompt_count;
                        return ava::agent::QuestionAnswer{.selected_options = {"openai"}, .custom_text = ""};
                      }
                      if (back_connect_prompt_count == 1) {
                        expect(prompt.question == "Choose login method",
                               "slash /connect can navigate back from method modal");
                        ++back_connect_prompt_count;
                        return ava::agent::QuestionAnswer{.selected_options = {"back"}, .custom_text = ""};
                      }
                      if (back_connect_prompt_count == 2) {
                        expect(prompt.question == "Select provider", "slash /connect back returns to provider modal");
                        ++back_connect_prompt_count;
                        return ava::agent::QuestionAnswer{.selected_options = {"anthropic"}, .custom_text = ""};
                      }
                      if (back_connect_prompt_count == 3) {
                        expect(prompt.modal && prompt.secret && prompt.question.find("anthropic") != std::string::npos,
                               "slash /connect back skips method modal for API-key-only providers");
                        ++back_connect_prompt_count;
                        return ava::agent::QuestionAnswer{.selected_options = {}, .custom_text = "slash-back-api-key"};
                      }
                      expect(false, "slash /connect back should not show an extra non-OpenAI method modal");
                      ++back_connect_prompt_count;
                      return ava::agent::QuestionAnswer{};
                    }});
  expect(connect_back_modal && connect_back_modal->handled && back_connect_prompt_count == 4 &&
             !connect_back_modal->output.empty() &&
             connect_back_modal->output[0].find("Stored anthropic API key credential") != std::string::npos,
         "command dispatcher /connect previous option returns to provider modal");

  auto connect_cancel = ava::app::run_command(
      *session,
      ava::app::CommandRequest{.command = "/connect", .question_resolver = [](ava::agent::QuestionPrompt const&) {
                                 return ava::core::Result<ava::agent::QuestionAnswer>{std::unexpected(
                                     ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt canceled"))};
                               }});
  expect(connect_cancel && connect_cancel->handled && connect_cancel->output.empty(),
         "command dispatcher /connect treats modal cancellation as a silent close");

  bool saw_openai_secret_prompt = false;
  auto connect_openai_api = ava::app::run_command(
      *session,
      ava::app::CommandRequest{
          .command = "/connect openai api-key", .question_resolver = [&](ava::agent::QuestionPrompt const& prompt) {
            saw_openai_secret_prompt = prompt.modal && prompt.secret && prompt.allow_custom &&
                                       prompt.question.find("openai") != std::string::npos;
            return ava::agent::QuestionAnswer{.selected_options = {}, .custom_text = "slash-openai-api-key"};
          }});
  expect(connect_openai_api && connect_openai_api->handled && saw_openai_secret_prompt &&
             !connect_openai_api->output.empty() &&
             connect_openai_api->output[0].find("Stored openai API key credential") != std::string::npos,
         "command dispatcher /connect openai api-key prompts once and stores OpenAI API key credential");
  auto slash_openai = ava::config::load_openai_credential(session->paths);
  expect(slash_openai && slash_openai->has_value() &&
             (*slash_openai)->type == ava::config::OpenAICredentialType::ApiKey &&
             (*slash_openai)->access_token == "slash-openai-api-key",
         "slash OpenAI API key connect writes loadable OpenAI credential");

  auto connect_without_tui = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/connect anthropic"});
  expect(connect_without_tui && connect_without_tui->handled && !connect_without_tui->output.empty() &&
             connect_without_tui->output[0].find("--api-key-stdin") != std::string::npos &&
             connect_without_tui->output[0].find("--api-key-env") != std::string::npos,
         "command dispatcher /connect no-TUI error lists API-key headless setup flags");

  std::vector<ava::app::RuntimeEvent> command_tool_events;
  auto glob = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/glob **/*.cpp",
                                         .event_sink = [&command_tool_events](ava::app::RuntimeEvent const& event) {
                                           command_tool_events.push_back(event);
                                           return ava::core::VoidResult{};
                                         }});
  expect(glob && glob->handled && !glob->output.empty() && glob->output[0].find("src/main.cpp") != std::string::npos,
         "command dispatcher /glob runs existing safe file search command");
  expect(glob && glob->tool_timeline.size() == 2 &&
             glob->tool_timeline[0].status == ava::agent::ToolTimelineStatus::Running &&
             glob->tool_timeline[1].status == ava::agent::ToolTimelineStatus::Success &&
             glob->tool_timeline[1].structured_result_json.find("\"status\":\"success\"") != std::string::npos &&
             glob->tool_timeline[1].total_matches,
         "command dispatcher records running and completed timeline entries with structured result metadata");
  expect(command_tool_events.size() == 2 && command_tool_events[1].type == ava::app::RuntimeEventType::ToolResult &&
             !command_tool_events[1].tool_structured_result_json.empty() &&
             command_tool_events[1].tool_structured_result_json.find("\"tool\":\"glob\"") != std::string::npos &&
             command_tool_events[1].total_matches > 0,
         "command dispatcher emits structured tool result runtime events");

  std::vector<ava::app::RuntimeEvent> write_tool_events;
  auto write = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/write src/main.cpp int changed() { return 1; }",
                                         .event_sink = [&write_tool_events](ava::app::RuntimeEvent const& event) {
                                           write_tool_events.push_back(event);
                                           return ava::core::VoidResult{};
                                         }});
  expect(write && write->handled && write->tool_timeline.size() == 2 &&
             write->tool_timeline[1].status == ava::agent::ToolTimelineStatus::Success &&
             write->tool_timeline[1].diff.find("-int main()") != std::string::npos &&
             write->tool_timeline[1].diff.find("+int changed()") != std::string::npos &&
             write_tool_events.size() == 2 && write_tool_events[1].diff.find("+int changed()") != std::string::npos &&
             std::ranges::any_of(write_tool_events[1].changed_paths,
                                 [](std::string const& path) { return path.ends_with("src/main.cpp"); }),
         "command dispatcher /write forwards successful mutation diffs and changed paths into tool events");

  std::size_t compact_generator_calls = 0;
  auto compact_generator = [&](std::vector<ava::session::SessionEntry> const& entries,
                               ava::session::CompactionConfig const& config, std::string_view instructions,
                               std::size_t estimated_tokens) -> ava::core::Result<std::string> {
    ++compact_generator_calls;
    static_cast<void>(instructions);
    expect(!entries.empty() && config.max_summary_bytes > 0,
           "command dispatcher /compact passes session source data to summary generator");
    static_cast<void>(estimated_tokens);
    return std::string(
        "# Goal\nKeep key facts\n# Constraints / Preferences\nNone noted.\n# Decisions\nNone noted.\n"
        "# Files Read or Modified\nsrc/main.cpp\n# Unresolved Tasks\nNone noted.\n# Next Steps\nContinue.");
  };
  auto compact =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/compact Keep key facts",
                                                               .compaction_summary_generator = compact_generator});
  expect(compact && compact->handled && !compact->output.empty() &&
             compact->output[0].find("compaction summary recorded") != std::string::npos,
         "command dispatcher /compact records generated compaction summary");
  auto compact_empty = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/compact", .compaction_summary_generator = compact_generator});
  expect(compact_empty && compact_empty->handled && !compact_empty->output.empty() &&
             compact_empty->output[0].find("compaction summary recorded") != std::string::npos,
         "command dispatcher /compact without instructions records generated compaction summary");
  auto compact_trailing = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/compact ", .compaction_summary_generator = compact_generator});
  expect(compact_trailing && compact_trailing->handled && !compact_trailing->output.empty() &&
             compact_trailing->output[0].find("compaction summary recorded") != std::string::npos,
         "command dispatcher /compact with trailing space records generated compaction summary");
  expect(compact_generator_calls == 3, "command dispatcher /compact invokes the summary generator once per command");

  auto entries = session->store.load();
  expect(entries && std::ranges::any_of(*entries,
                                        [](ava::session::SessionEntry const& entry) {
                                          return entry.type == ava::session::EntryType::Compaction &&
                                                 entry.data_json.find("Keep key facts") != std::string::npos &&
                                                 entry.data_json.find("\"summary_unavailable\":false") !=
                                                     std::string::npos;
                                        }),
         "command dispatcher /compact persists generated summary and instructions");

  auto const compactions_before_stale = entries ? count_compaction_entries(*entries) : 0;
  std::mutex session_mutex;
  bool introduced_manual_stale_snapshot = false;
  std::size_t manual_stale_generator_calls = 0;
  auto stale_compact = ava::app::run_command(
      *session,
      ava::app::CommandRequest{
          .command = "/compact stale snapshot",
          .compaction_summary_generator = [&](std::vector<ava::session::SessionEntry> const&,
                                              ava::session::CompactionConfig const&, std::string_view,
                                              std::size_t) -> ava::core::Result<std::string> {
            ++manual_stale_generator_calls;
            if (!introduced_manual_stale_snapshot) {
              introduced_manual_stale_snapshot = true;
              static_cast<void>(session->store.append(
                  ava::session::SessionEntry{.id = "entry_manual_compact_concurrent_change",
                                             .parent_id = "",
                                             .type = ava::session::EntryType::UserMessage,
                                             .timestamp = ava::session::now_timestamp(),
                                             .data_json = "{\"text\":\"manual compact concurrent change\"}"}));
            }
            return std::string(
                "# Goal\nStale\n# Constraints / Preferences\nNone noted.\n# Decisions\nNone noted.\n"
                "# Files Read or Modified\nNone noted.\n# Unresolved Tasks\nNone noted.\n# Next Steps\nContinue.");
          },
          .session_mutex = &session_mutex});
  entries = session->store.load();
  expect(stale_compact && stale_compact->handled && !stale_compact->output.empty() &&
             stale_compact->output[0].find("compaction summary recorded") != std::string::npos,
         "command dispatcher /compact retries one stale snapshot and records a fresh summary");
  expect(manual_stale_generator_calls == 2, "manual /compact regenerates summary after a stale snapshot");
  expect(entries && count_compaction_entries(*entries) == compactions_before_stale + 1 &&
             std::ranges::any_of(*entries,
                                 [](ava::session::SessionEntry const& entry) {
                                   return entry.data_json.find("manual compact concurrent change") != std::string::npos;
                                 }),
         "manual /compact stale snapshot preserves concurrent changes and appends one retried compaction");

  auto exported = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/export"});
  expect(exported && exported->handled && !exported->output.empty() &&
             exported->output[0].find("# AVA Session Export") != std::string::npos &&
             exported->output[0].find("## Compaction") != std::string::npos,
         "command dispatcher /export returns markdown for loaded session entries");
  auto exported_html = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/export html"});
  expect(exported_html && exported_html->handled && !exported_html->output.empty() &&
             exported_html->output[0].find("<!doctype html>") != std::string::npos &&
             exported_html->output[0].find("<title>AVA Session Export</title>") != std::string::npos &&
             exported_html->output[0].find("# AVA Session Export") != std::string::npos,
         "command dispatcher /export html returns a self-contained HTML session export");
  auto exported_html_file = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/export session-export.html"});
  auto const exported_html_path = workspace / "session-export.html";
  std::ifstream exported_html_input(exported_html_path, std::ios::binary);
  std::ostringstream exported_html_file_text;
  exported_html_file_text << exported_html_input.rdbuf();
  expect(exported_html_file && exported_html_file->handled && !exported_html_file->output.empty() &&
             exported_html_file->output[0].find("format: html") != std::string::npos &&
             exported_html_file->tool_timeline.size() == 2 &&
             exported_html_file->tool_timeline[1].status == ava::agent::ToolTimelineStatus::Success &&
             exported_html_file->tool_timeline[1].structured_result_json.find("\"tool\":\"export\"") != std::string::npos &&
             exported_html_file_text.str().find("<!doctype html>") != std::string::npos &&
             exported_html_file_text.str().find("# AVA Session Export") != std::string::npos,
         "command dispatcher /export <file.html> writes Pi-style HTML through command-side tool metadata");
  auto exported_markdown_file =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/export markdown session-export.md"});
  auto const exported_markdown_path = workspace / "session-export.md";
  std::ifstream exported_markdown_input(exported_markdown_path, std::ios::binary);
  std::ostringstream exported_markdown_file_text;
  exported_markdown_file_text << exported_markdown_input.rdbuf();
  expect(exported_markdown_file && exported_markdown_file->handled && !exported_markdown_file->output.empty() &&
             exported_markdown_file->output[0].find("format: markdown") != std::string::npos &&
             exported_markdown_file_text.str().find("# AVA Session Export") != std::string::npos &&
             exported_markdown_file_text.str().find("<!doctype html>") == std::string::npos,
         "command dispatcher /export markdown <path> keeps explicit Markdown file export available");

  auto seeded_stats_usage = session->store.append(
      ava::session::SessionEntry{.id = "entry_slash_stats_usage",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = "2026-05-02T00:00:00Z",
                                 .data_json = "{\"text\":\"usage\",\"usage\":{\"input_tokens\":12,\"output_tokens\":7,"
                                              "\"total_tokens\":19,\"cost_usd\":0.0015}}"});
  expect(seeded_stats_usage.has_value(), "command dispatcher /stats test seeds usage metadata");
  auto stats = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/stats"});
  expect(stats && stats->handled && !stats->output.empty() &&
             stats->output[0].find("Session stats") != std::string::npos &&
             stats->output[0].find("tokens: input=12 output=7 total=19") != std::string::npos &&
             stats->output[0].find("cost: $0.001500") != std::string::npos &&
             stats->output[0].find("compactions ") != std::string::npos &&
             stats->output[0].find("path:") == std::string::npos &&
             stats->output[0].find("export: /export   resume: ava --session ") != std::string::npos,
         "command dispatcher /stats renders compact session counts, usage, cost, and hints");
  auto status = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/status"});
  expect(status && status->handled && !status->output.empty() && status->output[0] == stats->output[0],
         "command dispatcher /status aliases the backend-backed session stats surface");

  auto quit = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/quit"});
  expect(quit && quit->handled && quit->quit, "command dispatcher /quit requests shell exit");
}

void test_app_session_branch_commands()
{
  auto const root = temp_root() / "app-session-branch-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "slash branch command test opens runtime session");
  if (!session) return;

  auto const source_session_id = session->store.session_id();
  auto seed = session->store.append(ava::session::SessionEntry{.id = "entry_branch_seed",
                                                               .parent_id = "",
                                                               .type = ava::session::EntryType::UserMessage,
                                                               .timestamp = "2026-05-07T00:00:00Z",
                                                               .data_json = "{\"text\":\"seed\"}"});
  expect(seed.has_value(), "slash branch command test seeds source entry");

  auto forked = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/fork Review branch"});
  auto const fork_session_id = session->store.session_id();
  auto fork_metadata = ava::session::load_session_metadata(session->store);
  expect(forked && forked->handled && !forked->output.empty() && fork_session_id != source_session_id &&
             forked->output[0].find("forked session " + fork_session_id) != std::string::npos &&
             forked->output[0].find("from " + source_session_id) != std::string::npos &&
             forked->output[0].find("switched to " + fork_session_id) != std::string::npos && fork_metadata &&
             fork_metadata->name == "Review branch" && fork_metadata->parent_session_id == source_session_id &&
             fork_metadata->source_session_id == source_session_id && fork_metadata->branch_from_entry_id == "entry_branch_seed" &&
             fork_metadata->branch_origin == "fork" && fork_metadata->actor == "tui",
         "slash /fork creates an append-only branch, persists provenance metadata, and switches runtime session");

  auto cloned = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/clone Full copy"});
  auto const clone_session_id = session->store.session_id();
  auto clone_metadata = ava::session::load_session_metadata(session->store);
  auto clone_entries = session->store.load();
  expect(cloned && cloned->handled && !cloned->output.empty() && clone_session_id != fork_session_id &&
             cloned->output[0].find("cloned session " + clone_session_id) != std::string::npos &&
             cloned->output[0].find("from " + fork_session_id) != std::string::npos &&
             cloned->output[0].find("switched to " + clone_session_id) != std::string::npos && clone_metadata &&
             clone_metadata->name == "Full copy" && clone_metadata->parent_session_id == fork_session_id &&
             clone_metadata->source_session_id == fork_session_id && clone_metadata->branch_origin == "clone" &&
             clone_metadata->actor == "tui" && clone_entries && clone_entries->size() >= 3,
         "slash /clone copies the full current branch, persists clone provenance, and switches runtime session");

  auto sessions = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/tree Full copy"});
  expect(sessions && sessions->handled && !sessions->output.empty() &&
             sessions->output[0].find("Full copy") != std::string::npos &&
             sessions->output[0].find("origin=clone") != std::string::npos,
         "slash /tree alias exposes newly cloned branch in the session tree");
}

void test_app_session_new_resume_commands()
{
  auto const root = temp_root() / "app-session-new-resume-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "slash new/resume command test opens runtime session");
  if (!session) return;

  auto const source_session_id = session->store.session_id();
  auto missing_resume = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/resume"});
  expect(missing_resume && missing_resume->handled && !missing_resume->output.empty() &&
             missing_resume->output[0] == "usage: /resume <id>",
         "slash /resume without an id returns usage text");

  auto fresh = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/new Fresh session"});
  auto const fresh_session_id = session->store.session_id();
  auto fresh_metadata = ava::session::load_session_metadata(session->store);
  auto fresh_entries = session->store.load();
  expect(fresh && fresh->handled && !fresh->output.empty() && fresh_session_id != source_session_id &&
             fresh->output[0].find("started session " + fresh_session_id) != std::string::npos &&
             fresh->output[0].find("name=\"Fresh session\"") != std::string::npos &&
             fresh->output[0].find("previous session " + source_session_id) != std::string::npos &&
             fresh->output[0].find("switched to " + fresh_session_id) != std::string::npos && fresh_metadata &&
             fresh_metadata->name == "Fresh session" && fresh_metadata->actor == "tui" && fresh_entries &&
             fresh_entries->size() >= 2,
         "slash /new creates a fresh named session, records metadata, and switches runtime session");

  auto resumed = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/resume " + source_session_id});
  expect(resumed && resumed->handled && !resumed->output.empty() && session->store.session_id() == source_session_id &&
             resumed->output[0].find("resumed session " + source_session_id) != std::string::npos,
         "slash /resume switches runtime session by id");

  auto sessions = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions Fresh"});
  expect(sessions && sessions->handled && !sessions->output.empty() &&
             sessions->output[0].find("Fresh session") != std::string::npos &&
             sessions->output[0].find(fresh_session_id) != std::string::npos,
         "slash /sessions shows named sessions created through /new");
}

void test_app_session_metadata_commands()
{
  auto const root = temp_root() / "app-session-metadata-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "slash metadata command test opens runtime session");
  if (!session) return;

  auto missing_name = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/name"});
  expect(missing_name && missing_name->handled && !missing_name->output.empty() &&
             missing_name->output[0] == "usage: /name <name|--clear>",
         "slash /name without a name returns usage text");

  auto no_labels = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/labels"});
  expect(no_labels && no_labels->handled && !no_labels->output.empty() &&
             no_labels->output[0].find("session labels: <none>") != std::string::npos,
         "slash /labels without arguments reports current labels and usage");

  auto renamed = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/rename Auth follow-up"});
  auto metadata_after_name = ava::session::load_session_metadata(session->store);
  expect(renamed && renamed->handled && !renamed->output.empty() &&
             renamed->output[0].find("session name set: \"Auth follow-up\"") != std::string::npos &&
             metadata_after_name && metadata_after_name->name == "Auth follow-up" && metadata_after_name->actor == "tui",
         "slash /rename alias appends current-session name metadata");

  auto labeled = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/label auth bug"});
  auto metadata_after_labels = ava::session::load_session_metadata(session->store);
  expect(labeled && labeled->handled && !labeled->output.empty() &&
             labeled->output[0].find("session labels set: auth,bug") != std::string::npos &&
             metadata_after_labels && metadata_after_labels->name == "Auth follow-up" &&
             metadata_after_labels->labels.size() == 2 && metadata_after_labels->labels[0] == "auth" &&
             metadata_after_labels->labels[1] == "bug" && metadata_after_labels->actor == "tui",
         "slash /label alias appends current-session label metadata without losing the session name");

  auto const slash_items = ava::app::command_catalog_slash_items(*session);
  auto find_slash_item = [&slash_items](std::string_view command) -> ava::tui::SlashCommandItem const* {
    for (auto const& item : slash_items) {
      if (item.command == command) return &item;
    }
    return nullptr;
  };
  auto has_session_completion = [](ava::tui::SlashCommandItem const* item, std::size_t argument_index,
                                   std::string_view value, std::string_view description_fragment,
                                   std::vector<std::string> previous_args = {}) {
    return item != nullptr && std::ranges::any_of(item->argument_completions, [&](auto const& completion) {
             return completion.argument_index == argument_index && completion.value == value &&
                    completion.required_previous_args == previous_args &&
                    completion.description.find(description_fragment) != std::string::npos;
           });
  };
  expect(has_session_completion(find_slash_item("/resume"), 0, session->store.session_id(), "Auth follow-up") &&
             has_session_completion(find_slash_item("/resume"), 0, session->store.session_id(), "labels=auth, bug") &&
             has_session_completion(find_slash_item("/sessions"), 0, session->store.session_id(), "Auth follow-up") &&
             has_session_completion(find_slash_item("/sessions"), 0, "rename", "Rename a session") &&
             has_session_completion(find_slash_item("/sessions"), 0, "labels", "Set or clear labels") &&
             has_session_completion(find_slash_item("/sessions"), 0, "archive", "Archive a session") &&
             has_session_completion(find_slash_item("/sessions"), 0, "unarchive", "Restore an archived session") &&
             has_session_completion(find_slash_item("/sessions"), 1, session->store.session_id(), "Auth follow-up",
                                    {"rename"}) &&
             has_session_completion(find_slash_item("/sessions"), 1, session->store.session_id(), "Auth follow-up",
                                    {"labels"}) &&
             has_session_completion(find_slash_item("/sessions"), 2, "--clear", "Clear labels",
                                    {"labels", session->store.session_id()}) &&
             has_session_completion(find_slash_item("/sessions"), 1, session->store.session_id(), "Auth follow-up",
                                    {"archive"}) &&
             has_session_completion(find_slash_item("/sessions"), 2, "--confirm", "Confirm archive",
                                    {"archive", session->store.session_id()}),
         "slash palette session completions expose session archive, rename, resume, and tree workflows");

  auto duplicate_labels = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/labels auth auth"});
  expect(!duplicate_labels && duplicate_labels.error().message() == "session labels must be unique",
         "slash /labels reuses backend metadata validation for duplicate labels");

  auto tree = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/tree auth"});
  expect(tree && tree->handled && !tree->output.empty() &&
             tree->output[0].find("Auth follow-up") != std::string::npos &&
             tree->output[0].find("labels=auth,bug") != std::string::npos,
         "slash /tree exposes names and labels written through slash metadata commands");

  auto const active_session_id_before_selected_rename = session->store.session_id();
  auto renamed_selected = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/sessions rename " + active_session_id_before_selected_rename + " Selector name"});
  auto metadata_after_selected_rename = ava::session::load_session_metadata(session->store);
  expect(renamed_selected && renamed_selected->handled && !renamed_selected->output.empty() &&
             renamed_selected->output[0].find("session " + active_session_id_before_selected_rename + " name set: \"Selector name\"") !=
                 std::string::npos &&
             metadata_after_selected_rename && metadata_after_selected_rename->name == "Selector name" &&
             session->store.session_id() == active_session_id_before_selected_rename,
         "slash /sessions rename appends name metadata to a selected session without switching runtime sessions");

  auto cleared_labels = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/labels --clear"});
  auto metadata_after_clear_labels = ava::session::load_session_metadata(session->store);
  expect(cleared_labels && cleared_labels->handled && !cleared_labels->output.empty() &&
             cleared_labels->output[0] == "session labels cleared" && metadata_after_clear_labels &&
             metadata_after_clear_labels->labels.empty(),
         "slash /labels --clear appends empty label metadata");

  auto cleared_name = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/name --clear"});
  auto metadata_after_clear_name = ava::session::load_session_metadata(session->store);
  expect(cleared_name && cleared_name->handled && !cleared_name->output.empty() &&
             cleared_name->output[0] == "session name cleared" && metadata_after_clear_name &&
             metadata_after_clear_name->name.empty(),
         "slash /name --clear appends empty name metadata");
}

void test_app_runtime_model_switch_persists_and_reopens()
{
  auto const root = temp_root() / "app-runtime-model-switch";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.models_file, std::ios::binary | std::ios::trunc);
    file << R"JSON({
      "default_provider":"openai",
      "default_model":"gpt-5.5",
      "scoped_model_cycle":["anthropic/claude-test","openai/gpt-5.5"],
      "models":[{
        "provider":"anthropic",
        "id":"claude-test",
        "name":"Claude Test",
        "family":"claude-test",
        "api_family":"anthropic_messages",
        "context_window_tokens":999,
        "max_output_tokens":123,
        "supports_tools":false,
        "supports_streaming":true,
        "supports_reasoning":false,
        "reports_usage":true,
        "input_modalities":["text"],
        "output_modalities":["text"],
        "compatibility_quirks":["test_quirk"]
      }]
    })JSON";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime model switch test opens runtime session");
  if (!session) return;
  expect(session->scoped_model_cycle && session->scoped_model_cycle->size() == 2 &&
             (*session->scoped_model_cycle)[0] == "anthropic/claude-test" &&
             (*session->scoped_model_cycle)[1] == "openai/gpt-5.5",
         "runtime session restores persisted scoped model cycle");
  auto const session_id = session->store.session_id();

  auto model = ava::app::resolve_runtime_model(paths, "anthropic", "claude-test");
  expect(model.has_value(), "runtime resolves configured Anthropic model");
  if (!model) return;
  auto switched = ava::app::switch_runtime_model(*session, *model);
  expect(switched.has_value() && *switched, "runtime model switch reports a change");
  expect(session->model.provider_id == "anthropic" && session->model.model_id == "claude-test",
         "runtime model switch updates active session model");

  auto entries = session->store.load();
  expect(entries.has_value(), "runtime model switch loads session entries");
  bool saw_model_change = false;
  if (entries) {
    for (auto const& entry : *entries) {
      saw_model_change =
          saw_model_change || (entry.type == ava::session::EntryType::ModelChange &&
                               entry.data_json.find("\"previous_provider\":\"openai\"") != std::string::npos &&
                               entry.data_json.find("\"provider\":\"anthropic\"") != std::string::npos);
    }
  }
  expect(saw_model_change, "runtime model switch appends model_change entry");

  auto appended_escaped_model_change = session->store.append(ava::session::SessionEntry{
      .id = ava::core::make_id("entry"),
      .parent_id = "",
      .type = ava::session::EntryType::ModelChange,
      .timestamp = ava::session::now_timestamp(),
      .data_json =
          R"JSON({"previous_provider":"anthropic","previous_model":"claude-test","provider":"anthropic","model":"claude-test","display_name":"Claude Test","family":"claude-test","api_family":"anthropic_messages","input_modalities":["text"],"output_modalities":["text"],"reasoning_levels":[],"compatibility_quirks":["test_quirk","\uD83D\uDE00"],"context_window_tokens":999,"max_output_tokens":123,"supports_tools":false,"supports_streaming":true,"supports_reasoning":false,"reports_usage":true})JSON"});
  expect(appended_escaped_model_change.has_value(), "runtime model switch test seeds escaped unicode metadata");

  ava::app::RuntimeOpenOptions reopen_options = open_options;
  reopen_options.requested_session_id = session_id;
  std::filesystem::remove(paths.models_file, remove_error);
  auto reopened = ava::app::open_runtime_session(reopen_options);
  expect(reopened.has_value(), "runtime model switch reopens persisted session");
  expect(reopened && reopened->model.provider_id == "anthropic" && reopened->model.model_id == "claude-test",
         "runtime reopen restores latest persisted model_change");
  bool restored_emoji_quirk = false;
  if (reopened) {
    auto const emoji_quirk = std::string("\xF0\x9F\x98\x80");
    restored_emoji_quirk = std::ranges::find(reopened->model.compatibility_quirks, emoji_quirk) !=
                           reopened->model.compatibility_quirks.end();
  }
  expect(restored_emoji_quirk, "runtime reopen decodes escaped supplementary-plane metadata");
  if (reopened) {
    ava::provider::OpenAIProvider const provider("https://api.example.test");
    ava::tests::FakeTransport transport({});
    std::istringstream in("{\"id\":\"list\",\"type\":\"list_models\"}\n");
    std::ostringstream out;
    auto result =
        ava::app::run_rpc_loop(*reopened, reopen_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
    auto const jsonl = out.str();
    auto const restored_position = jsonl.find("\"model\":\"claude-test\"");
    expect(result.has_value() && restored_position != std::string::npos,
           "RPC list_models includes restored removed current model");
    expect(restored_position != std::string::npos &&
               jsonl.find("\"selectable\":false", restored_position) != std::string::npos,
           "RPC list_models marks restored removed current model as not selectable");
    expect(restored_position != std::string::npos &&
               jsonl.find("\"context_window_tokens\":999", restored_position) != std::string::npos &&
               jsonl.find("\"max_output_tokens\":123", restored_position) != std::string::npos &&
               jsonl.find("\"supports_streaming\":true", restored_position) != std::string::npos &&
               jsonl.find("\"supports_tools\":false", restored_position) != std::string::npos &&
               jsonl.find("\"reports_usage\":true", restored_position) != std::string::npos &&
               jsonl.find("test_quirk", restored_position) != std::string::npos,
           "RPC list_models preserves capability metadata for restored removed models");
  }
}

void test_app_runtime_model_switch_rejects_incompatible_history()
{
  auto const root = temp_root() / "app-runtime-model-switch-compatibility";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.models_file, std::ios::binary | std::ios::trunc);
    file << R"JSON({
      "default_provider":"openai",
      "default_model":"gpt-5.5",
      "models":[{
        "provider":"openai",
        "id":"no-tools",
        "name":"No Tools",
        "family":"test",
        "api_family":"openai_responses",
        "supports_streaming":true,
        "input_modalities":["text"],
        "output_modalities":["text"]
      },{
        "provider":"anthropic",
        "id":"claude-replay",
        "name":"Claude Replay",
        "family":"claude-test",
        "api_family":"anthropic_messages",
        "supports_tools":true,
        "supports_streaming":true,
        "input_modalities":["text"],
        "output_modalities":["text"]
      },{
        "provider":"anthropic",
        "id":"claude-image",
        "name":"Claude Image",
        "family":"claude-test",
        "api_family":"anthropic_messages",
        "supports_tools":true,
        "supports_streaming":true,
        "input_modalities":["text","image"],
        "output_modalities":["text"]
      }]
    })JSON";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime model switch compatibility test opens runtime session");
  if (!session) return;

  auto appended_tool_call = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                             .parent_id = "",
                                                                             .type = ava::session::EntryType::ToolCall,
                                                                             .timestamp = ava::session::now_timestamp(),
                                                                             .data_json = "{\"call_id\":\"call_1\","
                                                                                          "\"name\":\"read_file\","
                                                                                          "\"arguments\":{}}"});
  auto appended_tool_result =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::ToolResult,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"call_id\":\"call_1\",\"content\":\"ok\"}"});
  expect(appended_tool_call.has_value() && appended_tool_result.has_value(),
         "model switch compatibility test seeds tool history");

  auto no_tools_model = ava::app::resolve_runtime_model(paths, "openai", "no-tools");
  expect(no_tools_model.has_value(), "runtime resolves no-tools model");
  if (!no_tools_model) return;
  auto rejected_tools = ava::app::switch_runtime_model(*session, *no_tools_model);
  expect(!rejected_tools.has_value(), "runtime rejects switch to model without tool support after tool history");
  expect(!rejected_tools && rejected_tools.error().format().find("tool support") != std::string::npos,
         "runtime tool-history switch error explains missing tool support");
  expect(session->model.provider_id == "openai" && session->model.model_id == "gpt-5.5",
         "rejected tool-history switch leaves active model unchanged");

  auto appended_reasoning =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::ReasoningBlock,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"provider\":\"anthropic\","
                                                                    "\"model\":\"claude-sonnet-4-5\","
                                                                    "\"format\":\"anthropic_thinking\","
                                                                    "\"text\":\"visible reasoning\","
                                                                    "\"signature\":\"sig-1\"}"});
  expect(appended_reasoning.has_value(), "model switch compatibility test seeds reasoning history");

  auto anthropic_replay = ava::app::resolve_runtime_model(paths, "anthropic", "claude-replay");
  expect(anthropic_replay.has_value(), "runtime resolves Anthropic replay model");
  if (!anthropic_replay) return;
  auto switched_anthropic = ava::app::switch_runtime_model(*session, *anthropic_replay);
  expect(switched_anthropic.has_value() && *switched_anthropic,
         "runtime allows switch to Anthropic model that can replay Anthropic reasoning");
  expect(session->model.provider_id == "anthropic" && session->model.model_id == "claude-replay",
         "compatible reasoning switch updates active model");

  auto kimi_model = ava::app::resolve_runtime_model(paths, "kimi", "kimi-k2-thinking");
  expect(kimi_model.has_value(), "runtime resolves Kimi model");
  if (!kimi_model) return;
  auto rejected_reasoning = ava::app::switch_runtime_model(*session, *kimi_model);
  expect(!rejected_reasoning.has_value(), "runtime rejects incompatible reasoning provider switch");
  expect(!rejected_reasoning && rejected_reasoning.error().format().find("anthropic_thinking") != std::string::npos,
         "runtime reasoning switch error includes incompatible reasoning format");
  expect(session->model.provider_id == "anthropic" && session->model.model_id == "claude-replay",
         "rejected reasoning switch leaves active model unchanged");

  auto appended_compaction =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::Compaction,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"summary\":\"old history\"}"});
  expect(appended_compaction.has_value(), "model switch compatibility test seeds compaction boundary");
  auto switched_no_tools_after_compaction = ava::app::switch_runtime_model(*session, *no_tools_model);
  expect(switched_no_tools_after_compaction.has_value() && *switched_no_tools_after_compaction,
         "runtime ignores pre-compaction native history for switch compatibility");
  expect(session->model.provider_id == "openai" && session->model.model_id == "no-tools",
          "post-compaction switch updates active model");

  auto appended_large_image = session->store.append(ava::session::SessionEntry{
      .id = ava::core::make_id("entry"),
      .parent_id = "",
      .type = ava::session::EntryType::UserMessage,
      .timestamp = ava::session::now_timestamp(),
      .data_json = R"({"text":"large image","attachments":[{"id":"img_big","type":"image","mime_type":"image/png","byte_size":6291456,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","storage_path":"attachments/img_big.png"}]})"});
  expect(appended_large_image.has_value(), "model switch compatibility test seeds large image history");
  auto anthropic_image = ava::app::resolve_runtime_model(paths, "anthropic", "claude-image");
  expect(anthropic_image.has_value(), "runtime resolves Anthropic image model");
  if (!anthropic_image) return;
  auto rejected_large_image = ava::app::switch_runtime_model(*session, *anthropic_image);
  expect(!rejected_large_image.has_value() && rejected_large_image.error().format().find("byte-size") != std::string::npos,
         "runtime rejects model switches that exceed provider-specific image limits");
  expect(session->model.provider_id == "openai" && session->model.model_id == "no-tools",
         "rejected image-limit switch leaves active model unchanged");
  auto appended_post_image_compaction =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::Compaction,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"summary\":\"image history compacted\"}"});
  expect(appended_post_image_compaction.has_value(), "model switch compatibility test clears image history with compaction");

  auto appended_kimi_reasoning =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::ReasoningBlock,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"provider\":\"kimi\","
                                                                    "\"model\":\"kimi-k2-thinking\","
                                                                    "\"format\":\"reasoning_content\","
                                                                    "\"text\":\"compatible kimi reasoning\"}"});
  expect(appended_kimi_reasoning.has_value(), "model switch compatibility test seeds Kimi reasoning history");

  auto switched_kimi = ava::app::switch_runtime_model(*session, *kimi_model);
  expect(switched_kimi.has_value() && *switched_kimi,
         "runtime allows switch to Kimi model with explicit reasoning preservation support");
  expect(session->model.provider_id == "kimi" && session->model.model_id == "kimi-k2-thinking",
         "Kimi reasoning-compatible switch updates active model");

  auto moonshot_model = ava::app::resolve_runtime_model(paths, "moonshot", "kimi-k2.6");
  expect(moonshot_model.has_value(), "runtime resolves Moonshot model");
  if (!moonshot_model) return;
  auto rejected_moonshot = ava::app::switch_runtime_model(*session, *moonshot_model);
  expect(!rejected_moonshot.has_value(), "runtime rejects reasoning_content switch without preservation quirk");
  expect(session->model.provider_id == "kimi" && session->model.model_id == "kimi-k2-thinking",
         "rejected Moonshot reasoning switch leaves active model unchanged");

  auto entries = session->store.load();
  expect(entries.has_value(), "model switch compatibility test reloads entries");
  if (entries) {
    auto const model_changes = std::ranges::count_if(
        *entries, [](auto const& entry) { return entry.type == ava::session::EntryType::ModelChange; });
    expect(model_changes == 3, "rejected model switches do not append model_change entries");
  }
}

void test_app_runtime_reasoning_selection_persists_and_requests()
{
  auto const root = temp_root() / "app-runtime-reasoning-selection";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.models_file, std::ios::binary | std::ios::trunc);
    file << R"JSON({
      "default_provider":"openai",
      "default_model":"gpt-5.5",
      "models":[{
        "provider":"openai",
        "id":"no-reasoning-levels",
        "name":"No Reasoning Levels",
        "family":"test",
        "api_family":"openai_responses",
        "supports_tools":true,
        "supports_streaming":true,
        "supports_reasoning":true,
        "input_modalities":["text"],
        "output_modalities":["text"]
      },{
        "provider":"anthropic",
        "id":"claude-default-max",
        "name":"Claude Default Max",
        "family":"claude",
        "api_family":"anthropic_messages",
        "supports_tools":true,
        "supports_streaming":true,
        "supports_reasoning":true,
        "reasoning_levels":["enabled"],
        "input_modalities":["text"],
        "output_modalities":["text"]
      },{
        "provider":"anthropic-proxy",
        "id":"claude-proxy",
        "name":"Claude Proxy",
        "family":"claude",
        "api_family":"anthropic_messages",
        "max_output_tokens":8192,
        "supports_tools":true,
        "supports_streaming":true,
        "supports_reasoning":true,
        "reasoning_levels":["enabled"],
        "input_modalities":["text"],
        "output_modalities":["text"]
      }]
    })JSON";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime reasoning test opens runtime session");
  if (!session) return;
  auto const session_id = session->store.session_id();

  auto selected = ava::app::set_runtime_reasoning(
      *session, ava::app::RuntimeReasoningSelection{.level = " low ", .budget_tokens = std::nullopt, .display = ""});
  expect(selected.has_value() && *selected && session->reasoning && session->reasoning->level == "low",
         "runtime reasoning selection validates, normalizes, and updates state");

  auto duplicate = ava::app::set_runtime_reasoning(
      *session, ava::app::RuntimeReasoningSelection{.level = "low", .budget_tokens = std::nullopt, .display = ""});
  expect(duplicate.has_value() && !*duplicate, "runtime reasoning selection is idempotent when unchanged");

  auto invalid = ava::app::set_runtime_reasoning(
      *session, ava::app::RuntimeReasoningSelection{.level = "ultra", .budget_tokens = std::nullopt, .display = ""});
  expect(!invalid.has_value(), "runtime reasoning selection rejects unsupported model levels");

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"reasoned answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions run_options;
  run_options.access_token = "token";
  auto result = ava::app::run_prompt(*session, "use reasoning", provider, transport, run_options);
  expect(result && result->final_text == "reasoned answer", "runtime reasoning prompt completes");
  expect(transport.requests().size() == 1, "runtime reasoning test sends one provider request");
  if (!transport.requests().empty()) {
    expect(transport.requests()[0].body.find("\"reasoning\"") != std::string::npos &&
               transport.requests()[0].body.find("\"effort\":\"low\"") != std::string::npos &&
               transport.requests()[0].body.find("\"summary\":\"auto\"") != std::string::npos,
           "runtime reasoning selection is sent to the provider request with visible summary request");
  }

  auto entries = session->store.load();
  expect(entries.has_value(), "runtime reasoning test reloads session entries");
  if (entries) {
    auto const reasoning_changes = std::ranges::count_if(
        *entries, [](auto const& entry) { return entry.type == ava::session::EntryType::ReasoningChange; });
    expect(reasoning_changes == 1, "runtime reasoning selection appends one durable reasoning_change entry");
  }

  ava::app::RuntimeOpenOptions reopen_options = open_options;
  reopen_options.requested_session_id = session_id;
  auto reopened = ava::app::open_runtime_session(reopen_options);
  expect(reopened.has_value() && reopened->reasoning && reopened->reasoning->level == "low",
         "runtime reopen restores latest reasoning selection");

  auto cleared = ava::app::set_runtime_reasoning(*session, std::nullopt);
  expect(cleared.has_value() && *cleared && !session->reasoning, "runtime reasoning selection can be cleared");

  auto reselected = ava::app::set_runtime_reasoning(
      *session, ava::app::RuntimeReasoningSelection{.level = "low", .budget_tokens = std::nullopt, .display = ""});
  expect(reselected.has_value() && *reselected, "runtime reasoning test re-enables reasoning before switch boundary");
  auto kimi_model = ava::app::resolve_runtime_model(paths, "kimi", "kimi-k2-thinking");
  auto openai_model = ava::app::resolve_runtime_model(paths, "openai", "gpt-5.5");
  expect(kimi_model.has_value() && openai_model.has_value(), "runtime reasoning test resolves switch boundary models");
  if (kimi_model && openai_model) {
    auto switched_away = ava::app::switch_runtime_model(*session, *kimi_model);
    expect(switched_away.has_value() && *switched_away, "runtime reasoning test switches to Kimi model");
    auto kimi_budget = ava::app::set_runtime_reasoning(
        *session,
        ava::app::RuntimeReasoningSelection{.level = "enabled", .budget_tokens = 1024, .display = "summarized"});
    expect(!kimi_budget.has_value() &&
               kimi_budget.error().format().find("Kimi reasoning supports level only") != std::string::npos,
           "runtime reasoning selection rejects unsupported OpenAI-compatible budget/display controls");
    auto switched_back = ava::app::switch_runtime_model(*session, *openai_model);
    expect(switched_back.has_value() && *switched_back && !session->reasoning,
           "runtime model switches clear active reasoning selection");
    auto reopened_after_switch = ava::app::open_runtime_session(reopen_options);
    expect(reopened_after_switch.has_value() && !reopened_after_switch->reasoning,
           "runtime reopen does not resurrect reasoning across model_change boundaries");
  }

  auto no_levels_model = ava::app::resolve_runtime_model(paths, "openai", "no-reasoning-levels");
  expect(no_levels_model.has_value(), "runtime reasoning test resolves no-level custom model");
  if (no_levels_model) {
    auto switched = ava::app::switch_runtime_model(*session, *no_levels_model);
    expect(switched.has_value() && *switched, "runtime reasoning test switches to no-level custom model");
    auto no_level_selection = ava::app::set_runtime_reasoning(
        *session, ava::app::RuntimeReasoningSelection{.level = "low", .budget_tokens = std::nullopt, .display = ""});
    expect(!no_level_selection.has_value() &&
               no_level_selection.error().format().find("supported reasoning levels") != std::string::npos,
           "runtime reasoning selection rejects models without declared reasoning levels");
  }

  auto anthropic_default_max = ava::app::resolve_runtime_model(paths, "anthropic", "claude-default-max");
  expect(anthropic_default_max.has_value(), "runtime reasoning test resolves Anthropic default max model");
  if (anthropic_default_max) {
    auto switched = ava::app::switch_runtime_model(*session, *anthropic_default_max);
    expect(switched.has_value() && *switched, "runtime reasoning test switches to Anthropic default max model");
    auto over_budget = ava::app::set_runtime_reasoning(
        *session,
        ava::app::RuntimeReasoningSelection{.level = "enabled", .budget_tokens = 4096, .display = "summarized"});
    expect(!over_budget.has_value() && over_budget.error().format().find(
                                           "reasoning budget must be below max output tokens") != std::string::npos,
           "runtime reasoning selection validates Anthropic budget against provider default max tokens");
  }

  auto proxy_registry = ava::config::load_model_registry(paths);
  expect(proxy_registry.has_value(), "runtime reasoning test loads registry for custom Anthropic-compatible model");
  auto anthropic_proxy = proxy_registry ? ava::config::find_model(*proxy_registry, "anthropic-proxy", "claude-proxy")
                                        : std::optional<ava::config::ModelInfo>{};
  expect(anthropic_proxy.has_value(), "runtime reasoning test finds custom Anthropic-compatible model");
  if (anthropic_proxy) {
    session->model = *anthropic_proxy;
    session->reasoning.reset();
    auto cycled = ava::app::cycle_runtime_reasoning(*session);
    expect(cycled.has_value() && session->reasoning && session->reasoning->level == "enabled" &&
               session->reasoning->budget_tokens && *session->reasoning->budget_tokens == 4096,
           "runtime reasoning cycling uses API-family fallback profile for custom Anthropic-compatible models");
    auto missing_budget = ava::app::set_runtime_reasoning(
        *session,
        ava::app::RuntimeReasoningSelection{.level = "enabled", .budget_tokens = std::nullopt, .display = ""});
    expect(!missing_budget.has_value() &&
               missing_budget.error().format().find("Anthropic-proxy enabled reasoning requires budget_tokens") !=
                   std::string::npos,
           "runtime reasoning validation labels missing-budget errors with the custom provider id");
    auto too_large_budget = ava::app::set_runtime_reasoning(
        *session, ava::app::RuntimeReasoningSelection{.level = "enabled", .budget_tokens = 8192, .display = ""});
    expect(!too_large_budget.has_value() &&
               too_large_budget.error().format().find("reasoning budget must be below max output tokens") !=
                   std::string::npos,
           "runtime reasoning validation applies fallback budget limits to custom providers");
  }
}

}  // namespace

void run_app_command_classification_tests()
{
  test_command_classification();
}

void run_app_event_serialization_tests()
{
  test_app_event_serialization();
}

void run_app_runtime_tests()
{
  test_app_runtime_open_session_and_context_prompt();
  test_app_runtime_no_session_mode();
  test_app_runtime_session_startup_options();
  test_app_runtime_cli_prompt_overrides();
  test_app_run_prompt_emits_events();
  test_app_run_prompt_expands_file_references();
  test_app_run_prompt_sends_imported_image_attachment();
  test_app_clipboard_image_file_override_imports_attachment();
  test_app_run_prompt_emits_provider_retry_events_when_enabled();
  test_app_run_prompt_emits_tool_progress_and_session_spill();
  test_app_first_run_auth_onboarding();
  test_app_run_prompt_event_sink_failure_cancels_before_next_provider_call();
  test_app_command_dispatcher();
  test_app_session_branch_commands();
  test_app_session_new_resume_commands();
  test_app_session_metadata_commands();
  test_app_runtime_model_switch_persists_and_reopens();
  test_app_runtime_model_switch_rejects_incompatible_history();
  test_app_runtime_reasoning_selection_persists_and_requests();
}
