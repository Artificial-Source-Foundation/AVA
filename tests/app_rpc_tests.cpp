#include "ava/app/headless_policy.h"
#include "ava/app/rpc/serialization.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"

#include "ava/agent/question.h"

#include "ava/config/auth.h"

#include "ava/session/session_store.h"

#include "ava/permissions/permission.h"

#include "ava/provider/openai_provider.h"

#include "ava/core/ids.h"
#include "ava/core/json.h"

#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace {

using namespace ava::tests;

void test_app_rpc_prompt_payload_serialization()
{
  auto const permission_json = ava::app::rpc::permission_request_payload_json(
      "permission_1", ava::permissions::PermissionPrompt{.permission_request_id = "permreq_1",
                                                         .operation = ava::permissions::Operation::EditFile,
                                                         .mode = ava::agent::Mode::Build,
                                                         .workspace_dir = "/workspace",
                                                         .target_path = "/workspace/src/main.cpp",
                                                         .command = "",
                                                         .tool_name = "edit_file",
                                                         .reason = "needs approval",
                                                         .risk = ava::permissions::PermissionRisk::High,
                                                         .diff_preview = "--- a\n+++ b\n-old\n+new",
                                                         .diff_truncated = true});
  expect(permission_json.find("\"operation\":\"edit\"") != std::string::npos &&
             permission_json.find("\"permission_request_id\":\"permreq_1\"") != std::string::npos &&
             permission_json.find("\"target_path\":\"/workspace/src/main.cpp\"") != std::string::npos &&
             permission_json.find("\"risk\":\"high\"") != std::string::npos &&
             permission_json.find("\"diff_preview\":\"--- a\\n+++ b\\n-old\\n+new\"") != std::string::npos &&
             permission_json.find("\"diff_truncated\":true") != std::string::npos,
         "RPC permission request payload preserves semantic operation, target, risk, reason, and diff preview data");

  auto const question_json = ava::app::rpc::question_request_payload_json(
      "question_1",
      ava::agent::QuestionPrompt{.header = "Choose",
                                 .question = "Pick providers",
                                 .options = {ava::agent::QuestionOption{.value = "openai", .label = "OpenAI"}},
                                 .multiple = true,
                                 .allow_custom = true,
                                 .secret = true,
                                 .modal = true,
                                 .searchable = true});
  expect(question_json.find("\"options\":[{\"value\":\"openai\",\"label\":\"OpenAI\"}]") != std::string::npos &&
             question_json.find("\"multiple\":true") != std::string::npos &&
             question_json.find("\"allow_custom\":true") != std::string::npos &&
             question_json.find("\"secret\":true") != std::string::npos &&
             question_json.find("\"modal\":true") != std::string::npos &&
             question_json.find("\"searchable\":true") != std::string::npos,
         "RPC question request payload preserves options, selection metadata, and local prompt flags");

  auto const permission_reply_json =
      ava::app::rpc::permission_reply_payload_json("permission_1", "deny", std::optional<std::string>{"not approved"});
  expect(permission_reply_json.find("\"resolver_request_id\":\"permission_1\"") != std::string::npos &&
             permission_reply_json.find("\"decision\":\"deny\"") != std::string::npos &&
             permission_reply_json.find("\"reason\":\"not approved\"") != std::string::npos,
         "RPC permission reply payload preserves client-supplied resolution reasons");

  auto const question_reply_json = ava::app::rpc::question_reply_payload_json(
      "question_1", std::optional<std::string>{"custom"}, std::nullopt,
      std::optional<std::vector<std::string>>{std::vector<std::string>{"alpha", "beta"}});
  expect(question_reply_json.find("\"resolver_request_id\":\"question_1\"") != std::string::npos &&
             question_reply_json.find("\"answer\":\"custom\"") != std::string::npos &&
             question_reply_json.find("\"selected_options\":[\"alpha\",\"beta\"]") != std::string::npos,
         "RPC question reply payload preserves multiple selections and custom text");
}

void test_app_rpc_parsing_and_response_serialization()
{
  auto command = ava::app::parse_rpc_command_line(
      "{\"id\":\"1\",\"type\":\"prompt\",\"message\":\"hello\\nava\",\"instructions\":\"keep\"}");
  expect(command && command->id == "1" && command->type == "prompt" && command->message &&
             *command->message == "hello\nava" && command->instructions && *command->instructions == "keep",
         "RPC parser extracts string envelope fields and unescapes JSON strings");

  auto reply = ava::app::parse_rpc_command_line(
      R"JSON({"id":"reply","type":"permission_reply","request_id":"permission_1",)JSON"
      R"JSON("correlation_id":"prompt_1","decision":"deny","reason":"not approved for this run"})JSON");
  expect(reply && reply->reason && *reply->reason == "not approved for this run",
         "RPC parser preserves optional permission reply reasons");

  auto question_reply = ava::app::parse_rpc_command_line(
      R"JSON({"id":"question","type":"question_reply","request_id":"question_1",)JSON"
      R"JSON("correlation_id":"prompt_1","selected_options":["alpha","beta"],"answer":"custom"})JSON");
  expect(question_reply && question_reply->selected_options && question_reply->selected_options->size() == 2 &&
             (*question_reply->selected_options)[0] == "alpha" && (*question_reply->selected_options)[1] == "beta",
         "RPC parser preserves selected_options arrays for question replies");

  auto invalid_selected_options =
      ava::app::parse_rpc_command_line(R"JSON({"id":"bad","type":"question_reply","selected_options":["ok",2]})JSON");
  expect(!invalid_selected_options &&
             invalid_selected_options.error().message() == "RPC selected_options must be an array of strings",
         "RPC parser rejects non-string selected_options entries");

  auto oversized_reason =
      ava::app::parse_rpc_command_line("{\"id\":\"reply\",\"type\":\"permission_reply\",\"reason\":\"" +
                                       std::string(ava::app::rpc::kMaxRpcReasonBytes + 1, 'x') + "\"}");
  expect(!oversized_reason && oversized_reason.error().message() == "RPC text field is too long",
         "RPC parser rejects oversized text fields before emitting resolver events");

  auto control_reason =
      ava::app::parse_rpc_command_line(R"JSON({"id":"reply","type":"permission_reply","reason":"bad\u001b"})JSON");
  expect(!control_reason && control_reason.error().message() == "RPC text field contains invalid character",
         "RPC parser rejects control bytes in free-text resolver reasons");

  auto malformed = ava::app::parse_rpc_command_line("{\"id\":\"bad\",\"type\":\"prompt\"");
  expect(!malformed && malformed.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "RPC parser rejects malformed JSON object lines");

  auto oversized_id =
      ava::app::parse_rpc_command_line("{\"id\":\"" + std::string(257, 'x') + "\",\"type\":\"prompt\"}");
  expect(!oversized_id && oversized_id.error().message() == "RPC identifier is too long",
         "RPC parser rejects oversized request identifiers before queueing");

  auto const success = ava::app::serialize_rpc_success_jsonl("a\"b", "{\"value\":1}");
  expect(success == "{\"id\":\"a\\\"b\",\"type\":\"response\",\"success\":true,\"result\":{\"value\":1}}\n",
         "RPC success response serializes deterministic JSONL with escaped id");

  auto const error = ava::app::serialize_rpc_error_jsonl(
      "e1", ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "bad \"request\""));
  expect(error.find("\"success\":false") != std::string::npos &&
             error.find("bad \\\"request\\\"") != std::string::npos && error.ends_with('\n'),
         "RPC error response serializes JSONL error details");
}

