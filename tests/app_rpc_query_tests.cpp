#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "ava/agent/mode.h"
#include "ava/app/rpc/query_handlers.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"
#include "ava/config/model_config.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/session_store.h"
#include "tests/support/test_harness.h"

namespace {

ava::app::RuntimeSession make_rpc_query_session()
{
  auto const root = temp_root() / "app-rpc-query";
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
          .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "rpc-query-session"}),
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

ava::app::RpcCommand rpc_command(std::string id, std::string type)
{
  ava::app::RpcCommand command;
  command.id = std::move(id);
  command.type = std::move(type);
  return command;
}

void append_query_message(ava::app::RuntimeSession& session, std::string id, ava::session::EntryType type,
                          std::string text)
{
  ava::session::SessionEntry entry;
  entry.id = std::move(id);
  entry.parent_id = {};
  entry.type = type;
  entry.timestamp = ava::session::now_timestamp();
  entry.data_json = "{\"text\":\"" + ava::session::json_escape(text) + "\"}";
  entry.version = ava::session::kCurrentSessionEntryVersion;
  auto appended = session.store.append(entry);
  expect(appended.has_value(), "RPC query test appends session entries");
}

void test_rpc_query_handlers_success_and_active_rejection()
{
  auto session = make_rpc_query_session();
  append_query_message(session, "user-query", ava::session::EntryType::UserMessage, "hello query");
  append_query_message(session, "assistant-query", ava::session::EntryType::AssistantMessage, "query answer");

  std::mutex session_mutex;
  std::ostringstream stream;
  ava::app::rpc::RpcOutput output(stream);
  ava::app::rpc::RpcRunState run_state;

  auto state = ava::app::rpc::handle_get_state_command(output, session, session_mutex, run_state,
                                                       rpc_command("state", "get_state"));
  auto sessions = ava::app::rpc::handle_list_sessions_command(output, session, session_mutex,
                                                              rpc_command("sessions", "list_sessions"));
  auto models =
      ava::app::rpc::handle_list_models_command(output, session, session_mutex, rpc_command("models", "list_models"));
  auto messages = ava::app::rpc::handle_get_messages_command(output, session, session_mutex, run_state,
                                                             rpc_command("messages", "get_messages"));
  auto stats = ava::app::rpc::handle_get_session_stats_command(output, session, session_mutex, run_state,
                                                               rpc_command("stats", "get_session_stats"));
  auto validation = ava::app::rpc::handle_validate_session_command(output, session, session_mutex, run_state,
                                                                   rpc_command("validation", "validate_session"));
  ava::app::rpc::set_active_run(run_state, true, "prompt-active");
  auto active_messages = ava::app::rpc::handle_get_messages_command(output, session, session_mutex, run_state,
                                                                    rpc_command("active", "get_messages"));
  auto const jsonl = stream.str();

  expect(state && sessions && models && messages && stats && validation && active_messages,
         "RPC query handlers write success or structured error responses");
  expect(
      jsonl.find("\"id\":\"state\"") != std::string::npos &&
          jsonl.find("\"session_id\":\"rpc-query-session\"") != std::string::npos &&
          jsonl.find("\"cancel_requested\":false") != std::string::npos &&
          jsonl.find("\"id\":\"sessions\"") != std::string::npos && jsonl.find("\"sessions\"") != std::string::npos &&
          jsonl.find("\"id\":\"models\"") != std::string::npos && jsonl.find("\"models\"") != std::string::npos &&
          jsonl.find("\"id\":\"messages\"") != std::string::npos && jsonl.find("hello query") != std::string::npos &&
          jsonl.find("\"message_count\":2") != std::string::npos &&
          jsonl.find("\"id\":\"stats\"") != std::string::npos && jsonl.find("\"entry_count\":2") != std::string::npos &&
          jsonl.find("\"id\":\"validation\"") != std::string::npos && jsonl.find("\"issues\"") != std::string::npos &&
          jsonl.find("RPC command is unavailable while a prompt is active") != std::string::npos,
      "RPC query handlers emit state, catalog, transcript, stats, validation, and active-run rejection payloads");
}

}  // namespace

void run_app_rpc_query_tests()
{
  test_rpc_query_handlers_success_and_active_rejection();
}
