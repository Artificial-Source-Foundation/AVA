#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"

#include "ava/config/auth.h"

#include "ava/provider/openai_provider.h"

#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace {

using namespace ava::tests;

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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(final_text_sse("prompt after cancel"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  bool const canceled = output_buffer.wait_contains("\"id\":\"cancel\"", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"state\",\"type\":\"get_state\"}\n");
  bool const state_reported_cancel = output_buffer.wait_contains("\"cancel_requested\":true", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"prompt\",\"type\":\"prompt\",\"message\":\"should run\"}\n");
  bool const completed = output_buffer.wait_contains("prompt after cancel", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC cancel loop completes after prompt response");
  expect(transport.requests().size() == 1, "RPC stale cancel flag is cleared when a subsequent prompt starts");
  expect(jsonl.find("\"id\":\"cancel\"") != std::string::npos && jsonl.find("\"cancel_requested\":true") != std::string::npos &&
             jsonl.find("\"name\":\"cancel_requested\"") != std::string::npos &&
             jsonl.find("\"payload_type\":\"cancellation\"") != std::string::npos && canceled && state_reported_cancel && completed &&
             jsonl.find("\"id\":\"prompt\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos,
         "RPC idle cancel updates state until the next accepted prompt clears it and runs normally");
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string()))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read note\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC active cancel test observes pending permission request");
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC active cancel loop exits successfully");
  expect(jsonl.find("\"id\":\"cancel\"") != std::string::npos && jsonl.find("\"active_run\":true") != std::string::npos &&
             jsonl.find("\"name\":\"cancel_requested\"") != std::string::npos && jsonl.find("\"name\":\"canceled\"") != std::string::npos &&
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())), sse_response(final_text_sse("after steer"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

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
  expect(jsonl.find("\"name\":\"steer_queued\"") != std::string::npos && jsonl.find("\"name\":\"steer_applied\"") != std::string::npos &&
             jsonl.find("\"payload_type\":\"queue\"") != std::string::npos && jsonl.find("\"id\":\"s1\"") != std::string::npos &&
             jsonl.find("\"success\":true") != std::string::npos && jsonl.find("after steer") != std::string::npos,
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())), sse_response(final_text_sse("first done")),
                                       sse_response(final_text_sse("follow done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

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
  expect(jsonl.find("\"name\":\"follow_up_queued\"") != std::string::npos && first_response != std::string::npos && started != std::string::npos &&
             follow_response != std::string::npos && first_response < started && started < follow_response,
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
  auto stored = ava::config::store_openai_credential(paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  BlockingResponseTransport transport(ava::provider::HttpResponse{.status_code = 400, .headers = {}, .body = "{\"error\":\"refresh failed\"}"});
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, {}, in, out); });

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
  expect(skipped && jsonl.find("\"name\":\"steer_skipped\"") != std::string::npos && jsonl.find("\"name\":\"follow_up_skipped\"") != std::string::npos &&
             jsonl.find("\"request_id\":\"fu1\"") != std::string::npos && jsonl.find("\"id\":\"fu1\"") != std::string::npos &&
             jsonl.find("\"success\":false") != std::string::npos,
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())), sse_response(final_text_sse("first done")),
                                       sse_response(read_file_call_sse(outside_path.generic_string())), sse_response(final_text_sse("follow steered done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"first prompt\"}\n");
  bool const parent_requested = output_buffer.wait_contains("\"resolver_request_id\":\"permission_", std::chrono::seconds(2));
  auto const parent_resolver_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(parent_requested && !parent_resolver_id.empty(), "RPC follow-up steer test observes parent permission wait");
  input_buffer.push("{\"id\":\"fu1\",\"type\":\"follow_up\",\"message\":\"follow message\"}\n");
  input_buffer.push("{\"id\":\"reply1\",\"type\":\"permission_reply\",\"request_id\":\"" + parent_resolver_id +
                    "\",\"correlation_id\":\"p1\",\"decision\":\"allow\"}\n");
  bool const started = output_buffer.wait_contains("\"name\":\"follow_up_started\"", std::chrono::seconds(2));
  expect(started, "RPC follow-up steer test observes follow-up start event");
  input_buffer.push("{\"id\":\"sfu\",\"type\":\"steer\",\"message\":\"steer follow turn\"}\n");
  bool const follow_requested = output_buffer.wait_contains("\"correlation_id\":\"fu1\",\"name\":\"permission_requested\"", std::chrono::seconds(2));
  auto const follow_resolver_id = extract_last_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(follow_requested && !follow_resolver_id.empty(), "RPC follow-up steer test observes follow-up permission wait");
  input_buffer.push("{\"id\":\"reply2\",\"type\":\"permission_reply\",\"request_id\":\"" + follow_resolver_id +
                    "\",\"correlation_id\":\"fu1\",\"decision\":\"allow\"}\n");
  bool const completed = output_buffer.wait_contains("follow steered done", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value() && completed, "RPC follow-up steer loop exits successfully");
  expect(transport.requests().size() == 4 && transport.requests()[3].body.find("steer follow turn") != std::string::npos,
         "RPC steer after follow_up_started is appended to the follow-up continuation request");
  expect(jsonl.find("\"request_id\":\"fu1\",\"correlation_id\":\"fu1\",\"name\":\"follow_up_started\"") != std::string::npos &&
             jsonl.find("\"id\":\"sfu\"") != std::string::npos && jsonl.find("\"correlation_id\":\"fu1\"") != std::string::npos,
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string()))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read before limit\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC queue limit test observes active permission wait");
  for (int index = 0; index < 65; ++index)
  {
    input_buffer.push("{\"id\":\"s" + std::to_string(index) + "\",\"type\":\"steer\",\"message\":\"queued steer\"}\n");
  }
  bool const rejected = output_buffer.wait_contains("RPC queued message limit exceeded", std::chrono::seconds(2));
  expect(rejected, "RPC queue limit test observes capped steer rejection");
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC queue limit loop exits successfully");
  expect(count_substrings(jsonl, "\"name\":\"steer_queued\"") == 64 && jsonl.find("\"id\":\"s64\"") != std::string::npos &&
             jsonl.find("RPC queued message limit exceeded") != std::string::npos && jsonl.find("\"cleared_steer\":64") != std::string::npos,
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(read_file_call_sse(outside_path.generic_string())), sse_response(final_text_sse("would only run if not canceled"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

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
  expect(transport.requests().size() == 1, "RPC EOF cancels active prompt and prevents queued follow-up provider calls");
  expect(jsonl.find("\"name\":\"follow_up_skipped\"") != std::string::npos && jsonl.find("\"reason\":\"canceled\"") != std::string::npos &&
             jsonl.find("\"id\":\"fu1\"") != std::string::npos && jsonl.find("agent loop canceled") != std::string::npos,
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string()))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read before cancel\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC queued cancel test observes active permission wait");
  input_buffer.push("{\"id\":\"s1\",\"type\":\"steer\",\"message\":\"never apply\"}\n");
  input_buffer.push("{\"id\":\"s2\",\"type\":\"steer\",\"message\":\"also never apply\"}\n");
  input_buffer.push("{\"id\":\"fu1\",\"type\":\"follow_up\",\"message\":\"never run\"}\n");
  input_buffer.push("{\"id\":\"fu2\",\"type\":\"follow_up\",\"message\":\"also never run\"}\n");
  bool const queued = output_buffer.wait_contains("\"request_id\":\"fu2\"", std::chrono::seconds(2));
  expect(queued, "RPC queued cancel test observes all queued follow-ups before cancellation");
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC queued cancel loop exits successfully");
  expect(transport.requests().size() == 1, "RPC cancel prevents queued steer/follow-up provider requests");
  expect(count_substrings(jsonl, "\"name\":\"steer_queued\"") == 2 && count_substrings(jsonl, "\"name\":\"follow_up_queued\"") == 2 &&
             count_substrings(jsonl, "\"name\":\"steer_skipped\"") == 2 && count_substrings(jsonl, "\"name\":\"follow_up_skipped\"") == 2,
         "RPC active cancel emits one queued and skipped event per queued steer/follow-up");
  expect(
      jsonl.find("\"request_id\":\"cancel\",\"correlation_id\":\"p1\",\"name\":\"cancel_requested\"") != std::string::npos &&
          jsonl.find(
              "\"payload_type\":\"cancellation\",\"payload\":{\"active_run\":true,\"cleared_steer\":2,\"cleared_follow_up\":2,\"active_request_id\":\"p1\"}") !=
              std::string::npos &&
          jsonl.find("\"id\":\"cancel\",\"type\":\"response\",\"success\":true,\"result\":{\"cancel_requested\":true,\"active_run\":true,\"cleared_steer\":2,"
                     "\"cleared_follow_up\":2}}") != std::string::npos,
      "RPC active cancel reports stable active-run cancellation payload counters and active request id");
  expect(count_substrings(jsonl, "\"id\":\"fu1\",\"type\":\"response\",\"success\":false") == 1 &&
             count_substrings(jsonl, "\"id\":\"fu2\",\"type\":\"response\",\"success\":false") == 1 && count_substrings(jsonl, "agent loop canceled") >= 3 &&
             jsonl.find("\"name\":\"follow_up_started\"") == std::string::npos && jsonl.find("\"name\":\"steer_applied\"") == std::string::npos,
         "RPC active cancel clears queued follow-up errors without starting queued turns or applying steer");
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
  if (!session || !other)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string()))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read note\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  expect(requested, "RPC active reject test observes pending permission request");
  input_buffer.push("{\"id\":\"p2\",\"type\":\"prompt\",\"message\":\"second\"}\n");
  input_buffer.push("{\"id\":\"new\",\"type\":\"new_session\"}\n");
  input_buffer.push("{\"id\":\"switch\",\"type\":\"switch_session\",\"session_id\":\"" + other->store.session_id() + "\"}\n");
  input_buffer.push("{\"id\":\"messages\",\"type\":\"get_messages\"}\n");
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC active reject loop exits successfully");
  expect(jsonl.find("\"id\":\"p2\"") != std::string::npos && jsonl.find("\"id\":\"new\"") != std::string::npos &&
             jsonl.find("\"id\":\"switch\"") != std::string::npos && jsonl.find("\"id\":\"messages\"") != std::string::npos &&
             jsonl.find("RPC command is unavailable while a prompt is active") != std::string::npos,
         "RPC rejects active-run mutations and session materialization queries");
}

}  // namespace

void run_app_rpc_queue_tests()
{
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
}