void test_app_rpc_identifier_validation()
{
  auto allowed = ava::app::parse_rpc_command_line(
      R"JSON({"id":"rpc.1","type":"set_model","request_id":"req_1","correlation_id":"corr-1",)JSON"
      R"JSON("provider":"openai","model":"openai/gpt-5.5","plugin_id":"com.example.rpc",)JSON"
      R"JSON("name":"demo-server_1","server_id":"demo-server_1"})JSON");
  expect(allowed && allowed->model && *allowed->model == "openai/gpt-5.5" && allowed->plugin_id &&
             *allowed->plugin_id == "com.example.rpc" && allowed->server_id && *allowed->server_id == "demo-server_1",
         "RPC parser allows practical dotted, dashed, underscored, and slash-delimited identifiers");

  auto control = ava::app::parse_rpc_command_line(R"JSON({"id":"bad\u001f","type":"prompt"})JSON");
  expect(!control && control.error().message() == "RPC identifier contains invalid character",
         "RPC parser rejects escaped control bytes in identifiers");

  std::string del_line = R"JSON({"id":"bad)JSON";
  del_line.push_back(static_cast<char>(0x7F));
  del_line += R"JSON(","type":"prompt"})JSON";
  auto del = ava::app::parse_rpc_command_line(del_line);
  expect(!del && del.error().message() == "RPC identifier contains invalid character",
         "RPC parser rejects DEL bytes in identifiers");

  auto whitespace = ava::app::parse_rpc_command_line(R"JSON({"id":"bad id","type":"prompt"})JSON");
  expect(!whitespace && whitespace.error().message() == "RPC identifier contains invalid character",
         "RPC parser rejects ASCII whitespace in identifiers");

  auto metachar =
      ava::app::parse_rpc_command_line(R"JSON({"id":"ok","type":"inspect_plugin","plugin_id":"bad;id"})JSON");
  expect(!metachar && metachar.error().message() == "RPC identifier contains invalid character",
         "RPC parser rejects command-ambiguous metacharacters in slash-command identifiers");

  auto path = ava::app::parse_rpc_command_line(
      R"JSON({"id":"path-ok","type":"validate_plugin","path":"./plugins/bad; path.json"})JSON");
  expect(path && path->path && *path->path == "./plugins/bad; path.json",
         "RPC parser leaves validate_plugin path validation to the plugin path handler");
}

void test_app_rpc_prompt_with_fake_transport_streams_events()
{
  auto const root = temp_root() / "app-rpc-prompt";
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
  expect(session.has_value(), "RPC prompt test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"rpc answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });
  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hello rpc\"}\n");
  bool const completed = output_buffer.wait_contains("rpc answer", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC prompt loop completes successfully");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("hello rpc") != std::string::npos,
         "RPC prompt sends command message through shared runtime");
  expect(jsonl.find("\"name\":\"session_start\"") != std::string::npos &&
             jsonl.find("\"name\":\"assistant_message\"") != std::string::npos &&
             jsonl.find("\"request_id\":\"p1\"") != std::string::npos && completed &&
             jsonl.find("\"id\":\"p1\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
             jsonl.find("rpc answer") != std::string::npos,
         "RPC prompt streams runtime event envelopes and ends with a successful response");
}

void test_app_rpc_prompt_streams_provider_deltas_before_final_response()
{
  auto const root = temp_root() / "app-rpc-prompt-streaming";
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
  expect(session.has_value(), "RPC streaming prompt test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ChunkedStreamingTransport transport({"data: {\"type\":\"response.output_text.delta\",\"delta\":\"rpc \"}\n\n",
                                       "data: {\"type\":\"response.output_text.delta\",\"delta\":\"stream\"}\n\n",
                                       "data: [DONE]\n\n"});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });
  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hello rpc stream\"}\n");
  bool const completed = output_buffer.wait_contains("\"success\":true", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  auto const update_position = jsonl.find("\"name\":\"message_update\"");
  auto const final_position = jsonl.find("\"name\":\"assistant_message\"");
  auto const response_position = jsonl.find("\"type\":\"response\"");
  expect(result.has_value(), "RPC streaming prompt loop completes successfully");
  expect(update_position != std::string::npos && final_position != std::string::npos && completed &&
             response_position != std::string::npos && update_position < final_position &&
             final_position < response_position && jsonl.find("rpc stream") != std::string::npos,
         "RPC prompt emits live provider deltas before final assistant event and command response");
}

void test_app_rpc_prompt_refreshes_expired_oauth_before_provider_request()
{
  auto const root = temp_root() / "app-rpc-oauth-refresh";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto stored = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "expired-rpc-access",
                                           .refresh_token = "rpc-refresh",
                                           .expires_at = 100,
                                           .account_id = "acct_old",
                                           .source_path = {}});
  expect(stored.has_value(), "RPC OAuth refresh test stores expired credential");

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC OAuth refresh test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "{\"access_token\":\"rpc-refreshed-access\","
                                                   "\"refresh_token\":\"rpc-rotated-refresh\","
                                                   "\"expires_in\":3600,\"account_id\":\"acct_rpc\"}",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"rpc refreshed answer\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] {
    result =
        ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  });
  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hello refreshed rpc\"}\n");
  bool const completed = output_buffer.wait_contains("\"success\":true", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC prompt with expired OAuth completes after refresh");
  expect(transport.requests().size() == 2 && transport.requests()[0].url == "https://auth.openai.com/oauth/token" &&
             transport.requests()[1].headers.at("Authorization") == "Bearer rpc-refreshed-access" &&
             transport.requests()[1].headers.at("ChatGPT-Account-Id") == "acct_rpc",
         "RPC prompt refreshes OAuth before sending provider request");
  expect(completed && jsonl.find("rpc refreshed answer") != std::string::npos &&
             jsonl.find("\"success\":true") != std::string::npos,
         "RPC prompt returns refreshed OAuth provider response");
  auto persisted = ava::config::load_openai_credential(paths);
  expect(persisted && persisted->has_value() && (*persisted)->access_token == "rpc-refreshed-access" &&
             (*persisted)->refresh_token == "rpc-rotated-refresh",
         "RPC OAuth preflight persists refreshed credential before provider startup");
}

void test_app_rpc_malformed_line_recovery_and_unknown_command()
{
  auto const root = temp_root() / "app-rpc-recovery";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC recovery test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "not json\n{\"id\":\"s1\",\"type\":\"get_state\"}\n"
      "{\"id\":\"u1\",\"type\":\"unknown\"}\n");
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC loop continues after malformed and unknown commands");
  expect(std::count(jsonl.begin(), jsonl.end(), '\n') == 3 && jsonl.find("\"id\":\"\"") != std::string::npos &&
             jsonl.find("malformed RPC JSON object") != std::string::npos &&
             jsonl.find("\"id\":\"s1\"") != std::string::npos && jsonl.find("\"session_id\":\"") != std::string::npos &&
             jsonl.find("\"id\":\"u1\"") != std::string::npos &&
             jsonl.find("unknown RPC command type") != std::string::npos,
         "RPC loop writes error responses and recovers for subsequent JSONL records");
}

void test_app_rpc_state_list_sessions_and_open_session()
{
  auto const root = temp_root() / "app-rpc-state";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto first = ava::app::open_runtime_session(open_options);
  auto second = ava::app::open_runtime_session(open_options);
  expect(first.has_value() && second.has_value(), "RPC state test opens multiple sessions");
  if (!first || !second) return;
  auto const first_id = first->store.session_id();
  auto const second_id = second->store.session_id();

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"list\",\"type\":\"list_sessions\"}\n"
      "{\"id\":\"open\",\"type\":\"open_session\",\"session_id\":\"" +
      first_id + "\"}\n");
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*second, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC state/list/open loop completes successfully");
  expect(jsonl.find("\"id\":\"state\"") != std::string::npos && jsonl.find(second_id) != std::string::npos &&
             jsonl.find("\"id\":\"list\"") != std::string::npos && jsonl.find(first_id) != std::string::npos &&
             jsonl.find("\"id\":\"open\"") != std::string::npos,
         "RPC state, list_sessions, and open_session return session metadata");
  expect(second->store.session_id() == first_id, "RPC open_session switches the active runtime session");
}

