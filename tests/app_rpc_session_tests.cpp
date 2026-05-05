#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "ava/agent/mode.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/rpc/session_handlers.h"
#include "ava/app/runtime.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "tests/support/test_harness.h"

namespace {

ava::config::XdgPaths rpc_session_test_paths(std::filesystem::path const& root)
{
  auto const config_home = root / "config";
  auto const state_home = root / "state";
  auto const data_home = root / "data";
  auto const ava_config = config_home / "ava";
  auto const ava_state = state_home / "ava";
  return ava::config::XdgPaths{.config_home = config_home,
                               .state_home = state_home,
                               .data_home = data_home,
                               .ava_config_dir = ava_config,
                               .ava_state_dir = ava_state,
                               .auth_file = ava_config / "auth.json",
                               .compaction_file = ava_config / "compaction.json",
                               .global_agents_file = ava_config / "AGENTS.md",
                               .models_file = ava_config / "models.json",
                               .prompts_dir = ava_config / "prompts",
                               .sessions_dir = ava_state / "sessions"};
}

ava::app::RuntimeOpenOptions rpc_session_open_options(std::filesystem::path const& root)
{
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions options;
  options.workspace_dir = workspace;
  options.current_dir = workspace;
  options.paths = rpc_session_test_paths(root);
  return options;
}

ava::app::RuntimeSession open_rpc_session(ava::app::RuntimeOpenOptions const& options)
{
  auto session = ava::app::open_runtime_session(options);
  expect(session.has_value(), "RPC session handler test opens runtime session");
  if (!session) {
    return ava::app::RuntimeSession{
        .store = ava::session::SessionStore(ava::session::SessionStoreOptions{.root_dir = options.paths.sessions_dir,
                                                                              .workspace_dir = options.workspace_dir,
                                                                              .session_id = "rpc-session-fallback"}),
        .mode = ava::agent::Mode::Build,
        .model = {},
        .prompt = ava::config::PromptSelection{},
        .paths = options.paths,
        .workspace_dir = options.workspace_dir,
        .current_dir = options.current_dir,
        .context_sources = {},
        .system_prompt = {},
        .reasoning = std::nullopt,
        .created = false};
  }
  return std::move(*session);
}

ava::app::RpcCommand session_command(std::string id, std::string type)
{
  ava::app::RpcCommand command;
  command.id = std::move(id);
  command.type = std::move(type);
  return command;
}

void set_cancel_requested(ava::app::rpc::RpcRunState& run_state)
{
  std::lock_guard lock(run_state.mutex);
  run_state.cancel_requested.store(true, std::memory_order_relaxed);
}

void test_rpc_new_session_handler()
{
  auto const root = temp_root() / "app-rpc-session-new";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const open_options = rpc_session_open_options(root);
  auto session = open_rpc_session(open_options);
  auto const original_session_id = session.store.session_id();

  std::mutex session_mutex;
  std::ostringstream stream;
  ava::app::rpc::RpcOutput output(stream);
  ava::app::rpc::RpcRunState run_state;
  set_cancel_requested(run_state);

  auto created = session_command("new-ok", "new_session");
  auto handled_created =
      ava::app::rpc::handle_new_session_command(output, session, open_options, session_mutex, run_state, created);

  ava::app::rpc::set_active_run(run_state, true, "active-prompt");
  auto active = session_command("new-active", "new_session");
  auto handled_active =
      ava::app::rpc::handle_new_session_command(output, session, open_options, session_mutex, run_state, active);
  auto const jsonl = stream.str();

  expect(handled_created && handled_active, "RPC new-session handler writes expected responses");
  expect(session.store.session_id() != original_session_id, "RPC new-session handler replaces the active session");
  expect(!ava::app::rpc::cancel_requested(run_state), "RPC new-session handler clears stale cancellation state");
  expect(jsonl.find("\"id\":\"new-ok\"") != std::string::npos &&
             jsonl.find("\"cancel_requested\":false") != std::string::npos &&
             jsonl.find("RPC command is unavailable while a prompt is active") != std::string::npos,
         "RPC new-session handler emits state and active-run rejection");
}

void test_rpc_open_session_handler()
{
  auto const root = temp_root() / "app-rpc-session-open";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const open_options = rpc_session_open_options(root);
  auto session = open_rpc_session(open_options);
  auto target_session = open_rpc_session(open_options);
  auto const target_session_id = target_session.store.session_id();

  std::mutex session_mutex;
  std::ostringstream stream;
  ava::app::rpc::RpcOutput output(stream);
  ava::app::rpc::RpcRunState run_state;
  set_cancel_requested(run_state);

  auto missing = session_command("open-missing", "open_session");
  auto handled_missing =
      ava::app::rpc::handle_open_session_command(output, session, open_options, session_mutex, run_state, missing);

  auto opened = session_command("open-ok", "switch_session");
  opened.session_id = target_session_id;
  auto handled_opened =
      ava::app::rpc::handle_open_session_command(output, session, open_options, session_mutex, run_state, opened);

  ava::app::rpc::set_active_run(run_state, true, "active-prompt");
  auto active = session_command("open-active", "open_session");
  active.session_id = target_session_id;
  auto handled_active =
      ava::app::rpc::handle_open_session_command(output, session, open_options, session_mutex, run_state, active);
  auto const jsonl = stream.str();

  expect(handled_missing && handled_opened && handled_active, "RPC open-session handler writes expected responses");
  expect(session.store.session_id() == target_session_id, "RPC open-session handler replaces the active session");
  expect(!ava::app::rpc::cancel_requested(run_state), "RPC open-session handler clears stale cancellation state");
  expect(jsonl.find("open_session requires session_id") != std::string::npos &&
             jsonl.find("\"id\":\"open-ok\"") != std::string::npos &&
             jsonl.find("\"session_id\":\"" + target_session_id + "\"") != std::string::npos &&
             jsonl.find("RPC command is unavailable while a prompt is active") != std::string::npos,
         "RPC open-session handler emits validation, state, and active-run rejection");
}

}  // namespace

void run_app_rpc_session_tests()
{
  test_rpc_new_session_handler();
  test_rpc_open_session_handler();
}
