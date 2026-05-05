#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include "ava/agent/mode.h"
#include "ava/app/rpc/prompt_handlers.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/provider/openai_provider.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

namespace {

ava::config::XdgPaths rpc_prompt_test_paths(std::filesystem::path const& root)
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

ava::app::RuntimeSession make_rpc_prompt_session()
{
  auto const root = temp_root() / "app-rpc-prompt-handler";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions options;
  options.workspace_dir = workspace;
  options.current_dir = workspace;
  options.paths = rpc_prompt_test_paths(root);
  auto session = ava::app::open_runtime_session(options);
  expect(session.has_value(), "RPC prompt handler test opens runtime session");
  if (!session) {
    return ava::app::RuntimeSession{
        .store = ava::session::SessionStore(ava::session::SessionStoreOptions{
            .root_dir = options.paths.sessions_dir, .workspace_dir = workspace, .session_id = "rpc-prompt-fallback"}),
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

ava::app::RpcCommand prompt_command(std::string id)
{
  ava::app::RpcCommand command;
  command.id = std::move(id);
  command.type = "prompt";
  return command;
}

bool wait_until_inactive(ava::app::rpc::RpcRunState& run_state)
{
  for (int attempt = 0; attempt < 200; ++attempt) {
    if (!ava::app::rpc::active_run(run_state)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

void test_rpc_prompt_handler_validation_and_active_rejection()
{
  auto session = make_rpc_prompt_session();
  std::mutex session_mutex;
  std::ostringstream stream;
  ava::app::rpc::RpcOutput output(stream);
  ava::app::rpc::RpcRunState run_state;
  ava::app::rpc::PendingResolverState pending_state;
  std::optional<std::jthread> prompt_worker;
  ava::app::RuntimeRunOptions runtime_options;
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});

  auto missing = prompt_command("missing");
  auto handled_missing = ava::app::rpc::handle_prompt_command(output, session, session_mutex, run_state, pending_state,
                                                              prompt_worker, missing, runtime_options, provider,
                                                              transport, transport, session.model.provider_id);

  ava::app::rpc::set_active_run(run_state, true, "active-prompt");
  auto active = prompt_command("active");
  active.message = "hello";
  auto handled_active = ava::app::rpc::handle_prompt_command(output, session, session_mutex, run_state, pending_state,
                                                             prompt_worker, active, runtime_options, provider,
                                                             transport, transport, session.model.provider_id);
  auto const jsonl = stream.str();

  expect(handled_missing && handled_active, "RPC prompt handler writes validation and active-run responses");
  expect(!prompt_worker, "RPC prompt handler does not start a worker for invalid or rejected prompts");
  expect(jsonl.find("prompt requires message") != std::string::npos &&
             jsonl.find("RPC command is unavailable while a prompt is active") != std::string::npos,
         "RPC prompt handler reports validation and active-run errors");
}

void test_rpc_prompt_handler_runs_prompt_worker()
{
  auto session = make_rpc_prompt_session();
  std::mutex session_mutex;
  std::ostringstream stream;
  ava::app::rpc::RpcOutput output(stream);
  ava::app::rpc::RpcRunState run_state;
  ava::app::rpc::PendingResolverState pending_state;
  std::optional<std::jthread> prompt_worker;
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"prompt handler answer\"}\n\n"
              "data: [DONE]\n\n",
  }});

  auto command = prompt_command("prompt-ok");
  command.message = "say hello";
  auto handled = ava::app::rpc::handle_prompt_command(output, session, session_mutex, run_state, pending_state,
                                                      prompt_worker, command, runtime_options, provider, transport,
                                                      transport, session.model.provider_id);
  auto inactive = wait_until_inactive(run_state);
  prompt_worker.reset();
  auto const jsonl = stream.str();

  expect(handled && inactive, "RPC prompt handler starts and completes a prompt worker");
  expect(jsonl.find("\"id\":\"prompt-ok\"") != std::string::npos &&
             jsonl.find("\"final_text\":\"prompt handler answer\"") != std::string::npos,
         "RPC prompt handler writes prompt result JSON from the worker");
}

}  // namespace

void run_app_rpc_prompt_tests()
{
  test_rpc_prompt_handler_validation_and_active_rejection();
  test_rpc_prompt_handler_runs_prompt_worker();
}