void test_app_rpc_model_commands()
{
  auto const root = temp_root() / "app-rpc-model-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC model command test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"list\",\"type\":\"list_models\"}\n"
      "{\"id\":\"set\",\"type\":\"set_model\",\"provider\":\"anthropic\","
      "\"model\":\"claude-sonnet-4-5\"}\n"
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"stats\",\"type\":\"get_session_stats\"}\n"
      "{\"id\":\"cycle\",\"type\":\"cycle_model\"}\n");
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC model command loop completes successfully");
  expect(jsonl.find("\"id\":\"list\"") != std::string::npos && jsonl.find("\"models\"") != std::string::npos &&
             jsonl.find("claude-sonnet-4-5") != std::string::npos,
         "RPC list_models returns configured model catalog");
  expect(jsonl.find("\"id\":\"set\"") != std::string::npos &&
             jsonl.find("\"provider\":\"anthropic\"") != std::string::npos &&
             jsonl.find("\"model\":\"claude-sonnet-4-5\"") != std::string::npos,
         "RPC set_model returns updated Anthropic state");
  expect(jsonl.find("\"id\":\"state\"") != std::string::npos &&
             jsonl.find("\"model\":\"claude-sonnet-4-5\"") != std::string::npos,
         "RPC get_state reflects selected model after set_model");
  expect(jsonl.find("\"id\":\"stats\"") != std::string::npos && jsonl.find("\"model_change\":1") != std::string::npos,
         "RPC get_session_stats reports model_change count");
  expect(
      jsonl.find("\"id\":\"cycle\"") != std::string::npos && jsonl.find("\"provider\":\"kimi\"") != std::string::npos,
      "RPC cycle_model advances to the next configured provider model");
  expect(session->model.provider_id == "kimi", "RPC cycle_model updates active session model");
}

void test_app_rpc_reasoning_commands()
{
  auto const root = temp_root() / "app-rpc-reasoning-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC reasoning command test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"set\",\"type\":\"set_reasoning\",\"reasoning_level\":\"medium\"}\n"
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"invalid\",\"type\":\"set_reasoning\",\"reasoning_level\":\"ultra\"}\n"
      "{\"id\":\"clear\",\"type\":\"clear_reasoning\"}\n");
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC reasoning command loop completes successfully");
  expect(jsonl.find("\"id\":\"set\"") != std::string::npos &&
             jsonl.find("\"reasoning_enabled\":true") != std::string::npos &&
             jsonl.find("\"reasoning_level\":\"medium\"") != std::string::npos,
         "RPC set_reasoning returns enabled reasoning state");
  expect(jsonl.find("\"id\":\"state\"") != std::string::npos &&
             jsonl.find("\"reasoning_level\":\"medium\"") != std::string::npos,
         "RPC get_state reflects selected reasoning");
  expect(jsonl.find("\"id\":\"invalid\"") != std::string::npos &&
             jsonl.find("reasoning level is not supported") != std::string::npos,
         "RPC set_reasoning reports invalid reasoning levels");
  expect(jsonl.find("\"id\":\"clear\"") != std::string::npos &&
             jsonl.rfind("\"reasoning_enabled\":false") != std::string::npos,
         "RPC clear_reasoning disables reasoning state");
  expect(!session->reasoning, "RPC clear_reasoning updates active session state");

  auto entries = session->store.load();
  expect(entries.has_value(), "RPC reasoning command test reloads entries");
  if (entries) {
    auto const reasoning_changes = std::ranges::count_if(
        *entries, [](auto const& entry) { return entry.type == ava::session::EntryType::ReasoningChange; });
    expect(reasoning_changes == 2, "RPC reasoning commands persist set and clear reasoning_change entries");
  }
}

void test_app_rpc_protocol_version_and_session_commands()
{
  auto const root = temp_root() / "app-rpc-protocol-session";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC protocol/session test opens runtime session");
  if (!session) return;
  auto const initial_id = session->store.session_id();

  auto appended_user = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                        .parent_id = "",
                                                                        .type = ava::session::EntryType::UserMessage,
                                                                        .timestamp = ava::session::now_timestamp(),
                                                                        .data_json = "{\"text\":\"hello\"}"});
  auto appended_internal_replay =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::UserMessage,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"text\":\"hidden rpc replay\","
                                                                    "\"internal_replay\":true,"
                                                                    "\"replay_of\":\"entry_user\","
                                                                    "\"reason\":\"test\"}"});
  auto appended_assistant = session->store.append(
      ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                 .parent_id = "",
                                 .type = ava::session::EntryType::AssistantMessage,
                                 .timestamp = ava::session::now_timestamp(),
                                 .data_json = "{\"text\":\"answer\",\"usage\":{\"input_tokens\":1,"
                                              "\"output_tokens\":1,\"total_tokens\":2,"
                                              "\"cost_usd\":0.001,"
                                              "\"source\":\"provider\"}}"});
  auto appended_unpriced_assistant =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::AssistantMessage,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"text\":\"unknown cost\",\"usage\":{"
                                                                    "\"input_tokens\":1,\"cache_read_tokens\":1,"
                                                                    "\"total_tokens\":1,"
                                                                    "\"source\":\"provider\"}}"});
  auto appended_reasoning =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::ReasoningBlock,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"provider\":\"anthropic\","
                                                                    "\"model\":\"claude-sonnet-4-5\","
                                                                    "\"format\":\"anthropic_thinking\","
                                                                    "\"text\":\"visible reasoning\","
                                                                    "\"signature\":\"rpc-secret-signature\"}"});
  auto appended_redacted_reasoning =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::ReasoningBlock,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"provider\":\"anthropic\","
                                                                    "\"model\":\"claude-sonnet-4-5\","
                                                                    "\"format\":\"anthropic_thinking\","
                                                                    "\"text\":\"hidden redacted rpc reasoning\","
                                                                    "\"signature\":\"rpc-redacted-secret-signature\","
                                                                    "\"redacted\": true }"});
  auto appended_mode = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                        .parent_id = "",
                                                                        .type = ava::session::EntryType::ModeChange,
                                                                        .timestamp = ava::session::now_timestamp(),
                                                                        .data_json = "{\"mode\":\"build\"}"});
  auto appended_compaction =
      session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::Compaction,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"summary\":\"prior\"}"});
  auto appended_cancel = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                          .parent_id = "",
                                                                          .type = ava::session::EntryType::Cancel,
                                                                          .timestamp = ava::session::now_timestamp(),
                                                                          .data_json = "{}"});
  expect(appended_user.has_value() && appended_internal_replay.has_value() && appended_assistant.has_value() &&
             appended_unpriced_assistant.has_value() && appended_reasoning.has_value() &&
             appended_redacted_reasoning.has_value() && appended_mode.has_value() && appended_compaction.has_value() &&
             appended_cancel.has_value(),
         "RPC protocol/session test appends messages and stats foundation entries");

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"proto\",\"type\":\"get_protocol\",\"protocol_version\":1}\n"
      "{\"id\":\"messages\",\"type\":\"get_messages\"}\n"
      "{\"id\":\"stats\",\"type\":\"get_session_stats\"}\n"
      "{\"id\":\"validate\",\"type\":\"validate_session\"}\n"
      "{\"id\":\"new\",\"type\":\"new_session\"}\n"
      "{\"id\":\"switch\",\"type\":\"switch_session\",\"session_id\":\"" +
      initial_id + "\"}\n");
  std::ostringstream out;
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC protocol/session loop completes successfully");
  expect(jsonl.find("\"id\":\"proto\"") != std::string::npos &&
             jsonl.find("\"protocol_version\":1") != std::string::npos &&
             jsonl.find("\"supported_protocol_versions\":[1]") != std::string::npos,
         "RPC get_protocol reports supported protocol version");
  expect(jsonl.find("\"id\":\"messages\"") != std::string::npos && jsonl.find("\"messages\"") != std::string::npos &&
             jsonl.find("\"version\":2") != std::string::npos && jsonl.find("hello") != std::string::npos &&
             jsonl.find("answer") != std::string::npos && jsonl.find("visible reasoning") != std::string::npos &&
             jsonl.find("hidden redacted rpc reasoning") == std::string::npos &&
             jsonl.find("\"signature_present\":true") != std::string::npos &&
             jsonl.find("hidden rpc replay") == std::string::npos &&
             jsonl.find("rpc-secret-signature") == std::string::npos &&
             jsonl.find("rpc-redacted-secret-signature") == std::string::npos,
         "RPC get_messages returns consumer-visible durable message entries without reasoning signatures");
  expect(jsonl.find("\"id\":\"stats\"") != std::string::npos && jsonl.find("\"entry_count\":10") != std::string::npos &&
             jsonl.find("\"user_message\":1") != std::string::npos &&
             jsonl.find("\"assistant_message\":2") != std::string::npos &&
             jsonl.find("\"reasoning_block\":2") != std::string::npos &&
             jsonl.find("\"mode_change\":1") != std::string::npos &&
             jsonl.find("\"compaction\":1") != std::string::npos && jsonl.find("\"cancel\":1") != std::string::npos,
         "RPC get_session_stats returns session counters");
  expect(jsonl.find("\"known_cost_usd\":0.001") != std::string::npos &&
             jsonl.find("\"cost_complete\":false") != std::string::npos &&
             jsonl.find("\"unknown_cost_entries\":1") != std::string::npos &&
             jsonl.find("\"total_cost_usd\"") == std::string::npos,
         "RPC get_session_stats omits incomplete total cost and reports known cost metadata");
  expect(jsonl.find("\"id\":\"validate\"") != std::string::npos && jsonl.find("\"ok\":true") != std::string::npos &&
             jsonl.find("\"error_count\":0") != std::string::npos,
         "RPC validate_session reports a clean replay audit for the active session");
  expect(jsonl.find("\"id\":\"new\"") != std::string::npos && jsonl.find("\"created\":true") != std::string::npos,
         "RPC new_session creates and switches to a new active session");
  expect(session->store.session_id() == initial_id && jsonl.find("\"id\":\"switch\"") != std::string::npos,
         "RPC switch_session switches back to the requested session");
}

