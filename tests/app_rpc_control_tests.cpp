#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "ava/agent/mode.h"
#include "ava/app/rpc/control_handlers.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"
#include "ava/config/model_config.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/permissions/permission.h"
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

ava::app::RpcCommand rpc_command(std::string id, std::string type, std::optional<std::string> message = std::nullopt)
{
  ava::app::RpcCommand command;
  command.id = std::move(id);
  command.type = std::move(type);
  command.message = std::move(message);
  return command;
}

void test_rpc_queue_control_handlers()
{
  auto session = make_rpc_control_session();
  std::mutex session_mutex;
  std::ostringstream stream;
  ava::app::rpc::RpcOutput output(stream);
  ava::app::rpc::RpcRunState run_state;
  ava::app::rpc::set_active_run(run_state, true, "prompt-queue");

  auto steer = rpc_command("steer-1", "steer", "steer message");
  auto handled_steer = ava::app::rpc::handle_steer_command(output, session, session_mutex, run_state, steer);
  auto steering = ava::app::rpc::take_queued_steering_messages(run_state, "prompt-queue");
  auto follow_up = rpc_command("follow-1", "follow_up", "follow message");
  auto handled_follow_up =
      ava::app::rpc::handle_follow_up_command(output, session, session_mutex, run_state, follow_up);
  auto queued_follow_up = ava::app::rpc::take_next_follow_up_message(run_state);
  auto missing = rpc_command("follow-missing", "follow_up");
  auto handled_missing = ava::app::rpc::handle_follow_up_command(output, session, session_mutex, run_state, missing);
  auto const jsonl = stream.str();

  expect(handled_steer && steering.size() == 1 && steering[0].request_id == "steer-1" &&
             steering[0].correlation_id == "prompt-queue" && steering[0].message == "steer message",
         "RPC steer handler queues steering messages against the active prompt");
  expect(handled_follow_up && queued_follow_up && queued_follow_up->request_id == "follow-1" &&
             queued_follow_up->correlation_id == "prompt-queue" && queued_follow_up->message == "follow message",
         "RPC follow_up handler queues follow-up messages against the active prompt");
  expect(handled_missing && jsonl.find("\"name\":\"steer_queued\"") != std::string::npos &&
             jsonl.find("\"name\":\"follow_up_queued\"") != std::string::npos &&
             jsonl.find("\"queued\":true") != std::string::npos &&
             jsonl.find("\"correlation_id\":\"prompt-queue\"") != std::string::npos &&
             jsonl.find("follow_up requires message") != std::string::npos,
         "RPC queue handlers emit queue events, steer success responses, and validation errors");
}

void test_rpc_resolver_reply_control_handlers()
{
  auto session = make_rpc_control_session();
  std::mutex session_mutex;
  std::ostringstream stream;
  ava::app::rpc::RpcOutput output(stream);
  ava::app::rpc::PendingResolverState pending_state;

  auto permission = std::make_shared<ava::app::rpc::PendingPermissionRequest>();
  permission->correlation_id = "prompt-permission";
  permission->permission_request_id = "perm-1";
  permission->operation = ava::permissions::Operation::EditFile;
  permission->mode = ava::agent::Mode::Build;
  permission->tool_name = "edit_file";
  permission->target_path = session.workspace_dir / "note.txt";
  permission->reason = "test permission";
  auto question = std::make_shared<ava::app::rpc::PendingQuestionRequest>();
  question->correlation_id = "prompt-question";
  question->options.push_back(ava::agent::QuestionOption{.value = "safe", .label = "Safe"});
  {
    std::lock_guard lock(pending_state.mutex);
    pending_state.permission_requests.emplace("perm-1", permission);
    pending_state.question_requests.emplace("question-1", question);
  }

  ava::app::RpcCommand permission_command;
  permission_command.id = "permission-reply-1";
  permission_command.type = "permission_reply";
  permission_command.request_id = "perm-1";
  permission_command.correlation_id = "prompt-permission";
  permission_command.decision = "allow_session";
  auto handled_permission =
      ava::app::rpc::handle_permission_reply_command(output, session, session_mutex, pending_state, permission_command);

  ava::app::RpcCommand question_command;
  question_command.id = "question-reply-1";
  question_command.type = "question_reply";
  question_command.request_id = "question-1";
  question_command.correlation_id = "prompt-question";
  question_command.selected = "safe";
  auto handled_question =
      ava::app::rpc::handle_question_reply_command(output, session, session_mutex, pending_state, question_command);

  ava::app::RpcCommand missing_decision;
  missing_decision.id = "permission-missing-decision";
  missing_decision.type = "permission_reply";
  missing_decision.request_id = "perm-missing";
  missing_decision.correlation_id = "prompt-permission";
  auto handled_missing =
      ava::app::rpc::handle_permission_reply_command(output, session, session_mutex, pending_state, missing_decision);
  auto const jsonl = stream.str();

  {
    std::lock_guard lock(pending_state.mutex);
    expect(handled_permission && handled_question && handled_missing && pending_state.permission_requests.empty() &&
               pending_state.question_requests.empty() && pending_state.permission_session_grants.size() == 1 &&
               pending_state.permission_session_grants[0].permission_request_id == "perm-1",
           "RPC resolver reply handlers resolve pending requests and create session grants when requested");
  }
  expect(jsonl.find("\"name\":\"permission_replied\"") != std::string::npos &&
             jsonl.find("\"name\":\"question_replied\"") != std::string::npos &&
             jsonl.find("\"decision\":\"allow_session\"") != std::string::npos &&
             jsonl.find("\"selected\":\"safe\"") != std::string::npos &&
             jsonl.find("\"id\":\"permission-reply-1\"") != std::string::npos &&
             jsonl.find("\"id\":\"question-reply-1\"") != std::string::npos &&
             jsonl.find("permission_reply requires decision") != std::string::npos,
         "RPC resolver reply handlers emit semantic reply events, success responses, and validation errors");
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
  test_rpc_queue_control_handlers();
  test_rpc_resolver_reply_control_handlers();
  test_rpc_cancel_handler_clears_queues_and_reports_follow_up_errors();
}
