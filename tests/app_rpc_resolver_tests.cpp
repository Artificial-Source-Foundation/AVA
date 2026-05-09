#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/app/headless_policy.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/session/session_store.h"
#include "ava/permissions/permission.h"
#include "ava/provider/openai_provider.h"
#include "ava/core/json.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace ava::tests;

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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())), sse_response(final_text_sse("after policy allow"))});
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
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read outside\"}\n");
  bool const completed = output_buffer.wait_contains("after policy allow", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value() && completed, "RPC permission policy auto-allow prompt completes");
  expect(jsonl.find("permission_requested") == std::string::npos, "RPC permission policy auto-allows matching read prompt before resolver event");
}

void test_app_rpc_permission_reply_allow_and_deny_flows()
{
  for (std::string_view decision : {"allow", "deny"})
  {
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
    if (!session)
      return;

    ava::provider::OpenAIProvider const provider("https://api.example.test");
    ava::tests::FakeTransport transport(
        {sse_response(read_file_call_sse(outside_path.generic_string())), sse_response(final_text_sse("after " + decision_text))});
    ava::app::RuntimeRunOptions runtime_options;
    runtime_options.access_token = "token";
    BlockingInputBuf input_buffer;
    std::istream in(&input_buffer);
    ThreadSafeStringBuf output_buffer;
    std::ostream out(&output_buffer);
    ava::core::VoidResult result;
    std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

    input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"read note\"}\n");
    bool const requested = output_buffer.wait_contains("\"resolver_request_id\":\"permission_", std::chrono::seconds(2));
    auto const resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
    expect(requested && !resolver_request_id.empty(), "RPC permission reply test observes resolver request id");
    auto const reply_reason_json = decision == "deny" ? std::string(",\"reason\":\"not approved for this run\"") : std::string();
    input_buffer.push("{\"id\":\"reply\",\"type\":\"permission_reply\",\"request_id\":\"" + resolver_request_id +
                      "\",\"correlation_id\":\"p1\",\"decision\":\"" + decision_text + "\"" + reply_reason_json + "}\n");
    bool const completed = output_buffer.wait_contains("after " + decision_text, std::chrono::seconds(2));
    input_buffer.close();
    rpc_thread.join();

    auto const jsonl = output_buffer.str();
    expect(result.has_value() && completed, "RPC permission reply loop exits successfully");
    expect(jsonl.find("\"id\":\"reply\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
               jsonl.find("after " + decision_text) != std::string::npos && jsonl.find("\"name\":\"permission_replied\"") != std::string::npos &&
               jsonl.find("\"decision\":\"" + decision_text + "\"") != std::string::npos,
           "RPC permission " + decision_text + " reply emits a reply event and unblocks the run");
    if (decision == "deny")
    {
      expect(jsonl.find("\"reason\":\"not approved for this run\"") != std::string::npos,
             "RPC permission deny reply preserves the client resolution reason in the event stream");
      auto entries = session->store.load();
      auto audits = entries ? permission_entries(*entries) : std::vector<ava::session::SessionEntry>{};
      expect(audits.size() >= 2 && ava::core::json::string_field(audits.back().data_json, "resolution") == "deny" &&
                 ava::core::json::string_field(audits.back().data_json, "resolution_source") == "resolver" &&
                 ava::core::json::string_field(audits.back().data_json, "resolution_reason") == "not approved for this run",
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())), sse_response(final_text_sse("first grant done")),
                                       sse_response(read_file_call_sse(outside_path.generic_string())), sse_response(final_text_sse("second grant done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

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
  expect(result.has_value() && first_completed && grant_listed && second_completed, "RPC permission session grant loop exits successfully");
  expect(count_substrings(jsonl, "\"name\":\"permission_requested\"") == 1 && jsonl.find("\"decision\":\"allow_session\"") != std::string::npos &&
             jsonl.find("\"id\":\"grants\"") != std::string::npos && jsonl.find("\"operation\":\"read\"") != std::string::npos &&
             jsonl.find("\"target_path\":\"" + outside_path.string() + "\"") != std::string::npos,
         "RPC session grant is inspectable and suppresses a repeated matching permission prompt");

  auto entries = session->store.load();
  auto audits = entries ? permission_entries(*entries) : std::vector<ava::session::SessionEntry>{};
  expect(audits.size() == 4 && ava::core::json::string_field(audits[3].data_json, "resolution_source") == "session_grant" &&
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(write_file_call_sse(outside_path.generic_string(), "rpc new\n")), sse_response(final_text_sse("after diff deny"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

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
  expect(result.has_value() && completed && !std::filesystem::exists(outside_path), "RPC permission diff test completes after denied mutation without writing");
  expect(jsonl.find("\"name\":\"permission_requested\"") != std::string::npos &&
             jsonl.find("\"permission_request_id\":\"" + permission_request_id + "\"") != std::string::npos &&
             jsonl.find("\"diff_preview\"") != std::string::npos && jsonl.find("+rpc new") != std::string::npos &&
             jsonl.find("\"diff_truncated\":false") != std::string::npos && jsonl.find("\"name\":\"permission_replied\"") != std::string::npos &&
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(question_call_sse()), sse_response(final_text_sse("question done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

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
             jsonl.find("question done") != std::string::npos && jsonl.find("\"name\":\"question_replied\"") != std::string::npos &&
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(question_call_sse()), sse_response(final_text_sse("selected question done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"ask question\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"question_requested\"", std::chrono::seconds(2));
  auto const resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(requested && !resolver_request_id.empty(), "RPC selected question reply observes request event");
  input_buffer.push("{\"id\":\"bad\",\"type\":\"question_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"selected\":\"no\"}\n");
  bool const rejected = output_buffer.wait_contains("question_reply selected option is not valid", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"reply\",\"type\":\"question_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"selected\":\"yes\"}\n");
  bool const completed = output_buffer.wait_contains("selected question done", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value() && rejected && completed, "RPC selected question reply loop exits successfully");
  expect(jsonl.find("\"id\":\"bad\"") != std::string::npos && jsonl.find("question_reply selected option is not valid") != std::string::npos,
         "RPC selected question reply rejects invalid selected option without resolving request");
  expect(jsonl.find("\"id\":\"reply\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
             jsonl.find("selected question done") != std::string::npos && jsonl.find("\"name\":\"question_replied\"") != std::string::npos &&
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
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(multi_question_call_sse()), sse_response(final_text_sse("multi question done"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });

  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"ask multi question\"}\n");
  bool const requested = output_buffer.wait_contains("\"name\":\"question_requested\"", std::chrono::seconds(2));
  auto const resolver_request_id = extract_json_string_field(output_buffer.str(), "resolver_request_id");
  expect(requested && !resolver_request_id.empty(), "RPC multi question reply observes request event");
  input_buffer.push("{\"id\":\"bad\",\"type\":\"question_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"selected_options\":[\"alpha\",\"alpha\"]}\n");
  bool const duplicate_rejected = output_buffer.wait_contains("question_reply selected_options contains duplicate values", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"reply\",\"type\":\"question_reply\",\"request_id\":\"" + resolver_request_id +
                    "\",\"correlation_id\":\"p1\",\"selected_options\":[\"alpha\",\"beta\"],"
                    "\"answer\":\"Use both\"}\n");
  bool const completed = output_buffer.wait_contains("multi question done", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value() && duplicate_rejected && completed, "RPC multi question reply loop exits successfully");
  expect(jsonl.find("\"multiple\":true") != std::string::npos, "RPC multi question reply request preserves multi-select metadata");
  expect(jsonl.find("\"id\":\"bad\"") != std::string::npos && jsonl.find("question_reply selected_options contains duplicate values") != std::string::npos,
         "RPC multi question reply rejects duplicate selected options without resolving request");
  expect(jsonl.find("\"id\":\"reply\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
             jsonl.find("multi question done") != std::string::npos && jsonl.find("\"name\":\"question_replied\"") != std::string::npos &&
             jsonl.find("\"selected_options\":[\"alpha\",\"beta\"]") != std::string::npos && jsonl.find("\"answer\":\"Use both\"") != std::string::npos,
         "RPC multi question reply emits selections, custom text, and unblocks question tool");
}

}  // namespace

void run_app_rpc_resolver_tests()
{
  test_app_rpc_permission_policy_auto_allows_before_resolver_event();
  test_app_rpc_permission_reply_allow_and_deny_flows();
  test_app_rpc_permission_reply_session_grant_flow();
  test_app_rpc_permission_request_includes_mutation_diff_preview();
  test_app_rpc_question_reply_flow();
  test_app_rpc_question_reply_selected_option_flow();
  test_app_rpc_question_reply_selected_options_flow();
}