void test_app_rpc_protocol_version_and_resolver_reply_errors()
{
  auto const root = temp_root() / "app-rpc-protocol-errors";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC protocol error test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::string input =
      "{\"id\":\"bad-version\",\"type\":\"get_state\",\"protocol_version\":999}\n"
      "{\"id\":\"reply-missing\",\"type\":\"permission_reply\"}\n"
      "{\"id\":\"bad-version-type\",\"type\":\"get_state\",\"protocol_version\":\"1\"}\n"
      "{\"id\":\"oversized-reply\",\"type\":\"permission_reply\",\"request_id\":\"" +
      std::string(257, 'r') +
      "\",\"correlation_id\":\"p1\",\"decision\":\"allow\"}\n"
      "{\"id\":\"reply\",\"type\":\"question_reply\",\"request_id\":\"question_1\",\"correlation_id\":\"p1\","
      "\"answer\":\"ok\"}\n"
      "{\"id\":\"state\",\"type\":\"get_state\",\"protocol_version\":1}\n";
  std::istringstream in(input);
  std::ostringstream out;
  auto result =
      ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC protocol error loop recovers after unsupported commands");
  expect(jsonl.find("unsupported RPC protocol version") != std::string::npos &&
             jsonl.find("RPC protocol_version must be an integer") != std::string::npos &&
             jsonl.find("permission_reply requires request_id") != std::string::npos &&
             jsonl.find("\"id\":\"oversized-reply\"") != std::string::npos &&
             jsonl.find("RPC identifier is too long") != std::string::npos &&
             jsonl.find("RPC resolver reply has no matching pending request") != std::string::npos &&
             jsonl.find("\"id\":\"state\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos,
         "RPC version and resolver reply errors are in-band and recoverable");
}

void test_app_rpc_mcp_command_responses()
{
  expect(!std::string_view(AVA_FAKE_MCP_SERVER_PATH).empty(), "RPC MCP command test has fake server path");
  if (std::string_view(AVA_FAKE_MCP_SERVER_PATH).empty()) return;

  auto const root = temp_root() / "app-rpc-mcp-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  write_app_test_file(workspace / ".ava" / "mcp.json",
                      app_test_mcp_config_json("demo", "Demo MCP", AVA_FAKE_MCP_SERVER_PATH));

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC MCP command test opens runtime session");
  if (!session) return;

  std::vector<ava::permissions::PermissionPrompt> prompts;
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.permission_resolver = [&prompts](ava::permissions::PermissionPrompt const& prompt)
      -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    prompts.push_back(prompt);
    return ava::permissions::PermissionResolution::Allow;
  };

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"mcp-list\",\"type\":\"list_mcp_servers\"}\n"
      "{\"id\":\"mcp-inspect\",\"type\":\"inspect_mcp_server\",\"server_id\":\"demo\"}\n"
      "{\"id\":\"mcp-tools\",\"type\":\"list_mcp_tools\",\"server_id\":\"demo\"}\n"
      "{\"id\":\"mcp-restart\",\"type\":\"restart_mcp_server\",\"server_id\":\"demo\"}\n");
  std::ostringstream out;
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  auto const jsonl = out.str();

  auto const has_id = [&jsonl](std::string_view id) {
    return jsonl.find("\"id\":\"" + std::string(id) + "\"") != std::string::npos;
  };
  expect(result.has_value(), "RPC MCP command loop completes successfully");
  expect(has_id("mcp-list") && has_id("mcp-inspect") && has_id("mcp-tools") && has_id("mcp-restart"),
         "RPC MCP command responses include all request ids");
  expect(jsonl.find("MCP servers:") != std::string::npos && jsonl.find("Demo MCP") != std::string::npos &&
             jsonl.find("MCP server demo") != std::string::npos &&
             jsonl.find("MCP tools for demo") != std::string::npos && jsonl.find("fake-mcp") != std::string::npos &&
             jsonl.find("echo") != std::string::npos && jsonl.find("mcp_demo_echo") != std::string::npos &&
             jsonl.find("next discovery or tool call will launch a fresh process") != std::string::npos,
         "RPC MCP command responses expose list, inspect, tools, and restart output");

  auto const has_launch_prompt = std::ranges::any_of(prompts, [](auto const& prompt) {
    return prompt.operation == ava::permissions::Operation::McpServerLaunch && prompt.tool_name == "mcp_tools";
  });
  auto const has_connect_prompt = std::ranges::any_of(prompts, [](auto const& prompt) {
    return prompt.operation == ava::permissions::Operation::McpServerConnect && prompt.tool_name == "mcp_tools" &&
           prompt.command == "demo";
  });
  expect(has_launch_prompt && has_connect_prompt,
         "RPC list_mcp_tools requests MCP launch and connect permissions before allowing discovery");
}

