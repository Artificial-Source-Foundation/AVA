#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/app/command_catalog.h"
#include "ava/app/command_palette.h"
#include "ava/app/commands.h"
#include "ava/app/connect_openai.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/print_mode.h"
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
#include "ava/tui/terminal.h"
#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"
#include "ava/session/stats.h"
#include "ava/permissions/permission.h"
#include "ava/provider/openai_provider.h"
#include "ava/context/context_loader.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

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
  expect(ava::permissions::classify_command("cmake --build build").action == ava::permissions::PermissionAction::Allow,
         "cmake build is allowed for non-TTY line shell verification");
  expect(ava::permissions::classify_command("ctest --test-dir build").action == ava::permissions::PermissionAction::Allow,
         "ctest is allowed for non-TTY line shell verification");
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
  expect(ava::permissions::classify_command("cmake -E copy docs/plan.md src/new.cpp").action == ava::permissions::PermissionAction::Deny,
         "cmake -E copy mutation is denied");
  expect(ava::permissions::classify_command("python3 scripts/run.py").action == ava::permissions::PermissionAction::Deny, "interpreters are denied");
  expect(ava::permissions::classify_command("bash -lc ls").action == ava::permissions::PermissionAction::Deny, "shell interpreters remain denied");
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
  expect(message_jsonl.ends_with('\n') && message_jsonl.substr(0, message_jsonl.size() - 1).find('\n') == std::string::npos,
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
  if (!session)
    return;

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
  if (reopened)
  {
    auto reopened_entries = reopened->store.load();
    expect(reopened_entries && reopened_entries->size() == 1, "runtime reopened session does not append another session_start");
  }
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
  if (!session)
    return;

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
  expect(events.size() == 4 && events[0].type == ava::app::RuntimeEventType::SessionStart && events[1].type == ava::app::RuntimeEventType::UserMessage &&
             events[2].type == ava::app::RuntimeEventType::AssistantMessage && events[3].type == ava::app::RuntimeEventType::Done,
         "runtime run_prompt emits session, user, assistant, and done events");
  expect(events.size() == 4 && events[2].text == "runtime answer" && events[3].provider_iterations == 1,
         "runtime run_prompt events include final text and completion counters");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("runtime run context") != std::string::npos,
         "runtime run_prompt sends context-augmented system prompt to provider");
  auto entries = session->store.load();
  expect(entries && entries->size() == 3 && (*entries)[1].type == ava::session::EntryType::UserMessage &&
             (*entries)[2].type == ava::session::EntryType::AssistantMessage,
         "runtime run_prompt persists user and assistant entries in the runtime session");
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 429, .headers = {{"Retry-After", "0"}}, .body = "{\"error\":{\"message\":\"rate limited\"}}"},
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
                               return event.type == ava::app::RuntimeEventType::Retry && event.trigger == "provider_transport" &&
                                      event.reason == "rate_limited" && event.attempt == 2 && event.max_attempts == 3 && event.delay_ms == 0 &&
                                      event.text == "HTTP status 429";
                             }),
         "runtime run_prompt emits provider retry metadata through the shared event sink");
  events.clear();
  auto retry_options = ava::app::runtime::runtime_retry_options(*session, run_options);
  expect(retry_options.on_retry != nullptr, "runtime retry options expose provider retry event mapping");
  runtime_retry_cancel = true;
  expect(retry_options.cancel_requested && retry_options.cancel_requested(), "runtime retry options preserve the active run cancellation callback");
  runtime_retry_cancel = false;
  if (retry_options.on_retry)
  {
    auto emitted_tick = retry_options.on_retry(ava::provider::RetryOptions::Event{.attempt = 2,
                                                                                  .max_attempts = 3,
                                                                                  .delay_ms = 1000,
                                                                                  .remaining_ms = 500,
                                                                                  .reason = "rate_limited",
                                                                                  .status_code = 429,
                                                                                  .streaming = true,
                                                                                  .countdown_tick = true});
    expect(emitted_tick.has_value() && events.size() == 1 && events[0].type == ava::app::RuntimeEventType::RetryTick &&
               events[0].trigger == "provider_transport" && events[0].remaining_ms == 500 && events[0].delay_ms == 1000 && events[0].status == "streaming",
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
  if (!session)
    return;

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
  for (std::filesystem::directory_iterator it(spill_dir, iter_error), end; !iter_error && it != end; it.increment(iter_error))
  {
    has_spill_file = true;
    expect(it->path().parent_path() == spill_dir, "runtime spill file stays under the session-local spill directory");
    break;
  }
  expect(result && result->final_text == "tool done" &&
             std::ranges::any_of(events,
                                 [](ava::app::RuntimeEvent const& event) {
                                   return event.type == ava::app::RuntimeEventType::ToolProgress && event.call_id == "call_bash" && event.tool_name == "bash" &&
                                          !event.text.empty();
                                 }),
         "runtime run_prompt emits additive tool_progress events from tool callbacks");
  expect(std::ranges::any_of(events,
                             [](ava::app::RuntimeEvent const& event) {
                               return event.type == ava::app::RuntimeEventType::ToolStart && event.call_id == "call_bash" && event.tool_name == "bash" &&
                                      event.tool_arguments_json.find("\"command\":\"pwd\"") != std::string::npos;
                             }) &&
             std::ranges::any_of(events,
                                 [](ava::app::RuntimeEvent const& event) {
                                   return event.type == ava::app::RuntimeEventType::ToolResult && event.call_id == "call_bash" && event.tool_name == "bash" &&
                                          event.truncated && event.total_bytes > 0 && event.output_lines > 0 && event.total_lines > 0 &&
                                          !event.spill_path.empty() && event.tool_result_json.find("\"spill_file\"") != std::string::npos;
                                 }),
         "runtime run_prompt emits semantic tool args, result, and spill metadata for frontend adapters");
  expect(has_spill_file, "runtime run_prompt configures session-local spill files for truncated tool output");
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
  if (!session)
    return;

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
    if (event.type == ava::app::RuntimeEventType::ToolStart)
    {
      return ava::core::VoidResult{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "event sink failed"))};
    }
    return ava::core::VoidResult{};
  };

  auto result = ava::app::run_prompt(*session, "read with failing sink", provider, transport, run_options);
  expect(!result && result.error().category() == ava::core::ErrorCategory::Io && result.error().message() == "event sink failed",
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
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "dispatcher context\n";
  }
  {
    std::ofstream file(workspace / "src" / "main.cpp", std::ios::binary | std::ios::trunc);
    file << "int main() { return 0; }\n";
  }
  write_app_test_file(paths.ava_config_dir / "plugins" / "com.example.global" / "plugin.json",
                      app_test_plugin_manifest_json("com.example.global", "Global Plugin"));
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.project" / "plugin.json",
                      app_test_plugin_manifest_json("com.example.project", "Project Plugin"));
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.bad" / "plugin.json", "{not-json");
  write_app_test_file(workspace / ".ava" / "mcp.json", app_test_mcp_config_json("fs", "Filesystem Server", AVA_FAKE_MCP_SERVER_PATH));

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "command dispatcher test opens runtime session");
  if (!session)
    return;
  auto const plan_system_prompt = session->system_prompt;

  expect(ava::app::is_backend_command("/model") && ava::app::is_backend_command("/models") && ava::app::is_backend_command("/hotkeys") &&
             ava::app::is_backend_command("/details") && ava::app::is_backend_command("/thinking") && ava::app::is_backend_command("/status") &&
             ava::app::is_backend_command("/plugins"),
         "command catalog classifies display toggles, status aliases, disabled aliases, and hotkeys as backend commands");

  std::vector<ava::app::CommandHotkey> const custom_hotkeys = {
      ava::app::CommandHotkey{.action = "submit", .description = "Submit custom", .keys = "Ctrl+M"},
      ava::app::CommandHotkey{.action = "variant_cycle", .description = "Cycle variants", .keys = "Ctrl+T"}};
  auto hotkeys = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/hotkeys", .hotkeys = custom_hotkeys});
  expect(hotkeys && hotkeys->handled && !hotkeys->output.empty() && hotkeys->output[0].find("Ctrl+M") != std::string::npos &&
             hotkeys->output[0].find("variant_cycle") != std::string::npos,
         "command dispatcher /hotkeys reports effective keybind metadata");
  auto details = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/details"});
  expect(details && details->handled && !details->output.empty() && details->output[0].find("TUI display toggle") != std::string::npos,
         "command dispatcher recognizes /details without inventing backend tool metadata");
  auto thinking = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/thinking"});
  expect(thinking && thinking->handled && !thinking->output.empty() && thinking->output[0].find("TUI display toggle") != std::string::npos &&
             thinking->output[0].find("does not change provider reasoning mode") != std::string::npos,
         "command dispatcher recognizes /thinking as display-only instead of changing backend reasoning mode");
  auto help = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/help", .hotkeys = custom_hotkeys});
  expect(help && help->handled && !help->output.empty() && help->output[0].find("/hotkeys") != std::string::npos &&
             help->output[0].find("/connect") != std::string::npos && help->output[0].find("/plugins") != std::string::npos &&
             help->output[0].find("Unavailable commands") != std::string::npos && help->output[0].find("Ctrl+M") != std::string::npos,
         "command dispatcher /help includes catalog commands and effective hotkeys");

  auto plugins_usage = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins"});
  expect(plugins_usage && plugins_usage->handled && !plugins_usage->output.empty() && plugins_usage->output[0].find("usage: /plugins") != std::string::npos,
         "command dispatcher /plugins without a subcommand reports usage");
  auto plugins = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins list"});
  expect(plugins && plugins->handled && !plugins->output.empty() && plugins->output[0].find("com.example.global") != std::string::npos &&
             plugins->output[0].find("com.example.project") != std::string::npos && plugins->output[0].find("Failures: 1") != std::string::npos,
         "command dispatcher /plugins list reports discovered plugins and diagnostics failures");
  auto inspect_plugin = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins inspect com.example.project"});
  expect(inspect_plugin && inspect_plugin->handled && !inspect_plugin->output.empty() &&
             inspect_plugin->output[0].find("entrypoint: node plugin.js --safe (not executed)") != std::string::npos &&
             inspect_plugin->output[0].find("no plugin process is started yet") != std::string::npos,
         "command dispatcher /plugins inspect shows manifest details without executing entrypoints");
  auto enable_plugin = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins enable com.example.project"});
  expect(enable_plugin && enable_plugin->handled && !enable_plugin->output.empty() &&
             enable_plugin->output[0].find("Enabled project plugin com.example.project") != std::string::npos &&
             enable_plugin->output[0].find("No plugin process was started") != std::string::npos,
         "command dispatcher /plugins enable records state without starting plugin processes");
  auto plugins_after_enable = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/plugins list"});
  expect(plugins_after_enable && plugins_after_enable->handled && !plugins_after_enable->output.empty() &&
             plugins_after_enable->output[0].find("com.example.project  enabled") != std::string::npos,
         "command dispatcher /plugins list reflects enablement state");
  auto const slash_items = ava::app::command_catalog_slash_items(*session, custom_hotkeys);
  auto find_slash_item = [&slash_items](std::string_view command) -> ava::tui::SlashCommandItem const* {
    for (auto const& item : slash_items)
    {
      if (item.command == command)
        return &item;
    }
    return nullptr;
  };
  auto has_completion = [](ava::tui::SlashCommandItem const* item, std::size_t argument_index, std::string_view value,
                           std::vector<std::string> previous_args = {}) {
    return item != nullptr && std::ranges::any_of(item->argument_completions, [&](auto const& completion) {
             return completion.argument_index == argument_index && completion.value == value && completion.required_previous_args == previous_args;
           });
  };
  auto const* connect_item = find_slash_item("/connect");
  auto const* models_item = find_slash_item("/models");
  auto const* sessions_item = find_slash_item("/sessions");
  auto const* context_item = find_slash_item("/context");
  auto const* mcp_item = find_slash_item("/mcp");
  auto const* plugin_item = find_slash_item("/plugin");
  expect(!has_completion(connect_item, 0, "openai") && !has_completion(connect_item, 1, "api-key") &&
             !has_completion(connect_item, 1, "browser-oauth", {"openai"}) && !has_completion(connect_item, 1, "headless-oauth", {"openai"}) &&
             has_completion(models_item, 0, "openai/gpt-5.5") && has_completion(sessions_item, 0, session->store.session_id()) &&
             has_completion(context_item, 0, (workspace / "AGENTS.md").generic_string()) && has_completion(mcp_item, 1, "fs", {"inspect"}) &&
             has_completion(plugin_item, 1, "com.example.project", {"run"}) && has_completion(plugin_item, 2, "todo", {"run", "com.example.project"}),
         "command catalog argument completions keep /connect provider and method choices in the modal while "
         "populating model, session, context, MCP, and plugin metadata");
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
             models->output[0].find("Ctrl+T cycles") != std::string::npos,
         "command dispatcher lists provider/model reasoning metadata and documents TUI reasoning cycling");
  auto filtered_models = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/models gpt-5.5"});
  expect(filtered_models && filtered_models->handled && !filtered_models->output.empty() &&
             filtered_models->output[0].find("filter gpt-5.5") != std::string::npos && filtered_models->output[0].find("gpt-5.5") != std::string::npos,
         "command dispatcher /models accepts backend-backed autocomplete query text without switching models");

  auto context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context"});
  expect(context && context->handled && !context->output.empty() && context->output[0].find("workspace") != std::string::npos &&
             context->output[0].find("AGENTS.md") != std::string::npos,
         "command dispatcher /context reports loaded context metadata");
  auto filtered_context = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/context AGENTS"});
  expect(
      filtered_context && filtered_context->handled && !filtered_context->output.empty() && filtered_context->output[0].find("AGENTS.md") != std::string::npos,
      "command dispatcher /context accepts backend context-source query text");
  auto filtered_sessions = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/sessions " + session->store.session_id()});
  expect(filtered_sessions && filtered_sessions->handled && !filtered_sessions->output.empty() &&
             filtered_sessions->output[0].find(session->store.session_id()) != std::string::npos,
         "command dispatcher /sessions accepts backend session-id query text");

  auto mode = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/mode"});
  expect(mode && mode->handled && session->mode == ava::agent::Mode::Build && !mode->output.empty() && mode->output[0].find("build") != std::string::npos,
         "command dispatcher /mode toggles runtime mode");
  expect(session->system_prompt != plan_system_prompt && session->system_prompt.find("Implement changes directly") != std::string::npos &&
             session->system_prompt.find("dispatcher context") != std::string::npos,
         "command dispatcher /mode rebuilds the mode-specific system prompt with context");

  bool saw_secret_prompt = false;
  auto connect = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/login moonshot api-key", .question_resolver = [&](ava::agent::QuestionPrompt const& prompt) {
                                           saw_secret_prompt =
                                               prompt.modal && prompt.secret && prompt.allow_custom && prompt.question.find("moonshot") != std::string::npos;
                                           return ava::agent::QuestionAnswer{.selected_options = {}, .custom_text = "slash-moonshot-api-key"};
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
      *session, ava::app::CommandRequest{.command = "/connect", .question_resolver = [&](ava::agent::QuestionPrompt const& prompt) {
                                           if (connect_prompt_count == 0)
                                           {
                                             expect(prompt.modal && prompt.searchable && prompt.allow_custom && prompt.question == "Select provider",
                                                    "slash /connect opens provider selection as searchable modal");
                                             ++connect_prompt_count;
                                             return ava::agent::QuestionAnswer{.selected_options = {"anthropic"}, .custom_text = ""};
                                           }
                                           if (connect_prompt_count == 1)
                                           {
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
  auto slash_anthropic = ava::config::provider_credential_for_request(session->paths, "anthropic", credential_transport);
  expect(slash_anthropic && slash_anthropic->has_value() && (*slash_anthropic)->access_token == "slash-api-key" &&
             (*slash_anthropic)->credential_type == "api_key",
         "slash provider connect modal writes loadable API key credential");

  std::size_t openai_connect_prompt_count = 0;
  auto connect_openai_modal = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/connect", .question_resolver = [&](ava::agent::QuestionPrompt const& prompt) {
                                           if (openai_connect_prompt_count == 0)
                                           {
                                             bool saw_active_openai = false;
                                             std::size_t kimi_moonshot_count = 0;
                                             bool saw_split_kimi = false;
                                             bool saw_manual_token_text = false;
                                             for (auto const& option : prompt.options)
                                             {
                                               saw_active_openai = saw_active_openai || option.label == "OpenAI ✓";
                                               if (option.label.find("Kimi / Moonshot") != std::string::npos)
                                                 ++kimi_moonshot_count;
                                               saw_split_kimi = saw_split_kimi || option.label.starts_with("Kimi -") || option.label.starts_with("Moonshot -");
                                               saw_manual_token_text = saw_manual_token_text || option.label.find("token") != std::string::npos;
                                             }
                                             expect(prompt.modal && prompt.searchable && prompt.allow_custom && prompt.question == "Select provider" &&
                                                        saw_active_openai && kimi_moonshot_count == 1 && !saw_split_kimi && !saw_manual_token_text,
                                                    "slash /connect opens provider modal with active OpenAI and merged API-key-only providers");
                                             ++openai_connect_prompt_count;
                                             return ava::agent::QuestionAnswer{.selected_options = {"openai"}, .custom_text = ""};
                                           }
                                           if (openai_connect_prompt_count == 1)
                                           {
                                             bool saw_browser = false;
                                             bool saw_headless = false;
                                             bool saw_api_key = false;
                                             bool saw_previous = false;
                                             for (auto const& option : prompt.options)
                                             {
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
  expect(connect_openai_modal && connect_openai_modal->handled && openai_connect_prompt_count == 3 && !connect_openai_modal->output.empty() &&
             connect_openai_modal->output[0].find("Stored openai API key credential") != std::string::npos,
         "command dispatcher /connect OpenAI walks provider, method, and secret modals");
  auto slash_openai_from_modal = ava::config::load_openai_credential(session->paths);
  expect(slash_openai_from_modal && slash_openai_from_modal->has_value() && (*slash_openai_from_modal)->type == ava::config::OpenAICredentialType::ApiKey &&
             (*slash_openai_from_modal)->access_token == "slash-openai-modal-api-key",
         "slash OpenAI connect modal writes loadable OpenAI credential");

  std::size_t back_connect_prompt_count = 0;
  auto connect_back_modal = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/connect", .question_resolver = [&](ava::agent::QuestionPrompt const& prompt) {
                                           if (back_connect_prompt_count == 0)
                                           {
                                             ++back_connect_prompt_count;
                                             return ava::agent::QuestionAnswer{.selected_options = {"openai"}, .custom_text = ""};
                                           }
                                           if (back_connect_prompt_count == 1)
                                           {
                                             expect(prompt.question == "Choose login method", "slash /connect can navigate back from method modal");
                                             ++back_connect_prompt_count;
                                             return ava::agent::QuestionAnswer{.selected_options = {"back"}, .custom_text = ""};
                                           }
                                           if (back_connect_prompt_count == 2)
                                           {
                                             expect(prompt.question == "Select provider", "slash /connect back returns to provider modal");
                                             ++back_connect_prompt_count;
                                             return ava::agent::QuestionAnswer{.selected_options = {"anthropic"}, .custom_text = ""};
                                           }
                                           if (back_connect_prompt_count == 3)
                                           {
                                             expect(prompt.modal && prompt.secret && prompt.question.find("anthropic") != std::string::npos,
                                                    "slash /connect back skips method modal for API-key-only providers");
                                             ++back_connect_prompt_count;
                                             return ava::agent::QuestionAnswer{.selected_options = {}, .custom_text = "slash-back-api-key"};
                                           }
                                           expect(false, "slash /connect back should not show an extra non-OpenAI method modal");
                                           ++back_connect_prompt_count;
                                           return ava::agent::QuestionAnswer{};
                                         }});
  expect(connect_back_modal && connect_back_modal->handled && back_connect_prompt_count == 4 && !connect_back_modal->output.empty() &&
             connect_back_modal->output[0].find("Stored anthropic API key credential") != std::string::npos,
         "command dispatcher /connect previous option returns to provider modal");

  auto connect_cancel = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/connect", .question_resolver = [](ava::agent::QuestionPrompt const&) {
                                           return ava::core::Result<ava::agent::QuestionAnswer>{
                                               std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt canceled"))};
                                         }});
  expect(connect_cancel && connect_cancel->handled && connect_cancel->output.empty(),
         "command dispatcher /connect treats modal cancellation as a silent close");

  bool saw_openai_secret_prompt = false;
  auto connect_openai_api = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/connect openai api-key", .question_resolver = [&](ava::agent::QuestionPrompt const& prompt) {
                                           saw_openai_secret_prompt =
                                               prompt.modal && prompt.secret && prompt.allow_custom && prompt.question.find("openai") != std::string::npos;
                                           return ava::agent::QuestionAnswer{.selected_options = {}, .custom_text = "slash-openai-api-key"};
                                         }});
  expect(connect_openai_api && connect_openai_api->handled && saw_openai_secret_prompt && !connect_openai_api->output.empty() &&
             connect_openai_api->output[0].find("Stored openai API key credential") != std::string::npos,
         "command dispatcher /connect openai api-key prompts once and stores OpenAI API key credential");
  auto slash_openai = ava::config::load_openai_credential(session->paths);
  expect(slash_openai && slash_openai->has_value() && (*slash_openai)->type == ava::config::OpenAICredentialType::ApiKey &&
             (*slash_openai)->access_token == "slash-openai-api-key",
         "slash OpenAI API key connect writes loadable OpenAI credential");

  auto connect_without_tui = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/connect anthropic"});
  expect(connect_without_tui && connect_without_tui->handled && !connect_without_tui->output.empty() &&
             connect_without_tui->output[0].find("--api-key-stdin") != std::string::npos &&
             connect_without_tui->output[0].find("--api-key-env") != std::string::npos,
         "command dispatcher /connect no-TUI error lists API-key headless setup flags");

  std::vector<ava::app::RuntimeEvent> command_tool_events;
  auto glob = ava::app::run_command(
      *session, ava::app::CommandRequest{.command = "/glob **/*.cpp", .event_sink = [&command_tool_events](ava::app::RuntimeEvent const& event) {
                                           command_tool_events.push_back(event);
                                           return ava::core::VoidResult{};
                                         }});
  expect(glob && glob->handled && !glob->output.empty() && glob->output[0].find("src/main.cpp") != std::string::npos,
         "command dispatcher /glob runs existing safe file search command");
  expect(glob && glob->tool_timeline.size() == 2 && glob->tool_timeline[0].status == ava::agent::ToolTimelineStatus::Running &&
             glob->tool_timeline[1].status == ava::agent::ToolTimelineStatus::Success &&
             glob->tool_timeline[1].structured_result_json.find("\"status\":\"success\"") != std::string::npos && glob->tool_timeline[1].total_matches,
         "command dispatcher records running and completed timeline entries with structured result metadata");
  expect(command_tool_events.size() == 2 && command_tool_events[1].type == ava::app::RuntimeEventType::ToolResult &&
             !command_tool_events[1].tool_structured_result_json.empty() &&
             command_tool_events[1].tool_structured_result_json.find("\"tool\":\"glob\"") != std::string::npos && command_tool_events[1].total_matches > 0,
         "command dispatcher emits structured tool result runtime events");

  std::size_t compact_generator_calls = 0;
  auto compact_generator = [&](std::vector<ava::session::SessionEntry> const& entries, ava::session::CompactionConfig const& config,
                               std::string_view instructions, std::size_t estimated_tokens) -> ava::core::Result<std::string> {
    ++compact_generator_calls;
    static_cast<void>(instructions);
    expect(!entries.empty() && config.max_summary_bytes > 0, "command dispatcher /compact passes session source data to summary generator");
    static_cast<void>(estimated_tokens);
    return std::string(
        "# Goal\nKeep key facts\n# Constraints / Preferences\nNone noted.\n# Decisions\nNone noted.\n"
        "# Files Read or Modified\nsrc/main.cpp\n# Unresolved Tasks\nNone noted.\n# Next Steps\nContinue.");
  };
  auto compact =
      ava::app::run_command(*session, ava::app::CommandRequest{.command = "/compact Keep key facts", .compaction_summary_generator = compact_generator});
  expect(compact && compact->handled && !compact->output.empty() && compact->output[0].find("compaction summary recorded") != std::string::npos,
         "command dispatcher /compact records generated compaction summary");
  auto compact_empty = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/compact", .compaction_summary_generator = compact_generator});
  expect(compact_empty && compact_empty->handled && !compact_empty->output.empty() &&
             compact_empty->output[0].find("compaction summary recorded") != std::string::npos,
         "command dispatcher /compact without instructions records generated compaction summary");
  auto compact_trailing = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/compact ", .compaction_summary_generator = compact_generator});
  expect(compact_trailing && compact_trailing->handled && !compact_trailing->output.empty() &&
             compact_trailing->output[0].find("compaction summary recorded") != std::string::npos,
         "command dispatcher /compact with trailing space records generated compaction summary");
  expect(compact_generator_calls == 3, "command dispatcher /compact invokes the summary generator once per command");

  auto entries = session->store.load();
  expect(entries && std::ranges::any_of(*entries,
                                        [](ava::session::SessionEntry const& entry) {
                                          return entry.type == ava::session::EntryType::Compaction &&
                                                 entry.data_json.find("Keep key facts") != std::string::npos &&
                                                 entry.data_json.find("\"summary_unavailable\":false") != std::string::npos;
                                        }),
         "command dispatcher /compact persists generated summary and instructions");

  auto const compactions_before_stale = entries ? count_compaction_entries(*entries) : 0;
  std::mutex session_mutex;
  bool introduced_manual_stale_snapshot = false;
  std::size_t manual_stale_generator_calls = 0;
  auto stale_compact = ava::app::run_command(
      *session, ava::app::CommandRequest{
                    .command = "/compact stale snapshot",
                    .compaction_summary_generator = [&](std::vector<ava::session::SessionEntry> const&, ava::session::CompactionConfig const&, std::string_view,
                                                        std::size_t) -> ava::core::Result<std::string> {
                      ++manual_stale_generator_calls;
                      if (!introduced_manual_stale_snapshot)
                      {
                        introduced_manual_stale_snapshot = true;
                        static_cast<void>(session->store.append(ava::session::SessionEntry{.id = "entry_manual_compact_concurrent_change",
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
  expect(
      entries && count_compaction_entries(*entries) == compactions_before_stale + 1 &&
          std::ranges::any_of(
              *entries, [](ava::session::SessionEntry const& entry) { return entry.data_json.find("manual compact concurrent change") != std::string::npos; }),
      "manual /compact stale snapshot preserves concurrent changes and appends one retried compaction");

  auto exported = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/export"});
  expect(exported && exported->handled && !exported->output.empty() && exported->output[0].find("# AVA Session Export") != std::string::npos &&
             exported->output[0].find("## Compaction") != std::string::npos,
         "command dispatcher /export returns markdown for loaded session entries");

  auto seeded_stats_usage =
      session->store.append(ava::session::SessionEntry{.id = "entry_slash_stats_usage",
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::AssistantMessage,
                                                       .timestamp = "2026-05-02T00:00:00Z",
                                                       .data_json = "{\"text\":\"usage\",\"usage\":{\"input_tokens\":12,\"output_tokens\":7,"
                                                                    "\"total_tokens\":19,\"cost_usd\":0.0015}}"});
  expect(seeded_stats_usage.has_value(), "command dispatcher /stats test seeds usage metadata");
  auto stats = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/stats"});
  expect(stats && stats->handled && !stats->output.empty() && stats->output[0].find("Session stats") != std::string::npos &&
             stats->output[0].find("tokens: input=12 output=7 total=19") != std::string::npos &&
             stats->output[0].find("cost: $0.001500") != std::string::npos && stats->output[0].find("compactions ") != std::string::npos &&
             stats->output[0].find("path:") == std::string::npos && stats->output[0].find("export: /export   resume: ava --session ") != std::string::npos,
         "command dispatcher /stats renders compact session counts, usage, cost, and hints");
  auto status = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/status"});
  expect(status && status->handled && !status->output.empty() && status->output[0] == stats->output[0],
         "command dispatcher /status aliases the backend-backed session stats surface");

  auto quit = ava::app::run_command(*session, ava::app::CommandRequest{.command = "/quit"});
  expect(quit && quit->handled && quit->quit, "command dispatcher /quit requests shell exit");
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
  if (!session)
    return;
  auto const session_id = session->store.session_id();

  auto model = ava::app::resolve_runtime_model(paths, "anthropic", "claude-test");
  expect(model.has_value(), "runtime resolves configured Anthropic model");
  if (!model)
    return;
  auto switched = ava::app::switch_runtime_model(*session, *model);
  expect(switched.has_value() && *switched, "runtime model switch reports a change");
  expect(session->model.provider_id == "anthropic" && session->model.model_id == "claude-test", "runtime model switch updates active session model");

  auto entries = session->store.load();
  expect(entries.has_value(), "runtime model switch loads session entries");
  bool saw_model_change = false;
  if (entries)
  {
    for (auto const& entry : *entries)
    {
      saw_model_change = saw_model_change ||
                         (entry.type == ava::session::EntryType::ModelChange && entry.data_json.find("\"previous_provider\":\"openai\"") != std::string::npos &&
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
  if (reopened)
  {
    auto const emoji_quirk = std::string("\xF0\x9F\x98\x80");
    restored_emoji_quirk = std::ranges::find(reopened->model.compatibility_quirks, emoji_quirk) != reopened->model.compatibility_quirks.end();
  }
  expect(restored_emoji_quirk, "runtime reopen decodes escaped supplementary-plane metadata");
  if (reopened)
  {
    ava::provider::OpenAIProvider const provider("https://api.example.test");
    ava::tests::FakeTransport transport({});
    std::istringstream in("{\"id\":\"list\",\"type\":\"list_models\"}\n");
    std::ostringstream out;
    auto result = ava::app::run_rpc_loop(*reopened, reopen_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
    auto const jsonl = out.str();
    auto const restored_position = jsonl.find("\"model\":\"claude-test\"");
    expect(result.has_value() && restored_position != std::string::npos, "RPC list_models includes restored removed current model");
    expect(restored_position != std::string::npos && jsonl.find("\"selectable\":false", restored_position) != std::string::npos,
           "RPC list_models marks restored removed current model as not selectable");
    expect(restored_position != std::string::npos && jsonl.find("\"context_window_tokens\":999", restored_position) != std::string::npos &&
               jsonl.find("\"max_output_tokens\":123", restored_position) != std::string::npos &&
               jsonl.find("\"supports_streaming\":true", restored_position) != std::string::npos &&
               jsonl.find("\"supports_tools\":false", restored_position) != std::string::npos &&
               jsonl.find("\"reports_usage\":true", restored_position) != std::string::npos && jsonl.find("test_quirk", restored_position) != std::string::npos,
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
      }]
    })JSON";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "runtime model switch compatibility test opens runtime session");
  if (!session)
    return;

  auto appended_tool_call = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                             .parent_id = "",
                                                                             .type = ava::session::EntryType::ToolCall,
                                                                             .timestamp = ava::session::now_timestamp(),
                                                                             .data_json = "{\"call_id\":\"call_1\","
                                                                                          "\"name\":\"read_file\","
                                                                                          "\"arguments\":{}}"});
  auto appended_tool_result = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                               .parent_id = "",
                                                                               .type = ava::session::EntryType::ToolResult,
                                                                               .timestamp = ava::session::now_timestamp(),
                                                                               .data_json = "{\"call_id\":\"call_1\",\"content\":\"ok\"}"});
  expect(appended_tool_call.has_value() && appended_tool_result.has_value(), "model switch compatibility test seeds tool history");

  auto no_tools_model = ava::app::resolve_runtime_model(paths, "openai", "no-tools");
  expect(no_tools_model.has_value(), "runtime resolves no-tools model");
  if (!no_tools_model)
    return;
  auto rejected_tools = ava::app::switch_runtime_model(*session, *no_tools_model);
  expect(!rejected_tools.has_value(), "runtime rejects switch to model without tool support after tool history");
  expect(!rejected_tools && rejected_tools.error().format().find("tool support") != std::string::npos,
         "runtime tool-history switch error explains missing tool support");
  expect(session->model.provider_id == "openai" && session->model.model_id == "gpt-5.5", "rejected tool-history switch leaves active model unchanged");

  auto appended_reasoning = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
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
  if (!anthropic_replay)
    return;
  auto switched_anthropic = ava::app::switch_runtime_model(*session, *anthropic_replay);
  expect(switched_anthropic.has_value() && *switched_anthropic, "runtime allows switch to Anthropic model that can replay Anthropic reasoning");
  expect(session->model.provider_id == "anthropic" && session->model.model_id == "claude-replay", "compatible reasoning switch updates active model");

  auto kimi_model = ava::app::resolve_runtime_model(paths, "kimi", "kimi-k2-thinking");
  expect(kimi_model.has_value(), "runtime resolves Kimi model");
  if (!kimi_model)
    return;
  auto rejected_reasoning = ava::app::switch_runtime_model(*session, *kimi_model);
  expect(!rejected_reasoning.has_value(), "runtime rejects incompatible reasoning provider switch");
  expect(!rejected_reasoning && rejected_reasoning.error().format().find("anthropic_thinking") != std::string::npos,
         "runtime reasoning switch error includes incompatible reasoning format");
  expect(session->model.provider_id == "anthropic" && session->model.model_id == "claude-replay", "rejected reasoning switch leaves active model unchanged");

  auto appended_compaction = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                              .parent_id = "",
                                                                              .type = ava::session::EntryType::Compaction,
                                                                              .timestamp = ava::session::now_timestamp(),
                                                                              .data_json = "{\"summary\":\"old history\"}"});
  expect(appended_compaction.has_value(), "model switch compatibility test seeds compaction boundary");
  auto switched_no_tools_after_compaction = ava::app::switch_runtime_model(*session, *no_tools_model);
  expect(switched_no_tools_after_compaction.has_value() && *switched_no_tools_after_compaction,
         "runtime ignores pre-compaction native history for switch compatibility");
  expect(session->model.provider_id == "openai" && session->model.model_id == "no-tools", "post-compaction switch updates active model");

  auto appended_kimi_reasoning = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                                  .parent_id = "",
                                                                                  .type = ava::session::EntryType::ReasoningBlock,
                                                                                  .timestamp = ava::session::now_timestamp(),
                                                                                  .data_json = "{\"provider\":\"kimi\","
                                                                                               "\"model\":\"kimi-k2-thinking\","
                                                                                               "\"format\":\"reasoning_content\","
                                                                                               "\"text\":\"compatible kimi reasoning\"}"});
  expect(appended_kimi_reasoning.has_value(), "model switch compatibility test seeds Kimi reasoning history");

  auto switched_kimi = ava::app::switch_runtime_model(*session, *kimi_model);
  expect(switched_kimi.has_value() && *switched_kimi, "runtime allows switch to Kimi model with explicit reasoning preservation support");
  expect(session->model.provider_id == "kimi" && session->model.model_id == "kimi-k2-thinking", "Kimi reasoning-compatible switch updates active model");

  auto moonshot_model = ava::app::resolve_runtime_model(paths, "moonshot", "kimi-k2.6");
  expect(moonshot_model.has_value(), "runtime resolves Moonshot model");
  if (!moonshot_model)
    return;
  auto rejected_moonshot = ava::app::switch_runtime_model(*session, *moonshot_model);
  expect(!rejected_moonshot.has_value(), "runtime rejects reasoning_content switch without preservation quirk");
  expect(session->model.provider_id == "kimi" && session->model.model_id == "kimi-k2-thinking",
         "rejected Moonshot reasoning switch leaves active model unchanged");

  auto entries = session->store.load();
  expect(entries.has_value(), "model switch compatibility test reloads entries");
  if (entries)
  {
    auto const model_changes = std::ranges::count_if(*entries, [](auto const& entry) { return entry.type == ava::session::EntryType::ModelChange; });
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
  if (!session)
    return;
  auto const session_id = session->store.session_id();

  auto selected =
      ava::app::set_runtime_reasoning(*session, ava::app::RuntimeReasoningSelection{.level = " low ", .budget_tokens = std::nullopt, .display = ""});
  expect(selected.has_value() && *selected && session->reasoning && session->reasoning->level == "low",
         "runtime reasoning selection validates, normalizes, and updates state");

  auto duplicate = ava::app::set_runtime_reasoning(*session, ava::app::RuntimeReasoningSelection{.level = "low", .budget_tokens = std::nullopt, .display = ""});
  expect(duplicate.has_value() && !*duplicate, "runtime reasoning selection is idempotent when unchanged");

  auto invalid = ava::app::set_runtime_reasoning(*session, ava::app::RuntimeReasoningSelection{.level = "ultra", .budget_tokens = std::nullopt, .display = ""});
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
  if (!transport.requests().empty())
  {
    expect(transport.requests()[0].body.find("\"reasoning\"") != std::string::npos &&
               transport.requests()[0].body.find("\"effort\":\"low\"") != std::string::npos &&
               transport.requests()[0].body.find("\"summary\":\"auto\"") != std::string::npos,
           "runtime reasoning selection is sent to the provider request with visible summary request");
  }

  auto entries = session->store.load();
  expect(entries.has_value(), "runtime reasoning test reloads session entries");
  if (entries)
  {
    auto const reasoning_changes = std::ranges::count_if(*entries, [](auto const& entry) { return entry.type == ava::session::EntryType::ReasoningChange; });
    expect(reasoning_changes == 1, "runtime reasoning selection appends one durable reasoning_change entry");
  }

  ava::app::RuntimeOpenOptions reopen_options = open_options;
  reopen_options.requested_session_id = session_id;
  auto reopened = ava::app::open_runtime_session(reopen_options);
  expect(reopened.has_value() && reopened->reasoning && reopened->reasoning->level == "low", "runtime reopen restores latest reasoning selection");

  auto cleared = ava::app::set_runtime_reasoning(*session, std::nullopt);
  expect(cleared.has_value() && *cleared && !session->reasoning, "runtime reasoning selection can be cleared");

  auto reselected =
      ava::app::set_runtime_reasoning(*session, ava::app::RuntimeReasoningSelection{.level = "low", .budget_tokens = std::nullopt, .display = ""});
  expect(reselected.has_value() && *reselected, "runtime reasoning test re-enables reasoning before switch boundary");
  auto kimi_model = ava::app::resolve_runtime_model(paths, "kimi", "kimi-k2-thinking");
  auto openai_model = ava::app::resolve_runtime_model(paths, "openai", "gpt-5.5");
  expect(kimi_model.has_value() && openai_model.has_value(), "runtime reasoning test resolves switch boundary models");
  if (kimi_model && openai_model)
  {
    auto switched_away = ava::app::switch_runtime_model(*session, *kimi_model);
    expect(switched_away.has_value() && *switched_away, "runtime reasoning test switches to Kimi model");
    auto kimi_budget =
        ava::app::set_runtime_reasoning(*session, ava::app::RuntimeReasoningSelection{.level = "enabled", .budget_tokens = 1024, .display = "summarized"});
    expect(!kimi_budget.has_value() && kimi_budget.error().format().find("Kimi reasoning supports level only") != std::string::npos,
           "runtime reasoning selection rejects unsupported OpenAI-compatible budget/display controls");
    auto switched_back = ava::app::switch_runtime_model(*session, *openai_model);
    expect(switched_back.has_value() && *switched_back && !session->reasoning, "runtime model switches clear active reasoning selection");
    auto reopened_after_switch = ava::app::open_runtime_session(reopen_options);
    expect(reopened_after_switch.has_value() && !reopened_after_switch->reasoning,
           "runtime reopen does not resurrect reasoning across model_change boundaries");
  }

  auto no_levels_model = ava::app::resolve_runtime_model(paths, "openai", "no-reasoning-levels");
  expect(no_levels_model.has_value(), "runtime reasoning test resolves no-level custom model");
  if (no_levels_model)
  {
    auto switched = ava::app::switch_runtime_model(*session, *no_levels_model);
    expect(switched.has_value() && *switched, "runtime reasoning test switches to no-level custom model");
    auto no_level_selection =
        ava::app::set_runtime_reasoning(*session, ava::app::RuntimeReasoningSelection{.level = "low", .budget_tokens = std::nullopt, .display = ""});
    expect(!no_level_selection.has_value() && no_level_selection.error().format().find("supported reasoning levels") != std::string::npos,
           "runtime reasoning selection rejects models without declared reasoning levels");
  }

  auto anthropic_default_max = ava::app::resolve_runtime_model(paths, "anthropic", "claude-default-max");
  expect(anthropic_default_max.has_value(), "runtime reasoning test resolves Anthropic default max model");
  if (anthropic_default_max)
  {
    auto switched = ava::app::switch_runtime_model(*session, *anthropic_default_max);
    expect(switched.has_value() && *switched, "runtime reasoning test switches to Anthropic default max model");
    auto over_budget =
        ava::app::set_runtime_reasoning(*session, ava::app::RuntimeReasoningSelection{.level = "enabled", .budget_tokens = 4096, .display = "summarized"});
    expect(!over_budget.has_value() && over_budget.error().format().find("reasoning budget must be below max output tokens") != std::string::npos,
           "runtime reasoning selection validates Anthropic budget against provider default max tokens");
  }

  auto proxy_registry = ava::config::load_model_registry(paths);
  expect(proxy_registry.has_value(), "runtime reasoning test loads registry for custom Anthropic-compatible model");
  auto anthropic_proxy = proxy_registry ? ava::config::find_model(*proxy_registry, "anthropic-proxy", "claude-proxy") : std::optional<ava::config::ModelInfo>{};
  expect(anthropic_proxy.has_value(), "runtime reasoning test finds custom Anthropic-compatible model");
  if (anthropic_proxy)
  {
    session->model = *anthropic_proxy;
    session->reasoning.reset();
    auto cycled = ava::app::cycle_runtime_reasoning(*session);
    expect(cycled.has_value() && session->reasoning && session->reasoning->level == "enabled" && session->reasoning->budget_tokens &&
               *session->reasoning->budget_tokens == 4096,
           "runtime reasoning cycling uses API-family fallback profile for custom Anthropic-compatible models");
    auto missing_budget =
        ava::app::set_runtime_reasoning(*session, ava::app::RuntimeReasoningSelection{.level = "enabled", .budget_tokens = std::nullopt, .display = ""});
    expect(!missing_budget.has_value() && missing_budget.error().format().find("Anthropic-proxy enabled reasoning requires budget_tokens") != std::string::npos,
           "runtime reasoning validation labels missing-budget errors with the custom provider id");
    auto too_large_budget =
        ava::app::set_runtime_reasoning(*session, ava::app::RuntimeReasoningSelection{.level = "enabled", .budget_tokens = 8192, .display = ""});
    expect(!too_large_budget.has_value() && too_large_budget.error().format().find("reasoning budget must be below max output tokens") != std::string::npos,
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
  test_app_run_prompt_emits_events();
  test_app_run_prompt_emits_provider_retry_events_when_enabled();
  test_app_run_prompt_emits_tool_progress_and_session_spill();
  test_app_run_prompt_event_sink_failure_cancels_before_next_provider_call();
  test_app_command_dispatcher();
  test_app_runtime_model_switch_persists_and_reopens();
  test_app_runtime_model_switch_rejects_incompatible_history();
  test_app_runtime_reasoning_selection_persists_and_requests();
}
