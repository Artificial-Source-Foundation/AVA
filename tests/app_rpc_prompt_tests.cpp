#include "sys.h"
#include "tests/app_rpc_test_cases.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/http/transport.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/Session.h"
#include "ava/config/auth.h"
#include "ava/session/record.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/json.h"
#include "ava/core/result.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <istream>
#include <ostream>
#include <ranges>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace ava::tests::app_rpc_test {

namespace {

std::string rpc_tiny_png_bytes()
{
  std::string bytes;
  bytes.push_back(static_cast<char>(0x89));
  bytes += "PNG\r\n";
  bytes.push_back(static_cast<char>(0x1A));
  bytes += "\nava-rpc-image";
  return bytes;
}

}  // namespace

void test_app_rpc_prompt_with_fake_transport_streams_events()
{
  auto const root = create_empty_root("app-rpc-prompt");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open(open_context);
  expect(session.has_value(), "RPC prompt test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::http::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"rpc answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hello rpc\"}\n");
  bool const completed = output_buffer.wait_contains("rpc answer", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC prompt loop completes successfully");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("hello rpc") != std::string::npos,
         "RPC prompt sends command message through shared runtime");
  expect(jsonl.find("\"name\":\"session_start\"") != std::string::npos && jsonl.find("\"name\":\"assistant_message\"") != std::string::npos &&
             jsonl.find("\"request_id\":\"p1\"") != std::string::npos && completed && jsonl.find("\"id\":\"p1\"") != std::string::npos &&
             jsonl.find("\"success\":true") != std::string::npos && jsonl.find("rpc answer") != std::string::npos,
         "RPC prompt streams runtime event envelopes and ends with a successful response");
}

void test_app_rpc_offline_allows_local_protocol_and_rejects_prompt_before_provider_request()
{
  auto const root = create_empty_root("app-rpc-offline");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  open_context.offline = true;
  auto session = ava::app::runtime::Session::open(open_context);
  expect(session.has_value(), "RPC offline test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.offline = true;
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"proto\",\"type\":\"get_protocol\"}\n");
  bool const protocol_completed = output_buffer.wait_contains("\"id\":\"proto\"", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"p-offline\",\"type\":\"prompt\",\"message\":\"hello offline rpc\"}\n");
  bool const prompt_failed = output_buffer.wait_contains("offline mode is enabled", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC offline loop completes successfully after prompt rejection");
  expect(protocol_completed && jsonl.find("\"id\":\"proto\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos,
         "RPC offline mode still serves local protocol commands");
  expect(prompt_failed && jsonl.find("\"id\":\"p-offline\"") != std::string::npos && jsonl.find("\"success\":false") != std::string::npos,
         "RPC offline mode rejects prompt commands with a machine-readable error");
  expect(transport.requests().empty(), "RPC offline prompt rejection avoids provider transport requests");
}

void test_app_rpc_prompt_imports_image_attachments()
{
  auto const root = create_empty_root("app-rpc-prompt-image-attachment");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const image_path = workspace / "screen.png";
  write_app_test_file(image_path, rpc_tiny_png_bytes());

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open(open_context);
  expect(session.has_value(), "RPC image prompt test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::http::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"rpc image answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"p-img\",\"type\":\"prompt\",\"message\":\"describe\",\"attachments\":[\"" + ava::core::json::escape(image_path.string()) +
                    "\"]}\n");
  bool const completed = output_buffer.wait_contains("rpc image answer", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  auto entries = ava::app::runtime::session_ts::rat(unlocked_session)->store.load();
  expect(result.has_value(), "RPC image prompt loop completes successfully");
  expect(completed && jsonl.find("\"id\":\"p-img\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos,
         "RPC image prompt returns a successful prompt response");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("\"type\":\"input_image\"") != std::string::npos &&
             transport.requests()[0].body.find("data:image/png;base64,") != std::string::npos,
         "RPC image prompt imports local image paths into provider image payloads");
  auto const persisted_metadata = entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
                                    return entry.type == ava::session::EntryType::UserMessage && entry.data_json.find("\"attachments\"") != std::string::npos &&
                                           entry.data_json.find("\"storage_path\":\"attachments/") != std::string::npos &&
                                           entry.data_json.find("data_base64") == std::string::npos;
                                  });
  expect(persisted_metadata, "RPC image prompt persists session-owned attachment metadata without raw image bytes");
}

void test_app_rpc_prompt_imports_inline_image_uploads()
{
  auto const root = create_empty_root("app-rpc-prompt-inline-image-upload");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open(open_context);
  expect(session.has_value(), "RPC inline image upload prompt test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::http::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"rpc upload answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"p-upload\",\"type\":\"prompt\",\"message\":\"describe\",\"images\":[{\"type\":\"image\",\"data\":\"" +
                    ava::provider::base64_encode(rpc_tiny_png_bytes()) + "\",\"mimeType\":\"image/png\"}]}\n");
  bool const completed = output_buffer.wait_contains("rpc upload answer", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  auto entries = ava::app::runtime::session_ts::rat(unlocked_session)->store.load();
  expect(result.has_value(), "RPC inline image upload prompt loop completes successfully");
  expect(completed && jsonl.find("\"id\":\"p-upload\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos,
         "RPC inline image upload prompt returns a successful prompt response");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("\"type\":\"input_image\"") != std::string::npos &&
             transport.requests()[0].body.find("data:image/png;base64,") != std::string::npos,
         "RPC inline image uploads are imported into provider image payloads");
  auto const persisted_metadata = entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
                                    return entry.type == ava::session::EntryType::UserMessage && entry.data_json.find("\"attachments\"") != std::string::npos &&
                                           entry.data_json.find("\"mime_type\":\"image/png\"") != std::string::npos &&
                                           entry.data_json.find("\"storage_path\":\"attachments/") != std::string::npos &&
                                           entry.data_json.find("data_base64") == std::string::npos;
                                  });
  expect(persisted_metadata, "RPC inline image upload persists metadata without raw image bytes");
}