void test_app_rpc_command_responses_for_context_compact_export()
{
  auto const root = temp_root() / "app-rpc-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "rpc command context\n";
  }
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.rpc" / "plugin.json",
                      app_test_plugin_manifest_json("com.example.rpc", "RPC Plugin"));
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.rpcbad" / "plugin.json", "{not-json");

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC command test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  std::string const rpc_summary =
      "# Goal\nRemember RPC facts\n# Constraints / Preferences\nNone noted.\n# Decisions\nNone noted.\n"
      "# Files Read or Modified\nNone noted.\n# Unresolved Tasks\nNone noted.\n# Next Steps\nContinue.";
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 200,
                                   .headers = {},
                                   .body = "{\"output_text\":\"" + ava::core::json::escape(rpc_summary) + "\"}"}});
  std::istringstream in(
      "{\"id\":\"plugins\",\"type\":\"list_plugins\"}\n"
      "{\"id\":\"plugin-enable\",\"type\":\"enable_plugin\",\"plugin_id\":\"com.example.rpc\"}\n"
      "{\"id\":\"plugin-inspect\",\"type\":\"inspect_plugin\",\"plugin_id\":\"com.example.rpc\"}\n"
      "{\"id\":\"plugin-validate\",\"type\":\"validate_plugin\",\"path\":\".ava/plugins/com.example.rpc/"
      "plugin.json\"}\n"
      "{\"id\":\"plugin-failures\",\"type\":\"plugin_failures\"}\n"
      "{\"id\":\"ctx\",\"type\":\"context\"}\n"
      "{\"id\":\"cmp\",\"type\":\"compact\",\"instructions\":\"remember rpc facts\"}\n"
      "{\"id\":\"exp\",\"type\":\"export\"}\n");
  std::ostringstream out;
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC context/compact/export loop completes successfully");
  expect(jsonl.find("\"id\":\"plugins\"") != std::string::npos && jsonl.find("com.example.rpc") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-enable\"") != std::string::npos &&
             jsonl.find("No plugin process was started") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-inspect\"") != std::string::npos &&
             jsonl.find("status: enabled") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-validate\"") != std::string::npos &&
             jsonl.find("Valid plugin manifest") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-failures\"") != std::string::npos &&
             jsonl.find("com.example.rpcbad") != std::string::npos &&
             jsonl.find("\"id\":\"ctx\"") != std::string::npos && jsonl.find("AGENTS.md") != std::string::npos &&
             jsonl.find("\"id\":\"cmp\"") != std::string::npos &&
             jsonl.find("\"name\":\"compaction_start\"") != std::string::npos &&
             jsonl.find("\"name\":\"compaction_end\"") != std::string::npos &&
             jsonl.find("compaction summary recorded") != std::string::npos &&
             jsonl.find("\"id\":\"exp\"") != std::string::npos &&
             jsonl.find("# AVA Session Export") != std::string::npos &&
             jsonl.find("remember rpc facts") != std::string::npos,
         "RPC command responses expose command dispatcher output as JSONL protocol records");
}

void test_app_rpc_compact_provider_failure_is_error_response()
{
  auto const root = temp_root() / "app-rpc-compact-failure";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC compact failure test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"summary failed\"}}"}});
  std::istringstream in("{\"id\":\"cmp-fail\",\"type\":\"compact\"}\n");
  std::ostringstream out;
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";

  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  auto const jsonl = out.str();
  auto entries = session->store.load();
  expect(result.has_value(), "RPC compact failure loop completes after error response");
  expect(jsonl.find("\"id\":\"cmp-fail\"") != std::string::npos &&
             jsonl.find("\"success\":false") != std::string::npos &&
             jsonl.find("compaction summary request failed with status 500") != std::string::npos &&
             jsonl.find("summary failed") != std::string::npos,
         "RPC compact provider failures are machine-readable error responses");
  expect(entries && count_compaction_entries(*entries) == 0,
         "RPC compact provider failure leaves session without a compaction entry");
}

void test_app_rpc_cancel_affects_subsequent_prompt()
{
  auto const root = temp_root() / "app-rpc-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC cancel test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  std::istringstream in(
      "{\"id\":\"cancel\",\"type\":\"cancel\"}\n"
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"prompt\",\"type\":\"prompt\",\"message\":\"should cancel\"}\n");
  std::ostringstream out;
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC cancel loop completes after canceled prompt response");
  expect(transport.requests().empty(), "RPC cancel flag prevents subsequent prompt provider request");
  expect(jsonl.find("\"id\":\"cancel\"") != std::string::npos &&
             jsonl.find("\"cancel_requested\":true") != std::string::npos &&
             jsonl.find("\"name\":\"cancel_requested\"") != std::string::npos &&
             jsonl.find("\"name\":\"canceled\"") != std::string::npos &&
             jsonl.find("\"id\":\"prompt\"") != std::string::npos &&
             jsonl.find("agent loop canceled") != std::string::npos &&
             jsonl.find("\"success\":false") != std::string::npos,
         "RPC cancel response updates state and canceled prompts return protocol errors");
}

void test_app_rpc_active_prompt_cancel_unblocks_pending_permission()
{
  auto const root = temp_root() / "app-rpc-active-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "rpc cancel note";
  }
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside cancel note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC active cancel test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string()))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read note\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC active cancel test observes pending permission request");
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC active cancel loop exits successfully");
  expect(
      jsonl.find("\"id\":\"cancel\"") != std::string::npos && jsonl.find("\"active_run\":true") != std::string::npos &&
          jsonl.find("\"name\":\"cancel_requested\"") != std::string::npos &&
          jsonl.find("\"name\":\"canceled\"") != std::string::npos &&
          jsonl.find("\"id\":\"p1\"") != std::string::npos && jsonl.find("agent loop canceled") != std::string::npos &&
          jsonl.find("\"success\":false") != std::string::npos,
      "RPC cancel is processed while prompt waits and prompt receives one canceled response");
}

void test_app_rpc_steer_applies_before_next_provider_request()
{
  auto const root = temp_root() / "app-rpc-steer";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside steer note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC steer test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(read_file_call_sse(outside_path.generic_string())), sse_response(final_text_sse("after steer"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read before steer\"}\n");
  bool const requested = output_buffer.wait_contains("\"resolver_request_id\":\"permission_", std::chrono::seconds(2));
  auto const resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(requested && !resolver_request_id.empty(), "RPC steer test observes permission wait safe point");
  input_buffer.push("{\"id\":\"s1\",\"type\":\"steer\",\"message\":\"steer this turn\"}\n");
  input_buffer.push("{\"id\":\"reply\",\"type\":\"permission_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"decision\":\"allow\"}\n");
  bool const completed = output_buffer.wait_contains("after steer", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value() && completed, "RPC steer loop exits successfully");
  expect(transport.requests().size() == 2 && transport.requests()[1].body.find("steer this turn") != std::string::npos,
         "RPC steer is appended before the next provider request after tool completion");
  expect(jsonl.find("\"name\":\"steer_queued\"") != std::string::npos &&
             jsonl.find("\"name\":\"steer_applied\"") != std::string::npos &&
             jsonl.find("\"id\":\"s1\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
             jsonl.find("after steer") != std::string::npos,
         "RPC steer emits queued/applied events and active prompt completes");
}

void test_app_rpc_follow_up_runs_after_active_prompt()
{
  auto const root = temp_root() / "app-rpc-follow-up";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside follow note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC follow_up test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())),
                                       sse_response(final_text_sse("first done")),
                                       sse_response(final_text_sse("follow done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"first prompt\"}\n");
  bool const requested = output_buffer.wait_contains("\"resolver_request_id\":\"permission_", std::chrono::seconds(2));
  auto const resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(requested && !resolver_request_id.empty(), "RPC follow_up test observes active prompt wait");
  input_buffer.push("{\"id\":\"fu1\",\"type\":\"follow_up\",\"message\":\"follow message\"}\n");
  input_buffer.push("{\"id\":\"reply\",\"type\":\"permission_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"decision\":\"allow\"}\n");
  bool const followed = output_buffer.wait_contains("follow done", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  auto const first_response = jsonl.find("\"id\":\"p1\"");
  auto const started = jsonl.find("\"name\":\"follow_up_started\"");
  auto const follow_response = jsonl.find("\"id\":\"fu1\"");
  expect(result.has_value() && followed, "RPC follow_up loop runs queued follow-up successfully");
  expect(transport.requests().size() == 3 && transport.requests()[2].body.find("follow message") != std::string::npos,
         "RPC follow_up starts a new provider turn with the queued message");
  expect(jsonl.find("\"name\":\"follow_up_queued\"") != std::string::npos && first_response != std::string::npos &&
             started != std::string::npos && follow_response != std::string::npos && first_response < started &&
             started < follow_response,
         "RPC follow_up emits queued/started events and responds after the active prompt response");
}

void test_app_rpc_prompt_start_failure_cleans_queued_messages()
{
  auto const root = temp_root() / "app-rpc-prompt-start-fail-queue";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto stored = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "expired-queued-access",
                                           .refresh_token = "queued-refresh",
                                           .expires_at = 100,
                                           .account_id = "acct_old",
                                           .source_path = {}});
  expect(stored.has_value(), "RPC prompt start failure test stores expired credential");

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC prompt start failure test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  BlockingResponseTransport transport(
      ava::provider::HttpResponse{.status_code = 400, .headers = {}, .body = "{\"error\":\"refresh failed\"}"});
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, {}, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"will fail before run\"}\n");
  bool const refresh_requested = transport.wait_for_request(std::chrono::seconds(2));
  expect(refresh_requested, "RPC prompt start failure test blocks during OAuth refresh");
  input_buffer.push("{\"id\":\"s1\",\"type\":\"steer\",\"message\":\"never apply\"}\n");
  input_buffer.push("{\"id\":\"fu1\",\"type\":\"follow_up\",\"message\":\"never run\"}\n");
  bool const queued = output_buffer.wait_contains("\"name\":\"follow_up_queued\"", std::chrono::seconds(2));
  expect(queued, "RPC prompt start failure test queues follow-up before startup failure");
  transport.release();
  bool const skipped = output_buffer.wait_contains("prompt_start_failed", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC prompt start failure queue cleanup loop exits successfully");
  expect(skipped && jsonl.find("\"name\":\"steer_skipped\"") != std::string::npos &&
             jsonl.find("\"name\":\"follow_up_skipped\"") != std::string::npos &&
             jsonl.find("\"request_id\":\"fu1\"") != std::string::npos &&
             jsonl.find("\"id\":\"fu1\"") != std::string::npos && jsonl.find("\"success\":false") != std::string::npos,
         "RPC prompt startup failure emits skipped queue events and a failed follow-up response");
  expect(transport.requests().size() == 1, "RPC prompt startup failure does not run queued follow-up provider calls");
}

