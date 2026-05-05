#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <utility>

#include "ava/agent/mode.h"
#include "ava/app/rpc/control_handlers.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"
#include "ava/config/model_config.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/session_store.h"
#include "tests/support/test_harness.h"

namespace {

ava::app::RuntimeSession make_rpc_control_session()
{
  auto const root = temp_root() / "app-rpc-control";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  ava::config::ModelInfo model;
  model.provider_id = "openai";
  model.model_id = "gpt-5.5";
  model.display_name = "gpt-5.5";
  model.family = "gpt";
  model.api_family = "responses";
  return ava::app::RuntimeSession{
      .store = ava::session::SessionStore(ava::session::SessionStoreOptions{
          .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "rpc-control-session"}),
      .mode = ava::agent::Mode::Build,
      .model = std::move(model),
      .prompt = ava::config::PromptSelection{},
      .paths = ava::config::xdg_paths(),
      .workspace_dir = workspace,
      .current_dir = workspace,
      .context_sources = {},
      .system_prompt = {},
      .reasoning = std::nullopt,
      .created = false};
}

void test_rpc_cancel_handler_clears_queues_and_reports_follow_up_errors()
{
  auto session = make_rpc_control_session();
  std::mutex session_mutex;
  std::ostringstream stream;
  ava::app::rpc::RpcOutput output(stream);
  ava::app::rpc::RpcRunState run_state;
  ava::app::rpc::PendingResolverState pending_state;

  ava::app::rpc::set_active_run(run_state, true, "prompt-1");
  auto steer = ava::app::rpc::queue_rpc_message(run_state.steering_messages, run_state, "steer", "steer-1", "steer");
  auto follow_up =
      ava::app::rpc::queue_rpc_message(run_state.follow_up_messages, run_state, "follow_up", "follow-1", "next");
  {
    std::lock_guard lock(pending_state.mutex);
    pending_state.question_requests.emplace("question-1", std::make_shared<ava::app::rpc::PendingQuestionRequest>());
  }

  ava::app::RpcCommand command;
  command.id = "cancel-1";
  command.type = "cancel";
  auto handled =
      ava::app::rpc::handle_cancel_command(output, session, session_mutex, run_state, pending_state, command);
  auto const jsonl = stream.str();
  expect(steer && follow_up && handled && ava::app::rpc::cancel_requested(run_state) &&
             ava::app::rpc::active_run(run_state) && !ava::app::rpc::take_next_follow_up_message(run_state),
         "RPC cancel handler requests cancellation and clears queued messages without deactivating the active run");
  expect(jsonl.find("\"name\":\"steer_skipped\"") != std::string::npos &&
             jsonl.find("\"name\":\"follow_up_skipped\"") != std::string::npos &&
             jsonl.find("\"name\":\"cancel_requested\"") != std::string::npos &&
             jsonl.find("\"id\":\"cancel-1\"") != std::string::npos &&
             jsonl.find("\"cancel_requested\":true") != std::string::npos &&
             jsonl.find("\"active_run\":true") != std::string::npos &&
             jsonl.find("\"cleared_steer\":1") != std::string::npos &&
             jsonl.find("\"cleared_follow_up\":1") != std::string::npos &&
             jsonl.find("\"id\":\"follow-1\"") != std::string::npos &&
             jsonl.find("agent loop canceled") != std::string::npos,
         "RPC cancel handler emits skipped queue events, cancel response, and follow-up error responses");
  {
    std::lock_guard lock(pending_state.mutex);
    expect(pending_state.question_requests.empty() && pending_state.permission_requests.empty(),
           "RPC cancel handler clears pending resolver state");
  }
}

}  // namespace

void run_app_rpc_control_tests()
{
  test_rpc_cancel_handler_clears_queues_and_reports_follow_up_errors();
}