void test_app_rpc_prompt_rejects_inline_image_upload_mime_mismatch()
{
  auto const root = create_empty_root("app-rpc-prompt-inline-image-upload-mismatch");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open(open_context);
  expect(session.has_value(), "RPC inline image MIME mismatch test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  std::istringstream in("{\"id\":\"p-upload-bad\",\"type\":\"prompt\",\"message\":\"describe\",\"images\":[{\"type\":\"image\",\"data\":\"" +
                        ava::provider::base64_encode(rpc_tiny_png_bytes()) + "\",\"mimeType\":\"image/jpeg\"}]}\n");
  std::ostringstream out;

  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result = ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, runtime_options, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC inline image MIME mismatch loop completes after error response");
  expect(jsonl.find("\"id\":\"p-upload-bad\"") != std::string::npos && jsonl.find("\"success\":false") != std::string::npos &&
             jsonl.find("image attachment MIME type does not match detected bytes") != std::string::npos,
         "RPC inline image upload MIME mismatches are machine-readable error responses");
  expect(transport.requests().empty(), "RPC inline image MIME mismatch avoids dispatching a provider request");
}

void test_app_rpc_prompt_streams_provider_deltas_before_final_response()
{
  auto const root = create_empty_root("app-rpc-prompt-streaming");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open(open_context);
  expect(session.has_value(), "RPC streaming prompt test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ChunkedStreamingTransport transport({"data: {\"type\":\"response.output_text.delta\",\"delta\":\"rpc \"}\n\n",
                                       "data: {\"type\":\"response.output_text.delta\",\"delta\":\"stream\"}\n\n", "data: [DONE]\n\n"});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hello rpc stream\"}\n");
  bool const completed = output_buffer.wait_contains("\"success\":true", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  auto const update_position = jsonl.find("\"name\":\"message_update\"");
  auto const final_position = jsonl.find("\"name\":\"assistant_message\"");
  auto const response_position = jsonl.find("\"type\":\"response\"");
  expect(result.has_value(), "RPC streaming prompt loop completes successfully");
  expect(update_position != std::string::npos && final_position != std::string::npos && completed && response_position != std::string::npos &&
             update_position < final_position && final_position < response_position && jsonl.find("rpc stream") != std::string::npos,
         "RPC prompt emits live provider deltas before final assistant event and command response");
}

void test_app_rpc_prompt_retry_transport_cancellation_is_canceled_event()
{
  auto const root = create_empty_root("app-rpc-prompt-retry-canceled");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open(open_context);
  expect(session.has_value(), "RPC retry-cancel prompt test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::http::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"retry later\"}}"},
                                       ava::http::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"retry later\"}}"},
                                       ava::http::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"retry later\"}}"}});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  runtime_options.enable_transport_retries = true;

  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"p-cancel\",\"type\":\"prompt\",\"message\":\"cancel during retry\"}\n");
  bool const retry_started = output_buffer.wait_contains("\"name\":\"retry\"", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  bool const completed = output_buffer.wait_contains("\"id\":\"p-cancel\"", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC retry-cancel prompt loop returns control after canceled run");
  expect(retry_started, "RPC retry-cancel prompt reaches retry backoff before cancellation");
  expect(completed, "RPC retry-cancel prompt produces a response before input closes");
  expect(!transport.requests().empty() && transport.requests().size() <= 2,
         "RPC retry-cancel prompt dispatches a bounded number of provider requests before cancellation");
  expect(jsonl.find("\"name\":\"canceled\"") != std::string::npos && jsonl.find("\"payload_type\":\"cancellation\"") != std::string::npos &&
             jsonl.find("\"name\":\"error\"") == std::string::npos,
         "RPC retry transport cancellation emits terminal canceled envelope instead of an error envelope");
}

void test_app_rpc_prompt_after_idle_cancel_clears_cancel_flag()
{
  auto const root = create_empty_root("app-rpc-prompt-after-idle-cancel");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open(open_context);
  expect(session.has_value(), "RPC idle-cancel prompt test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::http::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"after cancel\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"cancel-idle\",\"type\":\"cancel\"}\n");
  bool const canceled = output_buffer.wait_contains("\"id\":\"cancel-idle\"", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"p-after\",\"type\":\"prompt\",\"message\":\"run after idle cancel\"}\n");
  bool const completed = output_buffer.wait_contains("after cancel", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC idle-cancel prompt loop completes successfully");
  expect(canceled, "RPC idle cancel writes a response");
  expect(completed && transport.requests().size() == 1 && jsonl.find("\"id\":\"p-after\"") != std::string::npos &&
             jsonl.find("\"success\":true") != std::string::npos && jsonl.find("after cancel") != std::string::npos,
         "RPC prompt after idle cancel clears the latched cancel flag and runs normally");
}

void test_app_rpc_prompt_refreshes_expired_oauth_before_provider_request()
{
  auto const root = create_empty_root("app-rpc-oauth-refresh");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto stored = ava::config::store_openai_credential(paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                                                                          .access_token = "expired-rpc-access",
                                                                                          .refresh_token = "rpc-refresh",
                                                                                          .expires_at = 100,
                                                                                          .account_id = "acct_old",
                                                                                          .source_path = {}});
  expect(stored.has_value(), "RPC OAuth refresh test stores expired credential");

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto session = ava::app::runtime::Session::open(open_context);
  expect(session.has_value(), "RPC OAuth refresh test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::http::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "{\"access_token\":\"rpc-refreshed-access\","
                                                   "\"refresh_token\":\"rpc-rotated-refresh\","
                                                   "\"expires_in\":3600,\"account_id\":\"acct_rpc\"}",
                                       },
                                       ava::http::HttpResponse{
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
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_context, provider, transport, ava::app::runtime::RunOptions{}, in, out,
                                    [&] noexcept { input_buffer.close(); });
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
  expect(completed && jsonl.find("rpc refreshed answer") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos,
         "RPC prompt returns refreshed OAuth provider response");
  auto persisted = ava::config::load_openai_credential(paths);
  expect(persisted && persisted->has_value() && (*persisted)->access_token == "rpc-refreshed-access" && (*persisted)->refresh_token == "rpc-rotated-refresh",
         "RPC OAuth preflight persists refreshed credential before provider startup");
}

}  // namespace ava::tests::app_rpc_test