void test_app_rpc_steer_after_follow_up_started_targets_follow_up()
{
  auto const root = temp_root() / "app-rpc-follow-up-steer";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside follow steer note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC follow-up steer test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())),
                                       sse_response(final_text_sse("first done")),
                                       sse_response(read_file_call_sse(outside_path.generic_string())),
                                       sse_response(final_text_sse("follow steered done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"first prompt\"}\n");
  bool const parent_requested =
      output_buffer.wait_contains("\"resolver_request_id\":\"permission_", std::chrono::seconds(2));
  auto const parent_resolver_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(parent_requested && !parent_resolver_id.empty(), "RPC follow-up steer test observes parent permission wait");
  input_buffer.push("{\"id\":\"fu1\",\"type\":\"follow_up\",\"message\":\"follow message\"}\n");
  input_buffer.push("{\"id\":\"reply1\",\"type\":\"permission_reply\",\"request_id\":\"" + parent_resolver_id +
                    "\",\"correlation_id\":\"p1\",\"decision\":\"allow\"}\n");
  bool const started = output_buffer.wait_contains("\"name\":\"follow_up_started\"", std::chrono::seconds(2));
  expect(started, "RPC follow-up steer test observes follow-up start event");
  input_buffer.push("{\"id\":\"sfu\",\"type\":\"steer\",\"message\":\"steer follow turn\"}\n");
  bool const follow_requested = output_buffer.wait_contains(
      "\"correlation_id\":\"fu1\",\"name\":\"permission_requested\"", std::chrono::seconds(2));
  auto const follow_resolver_id = extract_last_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(follow_requested && !follow_resolver_id.empty(),
         "RPC follow-up steer test observes follow-up permission wait");
  input_buffer.push("{\"id\":\"reply2\",\"type\":\"permission_reply\",\"request_id\":\"" + follow_resolver_id +
                    "\",\"correlation_id\":\"fu1\",\"decision\":\"allow\"}\n");
  bool const completed = output_buffer.wait_contains("follow steered done", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value() && completed, "RPC follow-up steer loop exits successfully");
  expect(
      transport.requests().size() == 4 && transport.requests()[3].body.find("steer follow turn") != std::string::npos,
      "RPC steer after follow_up_started is appended to the follow-up continuation request");
  expect(jsonl.find("\"request_id\":\"fu1\",\"correlation_id\":\"fu1\",\"name\":\"follow_up_started\"") !=
                 std::string::npos &&
             jsonl.find("\"id\":\"sfu\"") != std::string::npos &&
             jsonl.find("\"correlation_id\":\"fu1\"") != std::string::npos,
         "RPC follow_up_started and subsequent steer use follow-up correlation");
}

void test_app_rpc_queue_limit_rejects_new_items()
{
  auto const root = temp_root() / "app-rpc-queue-limit";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside queue limit note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC queue limit test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string()))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read before limit\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC queue limit test observes active permission wait");
  for (int index = 0; index < 65; ++index) {
    input_buffer.push("{\"id\":\"s" + std::to_string(index) + "\",\"type\":\"steer\",\"message\":\"queued steer\"}\n");
  }
  bool const rejected = output_buffer.wait_contains("RPC queued message limit exceeded", std::chrono::seconds(2));
  expect(rejected, "RPC queue limit test observes capped steer rejection");
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC queue limit loop exits successfully");
  expect(count_substrings(jsonl, "\"name\":\"steer_queued\"") == 64 &&
             jsonl.find("\"id\":\"s64\"") != std::string::npos &&
             jsonl.find("RPC queued message limit exceeded") != std::string::npos &&
             jsonl.find("\"cleared_steer\":64") != std::string::npos,
         "RPC queue limit accepts bounded entries, rejects the next steer, and cancel clears the bounded queue");
}

void test_app_rpc_eof_clears_queued_follow_up_without_running()
{
  auto const root = temp_root() / "app-rpc-eof-clears-follow-up";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside eof note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC EOF queue cleanup test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())),
                                       sse_response(final_text_sse("would only run if not canceled"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read before eof\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC EOF queue cleanup test observes active permission wait");
  input_buffer.push("{\"id\":\"fu1\",\"type\":\"follow_up\",\"message\":\"must not run\"}\n");
  bool const queued = output_buffer.wait_contains("\"name\":\"follow_up_queued\"", std::chrono::seconds(2));
  expect(queued, "RPC EOF queue cleanup test observes queued follow-up");
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC EOF queue cleanup loop exits successfully");
  expect(transport.requests().size() == 1,
         "RPC EOF cancels active prompt and prevents queued follow-up provider calls");
  expect(jsonl.find("\"name\":\"follow_up_skipped\"") != std::string::npos &&
             jsonl.find("\"reason\":\"canceled\"") != std::string::npos &&
             jsonl.find("\"id\":\"fu1\"") != std::string::npos &&
             jsonl.find("agent loop canceled") != std::string::npos,
         "RPC EOF clears queued follow-up and sends its canceled response");
}

void test_app_rpc_cancel_clears_queued_steer_and_follow_up()
{
  auto const root = temp_root() / "app-rpc-queue-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside cancel queue note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC queued cancel test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string()))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read before cancel\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC queued cancel test observes active permission wait");
  input_buffer.push("{\"id\":\"s1\",\"type\":\"steer\",\"message\":\"never apply\"}\n");
  input_buffer.push("{\"id\":\"fu1\",\"type\":\"follow_up\",\"message\":\"never run\"}\n");
  bool const queued = output_buffer.wait_contains("\"name\":\"follow_up_queued\"", std::chrono::seconds(2));
  expect(queued, "RPC queued cancel test observes queued follow-up");
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC queued cancel loop exits successfully");
  expect(transport.requests().size() == 1, "RPC cancel prevents queued steer/follow-up provider requests");
  expect(jsonl.find("\"name\":\"steer_skipped\"") != std::string::npos &&
             jsonl.find("\"name\":\"follow_up_skipped\"") != std::string::npos &&
             jsonl.find("\"cleared_steer\":1") != std::string::npos &&
             jsonl.find("\"cleared_follow_up\":1") != std::string::npos &&
             jsonl.find("\"id\":\"fu1\"") != std::string::npos &&
             jsonl.find("agent loop canceled") != std::string::npos,
         "RPC cancel clears queued steer/follow-up items and reports skipped outcomes");
}

