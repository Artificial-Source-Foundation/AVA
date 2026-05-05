#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "ava/agent/mode.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/rpc/runtime_handlers.h"
#include "ava/app/runtime.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "tests/support/test_harness.h"

namespace {

ava::config::XdgPaths rpc_runtime_test_paths(std::filesystem::path const& root)
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

ava::app::RuntimeSession make_rpc_runtime_session(std::string const& name)
{
  auto const root = temp_root() / name;
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions options;
  options.workspace_dir = workspace;
  options.current_dir = workspace;
  options.paths = rpc_runtime_test_paths(root);
  auto session = ava::app::open_runtime_session(options);
  expect(session.has_value(), "RPC runtime control test opens runtime session");
  if (!session) {
    return ava::app::RuntimeSession{
        .store = ava::session::SessionStore(ava::session::SessionStoreOptions{
            .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "rpc-runtime-fallback"}),
        .mode = ava::agent::Mode::Build,
        .model = {},
        .prompt = ava::config::PromptSelection{},
        .paths = options.paths,
        .workspace_dir = workspace,
        .current_dir = workspace,
        .context_sources = {},
        .system_prompt = {},
        .reasoning = std::nullopt,
        .created = false};
  }
  return std::move(*session);
}

ava::app::RpcCommand runtime_command(std::string id, std::string type)
{
  ava::app::RpcCommand command;
  command.id = std::move(id);
  command.type = std::move(type);
  return command;
}

void test_rpc_model_runtime_handler()
{
  auto session = make_rpc_runtime_session("app-rpc-runtime-model-control");
  std::mutex session_mutex;
  std::ostringstream stream;
  ava::app::rpc::RpcOutput output(stream);
  ava::app::rpc::RpcRunState run_state;

  auto set_same = runtime_command("set-same", "set_model");
  set_same.provider = session.model.provider_id;
  set_same.model = session.model.model_id;
  auto handled_same = ava::app::rpc::handle_model_command(output, session, session_mutex, run_state, set_same);

  auto missing = runtime_command("set-missing", "set_model");
  auto handled_missing = ava::app::rpc::handle_model_command(output, session, session_mutex, run_state, missing);

  ava::app::rpc::set_active_run(run_state, true, "active-prompt");
  auto cycle_active = runtime_command("cycle-active", "cycle_model");
  auto handled_active = ava::app::rpc::handle_model_command(output, session, session_mutex, run_state, cycle_active);
  auto const jsonl = stream.str();

  expect(handled_same && handled_missing && handled_active, "RPC model runtime handler writes expected responses");
  expect(jsonl.find("\"id\":\"set-same\"") != std::string::npos &&
             jsonl.find("\"provider\":\"" + session.model.provider_id + "\"") != std::string::npos &&
             jsonl.find("\"model\":\"" + session.model.model_id + "\"") != std::string::npos &&
             jsonl.find("set_model requires model") != std::string::npos &&
             jsonl.find("RPC command is unavailable while a prompt is active") != std::string::npos,
         "RPC model runtime handler emits state, validation errors, and active-run rejection");
}

void test_rpc_reasoning_runtime_handler()
{
  auto session = make_rpc_runtime_session("app-rpc-runtime-reasoning-control");
  std::mutex session_mutex;
  std::ostringstream stream;
  ava::app::rpc::RpcOutput output(stream);
  ava::app::rpc::RpcRunState run_state;

  auto set_reasoning = runtime_command("set-reasoning", "set_reasoning");
  set_reasoning.reasoning_level = "medium";
  auto handled_set = ava::app::rpc::handle_reasoning_command(output, session, session_mutex, run_state, set_reasoning);

  auto missing = runtime_command("missing-reasoning", "set_reasoning");
  auto handled_missing = ava::app::rpc::handle_reasoning_command(output, session, session_mutex, run_state, missing);

  auto clear_reasoning = runtime_command("clear-reasoning", "clear_reasoning");
  auto handled_clear =
      ava::app::rpc::handle_reasoning_command(output, session, session_mutex, run_state, clear_reasoning);

  ava::app::rpc::set_active_run(run_state, true, "active-prompt");
  auto active_reasoning = runtime_command("active-reasoning", "set_reasoning");
  active_reasoning.reasoning_level = "low";
  auto handled_active =
      ava::app::rpc::handle_reasoning_command(output, session, session_mutex, run_state, active_reasoning);
  auto const jsonl = stream.str();

  expect(handled_set && handled_missing && handled_clear && handled_active,
         "RPC reasoning runtime handler writes expected responses");
  expect(!session.reasoning, "RPC reasoning runtime handler clears active reasoning state");
  expect(jsonl.find("\"id\":\"set-reasoning\"") != std::string::npos &&
             jsonl.find("\"reasoning_enabled\":true") != std::string::npos &&
             jsonl.find("\"reasoning_level\":\"medium\"") != std::string::npos &&
             jsonl.find("set_reasoning requires reasoning_level") != std::string::npos &&
             jsonl.rfind("\"reasoning_enabled\":false") != std::string::npos &&
             jsonl.find("RPC command is unavailable while a prompt is active") != std::string::npos,
         "RPC reasoning runtime handler emits state, validation errors, clear state, and active-run rejection");
}

}  // namespace

void run_app_rpc_runtime_control_tests()
{
  test_rpc_model_runtime_handler();
  test_rpc_reasoning_runtime_handler();
}