void test_app_rpc_active_prompt_rejects_second_prompt_and_session_switch()
{
  auto const root = temp_root() / "app-rpc-active-rejects";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "rpc active reject note";
  }
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside reject note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  auto other = ava::app::open_runtime_session(open_options);
  expect(session.has_value() && other.has_value(), "RPC active reject test opens runtime sessions");
  if (!session || !other) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string()))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read note\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC active reject test observes pending permission request");
  input_buffer.push("{\"id\":\"p2\",\"type\":\"prompt\",\"message\":\"second\"}\n");
  input_buffer.push("{\"id\":\"new\",\"type\":\"new_session\"}\n");
  input_buffer.push("{\"id\":\"switch\",\"type\":\"switch_session\",\"session_id\":\"" + other->store.session_id() +
                    "\"}\n");
  input_buffer.push("{\"id\":\"messages\",\"type\":\"get_messages\"}\n");
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC active reject loop exits successfully");
  expect(jsonl.find("\"id\":\"p2\"") != std::string::npos && jsonl.find("\"id\":\"new\"") != std::string::npos &&
             jsonl.find("\"id\":\"switch\"") != std::string::npos &&
             jsonl.find("\"id\":\"messages\"") != std::string::npos &&
             jsonl.find("RPC command is unavailable while a prompt is active") != std::string::npos,
         "RPC rejects active-run mutations and session materialization queries");
}

void test_app_rpc_permission_policy_auto_allows_before_resolver_event()
{
  auto const root = temp_root() / "app-rpc-policy-auto-allow";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside permission note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC permission policy auto-allow test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())),
                                       sse_response(final_text_sse("after policy allow"))});
  ava::app::HeadlessPermissionPolicyOptions policy_options;
  auto read_only_added = ava::app::add_headless_allow_policy(policy_options, "read-only");
  expect(read_only_added.has_value(), "RPC permission policy test configures read-only allow");
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  runtime_options.permission_resolver = ava::app::build_headless_permission_resolver(policy_options);
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read outside\"}\n");
  bool const completed = output_buffer.wait_contains("after policy allow", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value() && completed, "RPC permission policy auto-allow prompt completes");
  expect(jsonl.find("permission_requested") == std::string::npos,
         "RPC permission policy auto-allows matching read prompt before resolver event");
}

void test_app_rpc_permission_reply_allow_and_deny_flows()
{
  for (std::string_view decision : {"allow", "deny"}) {
    auto const decision_text = std::string(decision);
    auto const root = temp_root() / ("app-rpc-permission-" + decision_text);
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    auto const paths = app_test_paths(root);
    std::filesystem::create_directories(workspace);
    {
      std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
      file << "rpc permission note";
    }
    auto const outside_path = root / "outside.txt";
    {
      std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
      file << "outside permission note";
    }

    ava::app::RuntimeOpenOptions open_options;
    open_options.workspace_dir = workspace;
    open_options.current_dir = workspace;
    open_options.paths = paths;
    auto session = ava::app::open_runtime_session(open_options);
    expect(session.has_value(), "RPC permission reply test opens runtime session");
    if (!session) return;

    ava::provider::OpenAIProvider const provider("https://api.example.test");
    ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())),
                                         sse_response(final_text_sse("after " + decision_text))});
    ava::app::RuntimeRunOptions runtime_options;
    runtime_options.access_token = "token";
    BlockingInputBuf input_buffer;
    std::istream in(&input_buffer);
    ThreadSafeStringBuf output_buffer;
    std::ostream out(&output_buffer);
    ava::core::VoidResult result;
    std::jthread rpc_thread([&] {
      result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
    });

    input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read note\"}\n");
    bool const requested =
        output_buffer.wait_contains("\"resolver_request_id\":\"permission_", std::chrono::seconds(2));
    auto const resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
    expect(requested && !resolver_request_id.empty(), "RPC permission reply test observes resolver request id");
    auto const reply_reason_json =
        decision == "deny" ? std::string(",\"reason\":\"not approved for this run\"") : std::string();
    input_buffer.push("{\"id\":\"reply\",\"type\":\"permission_reply\",\"request_id\":\"" + resolver_request_id +
                      "\",\"correlation_id\":\"p1\",\"decision\":\"" + decision_text + "\"" + reply_reason_json +
                      "}\n");
    bool const completed = output_buffer.wait_contains("after " + decision_text, std::chrono::seconds(2));
    input_buffer.close();
    rpc_thread.join();

    auto const jsonl = output_buffer.str();
    expect(result.has_value() && completed, "RPC permission reply loop exits successfully");
    expect(jsonl.find("\"id\":\"reply\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
               jsonl.find("after " + decision_text) != std::string::npos &&
               jsonl.find("\"name\":\"permission_replied\"") != std::string::npos &&
               jsonl.find("\"decision\":\"" + decision_text + "\"") != std::string::npos,
           "RPC permission " + decision_text + " reply emits a reply event and unblocks the run");
    if (decision == "deny") {
      expect(jsonl.find("\"reason\":\"not approved for this run\"") != std::string::npos,
             "RPC permission deny reply preserves the client resolution reason in the event stream");
      auto entries = session->store.load();
      auto audits = entries ? permission_entries(*entries) : std::vector<ava::session::SessionEntry>{};
      expect(audits.size() >= 2 && ava::core::json::string_field(audits.back().data_json, "resolution") == "deny" &&
                 ava::core::json::string_field(audits.back().data_json, "resolution_source") == "resolver" &&
                 ava::core::json::string_field(audits.back().data_json, "resolution_reason") ==
                     "not approved for this run",
             "RPC permission deny reply preserves the client resolution reason in durable audit entries");
    }
  }
}

void test_app_rpc_permission_reply_session_grant_flow()
{
  auto const root = temp_root() / "app-rpc-permission-session-grant";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside grant note";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC permission session grant test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())),
                                       sse_response(final_text_sse("first grant done")),
                                       sse_response(read_file_call_sse(outside_path.generic_string())),
                                       sse_response(final_text_sse("second grant done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read outside once\"}\n");
  bool const requested = output_buffer.wait_contains("\"resolver_request_id\":\"permission_", std::chrono::seconds(2));
  auto const resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(requested && !resolver_request_id.empty(), "RPC permission session grant test observes resolver request id");
  input_buffer.push("{\"id\":\"reply\",\"type\":\"permission_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"decision\":\"allow_session\"}\n");
  input_buffer.push("{\"id\":\"p2\",\"type\":\"follow_up\",\"message\":\"read outside again\"}\n");
  bool const first_completed = output_buffer.wait_contains("first grant done", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"grants\",\"type\":\"permission_grants\"}\n");
  bool const grant_listed = output_buffer.wait_contains("\"grant_id\":\"permgrant_", std::chrono::seconds(2));
  bool const second_completed = output_buffer.wait_contains("second grant done", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value() && first_completed && grant_listed && second_completed,
         "RPC permission session grant loop exits successfully");
  expect(count_substrings(jsonl, "\"name\":\"permission_requested\"") == 1 &&
             jsonl.find("\"decision\":\"allow_session\"") != std::string::npos &&
             jsonl.find("\"id\":\"grants\"") != std::string::npos &&
             jsonl.find("\"operation\":\"read\"") != std::string::npos &&
             jsonl.find("\"target_path\":\"" + outside_path.string() + "\"") != std::string::npos,
         "RPC session grant is inspectable and suppresses a repeated matching permission prompt");

  auto entries = session->store.load();
  auto audits = entries ? permission_entries(*entries) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 4 &&
             ava::core::json::string_field(audits[3].data_json, "resolution_source") == "session_grant" &&
             ava::core::json::string_field(audits[3].data_json, "resolution") == "allow",
         "session grant approvals are still audited as backend permission outcomes");
}

void test_app_rpc_permission_request_includes_mutation_diff_preview()
{
  auto const root = temp_root() / "app-rpc-permission-diff";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const outside_path = root / "outside-created.txt";

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC permission diff test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(write_file_call_sse(outside_path.generic_string(), "rpc new\n")),
                                       sse_response(final_text_sse("after diff deny"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"write outside\"}\n");
  bool const requested = output_buffer.wait_contains("\"diff_preview\"", std::chrono::seconds(2));
  auto const resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  auto const permission_request_id = extract_json_string_field(output_buffer.str(), "permission_request_id");
  expect(requested && !resolver_request_id.empty() && permission_request_id.starts_with("permreq_"),
         "RPC permission diff test observes mutation diff preview with stable permission request id");
  input_buffer.push("{\"id\":\"reply\",\"type\":\"permission_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"decision\":\"deny\"}\n");
  bool const completed = output_buffer.wait_contains("after diff deny", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value() && completed && !std::filesystem::exists(outside_path),
         "RPC permission diff test completes after denied mutation without writing");
  expect(jsonl.find("\"name\":\"permission_requested\"") != std::string::npos &&
             jsonl.find("\"permission_request_id\":\"" + permission_request_id + "\"") != std::string::npos &&
             jsonl.find("\"diff_preview\"") != std::string::npos && jsonl.find("+rpc new") != std::string::npos &&
             jsonl.find("\"diff_truncated\":false") != std::string::npos &&
             jsonl.find("\"name\":\"permission_replied\"") != std::string::npos &&
             jsonl.find("\"decision\":\"deny\"") != std::string::npos,
         "RPC permission request payload includes backend-provided unified diff preview and reply event");
}

void test_app_rpc_question_reply_flow()
{
  auto const root = temp_root() / "app-rpc-question";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC question reply test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(question_call_sse()), sse_response(final_text_sse("question done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"ask question\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"question_requested\"", std::chrono::seconds(2));
  auto const resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(requested && !resolver_request_id.empty(), "RPC question reply test observes question request event");
  input_buffer.push("{\"id\":\"reply\",\"type\":\"question_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"answer\":\"custom ok\"}\n");
  bool const completed = output_buffer.wait_contains("question done", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value() && completed, "RPC question reply loop exits successfully");
  expect(jsonl.find("\"id\":\"reply\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
             jsonl.find("question done") != std::string::npos &&
             jsonl.find("\"name\":\"question_replied\"") != std::string::npos &&
             jsonl.find("\"answer\":\"custom ok\"") != std::string::npos,
         "RPC question reply emits a reply event and unblocks question tool");
}

void test_app_rpc_question_reply_selected_option_flow()
{
  auto const root = temp_root() / "app-rpc-question-selected";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC selected question reply test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(question_call_sse()), sse_response(final_text_sse("selected question done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"ask question\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"question_requested\"", std::chrono::seconds(2));
  auto const resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(requested && !resolver_request_id.empty(), "RPC selected question reply observes request event");
  input_buffer.push("{\"id\":\"bad\",\"type\":\"question_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"selected\":\"no\"}\n");
  bool const rejected =
      output_buffer.wait_contains("question_reply selected option is not valid", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"reply\",\"type\":\"question_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"selected\":\"yes\"}\n");
  bool const completed = output_buffer.wait_contains("selected question done", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value() && rejected && completed, "RPC selected question reply loop exits successfully");
  expect(jsonl.find("\"id\":\"bad\"") != std::string::npos &&
             jsonl.find("question_reply selected option is not valid") != std::string::npos,
         "RPC selected question reply rejects invalid selected option without resolving request");
  expect(jsonl.find("\"id\":\"reply\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
             jsonl.find("selected question done") != std::string::npos &&
             jsonl.find("\"name\":\"question_replied\"") != std::string::npos &&
             jsonl.find("\"selected\":\"yes\"") != std::string::npos,
         "RPC selected question reply emits a reply event and unblocks question tool");
}

void test_app_rpc_question_reply_selected_options_flow()
{
  auto const root = temp_root() / "app-rpc-question-selected-options";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC multi question reply test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(multi_question_call_sse()), sse_response(final_text_sse("multi question done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"ask multi question\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"question_requested\"", std::chrono::seconds(2));
  auto const resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(requested && !resolver_request_id.empty(), "RPC multi question reply observes request event");
  input_buffer.push("{\"id\":\"bad\",\"type\":\"question_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"selected_options\":[\"alpha\",\"alpha\"]}\n");
  bool const duplicate_rejected =
      output_buffer.wait_contains("question_reply selected_options contains duplicate values", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"reply\",\"type\":\"question_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"selected_options\":[\"alpha\",\"beta\"],"
                    "\"answer\":\"Use both\"}\n");
  bool const completed = output_buffer.wait_contains("multi question done", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value() && duplicate_rejected && completed, "RPC multi question reply loop exits successfully");
  expect(jsonl.find("\"multiple\":true") != std::string::npos,
         "RPC multi question reply request preserves multi-select metadata");
  expect(jsonl.find("\"id\":\"bad\"") != std::string::npos &&
             jsonl.find("question_reply selected_options contains duplicate values") != std::string::npos,
         "RPC multi question reply rejects duplicate selected options without resolving request");
  expect(jsonl.find("\"id\":\"reply\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
             jsonl.find("multi question done") != std::string::npos &&
             jsonl.find("\"name\":\"question_replied\"") != std::string::npos &&
             jsonl.find("\"selected_options\":[\"alpha\",\"beta\"]") != std::string::npos &&
             jsonl.find("\"answer\":\"Use both\"") != std::string::npos,
         "RPC multi question reply emits selections, custom text, and unblocks question tool");
}

}  // namespace

void run_app_rpc_tests()
{
  test_app_rpc_prompt_payload_serialization();
  test_app_rpc_parsing_and_response_serialization();
  test_app_rpc_identifier_validation();
  test_app_rpc_prompt_with_fake_transport_streams_events();
  test_app_rpc_prompt_streams_provider_deltas_before_final_response();
  test_app_rpc_prompt_refreshes_expired_oauth_before_provider_request();
  test_app_rpc_malformed_line_recovery_and_unknown_command();
  test_app_rpc_state_list_sessions_and_open_session();
  test_app_rpc_model_commands();
  test_app_rpc_reasoning_commands();
  test_app_rpc_protocol_version_and_session_commands();
  test_app_rpc_protocol_version_and_resolver_reply_errors();
  test_app_rpc_mcp_command_responses();
  test_app_rpc_command_responses_for_context_compact_export();
  test_app_rpc_compact_provider_failure_is_error_response();
  test_app_rpc_cancel_affects_subsequent_prompt();
  test_app_rpc_active_prompt_cancel_unblocks_pending_permission();
  test_app_rpc_steer_applies_before_next_provider_request();
  test_app_rpc_follow_up_runs_after_active_prompt();
  test_app_rpc_prompt_start_failure_cleans_queued_messages();
  test_app_rpc_steer_after_follow_up_started_targets_follow_up();
  test_app_rpc_queue_limit_rejects_new_items();
  test_app_rpc_eof_clears_queued_follow_up_without_running();
  test_app_rpc_cancel_clears_queued_steer_and_follow_up();
  test_app_rpc_active_prompt_rejects_second_prompt_and_session_switch();
  test_app_rpc_permission_policy_auto_allows_before_resolver_event();
  test_app_rpc_permission_reply_allow_and_deny_flows();
  test_app_rpc_permission_reply_session_grant_flow();
  test_app_rpc_permission_request_includes_mutation_diff_preview();
  test_app_rpc_question_reply_flow();
  test_app_rpc_question_reply_selected_option_flow();
  test_app_rpc_question_reply_selected_options_flow();
}
