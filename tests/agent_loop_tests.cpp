#include "sys.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/observability/run_observer.h"
#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/config/model_config.h"
#include "ava/session/record.h"
#include "ava/session/session_metadata.h"
#include "ava/session/session_store.h"
#include "ava/session/validation.h"
#include "ava/permissions/permission.h"
#include "ava/permissions/permission_rules.h"
#include "ava/provider/openai_provider.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <sys/stat.h>

namespace {

class TraceCollector final : public ava::observability::RunObserver
{
 public:
  void on_event(ava::observability::TraceEvent const& event) override
  {
    std::lock_guard lock(mutex);
    events.push_back(event);
  }
  std::mutex mutex;
  std::vector<ava::observability::TraceEvent> events;
};

ava::provider::HttpResponse sse_response(std::string const& body)
{
  return ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = body};
}

std::string tool_call_sse(std::string_view id, std::string_view name, std::string_view arguments_json)
{
  return "data: {\"type\":\"response.function_call.added\",\"call_id\":\"" + ava::core::json::escape(id) + "\",\"name\":\"" + ava::core::json::escape(name) +
         "\"}\n\n" + "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"" + ava::core::json::escape(id) + "\",\"delta\":\"" +
         ava::core::json::escape(arguments_json) + "\"}\n\n" + "data: {\"type\":\"response.function_call.done\",\"call_id\":\"" + ava::core::json::escape(id) +
         "\"}\n\n";
}

class OverflowOnceProvider final : public ava::provider::Provider
{
 public:
  explicit OverflowOnceProvider(std::string base_url) : delegate_(std::move(base_url)) { }

  [[nodiscard]] ava::core::Result<ava::provider::HttpRequest> build_request(ava::provider::ProviderRequest const& request,
                                                                            std::string_view access_token) const override
  {
    ++build_calls_;
    if (build_calls_ == 1)
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "context window exceeds token limit"));
    }
    return delegate_.build_request(request, access_token);
  }

  [[nodiscard]] std::unique_ptr<ava::provider::StreamParser> create_stream_parser() const override { return delegate_.create_stream_parser(); }

  [[nodiscard]] ava::core::Result<std::vector<ava::provider::StreamEvent>> parse_response(ava::provider::HttpResponse const& response,
                                                                                          bool stream) const override
  {
    return delegate_.parse_response(response, stream);
  }

 private:
  ava::provider::OpenAIProvider delegate_;
  mutable int build_calls_ = 0;
};

class NoopDiagnosticsProvider final : public ava::lsp::DiagnosticsProvider
{
 public:
  [[nodiscard]] ava::core::Result<std::vector<ava::lsp::Diagnostic>> diagnostics(std::filesystem::path const&, ava::lsp::CancelCallback = nullptr) override
  {
    return std::vector<ava::lsp::Diagnostic>{};
  }
};

class SharedFakeTransport final : public ava::provider::Transport
{
 public:
  SharedFakeTransport(std::shared_ptr<std::vector<ava::provider::HttpResponse>> responses, std::shared_ptr<std::vector<ava::provider::HttpRequest>> requests,
                      std::shared_ptr<std::mutex> mutex)
      : responses_(std::move(responses)), requests_(std::move(requests)), mutex_(std::move(mutex))
  {
  }

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override
  {
    std::lock_guard lock(*mutex_);
    requests_->push_back(request);
    if (responses_->empty())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "fake transport has no response"));
    }
    auto response = responses_->front();
    responses_->erase(responses_->begin());
    return response;
  }

 private:
  std::shared_ptr<std::vector<ava::provider::HttpResponse>> responses_;
  std::shared_ptr<std::vector<ava::provider::HttpRequest>> requests_;
  std::shared_ptr<std::mutex> mutex_;
};

class BlockingBackgroundTransport final : public ava::provider::Transport
{
 public:
  struct State
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool request_seen = false;
    bool release = false;
    bool cancel_observed = false;
    std::vector<ava::provider::HttpRequest> requests;

    void release_success()
    {
      {
        std::lock_guard lock(mutex);
        release = true;
      }
      changed.notify_all();
    }

    void notify() { changed.notify_all(); }

    [[nodiscard]] bool wait_for_request(std::chrono::milliseconds timeout)
    {
      std::unique_lock lock(mutex);
      return changed.wait_for(lock, timeout, [&] { return request_seen; });
    }

    [[nodiscard]] bool wait_for_cancel(std::chrono::milliseconds timeout)
    {
      std::unique_lock lock(mutex);
      return changed.wait_for(lock, timeout, [&] { return cancel_observed; });
    }

    [[nodiscard]] std::vector<ava::provider::HttpRequest> requests_snapshot()
    {
      std::lock_guard lock(mutex);
      return requests;
    }
  };

  BlockingBackgroundTransport(std::shared_ptr<State> state, ava::provider::HttpResponse response) : state_(std::move(state)), response_(std::move(response)) { }

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override { return send(request, nullptr); }

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request, CancelCallback cancel_requested) override
  {
    std::unique_lock lock(state_->mutex);
    state_->requests.push_back(request);
    state_->request_seen = true;
    state_->changed.notify_all();
    state_->changed.wait(lock, [&] { return state_->release || (cancel_requested && cancel_requested()); });
    if (cancel_requested && cancel_requested())
    {
      state_->cancel_observed = true;
      state_->changed.notify_all();
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
    }
    return response_;
  }

 private:
  std::shared_ptr<State> state_;
  ava::provider::HttpResponse response_;
};

void test_agent_loop_text_only_turn()
{
  auto const root = temp_root() / "agent-text";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "text"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"hello user\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .openai_oauth = true,
      .openai_account_id = "acct_123",
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("hi", store, provider, transport);
  expect(result && result->final_text == "hello user" && result->tool_calls == 0 && result->initial_context_messages == 1 && !result->used_compacted_context &&
             result->tool_iterations == 0 && result->outcome == ava::core::RuntimeTerminalOutcome::Completed,
         "agent loop returns text-only provider response with status metadata");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("read_file") != std::string::npos,
         "agent loop includes tool schemas in provider request");
  expect(transport.requests().size() == 1 && transport.requests()[0].url == "https://chatgpt.com/backend-api/codex/responses" &&
             transport.requests()[0].headers.at("ChatGPT-Account-Id") == "acct_123",
         "agent loop routes OpenAI OAuth turns through delegated endpoint");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("\"store\":false") != std::string::npos,
         "agent loop disables response storage for OpenAI OAuth turns");
  auto entries = store.load();
  expect(entries && entries->size() == 2 && (*entries)[0].type == ava::session::EntryType::UserMessage &&
             (*entries)[1].type == ava::session::EntryType::AssistantMessage,
         "agent loop persists user and assistant entries for text-only turn");
}

void test_agent_loop_model_capability_gating()
{
  auto const root = temp_root() / "agent-capabilities";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "capabilities"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"status\":\"completed\",\"output_text\":\"plain\"}"}});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "text-only-model",
      .system_prompt = "system prompt",
      .access_token = "token",
      .stream = true,
      .model_supports_tools = false,
      .model_supports_streaming = false,
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("hi", store, provider, transport);
  expect(result && result->final_text == "plain", "agent loop accepts text-only model response");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("read_file") == std::string::npos &&
             transport.requests()[0].body.find("write_file") == std::string::npos,
         "agent loop omits tool definitions for models without tool support");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("\"stream\":false") != std::string::npos,
         "agent loop disables streaming for models without streaming support");
}

void test_agent_loop_rejects_persistent_store_without_append_route()
{
  auto const root = temp_root() / "agent-missing-append-route";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "missing-route"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token"});
  auto result = loop.run_turn("hi", store, provider, transport);
  expect(!result && result.error().message().find("append authority route") != std::string::npos && transport.requests().empty() &&
             !std::filesystem::exists(store.session_path()),
         "persistent AgentLoop without a bound append route fails before provider work or session mutation");
}

void test_agent_loop_rejects_replaced_history_before_provider_use()
{
  auto const root = temp_root() / "agent-history-path-replacement";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "history-replacement"});

  auto replacement = ava::session::serialize_session_entry_line(ava::session::SessionEntry{.id = "replacement",
                                                                                           .parent_id = "",
                                                                                           .type = ava::session::EntryType::UserMessage,
                                                                                           .timestamp = "2026-05-08T00:00:00Z",
                                                                                           .data_json = "{\"text\":\"REPLACEMENT_CANARY\"}"});
  expect(replacement.has_value(), "agent replacement fixture serializes a valid session record");
  if (!replacement)
    return;
  bool replaced = false;
  store.set_after_lease_bound_read_for_test([&] {
    if (replaced)
      return;
    replaced = true;
    std::filesystem::rename(store.session_path(), store.session_path().string() + ".parked");
    std::ofstream file(store.session_path(), std::ios::binary | std::ios::trunc);
    file << *replacement << '\n';
  });

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"must not run\"}\n\ndata: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("do not mix history", store, provider, transport);
  store.set_after_lease_bound_read_for_test({});
  auto pathname_entries = store.load();
  expect(replaced && !result && transport.requests().empty() && pathname_entries && pathname_entries->size() == 1 &&
             pathname_entries->front().data_json.find("REPLACEMENT_CANARY") != std::string::npos,
         "provider history fails closed after authority binding when the live session pathname is replaced and never consumes replacement content");
}

void test_agent_loop_image_attachment_load_failure_records_error()
{
  auto const root = temp_root() / "agent-image-load-failure";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "image-load-failure"});
  expect(
      append_session_entry_for_test(
          store,
          ava::session::SessionEntry{
              .id = "prior_image",
              .parent_id = "",
              .type = ava::session::EntryType::UserMessage,
              .timestamp = "2026-05-08T00:00:00Z",
              .data_json =
                  R"({"text":"look","attachments":[{"id":"img_missing","type":"image","mime_type":"image/png","byte_size":5,"sha256":"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824","storage_path":"attachments/missing.png"}]})"})
          .has_value(),
      "agent loop image load failure test appends prior image message");

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"should not call\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-image",
      .system_prompt = "system prompt",
      .access_token = "token",
      .model_input_modalities = {"text", "image"},
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("continue", store, provider, transport);
  expect(!result && result.error().message().find("image attachment") != std::string::npos, "agent loop returns attachment load errors before provider calls");
  expect(transport.requests().empty(), "agent loop does not call provider when attachment loading fails");
  auto entries = store.load();
  expect(entries && std::ranges::any_of(*entries,
                                        [](ava::session::SessionEntry const& entry) {
                                          return entry.type == ava::session::EntryType::Error && entry.data_json.find("image attachment") != std::string::npos;
                                        }),
         "agent loop records image attachment load errors in the session");
}

void test_agent_loop_usage_and_cost_persistence()
{
  auto const root = temp_root() / "agent-usage";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::config::ModelPricing const pricing{.input_per_million = 10.0L,
                                          .output_per_million = 20.0L,
                                          .cache_read_per_million = std::nullopt,
                                          .cache_write_per_million = std::nullopt,
                                          .reasoning_per_million = std::nullopt};

  ava::session::SessionStore exact_store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "exact"});
  ava::tests::FakeTransport exact_transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"priced\"}\n\n"
                    "data: {\"type\":\"response.completed\",\"response\":{\"usage\":{\"input_tokens\":1000,"
                    "\"output_tokens\":2000,\"total_tokens\":3000}}}\n\n")});
  ava::agent::AgentLoop exact_loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                                .mode = ava::agent::Mode::Build,
                                                                .provider_id = "openai",
                                                                .model_id = "gpt-5.5",
                                                                .system_prompt = "system prompt",
                                                                .access_token = "token",
                                                                .append_entry = append_route_for_test(exact_store),
                                                                .session_read_authority = read_authority_for_test(exact_store),
                                                                .model_pricing = pricing});
  auto exact_result = exact_loop.run_turn("hi", exact_store, provider, exact_transport);
  expect(exact_result && exact_result->usage && !exact_result->usage->estimated && exact_result->cost_usd && *exact_result->cost_usd > 0.049L &&
             *exact_result->cost_usd < 0.051L,
         "agent loop calculates cost from provider usage when pricing is known");
  auto exact_entries = exact_store.load();
  expect(exact_entries && exact_entries->size() == 2 && (*exact_entries)[1].data_json.find("\"source\":\"provider\"") != std::string::npos &&
             (*exact_entries)[1].data_json.find("\"cost_usd\":0.05") != std::string::npos,
         "agent loop persists exact provider usage and known cost on assistant messages");

  ava::session::SessionStore unknown_price_store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "unknown-price"});
  ava::tests::FakeTransport unknown_price_transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"unknown\"}\n\n"
                    "data: {\"type\":\"response.completed\",\"response\":{\"usage\":{\"input_tokens\":1,"
                    "\"output_tokens\":1,\"total_tokens\":2}}}\n\n")});
  ava::agent::AgentLoop unknown_price_loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "unknown-model",
      .system_prompt = "system prompt",
      .access_token = "token",
      .append_entry = append_route_for_test(unknown_price_store),
      .session_read_authority = read_authority_for_test(unknown_price_store),
  });
  auto unknown_price_result = unknown_price_loop.run_turn("hi", unknown_price_store, provider, unknown_price_transport);
  auto unknown_price_entries = unknown_price_store.load();
  expect(unknown_price_result && unknown_price_entries && (*unknown_price_entries)[1].data_json.find("cost_usd") == std::string::npos,
         "agent loop does not persist fake cost when model pricing is unknown");

  ava::session::SessionStore estimated_store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "estimated"});
  ava::tests::FakeTransport estimated_transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"estimated\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop estimated_loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                                    .mode = ava::agent::Mode::Build,
                                                                    .provider_id = "openai",
                                                                    .model_id = "gpt-5.5",
                                                                    .system_prompt = "system prompt",
                                                                    .access_token = "token",
                                                                    .append_entry = append_route_for_test(estimated_store),
                                                                    .session_read_authority = read_authority_for_test(estimated_store),
                                                                    .model_pricing = pricing});
  auto estimated_result = estimated_loop.run_turn("hi", estimated_store, provider, estimated_transport);
  auto estimated_entries = estimated_store.load();
  expect(estimated_result && estimated_result->usage && estimated_result->usage->estimated && estimated_entries &&
             (*estimated_entries)[1].data_json.find("\"source\":\"estimated\"") != std::string::npos &&
             (*estimated_entries)[1].data_json.find("\"estimation_method\":\"byte_count\"") != std::string::npos &&
             (*estimated_entries)[1].data_json.find("\"estimated_input_bytes\":") != std::string::npos &&
             (*estimated_entries)[1].data_json.find("cost_usd") == std::string::npos,
         "agent loop estimates byte usage without persisting fake cost when provider usage is unavailable");
}

void test_agent_loop_tool_turn_and_continuation()
{
  auto const root = temp_root() / "agent-tool";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "tool content";
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "tool"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::config::ModelPricing const pricing{.input_per_million = 10.0L,
                                          .output_per_million = 20.0L,
                                          .cache_read_per_million = std::nullopt,
                                          .cache_write_per_million = std::nullopt,
                                          .reasoning_per_million = std::nullopt};
  auto const private_reasoning_item_json =
      R"({"id":"rs_tool","type":"reasoning","summary":[{"type":"summary_text","text":"inspect note"}],"status":"completed","encrypted_content":"ciphertext-tool"})";
  ava::tests::FakeTransport transport(
      {sse_response(std::string("data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"rs_tool\",\"type\":\"reasoning\"}}\n\n") +
                    "data: {\"type\":\"response.output_item.done\",\"item\":" + private_reasoning_item_json + "}\n\n" +
                    "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_1\",\"type\":\"function_call\","
                    "\"call_id\":\"call_1\",\"name\":\"read_file\",\"arguments\":\"\"}}\n\n"
                    "data: "
                    "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"fc_1\",\"delta\":\"{\\\"path\\\":"
                    "\\\"note.txt\\\"}\"}\n\n"
                    "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_1\",\"type\":\"function_call\","
                    "\"call_id\":\"call_1\",\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\\\"note.txt\\\"}\"}}\n\n"
                    "data: {\"type\":\"response.completed\",\"response\":{\"usage\":{\"input_tokens\":10,"
                    "\"output_tokens\":2,\"total_tokens\":12}}}\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"read it\"}\n\n"
                    "data: {\"type\":\"response.completed\",\"response\":{\"usage\":{\"input_tokens\":5,"
                    "\"output_tokens\":3,\"total_tokens\":8}}}\n\n")});
  std::vector<ava::agent::ToolTimelineEntry> tool_events;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .on_tool_event = [&tool_events](auto const& entry) { tool_events.push_back(entry); },
                                                          .append_entry = append_route_for_test(store),
                                                          .session_read_authority = read_authority_for_test(store),
                                                          .model_pricing = pricing});
  auto result = loop.run_turn("read note", store, provider, transport);
  expect(result && result->final_text == "read it" && result->tool_calls == 1 && result->provider_iterations == 2 && result->initial_context_messages == 1 &&
             result->tool_iterations == 1 && result->outcome == ava::core::RuntimeTerminalOutcome::Completed,
         "agent loop runs one sequential tool call then continues to final answer with status metadata");
  expect(transport.requests().size() == 2 && transport.requests()[1].body.find("tool content") != std::string::npos &&
             transport.requests()[1].body.find(private_reasoning_item_json) != std::string::npos &&
             transport.requests()[1].body.find(R"({"type":"function_call","call_id":"call_1","name":"read_file",)") != std::string::npos &&
             transport.requests()[1].body.find(R"({"type":"function_call_output","call_id":"call_1",)") != std::string::npos &&
             transport.requests()[1].body.find(private_reasoning_item_json) <
                 transport.requests()[1].body.find(R"({"type":"function_call","call_id":"call_1",)") &&
             transport.requests()[1].body.find(R"({"type":"function_call","call_id":"call_1",)") <
                 transport.requests()[1].body.find(R"({"type":"function_call_output","call_id":"call_1",)") &&
             transport.requests()[1].body.find("Tool call requested by assistant") == std::string::npos &&
             transport.requests()[1].body.find("Tool result data only") == std::string::npos,
         "agent loop continues OpenAI reasoning tool turns with exact native reasoning before function replay");
  expect(result && result->tool_timeline.size() == 1 && result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success &&
             result->tool_timeline.front().name == "read_file" && result->tool_timeline.front().argument_summary.find("path=note.txt") != std::string::npos &&
             result->tool_timeline.front().argument_summary.find('{') == std::string::npos &&
             result->tool_timeline.front().result_summary.find("tool content") == std::string::npos &&
             result->tool_timeline.front().result_summary.find("lines") != std::string::npos && result->tool_timeline.front().output_lines &&
             *result->tool_timeline.front().output_lines == 1 &&
             result->tool_timeline.front().structured_result_json.find("\"status\":\"success\"") != std::string::npos &&
             result->tool_timeline.front().content_type == "application/json",
         "agent loop returns safe compact tool timeline summaries and structured result metadata");
  expect(tool_events.size() == 2 && tool_events.front().status == ava::agent::ToolTimelineStatus::Running &&
             tool_events.back().status == ava::agent::ToolTimelineStatus::Success,
         "agent loop publishes running and completed tool timeline events");
  expect(result && result->usage && !result->usage->estimated && result->usage->input_tokens == 15 && result->usage->output_tokens == 5 &&
             result->usage->total_tokens == 20 && result->cost_usd && *result->cost_usd > 0.00024L && *result->cost_usd < 0.00026L,
         "agent loop accumulates usage and cost across provider iterations");

  auto entries = store.load();
  expect(entries.has_value(), "agent tool turn session loads");
  if (!entries)
    return;
  bool saw_tool_call = false;
  bool saw_tool_result = false;
  bool saw_structured_tool_result = false;
  bool saw_private_reasoning_item = false;
  bool saw_final_assistant = false;
  for (auto const& entry : *entries)
  {
    saw_tool_call = saw_tool_call || entry.type == ava::session::EntryType::ToolCall;
    saw_tool_result = saw_tool_result || entry.type == ava::session::EntryType::ToolResult;
    saw_structured_tool_result = saw_structured_tool_result || (entry.type == ava::session::EntryType::ToolResult &&
                                                                entry.data_json.find("\"structured_result\":{\"schema_version\":1") != std::string::npos &&
                                                                entry.data_json.find("\"content_type\":\"application/json\"") != std::string::npos);
    saw_private_reasoning_item = saw_private_reasoning_item || (entry.type == ava::session::EntryType::ReasoningBlock &&
                                                                entry.data_json.find("encrypted_content\\\":\\\"ciphertext-tool") != std::string::npos);
    saw_final_assistant =
        saw_final_assistant || (entry.type == ava::session::EntryType::AssistantMessage && entry.data_json.find("read it") != std::string::npos);
  }
  expect(saw_tool_call && saw_tool_result && saw_structured_tool_result && saw_private_reasoning_item && saw_final_assistant,
         "agent loop persists private native reasoning with assistant, tool call, and semantic structured tool result entries");
}

void test_agent_loop_task_subagent_runs_child_session()
{
  auto const root = temp_root() / "agent-task-subagent";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const session_root = root / "sessions";
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                    "\\\"description\\\":\\\"Check docs\\\",\\\"prompt\\\":\\\"Return child result only.\\\","
                                                    "\\\"subagent_type\\\":\\\"general\\\",\\\"task_id\\\":\\\"\\\",\\\"command\\\":\\\"\\\","
                                                    "\\\"background\\\":false}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"child result\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"parent saw task\"}\n\n"
                                                    "data: [DONE]\n\n")});
  int prompts = 0;
  std::optional<ava::permissions::PermissionPrompt> captured_prompt;
  auto trace_collector = std::make_shared<TraceCollector>();
  auto observation = std::make_shared<ava::observability::RunObservation>(trace_collector);
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .permission_resolver = [&prompts, &captured_prompt](ava::permissions::PermissionPrompt const& prompt)
                                                              -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
                                                            ++prompts;
                                                            captured_prompt = prompt;
                                                            return ava::permissions::PermissionResolution::Allow;
                                                          },
                                                          .append_entry = append_route_for_test(store),
                                                          .session_read_authority = read_authority_for_test(store),
                                                          .observation = observation});

  auto result = loop.run_turn("delegate", store, provider, transport);
  expect(result && result->final_text == "parent saw task" && result->tool_calls == 1 && result->provider_iterations == 2 && prompts == 1,
         "agent loop runs foreground task subagents and continues the parent turn");
  expect(captured_prompt && captured_prompt->operation == ava::permissions::Operation::TaskRun && captured_prompt->tool_name == "task" &&
             captured_prompt->command == "general" && captured_prompt->target_path == workspace,
         "task subagent dispatch requests explicit task permission");
  expect(transport.requests().size() == 3, "task subagent uses parent-child-parent provider request order");
  if (transport.requests().size() == 3)
  {
    expect(transport.requests()[0].body.find("\"name\":\"task\"") != std::string::npos, "parent provider request exposes the task tool schema");
    expect(transport.requests()[1].body.find("Return child result only.") != std::string::npos, "child provider request receives the delegated prompt");
    expect(transport.requests()[1].body.find("\"name\":\"task\"") == std::string::npos, "child provider request hides recursive task tool access");
    expect(transport.requests()[2].body.find("child result") != std::string::npos, "parent continuation receives child task result context");
  }

  auto entries = store.load();
  expect(entries.has_value(), "task parent session loads");
  bool saw_task_call = false;
  bool saw_task_result = false;
  bool saw_task_permission = false;
  if (entries)
  {
    for (auto const& entry : *entries)
    {
      saw_task_call = saw_task_call || (entry.type == ava::session::EntryType::ToolCall && entry.data_json.find("\"name\":\"task\"") != std::string::npos);
      saw_task_result =
          saw_task_result || (entry.type == ava::session::EntryType::ToolResult && entry.data_json.find("\\\"tool\\\":\\\"task\\\"") != std::string::npos &&
                              entry.data_json.find("child result") != std::string::npos);
      saw_task_permission = saw_task_permission ||
                            (entry.type == ava::session::EntryType::PermissionDecision && entry.data_json.find("\"operation\":\"task\"") != std::string::npos &&
                             entry.data_json.find("\"resolution\":\"allow\"") != std::string::npos);
    }
  }
  expect(saw_task_call && saw_task_result && saw_task_permission, "task parent session persists task call, result, and permission decision");
  auto const parent_validation = entries ? ava::session::validate_session_replay(*entries) : ava::session::SessionReplayValidation{};
  expect(entries && parent_validation.ok(), "task parent session passes strict replay validation");

  auto summaries = ava::session::SessionStore::list_sessions(workspace, session_root);
  expect(summaries && summaries->size() == 2, "task subagent creates a persisted child session beside the parent");
  bool saw_child_metadata = false;
  bool saw_child_prompt = false;
  bool saw_child_answer = false;
  if (summaries)
  {
    for (auto const& summary : *summaries)
    {
      if (summary.session_id == store.session_id())
        continue;
      auto child_store = ava::session::SessionStore::open(workspace, summary.session_id, session_root);
      if (!child_store)
        continue;
      auto child_entries = child_store->load();
      if (!child_entries)
        continue;
      for (auto const& entry : *child_entries)
      {
        saw_child_metadata = saw_child_metadata || (entry.type == ava::session::EntryType::SessionMetadata &&
                                                    entry.data_json.find("\"parent_session_id\":\"parent\"") != std::string::npos &&
                                                    entry.data_json.find("@general subagent") != std::string::npos);
        saw_child_prompt =
            saw_child_prompt || (entry.type == ava::session::EntryType::UserMessage && entry.data_json.find("Return child result only.") != std::string::npos);
        saw_child_answer =
            saw_child_answer || (entry.type == ava::session::EntryType::AssistantMessage && entry.data_json.find("child result") != std::string::npos);
      }
    }
  }
  expect(saw_child_metadata && saw_child_prompt && saw_child_answer, "task child session records parent linkage, delegated prompt, and child answer");
  std::lock_guard trace_lock(trace_collector->mutex);
  auto trace = ava::observability::validate_and_score_trace(trace_collector->events);
  std::map<std::string, unsigned> starts, terminals;
  bool child_parent_correlation = false;
  for (auto const& event : trace_collector->events)
  {
    starts[event.run_id] += event.type == ava::observability::TraceEventType::AgentRunStart;
    terminals[event.run_id] += event.type == ava::observability::TraceEventType::AgentRunTerminal;
    child_parent_correlation =
        child_parent_correlation || (!event.parent_run_id.empty() && event.parent_session_id == "parent" && event.session_id != event.parent_session_id);
  }
  expect(trace.valid && starts.size() == 2 && starts == terminals && child_parent_correlation,
         "observed foreground task has separate parent/child lifecycles, fresh child session IDs, and typed parent correlation");
}

void test_agent_loop_task_subagent_propagates_authority_roots_to_foreground_and_background_children()
{
  auto run_case = [](bool background) {
    auto const test_root = temp_root();
    expect(::chmod(test_root.c_str(), S_IRWXU) == 0, "task authority-root test secures its test-root ancestor");
    auto const root = test_root / (background ? "agent-task-authority-background" : "agent-task-authority-foreground");
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    expect(::chmod(root.c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0,
           "task authority-root fixture keeps sealed planning roots owner-only");
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = background ? "task-authority-bg" : "task-authority-fg"});
    ava::provider::OpenAIProvider const provider("https://api.example.test");
    auto const task_arguments = std::string(R"({"description":"authority child","prompt":"run child bash","subagent_type":"general","background":)") +
                                (background ? "true}" : "false}");
    auto const child_bash = tool_call_sse("call_child_bash", "bash", R"({"command":"ls"})") + "data: [DONE]\n\n";
    int task_prompts = 0;
    auto collector = std::make_shared<TraceCollector>();
    auto observation = std::make_shared<ava::observability::RunObservation>(collector);
    std::shared_ptr<ava::agent::BackgroundJobRegistry> registry;
    std::shared_ptr<std::vector<ava::provider::HttpResponse>> background_responses;
    std::shared_ptr<std::vector<ava::provider::HttpRequest>> background_requests;
    std::shared_ptr<std::mutex> background_mutex;
    ava::tests::FakeTransport transport(background
                                            ? std::vector<ava::provider::HttpResponse>{sse_response(tool_call_sse("call_task", "task", task_arguments) + "data: [DONE]\n\n"),
                                                                                        sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"parent queued\"}\n\n"
                                                                                                     "data: [DONE]\n\n")}
                                            : std::vector<ava::provider::HttpResponse>{sse_response(tool_call_sse("call_task", "task", task_arguments) + "data: [DONE]\n\n"),
                                                                                        sse_response(child_bash),
                                                                                        sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"child denied\"}\n\n"
                                                                                                     "data: [DONE]\n\n"),
                                                                                        sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"parent continued\"}\n\n"
                                                                                                     "data: [DONE]\n\n")});
    if (background)
    {
      registry = std::make_shared<ava::agent::BackgroundJobRegistry>();
      background_responses = std::make_shared<std::vector<ava::provider::HttpResponse>>(
          std::vector<ava::provider::HttpResponse>{sse_response(child_bash),
                                                    sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"child denied\"}\n\n"
                                                                 "data: [DONE]\n\n")});
      background_requests = std::make_shared<std::vector<ava::provider::HttpRequest>>();
      background_mutex = std::make_shared<std::mutex>();
    }

    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .ava_authority_roots = {workspace},
        .permission_resolver = [&task_prompts](ava::permissions::PermissionPrompt const& prompt)
            -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          ++task_prompts;
          expect(prompt.operation == ava::permissions::Operation::TaskRun, "authority-root child only prompts to authorize its parent task");
          return ava::permissions::PermissionResolution::Allow;
        },
        .background_provider_factory = background
                                           ? []() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
                                               std::unique_ptr<ava::provider::Provider> child =
                                                   std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
                                               return child;
                                             }
                                           : decltype(ava::agent::AgentLoopOptions{}.background_provider_factory){},
        .background_transport_factory =
            background
                ? [background_responses, background_requests, background_mutex]() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
                    std::unique_ptr<ava::provider::Transport> child =
                        std::make_unique<SharedFakeTransport>(background_responses, background_requests, background_mutex);
                    return child;
                  }
                : decltype(ava::agent::AgentLoopOptions{}.background_transport_factory){},
        .background_jobs = registry,
        .append_entry = append_route_for_test(store),
        .session_read_authority = read_authority_for_test(store),
        .observation = observation,
    });
    auto result = loop.run_turn("delegate authority child", store, provider, transport);

    bool child_completed = !background;
    std::vector<ava::provider::HttpRequest> child_requests;
    if (background)
    {
      auto jobs = registry->snapshot();
      if (!jobs.empty())
      {
        auto completed = registry->wait(jobs.front().job_id, std::chrono::milliseconds(1000));
        child_completed = completed && completed->state == ava::agent::BackgroundJobState::Completed && completed->final_text == "child denied";
      }
      registry->join_finished();
      std::lock_guard lock(*background_mutex);
      child_requests = *background_requests;
    }
    bool process_started = false;
    {
      std::lock_guard lock(collector->mutex);
      process_started = std::ranges::any_of(collector->events, [](ava::observability::TraceEvent const& event) {
        return event.type == ava::observability::TraceEventType::ProcessStart;
      });
    }
    auto const child_error_propagated = background
                                            ? child_requests.size() == 2 && child_requests[1].body.find("must not overlap with any AVA authority root") != std::string::npos
                                            : transport.requests().size() == 4 &&
                                                  transport.requests()[2].body.find("must not overlap with any AVA authority root") != std::string::npos;
    expect(result && task_prompts == 1 && child_completed && child_error_propagated && !process_started,
           background ? "background child copies AVA authority roots before its AgentLoop starts and blocks overlapping model commands"
                      : "foreground child copies AVA authority roots before its AgentLoop starts and blocks overlapping model commands");
  };

  run_case(false);
  run_case(true);
}

void test_agent_loop_task_subagent_recovers_torn_child_before_resume()
{
  auto const root = temp_root() / "agent-task-subagent-torn-resume";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const session_root = root / "sessions";
  std::filesystem::create_directories(workspace);

  auto child = ava::session::SessionStore::create(workspace, session_root);
  expect(child.has_value(), "torn child resume test creates a child session");
  if (!child)
    return;
  auto metadata = append_session_metadata_for_test(
      *child, ava::session::SessionMetadataUpdate{.name = "resumable child", .parent_session_id = "parent-resume", .actor = "subagent"});
  expect(metadata.has_value(), "torn child resume test seeds child metadata");
  if (!metadata)
    return;
  auto const child_id = child->session_id();
  auto const child_path = child->session_path();
  auto const valid_child_bytes = [&] {
    std::ifstream file(child_path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  }();
  {
    std::ofstream file(child_path, std::ios::binary | std::ios::app);
    file << "{\"version\":3,\"id\":\"torn-child";
  }

  ava::session::SessionStore parent(ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent-resume"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  auto const task_arguments = std::string("{\\\"description\\\":\\\"Resume child\\\",\\\"prompt\\\":\\\"Continue child.\\\",") +
                              "\\\"subagent_type\\\":\\\"general\\\",\\\"task_id\\\":\\\"" + child_id + "\\\"}";
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_resume\",\"name\":\"task\"}\n\n"
                                                    "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_resume\",\"delta\":\"" +
                                                    task_arguments +
                                                    "\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"resumed child answer\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"parent resumed child\"}\n\n"
                                                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .append_entry = append_route_for_test(parent),
      .session_read_authority = read_authority_for_test(parent),
  });

  auto result = loop.run_turn("resume torn child", parent, provider, transport);
  std::ifstream repaired_file(child_path, std::ios::binary);
  std::string repaired_bytes{std::istreambuf_iterator<char>(repaired_file), std::istreambuf_iterator<char>()};
  auto child_entries = child->load();
  bool quarantine_found = false;
  auto const quarantine_prefix = child_path.filename().string() + ".torn-tail.";
  std::error_code iter_error;
  for (std::filesystem::directory_iterator iterator(child_path.parent_path(), iter_error), end; !iter_error && iterator != end; iterator.increment(iter_error))
  {
    quarantine_found = quarantine_found || iterator->path().filename().string().starts_with(quarantine_prefix);
  }
  expect(result && result->final_text == "parent resumed child" && transport.requests().size() == 3 && child_entries && child_entries->size() >= 3 &&
             repaired_bytes.starts_with(valid_child_bytes) && repaired_bytes.find("torn-child") == std::string::npos && quarantine_found,
         "foreground task_id resume owns and recovers a torn child before loading and running it");
}

void test_subagent_config_loads_project_definitions()
{
  auto const root = temp_root() / "subagent-config";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const agent_dir = workspace / ".ava" / "agents";
  std::filesystem::create_directories(agent_dir);
  {
    std::ofstream file(agent_dir / "reviewer.md", std::ios::binary | std::ios::trunc);
    file << "---\n"
            "name: reviewer\n"
            "description: Review implementation details.\n"
            "tools: read-only\n"
            "---\n"
            "Inspect files and return concise review findings.";
  }
  {
    std::ofstream file(agent_dir / "general.md", std::ios::binary | std::ios::trunc);
    file << "---\n"
            "description: Attempt to override builtin.\n"
            "---\n"
            "Should be ignored.";
  }

  auto loaded = ava::agent::load_subagents(
      ava::agent::SubagentLoadOptions{.workspace_root = workspace, .global_agent_dirs = {}, .project_agent_dirs = {agent_dir}, .include_project_agents = true});
  auto const* reviewer = ava::agent::find_subagent(loaded.subagents, "reviewer");
  auto const* general = ava::agent::find_subagent(loaded.subagents, "general");
  expect(reviewer && reviewer->description == "Review implementation details." && reviewer->tool_preset == ava::agent::SubagentToolPreset::ReadOnly &&
             reviewer->system_prompt.find("Inspect files") != std::string::npos,
         "subagent config loads project-defined read-only subagents");
  expect(general && general->builtin, "subagent config keeps builtin subagents from project override");
  expect(std::ranges::any_of(
             loaded.diagnostics,
             [](ava::agent::SubagentDiagnostic const& diagnostic) { return diagnostic.message.find("collides with a builtin") != std::string::npos; }),
         "subagent config reports builtin-name collisions");

  auto untrusted = ava::agent::load_subagents(ava::agent::SubagentLoadOptions{
      .workspace_root = workspace, .global_agent_dirs = {}, .project_agent_dirs = {agent_dir}, .include_project_agents = false});
  expect(ava::agent::find_subagent(untrusted.subagents, "reviewer") == nullptr, "project subagents are gated by project resource trust");
}

void test_agent_loop_custom_subagent_definition_controls_prompt_and_tools()
{
  auto const root = temp_root() / "agent-task-custom-subagent";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const session_root = root / "sessions";
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent-custom"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                    "\\\"description\\\":\\\"Review docs\\\",\\\"prompt\\\":\\\"Return review result.\\\","
                                                    "\\\"subagent_type\\\":\\\"reviewer\\\"}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"review result\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"custom done\"}\n\n"
                                                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .subagents = {ava::agent::SubagentDefinition{.name = "reviewer",
                                                   .description = "Read-only reviewer",
                                                   .system_prompt = "CUSTOM REVIEWER ROLE",
                                                   .tool_preset = ava::agent::SubagentToolPreset::ReadOnly}},
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });

  auto result = loop.run_turn("delegate custom", store, provider, transport);
  expect(result && result->final_text == "custom done" && transport.requests().size() == 3, "agent loop runs configured custom subagents");
  if (transport.requests().size() == 3)
  {
    expect(transport.requests()[1].body.find("CUSTOM REVIEWER ROLE") != std::string::npos, "custom subagent system prompt is appended to child request");
    expect(transport.requests()[1].body.find("\"name\":\"read_file\"") != std::string::npos, "read-only custom subagents retain read tools");
    expect(transport.requests()[1].body.find("\"name\":\"bash\"") == std::string::npos &&
               transport.requests()[1].body.find("\"name\":\"write_file\"") == std::string::npos &&
               transport.requests()[1].body.find("\"name\":\"task\"") == std::string::npos,
           "read-only custom subagents hide mutation, shell, and recursive task tools");
  }
}

void test_agent_loop_background_task_starts_child_session()
{
  auto const root = temp_root() / "agent-task-background";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const session_root = root / "sessions";
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent-bg"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  std::mutex session_mutex;
  auto registry = std::make_shared<ava::agent::BackgroundJobRegistry>();
  auto trace_collector = std::make_shared<TraceCollector>();
  auto observation = std::make_shared<ava::observability::RunObservation>(trace_collector);
  auto background_state = std::make_shared<BlockingBackgroundTransport::State>();
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                    "\\\"description\\\":\\\"Explore async\\\",\\\"prompt\\\":\\\"Return background child.\\\","
                                                    "\\\"subagent_type\\\":\\\"general\\\",\\\"background\\\":true}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"queued\"}\n\n"
                                                    "data: [DONE]\n\n")});
  auto parent_append = append_route_for_test(store);
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .lsp_diagnostics_provider = std::make_shared<NoopDiagnosticsProvider>(),
      .background_provider_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        std::unique_ptr<ava::provider::Provider> provider = std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
        return provider;
      },
      .background_transport_factory = [background_state]() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
        std::unique_ptr<ava::provider::Transport> transport = std::make_unique<BlockingBackgroundTransport>(
            background_state, sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"background child\"}\n\n"
                                           "data: [DONE]\n\n"));
        return transport;
      },
      .background_jobs = registry,
      .session_mutex = &session_mutex,
      .append_entry = parent_append,
      .session_read_authority = read_authority_for_test(store),
      .parent_notification_sink = parent_append,
      .observation = observation});

  auto result = loop.run_turn("delegate background", store, provider, transport);
  expect(result && result->final_text == "queued" && result->tool_calls == 1 && transport.requests().size() == 2,
         "agent loop starts background task and continues parent turn immediately");
  if (transport.requests().size() == 2)
  {
    expect(transport.requests()[1].body.find("\\\"state\\\":\\\"running\\\"") != std::string::npos, "parent continuation receives running task state");
    expect(transport.requests()[1].body.find("\\\"job_id\\\":\\\"job_") != std::string::npos, "parent continuation receives registry job id");
  }

  auto running_jobs = registry->snapshot();
  expect(running_jobs.size() == 1 && running_jobs.front().state == ava::agent::BackgroundJobState::Running &&
             running_jobs.front().child_session_id.starts_with("session_"),
         "background task appears as running in the registry");
  expect(background_state->wait_for_request(std::chrono::milliseconds(1000)), "background child reaches provider transport while registered");
  auto background_requests = background_state->requests_snapshot();
  bool const saw_background_request_without_lsp = !background_requests.empty() &&
                                                  background_requests.front().body.find("\"name\":\"lsp_diagnostics\"") == std::string::npos &&
                                                  background_requests.front().body.find("\"name\":\"lsp_workspace_symbols\"") == std::string::npos;

  bool foreground_resume_blocked = false;
  if (!running_jobs.empty())
  {
    ava::session::SessionStore competing_parent(
        ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent-bg-contender"});
    auto const resume_arguments = std::string("{\\\"description\\\":\\\"Resume running child\\\",\\\"prompt\\\":\\\"Compete.\\\",") +
                                  "\\\"subagent_type\\\":\\\"general\\\",\\\"task_id\\\":\\\"" + running_jobs.front().child_session_id + "\\\"}";
    ava::tests::FakeTransport competing_transport(
        {sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_compete\",\"name\":\"task\"}\n\n"
                      "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_compete\",\"delta\":\"" +
                      resume_arguments +
                      "\"}\n\n"
                      "data: [DONE]\n\n"),
         sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"resume was blocked\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop competing_loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          return ava::permissions::PermissionResolution::Allow;
        },
        .append_entry = append_route_for_test(competing_parent),
        .session_read_authority = read_authority_for_test(competing_parent),
    });
    auto competing_result = competing_loop.run_turn("resume running child", competing_parent, provider, competing_transport);
    foreground_resume_blocked = competing_result && competing_result->final_text == "resume was blocked" && competing_transport.requests().size() == 2 &&
                                competing_transport.requests()[1].body.find("already owned") != std::string::npos;
  }
  expect(foreground_resume_blocked, "foreground task_id resume fails while the background child owns its session lease");

  background_state->release_success();
  ava::core::Result<ava::agent::BackgroundJobSnapshot> completed =
      std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "missing background job"));
  if (!running_jobs.empty())
  {
    completed = registry->wait(running_jobs.front().job_id, std::chrono::milliseconds(1000));
  }
  expect(completed && completed->state == ava::agent::BackgroundJobState::Completed && completed->final_text == "background child",
         "background task transitions to completed in the registry");
  registry->join_finished();
  bool saw_background_answer = false;
  if (completed)
  {
    auto child_store = ava::session::SessionStore::open(workspace, completed->child_session_id, session_root);
    if (child_store)
    {
      auto child_entries = child_store->load();
      saw_background_answer = child_entries && std::ranges::any_of(*child_entries, [](ava::session::SessionEntry const& entry) {
                                return entry.type == ava::session::EntryType::AssistantMessage && entry.data_json.find("background child") != std::string::npos;
                              });
    }
  }
  expect(saw_background_request_without_lsp, "background subagents do not inherit the parent LSP provider");
  expect(saw_background_answer, "background subagents write completion to the child session");
  std::lock_guard trace_lock(trace_collector->mutex);
  auto trace = ava::observability::validate_and_score_trace(trace_collector->events);
  std::map<std::string, unsigned> starts, terminals;
  bool child_parent_correlation = false;
  for (auto const& event : trace_collector->events)
  {
    starts[event.run_id] += event.type == ava::observability::TraceEventType::AgentRunStart;
    terminals[event.run_id] += event.type == ava::observability::TraceEventType::AgentRunTerminal;
    child_parent_correlation =
        child_parent_correlation || (!event.parent_run_id.empty() && event.parent_session_id == "parent-bg" && event.session_id != event.parent_session_id);
  }
  expect(trace.valid && starts.size() == 2 && starts == terminals && child_parent_correlation,
         "observed background task has separate parent/child lifecycles and typed parent correlation");
}

void test_agent_loop_background_task_failure_records_parent_and_child_errors()
{
  auto const root = temp_root() / "agent-task-background-failure";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const session_root = root / "sessions";
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent-bg-fail"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  std::mutex session_mutex;
  auto background_responses = std::make_shared<std::vector<ava::provider::HttpResponse>>();
  auto background_requests = std::make_shared<std::vector<ava::provider::HttpRequest>>();
  auto background_mutex = std::make_shared<std::mutex>();
  auto registry = std::make_shared<ava::agent::BackgroundJobRegistry>();
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                    "\\\"description\\\":\\\"Fail async\\\",\\\"prompt\\\":\\\"This background request will fail.\\\","
                                                    "\\\"subagent_type\\\":\\\"general\\\",\\\"background\\\":true}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"queued failure\"}\n\n"
                                                    "data: [DONE]\n\n")});
  auto parent_append = append_route_for_test(store);
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .background_provider_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        std::unique_ptr<ava::provider::Provider> provider = std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
        return provider;
      },
      .background_transport_factory = [background_responses, background_requests,
                                       background_mutex]() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
        std::unique_ptr<ava::provider::Transport> transport =
            std::make_unique<SharedFakeTransport>(background_responses, background_requests, background_mutex);
        return transport;
      },
      .background_jobs = registry,
      .session_mutex = &session_mutex,
      .append_entry = parent_append,
      .session_read_authority = read_authority_for_test(store),
      .parent_notification_sink = parent_append,
  });

  auto result = loop.run_turn("delegate failing background", store, provider, transport);
  expect(result && result->final_text == "queued failure" && result->tool_calls == 1, "agent loop can queue a background task that later fails");

  auto jobs = registry->snapshot();
  expect(jobs.size() == 1, "failed background task is registered");
  ava::core::Result<ava::agent::BackgroundJobSnapshot> failed = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "missing background job"));
  if (!jobs.empty())
  {
    failed = registry->wait(jobs.front().job_id, std::chrono::milliseconds(1000));
  }
  expect(failed && failed->state == ava::agent::BackgroundJobState::Failed && failed->error &&
             failed->error->find("fake transport has no response") != std::string::npos,
         "failed background task is marked failed in the registry");
  registry->join_finished();

  bool saw_background_request = false;
  {
    std::lock_guard lock(*background_mutex);
    saw_background_request = !background_requests->empty();
  }
  auto parent_entries = store.load();
  bool const saw_parent_error = parent_entries && std::ranges::any_of(*parent_entries, [](ava::session::SessionEntry const& entry) {
                                  return entry.type == ava::session::EntryType::Error &&
                                         entry.data_json.find("fake transport has no response") != std::string::npos &&
                                         entry.data_json.find("background_task_id") != std::string::npos;
                                });
  bool saw_child_error = false;
  if (failed)
  {
    auto child_store = ava::session::SessionStore::open(workspace, failed->child_session_id, session_root);
    if (child_store)
    {
      auto child_entries = child_store->load();
      saw_child_error = child_entries && std::ranges::any_of(*child_entries, [](ava::session::SessionEntry const& entry) {
                          return entry.type == ava::session::EntryType::Error && entry.data_json.find("fake transport has no response") != std::string::npos;
                        });
    }
  }
  expect(saw_background_request, "background failure test reaches the child provider transport");
  expect(saw_parent_error, "background task failures are visible in the parent session");
  expect(saw_child_error, "background task failures are persisted in the child session");
}

void test_agent_loop_background_task_cancel_requests_child_cancellation()
{
  auto const root = temp_root() / "agent-task-background-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const session_root = root / "sessions";
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent-bg-cancel"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  auto registry = std::make_shared<ava::agent::BackgroundJobRegistry>();
  auto background_state = std::make_shared<BlockingBackgroundTransport::State>();
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                    "\\\"description\\\":\\\"Cancel async\\\",\\\"prompt\\\":\\\"Wait until canceled.\\\","
                                                    "\\\"subagent_type\\\":\\\"general\\\",\\\"background\\\":true}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"queued cancel\"}\n\n"
                                                    "data: [DONE]\n\n")});
  auto parent_append = append_route_for_test(store);
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .background_provider_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        std::unique_ptr<ava::provider::Provider> provider = std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
        return provider;
      },
      .background_transport_factory = [background_state]() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
        std::unique_ptr<ava::provider::Transport> transport = std::make_unique<BlockingBackgroundTransport>(
            background_state, sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"should not complete\"}\n\n"
                                           "data: [DONE]\n\n"));
        return transport;
      },
      .background_jobs = registry,
      .append_entry = parent_append,
      .session_read_authority = read_authority_for_test(store),
      .parent_notification_sink = parent_append,
  });

  auto result = loop.run_turn("delegate cancelable background", store, provider, transport);
  expect(result && result->final_text == "queued cancel" && result->tool_calls == 1, "agent loop can queue a cancelable background task");
  auto jobs = registry->snapshot();
  expect(jobs.size() == 1 && jobs.front().state == ava::agent::BackgroundJobState::Running, "cancelable background task is running in registry");
  expect(background_state->wait_for_request(std::chrono::milliseconds(1000)), "cancel test background child reaches provider transport");
  if (!jobs.empty())
  {
    auto canceled = registry->cancel(jobs.front().job_id);
    background_state->notify();
    expect(canceled && canceled->cancel_requested, "background registry cancel requests stop");
    expect(background_state->wait_for_cancel(std::chrono::milliseconds(1000)), "background child transport observes cancellation");
    auto final = registry->wait(jobs.front().job_id, std::chrono::milliseconds(1000));
    expect(final && final->state == ava::agent::BackgroundJobState::Canceled, "background registry marks canceled child jobs canceled");
    expect(final && !final->error, "background registry canceled job snapshots do not carry failure errors");
    registry->join_finished();

    bool saw_child_cancel = false;
    if (final)
    {
      auto child_store = ava::session::SessionStore::open(workspace, final->child_session_id, session_root);
      if (child_store)
      {
        auto child_entries = child_store->load();
        saw_child_cancel = child_entries && std::ranges::any_of(*child_entries, [](ava::session::SessionEntry const& entry) {
                             return entry.type == ava::session::EntryType::Cancel && entry.data_json.find("cancel_requested") != std::string::npos;
                           });
      }
    }
    expect(saw_child_cancel, "canceled background child records cancellation in its child session");
  }
}

void test_agent_loop_background_task_requires_registry_owner()
{
  auto const root = temp_root() / "agent-task-background-no-registry";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "no-registry"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                    "\\\"description\\\":\\\"No registry\\\",\\\"prompt\\\":\\\"Try background.\\\","
                                                    "\\\"subagent_type\\\":\\\"general\\\",\\\"background\\\":true}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"handled\"}\n\n"
                                                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .background_provider_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        std::unique_ptr<ava::provider::Provider> provider = std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
        return provider;
      },
      .background_transport_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
        std::unique_ptr<ava::provider::Transport> transport = std::make_unique<ava::tests::FakeTransport>(std::vector<ava::provider::HttpResponse>{});
        return transport;
      },
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });

  auto result = loop.run_turn("delegate unavailable background", store, provider, transport);
  expect(result && result->final_text == "handled" && transport.requests().size() == 2,
         "agent loop continues after unavailable background registry tool error");
  if (transport.requests().size() == 2)
  {
    expect(transport.requests()[1].body.find("background task subagents are unavailable") != std::string::npos,
           "background task requires an explicit registry owner");
  }
}

void test_background_job_registry_worker_exception_marks_failed()
{
  ava::agent::BackgroundJobRegistry registry;
  auto started = registry.start(ava::agent::BackgroundJobStartOptions{.title = "throws"},
                                [](ava::agent::BackgroundJobContext const&) -> ava::agent::BackgroundJobCompletion { throw std::runtime_error("boom"); });
  expect(started.has_value(), "background registry starts worker that throws");
  if (started)
  {
    auto failed = registry.wait(started->job_id, std::chrono::milliseconds(1000));
    expect(failed && failed->state == ava::agent::BackgroundJobState::Failed && failed->error && failed->error->find("boom") != std::string::npos,
           "background registry records thrown worker exceptions as failed jobs");
    auto const joined = registry.join_finished();
    auto retained = registry.wait(started->job_id, std::chrono::milliseconds(0));
    expect(joined == 1 && retained && retained->state == ava::agent::BackgroundJobState::Failed,
           "background registry retains terminal job snapshots after joining worker threads");
  }
}

void test_background_job_registry_enforces_running_limit()
{
  struct State
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
  };
  auto state = std::make_shared<State>();
  ava::agent::BackgroundJobRegistry registry(ava::agent::BackgroundJobRegistryOptions{.max_running_jobs = 1, .max_retained_finished_jobs = 4});
  auto blocking_worker = [state](ava::agent::BackgroundJobContext const& context) {
    std::unique_lock lock(state->mutex);
    std::stop_callback notify_stop(context.stop_token, [&] { state->changed.notify_all(); });
    state->started = true;
    state->changed.notify_all();
    state->changed.wait(lock, [&] { return context.stop_token.stop_requested(); });
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Canceled, .final_text = "", .stop_reason = "canceled"};
  };
  auto first = registry.start(ava::agent::BackgroundJobStartOptions{.title = "first"}, blocking_worker);
  expect(first.has_value(), "background registry starts first job under running limit");
  {
    std::unique_lock lock(state->mutex);
    expect(state->changed.wait_for(lock, std::chrono::milliseconds(1000), [&] { return state->started; }),
           "running-limit test worker starts before second job attempt");
  }
  auto second = registry.start(ava::agent::BackgroundJobStartOptions{.title = "second"}, [](ava::agent::BackgroundJobContext const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Completed, .final_text = "unexpected", .stop_reason = "completed"};
  });
  expect(!second && second.error().message().find("limit") != std::string::npos, "background registry rejects jobs above the running limit");
  if (first)
  {
    static_cast<void>(registry.cancel(first->job_id));
    auto canceled = registry.wait(first->job_id, std::chrono::milliseconds(1000));
    expect(canceled && canceled->state == ava::agent::BackgroundJobState::Canceled, "background registry frees running capacity after cancellation");
  }
  registry.join_finished();
  auto third = registry.start(ava::agent::BackgroundJobStartOptions{.title = "third"}, [](ava::agent::BackgroundJobContext const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Completed, .final_text = "third done", .stop_reason = "completed"};
  });
  expect(third.has_value(), "background registry accepts a new job after running job completes");
  if (third)
  {
    auto completed = registry.wait(third->job_id, std::chrono::milliseconds(1000));
    expect(completed && completed->final_text == "third done", "background registry completes job after running limit frees capacity");
  }
  registry.join_finished();
}

void test_background_job_registry_coerces_non_terminal_completion_to_failed()
{
  ava::agent::BackgroundJobRegistry registry;
  auto started = registry.start(ava::agent::BackgroundJobStartOptions{.title = "bad completion"}, [](ava::agent::BackgroundJobContext const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Running, .final_text = "bad", .stop_reason = "running"};
  });
  expect(started.has_value(), "background registry starts worker with invalid completion state");
  if (started)
  {
    auto failed = registry.wait(started->job_id, std::chrono::milliseconds(1000));
    expect(failed && failed->state == ava::agent::BackgroundJobState::Failed && failed->error && failed->error->find("non-terminal") != std::string::npos,
           "background registry coerces non-terminal worker completions to failed snapshots");
    registry.join_finished();
  }
}

void test_background_job_registry_bounds_snapshot_text()
{
  ava::agent::BackgroundJobRegistry registry(
      ava::agent::BackgroundJobRegistryOptions{.max_running_jobs = 4, .max_retained_finished_jobs = 4, .max_description_bytes = 5, .max_final_text_bytes = 7});
  auto const multi_byte_suffix = std::string("\xE2\x82\xAC", 3) + "tail";
  auto started = registry.start(ava::agent::BackgroundJobStartOptions{.title = "bounded", .description = std::string("abcd") + multi_byte_suffix},
                                [](ava::agent::BackgroundJobContext const&) {
                                  return ava::agent::BackgroundJobCompletion{
                                      .state = ava::agent::BackgroundJobState::Completed,
                                      .final_text = std::string("abcdef") + std::string("\xE2\x82\xAC", 3) + "tail",
                                      .stop_reason = "",
                                  };
                                });
  expect(started && started->description == "abcd" && started->description_truncated,
         "background registry truncates oversized job descriptions without splitting UTF-8 codepoints");
  if (started)
  {
    auto completed = registry.wait(started->job_id, std::chrono::milliseconds(1000));
    expect(completed && completed->final_text == "abcdef" && completed->final_text_truncated,
           "background registry truncates oversized final text without splitting UTF-8 codepoints");
    registry.join_finished();
  }
}

void test_background_job_registry_normalizes_terminal_completion_fields()
{
  ava::agent::BackgroundJobRegistry registry;
  auto completed_with_error =
      registry.start(ava::agent::BackgroundJobStartOptions{.title = "completed with error"}, [](ava::agent::BackgroundJobContext const&) {
        auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "ignored completion error");
        return ava::agent::BackgroundJobCompletion{
            .state = ava::agent::BackgroundJobState::Completed,
            .final_text = "done",
            .stop_reason = "",
            .error = std::move(error),
        };
      });
  auto failed_without_error =
      registry.start(ava::agent::BackgroundJobStartOptions{.title = "failed without error"}, [](ava::agent::BackgroundJobContext const&) {
        return ava::agent::BackgroundJobCompletion{
            .state = ava::agent::BackgroundJobState::Failed,
            .final_text = "should disappear",
            .stop_reason = "",
        };
      });
  auto canceled_with_error = registry.start(ava::agent::BackgroundJobStartOptions{.title = "canceled with error"}, [](ava::agent::BackgroundJobContext const&) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "ignored cancel error");
    return ava::agent::BackgroundJobCompletion{
        .state = ava::agent::BackgroundJobState::Canceled,
        .final_text = "should disappear",
        .stop_reason = "",
        .error = std::move(error),
    };
  });

  expect(completed_with_error && failed_without_error && canceled_with_error, "background registry starts terminal normalization workers");
  if (completed_with_error)
  {
    auto completed = registry.wait(completed_with_error->job_id, std::chrono::milliseconds(1000));
    expect(completed && completed->state == ava::agent::BackgroundJobState::Completed && completed->final_text == "done" &&
               completed->stop_reason == "completed" && !completed->error,
           "background registry clears completed-job errors and defaults stop reasons");
  }
  if (failed_without_error)
  {
    auto failed = registry.wait(failed_without_error->job_id, std::chrono::milliseconds(1000));
    expect(failed && failed->state == ava::agent::BackgroundJobState::Failed && failed->final_text.empty() && failed->stop_reason == "failed" &&
               failed->error && failed->error->find("without an error") != std::string::npos,
           "background registry synthesizes failed-job errors and clears final text");
  }
  if (canceled_with_error)
  {
    auto canceled = registry.wait(canceled_with_error->job_id, std::chrono::milliseconds(1000));
    expect(canceled && canceled->state == ava::agent::BackgroundJobState::Canceled && canceled->final_text.empty() && canceled->stop_reason == "canceled" &&
               !canceled->error,
           "background registry clears canceled-job errors and final text");
  }
  registry.join_finished();
}

void test_background_job_registry_join_finished_is_concurrency_safe()
{
  ava::agent::BackgroundJobRegistry registry;
  auto started = registry.start(ava::agent::BackgroundJobStartOptions{.title = "concurrent join"}, [](ava::agent::BackgroundJobContext const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Completed, .final_text = "done", .stop_reason = ""};
  });
  expect(started.has_value(), "background registry starts concurrent join worker");
  if (!started)
    return;
  auto completed = registry.wait(started->job_id, std::chrono::milliseconds(1000));
  expect(completed && completed->state == ava::agent::BackgroundJobState::Completed, "background registry has a terminal job before concurrent join");

  std::atomic_bool threw = false;
  std::atomic_size_t joined_total = 0;
  auto joiner = [&] {
    try
    {
      joined_total.fetch_add(registry.join_finished(), std::memory_order_relaxed);
    }
    catch (...)
    {
      threw = true;
    }
  };
  std::thread first(joiner);
  std::thread second(joiner);
  first.join();
  second.join();
  expect(!threw && joined_total.load(std::memory_order_relaxed) == 1, "background registry concurrent join_finished calls join exactly once");
}

void test_background_job_registry_prunes_retained_finished_jobs()
{
  ava::agent::BackgroundJobRegistry registry(ava::agent::BackgroundJobRegistryOptions{.max_running_jobs = 4, .max_retained_finished_jobs = 1});
  auto first = registry.start(ava::agent::BackgroundJobStartOptions{.title = "first retained"}, [](ava::agent::BackgroundJobContext const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Completed, .final_text = "first", .stop_reason = ""};
  });
  auto second = registry.start(ava::agent::BackgroundJobStartOptions{.title = "second retained"}, [](ava::agent::BackgroundJobContext const&) {
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Completed, .final_text = "second", .stop_reason = ""};
  });
  expect(first && second, "background registry starts retained-pruning workers");
  if (first)
    expect(registry.wait(first->job_id, std::chrono::milliseconds(1000)).has_value(), "first retained-pruning job completes");
  if (second)
    expect(registry.wait(second->job_id, std::chrono::milliseconds(1000)).has_value(), "second retained-pruning job completes");
  registry.join_finished();
  expect(registry.snapshot().size() <= 1, "background registry prunes joined terminal snapshots beyond retention limit");
}

void test_background_job_registry_destructor_stops_running_jobs()
{
  struct State
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
    bool canceled = false;
  };
  auto state = std::make_shared<State>();

  {
    ava::agent::BackgroundJobRegistry registry;
    auto started = registry.start(ava::agent::BackgroundJobStartOptions{.title = "destructor stop"}, [state](ava::agent::BackgroundJobContext const& context) {
      std::unique_lock lock(state->mutex);
      std::stop_callback notify_stop(context.stop_token, [&] { state->changed.notify_all(); });
      state->started = true;
      state->changed.notify_all();
      state->changed.wait(lock, [&] { return context.stop_token.stop_requested(); });
      state->canceled = true;
      state->changed.notify_all();
      return ava::agent::BackgroundJobCompletion{
          .state = ava::agent::BackgroundJobState::Canceled,
          .final_text = "",
          .stop_reason = "canceled",
      };
    });
    expect(started.has_value(), "background registry starts destructor stop worker");
    std::unique_lock lock(state->mutex);
    expect(state->changed.wait_for(lock, std::chrono::milliseconds(1000), [&] { return state->started; }),
           "background registry destructor test worker starts before scope exit");
  }

  std::lock_guard lock(state->mutex);
  expect(state->canceled, "background registry destructor requests stop and joins running jobs");
}

void test_agent_loop_permission_resolver_threads_to_tools()
{
  auto const root = temp_root() / "agent-permission-resolver";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  expect(::chmod(temp_root().c_str(), S_IRWXU) == 0 && ::chmod(root.c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0,
         "agent command permission workspace is owner-only for sealed planning");
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside via agent";
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "resolver"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_outside\",\"name\":\"read_file\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_outside\",\"delta\":\"{"
                                                    "\\\"path\\\":\\\"" +
                                                    ava::core::json::escape(outside_path.generic_string()) +
                                                    "\\\"}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"used resolver\"}\n\n"
                                                    "data: [DONE]\n\n")});
  int prompts = 0;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver =
          [&prompts, &outside_path](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++prompts;
        expect(prompt.target_path == outside_path, "agent loop resolver sees tool target path");
        return ava::permissions::PermissionResolution::Allow;
      },
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("read outside", store, provider, transport);
  expect(result && result->final_text == "used resolver" && prompts == 1 && result->tool_timeline.size() == 1 &&
             result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success,
         "agent loop threads permission resolver into tool dispatcher");
  expect(transport.requests().size() == 2 && transport.requests()[1].body.find("outside via agent") != std::string::npos,
         "agent loop continuation includes resolver-approved tool result");
  auto resolver_entries = store.load();
  auto resolver_audits = resolver_entries ? permission_entries(*resolver_entries) : std::vector<ava::session::SessionEntry>{};
  auto const resolver_permission_request_id =
      resolver_audits.size() >= 2 ? ava::core::json::string_field(resolver_audits[0].data_json, "permission_request_id").value_or("") : "";
  expect(resolver_audits.size() == 2 && resolver_permission_request_id.starts_with("permreq_") &&
             ava::core::json::string_field(resolver_audits[0].data_json, "action") == "ask" &&
             ava::core::json::string_field(resolver_audits[0].data_json, "resolution_source") == "policy" &&
             ava::core::json::string_field(resolver_audits[1].data_json, "permission_request_id") == resolver_permission_request_id &&
             ava::core::json::string_field(resolver_audits[1].data_json, "resolution") == "allow" &&
             ava::core::json::string_field(resolver_audits[1].data_json, "resolution_source") == "resolver",
         "agent loop persists linked ask and resolver permission audit entries");
  auto const resolver_structured_permission_ids =
      result && !result->tool_timeline.empty()
          ? ava::core::json::strings_in_array_field(result->tool_timeline.front().structured_result_json, "permission_request_ids")
          : std::vector<std::string>{};
  expect(resolver_structured_permission_ids.size() == 1 && resolver_structured_permission_ids[0] == resolver_permission_request_id,
         "agent loop links structured tool result to permission audit request id");
  expect(result && !result->tool_timeline.empty() && result->tool_timeline.front().permission_request_ids.size() == 1 &&
             result->tool_timeline.front().permission_request_ids[0] == resolver_permission_request_id,
         "agent loop exposes permission request ids on tool timeline entries");

  {
    auto const bash_root = temp_root() / "agent-bash-ask-allow";
    std::filesystem::remove_all(bash_root, remove_error);
    auto const bash_workspace = bash_root / "workspace";
    std::filesystem::create_directories(bash_workspace);
    expect(::chmod(bash_root.c_str(), S_IRWXU) == 0 && ::chmod(bash_workspace.c_str(), S_IRWXU) == 0,
           "agent bash allow workspace is owner-only for sealed planning");
    ava::session::SessionStore bash_store(
        ava::session::SessionStoreOptions{.root_dir = bash_root / "sessions", .workspace_dir = bash_workspace, .session_id = "bash-allow"});
    ava::tests::FakeTransport bash_transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
                                                           "data: "
                                                           "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_bash\",\"delta\":\"{"
                                                           "\\\"command\\\":\\\"true\\\"}\"}\n\n"
                                                           "data: [DONE]\n\n"),
                                              sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"bash allowed\"}\n\n"
                                                           "data: [DONE]\n\n")});
    int bash_allow_prompts = 0;
    ava::agent::AgentLoop bash_loop(ava::agent::AgentLoopOptions{
        .workspace_dir = bash_workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .permission_resolver =
            [&bash_allow_prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          ++bash_allow_prompts;
          expect(prompt.operation == ava::permissions::Operation::RunCommand, "agent bash allow resolver sees run command");
          expect(prompt.command == "true", "agent bash allow resolver sees command text");
          return ava::permissions::PermissionResolution::Allow;
        },
        .append_entry = append_route_for_test(bash_store),
        .session_read_authority = read_authority_for_test(bash_store),
    });
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash allowed" && bash_allow_prompts == 1 && bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success,
           "agent loop allows bash Ask decisions when resolver allows once");
  }

  {
    auto const bash_root = temp_root() / "agent-bash-ask-deny";
    std::filesystem::remove_all(bash_root, remove_error);
    auto const bash_workspace = bash_root / "workspace";
    std::filesystem::create_directories(bash_workspace);
    expect(::chmod(bash_root.c_str(), S_IRWXU) == 0 && ::chmod(bash_workspace.c_str(), S_IRWXU) == 0,
           "agent bash deny workspace is owner-only for sealed planning");
    ava::session::SessionStore bash_store(
        ava::session::SessionStoreOptions{.root_dir = bash_root / "sessions", .workspace_dir = bash_workspace, .session_id = "bash-deny"});
    ava::tests::FakeTransport bash_transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
                                                           "data: "
                                                           "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_bash\",\"delta\":\"{"
                                                           "\\\"command\\\":\\\"true\\\"}\"}\n\n"
                                                           "data: [DONE]\n\n"),
                                              sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"bash denied\"}\n\n"
                                                           "data: [DONE]\n\n")});
    int bash_deny_prompts = 0;
    ava::agent::AgentLoop bash_loop(ava::agent::AgentLoopOptions{
        .workspace_dir = bash_workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .permission_resolver =
            [&bash_deny_prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          ++bash_deny_prompts;
          expect(prompt.operation == ava::permissions::Operation::RunCommand, "agent bash deny resolver sees run command");
          return ava::permissions::PermissionResolution::Deny;
        },
        .append_entry = append_route_for_test(bash_store),
        .session_read_authority = read_authority_for_test(bash_store),
    });
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash denied" && bash_deny_prompts == 1 && bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error,
           "agent loop records denied bash Ask decisions as failed tool results and continues");
    auto bash_entries = bash_store.load();
    auto bash_audits = bash_entries ? permission_entries(*bash_entries) : std::vector<ava::session::SessionEntry>{};
    expect(bash_audits.size() == 2 && ava::core::json::string_field(bash_audits[1].data_json, "command") == "<redacted one-shot command>" &&
               ava::core::json::string_field(bash_audits[1].data_json, "resolution") == "deny" &&
               ava::core::json::string_field(bash_audits[1].data_json, "resolution_source") == "resolver",
           "agent loop persists resolver-denied command permission audit entries");
  }

  {
    auto const bash_root = temp_root() / "agent-bash-ask-fail";
    std::filesystem::remove_all(bash_root, remove_error);
    auto const bash_workspace = bash_root / "workspace";
    std::filesystem::create_directories(bash_workspace);
    expect(::chmod(bash_root.c_str(), S_IRWXU) == 0 && ::chmod(bash_workspace.c_str(), S_IRWXU) == 0,
           "agent bash resolver failure workspace is owner-only for sealed planning");
    ava::session::SessionStore bash_store(
        ava::session::SessionStoreOptions{.root_dir = bash_root / "sessions", .workspace_dir = bash_workspace, .session_id = "bash-fail"});
    ava::tests::FakeTransport bash_transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
                                                           "data: "
                                                           "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_bash\",\"delta\":\"{"
                                                           "\\\"command\\\":\\\"true\\\"}\"}\n\n"
                                                           "data: [DONE]\n\n"),
                                              sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"bash resolver failed\"}\n\n"
                                                           "data: [DONE]\n\n")});
    int bash_fail_prompts = 0;
    ava::agent::AgentLoop bash_loop(ava::agent::AgentLoopOptions{
        .workspace_dir = bash_workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .permission_resolver =
            [&bash_fail_prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          ++bash_fail_prompts;
          expect(prompt.operation == ava::permissions::Operation::RunCommand, "agent bash fail resolver sees run command");
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
        },
        .append_entry = append_route_for_test(bash_store),
        .session_read_authority = read_authority_for_test(bash_store),
    });
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash resolver failed" && bash_fail_prompts == 1 && bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error && bash_transport.requests().size() == 2,
           "agent loop records failed bash Ask resolver as failed tool result and continues");
  }
}

void test_agent_loop_question_resolver_threads_to_tools()
{
  auto const root = temp_root() / "agent-question-resolver";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "question-resolver"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_question\",\"name\":\"question\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_question\",\"delta\":\"{"
                                                    "\\\"question\\\":\\\"Pick one?\\\",\\\"options\\\":[\\\"A\\\",\\\"B\\\"]}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"question answered\"}\n\n"
                                                    "data: [DONE]\n\n")});
  int prompts = 0;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .question_resolver = [&prompts](ava::agent::QuestionPrompt const& prompt) -> ava::core::Result<ava::agent::QuestionAnswer> {
        ++prompts;
        expect(prompt.question == "Pick one?" && prompt.options.size() == 2, "agent loop question resolver receives provider prompt");
        return ava::agent::QuestionAnswer{.selected_options = {"B"}, .custom_text = ""};
      },
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("ask", store, provider, transport);
  expect(result && result->final_text == "question answered" && prompts == 1 && result->tool_timeline.size() == 1 &&
             result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success,
         "agent loop threads question resolver into tool dispatcher");
  expect(transport.requests().size() == 2 && transport.requests()[1].body.find("\\\"selected_options\\\":[\\\"B\\\"]") != std::string::npos,
         "agent loop continuation includes serialized question answer");
}

void test_agent_loop_non_stream_response()
{
  auto const root = temp_root() / "agent-non-stream";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "nonstream"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200, .headers = {}, .body = "{\"status\":\"completed\",\"output_text\":\"plain response with data: literal\"}"}});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .stream = false,
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("hi", store, provider, transport);
  expect(result && result->final_text == "plain response with data: literal", "agent loop parses non-stream response without sniffing data text");
  expect(!transport.requests().empty() && transport.requests()[0].body.find("\"stream\":false") != std::string::npos,
         "agent loop passes explicit non-stream request expectation");
}

void test_agent_loop_non_stream_error_prevents_tool_dispatch()
{
  auto const root = temp_root() / "agent-nonstream-provider-error";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "nonstream-provider-error"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 200,
                                   .headers = {},
                                   .body = "{\"output\":[{\"id\":\"fc_first\",\"type\":\"function_call\",\"call_id\":\"call_duplicate\","
                                           "\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\\\"README.md\\\"}\"},{\"id\":\"fc_second\","
                                           "\"type\":\"function_call\",\"call_id\":\"call_duplicate\",\"name\":\"read_file\","
                                           "\"arguments\":\"{\\\"path\\\":\\\"README.md\\\"}\"}]}"}});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .stream = false,
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("read", store, provider, transport);
  auto entries = store.load();
  expect(!result && result.error().category() == ava::core::ErrorCategory::Provider && entries &&
             std::none_of(entries->begin(), entries->end(),
                          [](ava::session::SessionEntry const& entry) { return entry.type == ava::session::EntryType::ToolCall; }),
         "a non-stream OpenAI parser Error prevents dispatch of an earlier otherwise valid tool call");
}

void test_agent_loop_stream_unended_documented_function_prevents_dispatch()
{
  auto const root = temp_root() / "agent-stream-unended-documented-function";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "stream-unended-documented-function"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_open\",\"type\":\"function_call\","
                    "\"call_id\":\"call_open\",\"name\":\"read_file\",\"arguments\":\"\"}}\n\n"
                    "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("read", store, provider, transport);
  auto entries = store.load();
  expect(!result && result.error().category() == ava::core::ErrorCategory::Provider && entries &&
             std::none_of(entries->begin(), entries->end(),
                          [](ava::session::SessionEntry const& entry) { return entry.type == ava::session::EntryType::ToolCall; }),
         "an unfinished documented streaming function item fails before agent tool dispatch");
}

void test_agent_loop_stream_post_terminal_function_prevents_dispatch()
{
  auto const root = temp_root() / "agent-stream-post-terminal-function";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "stream-post-terminal-function"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n"
                    "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_late\",\"type\":\"function_call\","
                    "\"call_id\":\"call_late\",\"name\":\"list_directory\",\"arguments\":\"{}\"}}\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("list", store, provider, transport);
  auto entries = store.load();
  expect(!result && result.error().category() == ava::core::ErrorCategory::Provider && entries &&
             std::none_of(entries->begin(), entries->end(),
                          [](ava::session::SessionEntry const& entry) { return entry.type == ava::session::EntryType::ToolCall; }),
         "a documented function item after the terminal response boundary fails before agent tool dispatch");
}

void test_agent_loop_compaction_status_metadata()
{
  auto const root = temp_root() / "agent-compaction-status";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "compaction-status"});
  auto appended = append_session_entry_for_test(store, ava::session::SessionEntry{.id = "entry_compaction_status",
                                                                                  .parent_id = "",
                                                                                  .type = ava::session::EntryType::Compaction,
                                                                                  .timestamp = ava::session::now_timestamp(),
                                                                                  .data_json = "{\"summary\":\"older context\"}"});
  expect(appended.has_value(), "agent loop compaction metadata test seeds compaction entry");
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"after compaction\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("continue", store, provider, transport);
  expect(result && result->used_compacted_context && result->initial_context_messages == 2 && result->outcome == ava::core::RuntimeTerminalOutcome::Completed,
         "agent loop status metadata reports compacted initial provider context");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("Compacted prior conversation summary") != std::string::npos,
         "agent loop sends compacted context in initial provider request");
}

void test_agent_loop_replays_steering_after_mid_turn_auto_compaction()
{
  auto const root = temp_root() / "agent-steering-compaction-replay";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "steering-replay"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"ok\"}\n\n"
                    "data: [DONE]\n\n")});
  int compact_calls = 0;
  bool steering_taken = false;
  auto append_lease = ava::session::SessionLease::create_and_acquire(store.session_path());
  expect(append_lease.has_value(), "steering compaction fixture acquires its append lease");
  if (!append_lease)
    return;
  auto append_target = ava::session::SessionAppendTarget::create_persistent(store, *append_lease);
  expect(append_target.has_value(), "steering compaction fixture creates its append target");
  if (!append_target)
    return;
  auto read_authority = (*append_target)->read_authority();
  expect(read_authority.has_value(), "steering compaction fixture creates its read authority");
  if (!read_authority)
    return;
  auto append_route = [target = std::move(*append_target)](ava::session::SessionEntry entry) { return target->append(entry); };
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .take_steering_messages = [&steering_taken]() -> ava::core::Result<std::vector<std::string>> {
        if (steering_taken)
          return std::vector<std::string>{};
        steering_taken = true;
        return std::vector<std::string>{"mid-turn steering"};
      },
      .compact_context = [&compact_calls, &append_lease, &store](ava::session::SessionReadAuthority, std::string_view trigger,
                                                                 std::vector<std::string> const&) -> ava::core::Result<bool> {
        ++compact_calls;
        if (trigger == "auto" && compact_calls == 2)
        {
          auto appended = store.append(*append_lease, ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                                 .parent_id = "",
                                                                                 .type = ava::session::EntryType::Compaction,
                                                                                 .timestamp = ava::session::now_timestamp(),
                                                                                 .data_json = "{\"summary\":\"mid turn\"}"});
          if (!appended)
            return std::unexpected(std::move(appended.error()));
          return true;
        }
        return false;
      },
      .append_entry = append_route,
      .session_read_authority = std::move(*read_authority),
  });

  auto result = loop.run_turn("initial prompt", store, provider, transport);
  expect(result && result->final_text == "ok", "agent loop succeeds after mid-turn auto compaction");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("initial prompt") != std::string::npos &&
             transport.requests()[0].body.find("mid-turn steering") != std::string::npos,
         "mid-turn auto compaction replays both the initial prompt and consumed steering messages");
}

void test_agent_loop_context_overflow_retry_skips_duplicate_auto_compaction()
{
  auto const root = temp_root() / "agent-overflow-skip-auto";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "overflow-skip-auto"});
  OverflowOnceProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"retry ok\"}\n\n"
                    "data: [DONE]\n\n")});
  bool overflow_compacted = false;
  std::vector<std::string> triggers;
  auto append_lease = ava::session::SessionLease::create_and_acquire(store.session_path());
  expect(append_lease.has_value(), "overflow compaction fixture acquires its append lease");
  if (!append_lease)
    return;
  auto append_target = ava::session::SessionAppendTarget::create_persistent(store, *append_lease);
  expect(append_target.has_value(), "overflow compaction fixture creates its append target");
  if (!append_target)
    return;
  auto read_authority = (*append_target)->read_authority();
  expect(read_authority.has_value(), "overflow compaction fixture creates its read authority");
  if (!read_authority)
    return;
  auto append_route = [target = std::move(*append_target)](ava::session::SessionEntry entry) { return target->append(entry); };
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .compact_context = [&append_lease, &overflow_compacted, &triggers, &store](ava::session::SessionReadAuthority, std::string_view trigger,
                                                                                 std::vector<std::string> const&) -> ava::core::Result<bool> {
        triggers.push_back(std::string(trigger));
        if (trigger == "context_overflow")
        {
          overflow_compacted = true;
          auto appended = store.append(*append_lease, ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                                 .parent_id = "",
                                                                                 .type = ava::session::EntryType::Compaction,
                                                                                 .timestamp = ava::session::now_timestamp(),
                                                                                 .data_json = "{\"summary\":\"overflow summary\"}"});
          if (!appended)
            return std::unexpected(std::move(appended.error()));
          return true;
        }
        if (trigger == "auto" && overflow_compacted)
        {
          auto appended = store.append(*append_lease, ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                                 .parent_id = "",
                                                                                 .type = ava::session::EntryType::Compaction,
                                                                                 .timestamp = ava::session::now_timestamp(),
                                                                                 .data_json = "{\"summary\":\"duplicate\"}"});
          if (!appended)
            return std::unexpected(std::move(appended.error()));
          return true;
        }
        return false;
      },
      .append_entry = append_route,
      .session_read_authority = std::move(*read_authority),
  });

  auto result = loop.run_turn("overflow prompt", store, provider, transport);
  auto entries = store.load();
  auto const compactions = entries ? static_cast<std::size_t>(std::ranges::count_if(
                                         *entries, [](ava::session::SessionEntry const& entry) { return entry.type == ava::session::EntryType::Compaction; }))
                                   : 0;
  auto const user_messages = entries
                                 ? static_cast<std::size_t>(std::ranges::count_if(
                                       *entries, [](ava::session::SessionEntry const& entry) { return entry.type == ava::session::EntryType::UserMessage; }))
                                 : 0;
  expect(result && result->final_text == "retry ok", "context overflow retry succeeds after compaction");
  expect(triggers == std::vector<std::string>({"auto", "auto", "context_overflow"}), "context overflow retry skips immediate duplicate auto compaction");
  expect(entries && compactions == 1 && user_messages == 2, "context overflow retry appends one compaction and replays the active prompt once");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("overflow summary") != std::string::npos &&
             transport.requests()[0].body.find("overflow prompt") != std::string::npos,
         "context overflow retry rebuilds context from the overflow compaction boundary");
}

void test_agent_loop_multiple_tools_and_denied_continuation()
{
  auto const root = temp_root() / "agent-multi-tools";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream one(workspace / "one.txt", std::ios::binary | std::ios::trunc);
    one << "one";
    std::ofstream two(workspace / "two.txt", std::ios::binary | std::ios::trunc);
    two << "two";
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "multi"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
                                                    "\\\"one.txt\\\"}\"}\n\n"
                                                    "data: {\"type\":\"response.function_call.done\",\"call_id\":\"call_1\"}\n\n"
                                                    "data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_2\",\"name\":\"read_file\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_2\",\"delta\":\"{\\\"path\\\":"
                                                    "\\\"two.txt\\\"}\"}\n\n"
                                                    "data: {\"type\":\"response.function_call.done\",\"call_id\":\"call_2\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"done\"}\n\n"
                                                    "data: [DONE]\n\n")});
  std::vector<ava::agent::ToolTimelineEntry> tool_events;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .on_tool_event = [&tool_events](auto const& entry) { tool_events.push_back(entry); },
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("read both", store, provider, transport);
  expect(result && result->tool_calls == 2 && result->final_text == "done", "agent loop handles multiple tool calls before continuation");
  expect(result && result->tool_timeline.size() == 2 && result->tool_timeline[0].call_id == "call_1" && result->tool_timeline[0].name == "read_file" &&
             result->tool_timeline[0].status == ava::agent::ToolTimelineStatus::Success &&
             result->tool_timeline[0].argument_summary.find("path=one.txt") != std::string::npos && result->tool_timeline[0].output_lines &&
             *result->tool_timeline[0].output_lines == 1 &&
             result->tool_timeline[0].structured_result_json.find("\"call_id\":\"call_1\"") != std::string::npos &&
             result->tool_timeline[1].call_id == "call_2" && result->tool_timeline[1].name == "read_file" &&
             result->tool_timeline[1].status == ava::agent::ToolTimelineStatus::Success &&
             result->tool_timeline[1].argument_summary.find("path=two.txt") != std::string::npos && result->tool_timeline[1].output_lines &&
             *result->tool_timeline[1].output_lines == 1 && result->tool_timeline[1].structured_result_json.find("\"call_id\":\"call_2\"") != std::string::npos,
         "agent loop preserves provider-order timeline metadata for multiple tool calls");
  expect(tool_events.size() == 4 && tool_events[0].call_id == "call_1" && tool_events[0].status == ava::agent::ToolTimelineStatus::Running &&
             tool_events[1].call_id == "call_1" && tool_events[1].status == ava::agent::ToolTimelineStatus::Success && tool_events[2].call_id == "call_2" &&
             tool_events[2].status == ava::agent::ToolTimelineStatus::Running && tool_events[3].call_id == "call_2" &&
             tool_events[3].status == ava::agent::ToolTimelineStatus::Success,
         "agent loop publishes running and completed tool events in provider order");

  auto entries = store.load();
  std::vector<ava::session::SessionEntry const*> replay_entries;
  if (entries)
  {
    for (auto const& entry : *entries)
    {
      if (entry.type == ava::session::EntryType::AssistantMessage || entry.type == ava::session::EntryType::ToolCall ||
          entry.type == ava::session::EntryType::ToolResult)
      {
        replay_entries.push_back(&entry);
      }
    }
  }
  expect(entries && replay_entries.size() == 6, "agent loop multi-tool session has assistant, paired tools, and final assistant replay entries");
  if (entries && replay_entries.size() == 6)
  {
    expect(replay_entries[0]->type == ava::session::EntryType::AssistantMessage && replay_entries[1]->type == ava::session::EntryType::ToolCall &&
               replay_entries[2]->type == ava::session::EntryType::ToolResult && replay_entries[3]->type == ava::session::EntryType::ToolCall &&
               replay_entries[4]->type == ava::session::EntryType::ToolResult && replay_entries[5]->type == ava::session::EntryType::AssistantMessage,
           "agent loop persists multi-tool entries as AssistantMessage -> ToolCall_1 -> ToolResult_1 -> ToolCall_2 -> ToolResult_2 before continuation");
    expect(replay_entries[0]->data_json.find("\"tool_calls\":2") != std::string::npos &&
               ava::core::json::string_field(replay_entries[1]->data_json, "call_id").value_or("") == "call_1" &&
               ava::core::json::string_field(replay_entries[1]->data_json, "name").value_or("") == "read_file" &&
               ava::core::json::string_field(replay_entries[2]->data_json, "call_id").value_or("") == "call_1" &&
               ava::core::json::string_field(replay_entries[2]->data_json, "name").value_or("") == "read_file" &&
               ava::core::json::string_field(replay_entries[3]->data_json, "call_id").value_or("") == "call_2" &&
               ava::core::json::string_field(replay_entries[3]->data_json, "name").value_or("") == "read_file" &&
               ava::core::json::string_field(replay_entries[4]->data_json, "call_id").value_or("") == "call_2" &&
               ava::core::json::string_field(replay_entries[4]->data_json, "name").value_or("") == "read_file" &&
               ava::core::json::string_field(replay_entries[5]->data_json, "text").value_or("") == "done",
           "agent loop keeps provider call ids attached to their immediate provider-order tool results");
  }

  auto const denied_root = temp_root() / "agent-denied-continuation";
  std::filesystem::remove_all(denied_root, remove_error);
  auto const denied_workspace = denied_root / "workspace";
  std::filesystem::create_directories(denied_workspace);
  ava::session::SessionStore denied_store(
      ava::session::SessionStoreOptions{.root_dir = denied_root / "sessions", .workspace_dir = denied_workspace, .session_id = "denied"});
  ava::tests::FakeTransport denied_transport(
      {sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_write\",\"name\":\"write_file\"}\n\n"
                    "data: "
                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_write\",\"delta\":\"{\\\"path\\\":"
                    "\\\"src/new.cpp\\\",\\\"content\\\":\\\"bad\\\"}\"}\n\n"
                    "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"permission explained\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop denied_loop(ava::agent::AgentLoopOptions{
      .workspace_dir = denied_workspace,
      .mode = ava::agent::Mode::Plan,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .openai_oauth = true,
      .openai_account_id = "acct_123",
      .append_entry = append_route_for_test(denied_store),
      .session_read_authority = read_authority_for_test(denied_store),
  });
  auto denied_result = denied_loop.run_turn("write source", denied_store, provider, denied_transport);
  expect(denied_result && denied_result->final_text == "permission explained" && denied_result->provider_iterations == 2,
         "agent loop continues after permission-denied tool results");
  expect(denied_result && denied_result->tool_timeline.size() == 1 && denied_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error &&
             denied_result->tool_timeline.front().argument_summary.find("content=3 bytes") != std::string::npos &&
             denied_result->tool_timeline.front().argument_summary.find("bad") == std::string::npos &&
             denied_result->tool_timeline.front().result_summary.find("error:") == 0,
         "agent loop marks denied tool results as safe error timeline entries");
  expect(denied_transport.requests().size() == 2 && denied_transport.requests()[1].body.find("permission_denied") != std::string::npos,
         "permission-denied tool result is framed into continuation context");
}

void test_agent_loop_parallel_read_search_preserves_provider_order_and_replay()
{
  auto const root = temp_root() / "agent-parallel-read-search";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace / "a");
  std::filesystem::create_directories(workspace / "b");
  for (int index = 0; index < 500; ++index)
  {
    std::ofstream a(workspace / "a" / ("file_" + std::to_string(index) + ".txt"), std::ios::binary | std::ios::trunc);
    a << "alpha " << index;
    std::ofstream b(workspace / "b" / ("file_" + std::to_string(index) + ".txt"), std::ios::binary | std::ios::trunc);
    b << "bravo " << index;
  }

  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "parallel-order"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(tool_call_sse("glob_a", "glob", R"({"pattern":"a/*.txt"})") +
                                                    tool_call_sse("glob_b", "glob", R"({"pattern":"b/*.txt"})") + "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"done\"}\n\n"
                                                    "data: [DONE]\n\n")});
  std::vector<ava::agent::ToolTimelineEntry> tool_events;
  std::vector<ava::agent::ToolProgressEntry> progress_events;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .on_tool_event = [&tool_events](auto const& entry) { tool_events.push_back(entry); },
                                                          .on_tool_progress = [&progress_events](auto const& entry) -> ava::core::VoidResult {
                                                            progress_events.push_back(entry);
                                                            return {};
                                                          },
                                                          .append_entry = append_route_for_test(store),
                                                          .session_read_authority = read_authority_for_test(store),
                                                          .parallel_read_search_tools = true,
                                                          .parallel_read_search_max_workers = 2});
  auto result = loop.run_turn("glob both", store, provider, transport);
  expect(result && result->final_text == "done" && result->tool_calls == 2 && result->provider_iterations == 2,
         "parallel read/search opt-in completes provider continuation");
  expect(result && result->tool_timeline.size() == 2 && result->tool_timeline[0].call_id == "glob_a" &&
             result->tool_timeline[0].status == ava::agent::ToolTimelineStatus::Success && result->tool_timeline[0].total_matches &&
             *result->tool_timeline[0].total_matches == 500 && result->tool_timeline[1].call_id == "glob_b" &&
             result->tool_timeline[1].status == ava::agent::ToolTimelineStatus::Success && result->tool_timeline[1].total_matches &&
             *result->tool_timeline[1].total_matches == 500,
         "parallel read/search timeline remains in provider order");
  expect(tool_events.size() == 4 && tool_events[0].call_id == "glob_a" && tool_events[0].status == ava::agent::ToolTimelineStatus::Running &&
             tool_events[1].call_id == "glob_a" && tool_events[1].status == ava::agent::ToolTimelineStatus::Success && tool_events[2].call_id == "glob_b" &&
             tool_events[2].status == ava::agent::ToolTimelineStatus::Running && tool_events[3].call_id == "glob_b" &&
             tool_events[3].status == ava::agent::ToolTimelineStatus::Success,
         "parallel read/search publishes running and final tool events in provider order");

  std::size_t glob_a_progress = 0;
  std::size_t glob_b_progress = 0;
  bool saw_glob_b_progress = false;
  bool glob_a_after_glob_b = false;
  for (auto const& event : progress_events)
  {
    if (event.call_id == "glob_b")
      saw_glob_b_progress = true;
    if (event.call_id == "glob_a")
    {
      ++glob_a_progress;
      glob_a_after_glob_b = glob_a_after_glob_b || saw_glob_b_progress;
    }
    if (event.call_id == "glob_b")
      ++glob_b_progress;
  }
  expect(glob_a_progress >= 2 && glob_b_progress >= 2 && !glob_a_after_glob_b, "parallel read/search buffers progress and publishes it by provider slot");

  auto entries = store.load();
  auto audits = entries ? permission_entries(*entries) : std::vector<ava::session::SessionEntry>{};
  auto const audit_id_0 = audits.size() >= 1 ? ava::core::json::string_field(audits[0].data_json, "permission_request_id").value_or("") : "";
  auto const audit_id_1 = audits.size() >= 2 ? ava::core::json::string_field(audits[1].data_json, "permission_request_id").value_or("") : "";
  expect(audits.size() == 2 && audit_id_0.starts_with("permreq_") && audit_id_1.starts_with("permreq_") && audit_id_0 != audit_id_1 &&
             ava::core::json::string_field(audits[0].data_json, "operation") == "search" &&
             ava::core::json::string_field(audits[1].data_json, "operation") == "search",
         "parallel read/search commits unique buffered permission audit entries in provider order");

  std::vector<ava::session::SessionEntry const*> ordered_entries;
  if (entries)
  {
    for (auto const& entry : *entries)
    {
      if (entry.type == ava::session::EntryType::AssistantMessage || entry.type == ava::session::EntryType::ToolCall ||
          entry.type == ava::session::EntryType::PermissionDecision || entry.type == ava::session::EntryType::ToolResult)
      {
        ordered_entries.push_back(&entry);
      }
    }
  }
  expect(entries && ordered_entries.size() == 8, "parallel read/search session has assistant, ordered tool pairs, audits, and final assistant");
  if (entries && ordered_entries.size() == 8)
  {
    expect(ordered_entries[0]->type == ava::session::EntryType::AssistantMessage && ordered_entries[1]->type == ava::session::EntryType::ToolCall &&
               ordered_entries[2]->type == ava::session::EntryType::PermissionDecision && ordered_entries[3]->type == ava::session::EntryType::ToolResult &&
               ordered_entries[4]->type == ava::session::EntryType::ToolCall && ordered_entries[5]->type == ava::session::EntryType::PermissionDecision &&
               ordered_entries[6]->type == ava::session::EntryType::ToolResult && ordered_entries[7]->type == ava::session::EntryType::AssistantMessage &&
               ava::core::json::string_field(ordered_entries[1]->data_json, "call_id") == "glob_a" &&
               ava::core::json::string_field(ordered_entries[3]->data_json, "call_id") == "glob_a" &&
               ava::core::json::string_field(ordered_entries[4]->data_json, "call_id") == "glob_b" &&
               ava::core::json::string_field(ordered_entries[6]->data_json, "call_id") == "glob_b",
           "parallel read/search persists ToolCall -> PermissionDecision -> ToolResult in provider order");
  }
  auto const continuation = transport.requests().size() >= 2 ? transport.requests()[1].body : std::string{};
  auto const continuation_a = continuation.find("glob_a");
  auto const continuation_b = continuation.find("glob_b");
  expect(transport.requests().size() == 2 && continuation_a != std::string::npos && continuation_b != std::string::npos && continuation_a < continuation_b,
         "parallel read/search continuation replay keeps provider-order tool results");
}

void test_agent_loop_parallel_read_search_zero_max_workers_clamps_to_one()
{
  auto const root = temp_root() / "agent-parallel-zero-workers";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream one(workspace / "one.txt", std::ios::binary | std::ios::trunc);
    one << "one";
    std::ofstream two(workspace / "two.txt", std::ios::binary | std::ios::trunc);
    two << "two";
  }

  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "parallel-zero-workers"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(tool_call_sse("call_1", "read_file", R"({"path":"one.txt"})") +
                                                    tool_call_sse("call_2", "read_file", R"({"path":"two.txt"})") + "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"done\"}\n\n"
                                                    "data: [DONE]\n\n")});
  std::vector<ava::agent::ToolTimelineEntry> tool_events;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .on_tool_event = [&tool_events](auto const& entry) { tool_events.push_back(entry); },
                                                          .append_entry = append_route_for_test(store),
                                                          .session_read_authority = read_authority_for_test(store),
                                                          .parallel_read_search_tools = true,
                                                          .parallel_read_search_max_workers = 0});
  auto result = loop.run_turn("read both with zero worker cap", store, provider, transport);
  expect(result && result->final_text == "done" && result->tool_calls == 2 && result->provider_iterations == 2,
         "parallel read/search zero max_workers is clamped and does not fail the turn");
  expect(tool_events.size() == 4 && tool_events[0].call_id == "call_1" && tool_events[1].call_id == "call_1" &&
             tool_events[1].status == ava::agent::ToolTimelineStatus::Success && tool_events[2].call_id == "call_2" && tool_events[3].call_id == "call_2" &&
             tool_events[3].status == ava::agent::ToolTimelineStatus::Success,
         "zero-worker clamp keeps provider-order tool event commits");
}

void test_agent_loop_parallel_read_search_falls_back_for_ask_preflight()
{
  auto const root = temp_root() / "agent-parallel-ask-fallback";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream outside(outside_path, std::ios::binary | std::ios::trunc);
    outside << "outside content";
    std::ofstream inside(workspace / "inside.txt", std::ios::binary | std::ios::trunc);
    inside << "inside content";
  }

  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "ask-fallback"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(tool_call_sse("outside_read", "read_file", "{\"path\":\"" + ava::core::json::escape(outside_path.generic_string()) + "\"}") +
                    tool_call_sse("inside_read", "read_file", R"({"path":"inside.txt"})") + "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"done\"}\n\n"
                    "data: [DONE]\n\n")});
  auto const main_thread = std::this_thread::get_id();
  std::thread::id resolver_thread;
  int prompts = 0;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [&](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++prompts;
        resolver_thread = std::this_thread::get_id();
        expect(prompt.target_path == outside_path, "parallel ask fallback resolver sees the outside read path");
        auto entries = store.load();
        bool saw_tool_call_before_prompt = false;
        if (entries)
        {
          for (auto const& entry : *entries)
          {
            saw_tool_call_before_prompt =
                saw_tool_call_before_prompt ||
                (entry.type == ava::session::EntryType::ToolCall && ava::core::json::string_field(entry.data_json, "call_id").value_or("") == "outside_read");
          }
        }
        expect(entries && saw_tool_call_before_prompt, "Ask fallback appends the ToolCall before invoking the resolver");
        return ava::permissions::PermissionResolution::Allow;
      },
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
      .parallel_read_search_tools = true,
      .parallel_read_search_max_workers = 2});
  auto result = loop.run_turn("read outside and inside", store, provider, transport);
  expect(result && result->final_text == "done" && prompts == 1 && resolver_thread == main_thread,
         "Ask read/search calls stay on the sequential barrier path when parallel opt-in is enabled");

  auto entries = store.load();
  std::vector<ava::session::SessionEntry const*> ordered_entries;
  if (entries)
  {
    for (auto const& entry : *entries)
    {
      if (entry.type == ava::session::EntryType::ToolCall || entry.type == ava::session::EntryType::PermissionDecision ||
          entry.type == ava::session::EntryType::ToolResult)
      {
        ordered_entries.push_back(&entry);
      }
    }
  }
  expect(entries && ordered_entries.size() == 7, "Ask fallback and later parallel-ready read both persist paired tool entries");
  if (entries && ordered_entries.size() == 7)
  {
    expect(ordered_entries[0]->type == ava::session::EntryType::ToolCall && ordered_entries[1]->type == ava::session::EntryType::PermissionDecision &&
               ordered_entries[2]->type == ava::session::EntryType::PermissionDecision && ordered_entries[3]->type == ava::session::EntryType::ToolResult &&
               ava::core::json::string_field(ordered_entries[0]->data_json, "call_id") == "outside_read" &&
               ava::core::json::string_field(ordered_entries[3]->data_json, "call_id") == "outside_read" &&
               ordered_entries[4]->type == ava::session::EntryType::ToolCall && ordered_entries[5]->type == ava::session::EntryType::PermissionDecision &&
               ordered_entries[6]->type == ava::session::EntryType::ToolResult &&
               ava::core::json::string_field(ordered_entries[4]->data_json, "call_id") == "inside_read" &&
               ava::core::json::string_field(ordered_entries[6]->data_json, "call_id") == "inside_read",
           "Ask fallback preserves sequential ToolCall -> PermissionDecision entries before the next ready read slot");
  }
}

void test_agent_loop_parallel_read_search_active_cancellation_stops_unstarted_slots()
{
  auto const root = temp_root() / "agent-parallel-active-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  for (int index = 1; index <= 4; ++index)
  {
    std::ofstream file(workspace / ("file_" + std::to_string(index) + ".txt"), std::ios::binary | std::ios::trunc);
    file << "file " << index;
  }

  struct ActiveCancelState
  {
    std::mutex mutex;
    std::condition_variable changed;
    std::thread::id main_thread;
    std::vector<std::thread::id> worker_threads;
    bool cancel_requested = false;
    bool timed_out = false;

    bool operator()()
    {
      auto const thread_id = std::this_thread::get_id();
      std::unique_lock lock(mutex);
      if (thread_id == main_thread)
      {
        return cancel_requested;
      }

      if (std::ranges::find(worker_threads, thread_id) == worker_threads.end())
      {
        worker_threads.push_back(thread_id);
      }
      if (worker_threads.size() >= 2)
      {
        cancel_requested = true;
        changed.notify_all();
      }
      if (!changed.wait_for(lock, std::chrono::seconds(5), [&] { return cancel_requested || worker_threads.size() >= 2; }))
      {
        timed_out = true;
        cancel_requested = true;
        changed.notify_all();
      }
      return cancel_requested;
    }
  } cancel_state;
  cancel_state.main_thread = std::this_thread::get_id();

  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "parallel-active-cancel"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(tool_call_sse("call_1", "read_file", R"({"path":"file_1.txt"})") + tool_call_sse("call_2", "read_file", R"({"path":"file_2.txt"})") +
                    tool_call_sse("call_3", "read_file", R"({"path":"file_3.txt"})") + tool_call_sse("call_4", "read_file", R"({"path":"file_4.txt"})") +
                    tool_call_sse("call_bash", "bash", R"({"command":"true"})") + "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"should not continue\"}\n\n"
                    "data: [DONE]\n\n")});
  std::atomic<int> resolver_calls = 0;
  std::vector<ava::agent::ToolTimelineEntry> tool_events;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .on_tool_event = [&tool_events](auto const& entry) { tool_events.push_back(entry); },
      .permission_resolver = [&resolver_calls](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++resolver_calls;
        return ava::permissions::PermissionResolution::Allow;
      },
      .cancel_requested = [&cancel_state] { return cancel_state(); },
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
      .parallel_read_search_tools = true,
      .parallel_read_search_max_workers = 2});
  auto result = loop.run_turn("read four then cancel during active epoch", store, provider, transport);

  std::vector<std::thread::id> worker_threads;
  bool cancel_timed_out = false;
  {
    std::lock_guard lock(cancel_state.mutex);
    worker_threads = cancel_state.worker_threads;
    cancel_timed_out = cancel_state.timed_out;
  }
  expect(!cancel_timed_out, "active parallel cancellation test releases workers through condition variables without timing out");
  expect(worker_threads.size() == 2, "active parallel cancellation stops unstarted later read/search workers after the capped active batch");
  expect(!result && result.error().message().find("canceled") != std::string::npos,
         "active parallel read/search cancellation returns the agent-loop cancellation error");
  expect(resolver_calls.load() == 0, "active parallel cancellation does not call live permission resolvers from workers");
  expect(tool_events.size() == 4 && tool_events[0].call_id == "call_1" && tool_events[0].status == ava::agent::ToolTimelineStatus::Running &&
             tool_events[1].call_id == "call_1" && tool_events[1].status == ava::agent::ToolTimelineStatus::Canceled && tool_events[2].call_id == "call_2" &&
             tool_events[2].status == ava::agent::ToolTimelineStatus::Running && tool_events[3].call_id == "call_2" &&
             tool_events[3].status == ava::agent::ToolTimelineStatus::Canceled,
         "active parallel cancellation commits launched canceled slots in provider order only");

  auto entries = store.load();
  std::vector<ava::session::SessionEntry const*> ordered_tool_entries;
  bool saw_later_slot = false;
  bool saw_cancel_boundary = false;
  if (entries)
  {
    for (auto const& entry : *entries)
    {
      if (entry.type == ava::session::EntryType::ToolCall || entry.type == ava::session::EntryType::ToolResult)
      {
        auto const call_id = ava::core::json::string_field(entry.data_json, "call_id").value_or("");
        saw_later_slot = saw_later_slot || call_id == "call_3" || call_id == "call_4" || call_id == "call_bash";
        ordered_tool_entries.push_back(&entry);
      }
      saw_cancel_boundary = saw_cancel_boundary || entry.type == ava::session::EntryType::Cancel;
    }
  }
  expect(entries && ordered_tool_entries.size() == 4 && !saw_later_slot && saw_cancel_boundary && transport.requests().size() == 1,
         "active parallel cancellation persists only launched tool pairs, appends a cancel boundary, and skips the later barrier/provider continuation");
  if (entries && ordered_tool_entries.size() == 4)
  {
    expect(ordered_tool_entries[0]->type == ava::session::EntryType::ToolCall && ordered_tool_entries[1]->type == ava::session::EntryType::ToolResult &&
               ordered_tool_entries[2]->type == ava::session::EntryType::ToolCall && ordered_tool_entries[3]->type == ava::session::EntryType::ToolResult &&
               ava::core::json::string_field(ordered_tool_entries[0]->data_json, "call_id") == "call_1" &&
               ava::core::json::string_field(ordered_tool_entries[1]->data_json, "call_id") == "call_1" &&
               ava::core::json::string_field(ordered_tool_entries[1]->data_json, "status") == "canceled" &&
               ava::core::json::string_field(ordered_tool_entries[2]->data_json, "call_id") == "call_2" &&
               ava::core::json::string_field(ordered_tool_entries[3]->data_json, "call_id") == "call_2" &&
               ava::core::json::string_field(ordered_tool_entries[3]->data_json, "status") == "canceled",
           "active parallel cancellation keeps provider-order ToolCall -> canceled ToolResult pairs for launched slots");
  }
}

void test_agent_loop_parallel_read_search_cancellation_stops_later_barrier()
{
  auto const root = temp_root() / "agent-parallel-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream one(workspace / "one.txt", std::ios::binary | std::ios::trunc);
    one << "one";
    std::ofstream two(workspace / "two.txt", std::ios::binary | std::ios::trunc);
    two << "two";
  }

  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "parallel-cancel"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(tool_call_sse("call_1", "read_file", R"({"path":"one.txt"})") + tool_call_sse("call_2", "read_file", R"({"path":"two.txt"})") +
                    tool_call_sse("call_bash", "bash", R"({"command":"true"})") + "data: [DONE]\n\n")});
  bool cancel_after_first_tool = false;
  std::vector<ava::agent::ToolTimelineEntry> tool_events;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .on_tool_event =
                                                              [&](ava::agent::ToolTimelineEntry const& entry) {
                                                                tool_events.push_back(entry);
                                                                if (entry.call_id == "call_1" && entry.status == ava::agent::ToolTimelineStatus::Success)
                                                                {
                                                                  cancel_after_first_tool = true;
                                                                }
                                                              },
                                                          .cancel_requested = [&cancel_after_first_tool] { return cancel_after_first_tool; },
                                                          .append_entry = append_route_for_test(store),
                                                          .session_read_authority = read_authority_for_test(store),
                                                          .parallel_read_search_tools = true,
                                                          .parallel_read_search_max_workers = 2});
  auto result = loop.run_turn("read both then cancel before bash", store, provider, transport);
  expect(!result && result.error().message().find("canceled") != std::string::npos,
         "parallel read/search reports cancellation after committing the completed epoch");
  expect(tool_events.size() == 4 && tool_events[0].call_id == "call_1" && tool_events[0].status == ava::agent::ToolTimelineStatus::Running &&
             tool_events[1].call_id == "call_1" && tool_events[1].status == ava::agent::ToolTimelineStatus::Success && tool_events[2].call_id == "call_2" &&
             tool_events[2].status == ava::agent::ToolTimelineStatus::Running && tool_events[3].call_id == "call_2" &&
             tool_events[3].status == ava::agent::ToolTimelineStatus::Success,
         "parallel cancellation commits completed read/search outcomes in provider order and does not publish later barrier events");

  auto entries = store.load();
  std::size_t tool_calls = 0;
  std::size_t tool_results = 0;
  bool saw_bash = false;
  if (entries)
  {
    for (auto const& entry : *entries)
    {
      if (entry.type == ava::session::EntryType::ToolCall)
      {
        ++tool_calls;
        saw_bash = saw_bash || ava::core::json::string_field(entry.data_json, "call_id").value_or("") == "call_bash";
      }
      if (entry.type == ava::session::EntryType::ToolResult)
      {
        ++tool_results;
        saw_bash = saw_bash || ava::core::json::string_field(entry.data_json, "call_id").value_or("") == "call_bash";
      }
    }
  }
  expect(entries && tool_calls == 2 && tool_results == 2 && !saw_bash && transport.requests().size() == 1,
         "parallel cancellation leaves no orphan read/search entries and does not launch the later barrier or continuation");
}

void test_agent_loop_cancellation_stops_later_sequential_tools()
{
  auto const root = temp_root() / "agent-multi-tools-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream one(workspace / "one.txt", std::ios::binary | std::ios::trunc);
    one << "one";
    std::ofstream two(workspace / "two.txt", std::ios::binary | std::ios::trunc);
    two << "two";
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "multi-cancel"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
                    "data: "
                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
                    "\\\"one.txt\\\"}\"}\n\n"
                    "data: {\"type\":\"response.function_call.done\",\"call_id\":\"call_1\"}\n\n"
                    "data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_2\",\"name\":\"read_file\"}\n\n"
                    "data: "
                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_2\",\"delta\":\"{\\\"path\\\":"
                    "\\\"two.txt\\\"}\"}\n\n"
                    "data: {\"type\":\"response.function_call.done\",\"call_id\":\"call_2\"}\n\n"
                    "data: [DONE]\n\n")});
  bool cancel_after_first_tool = false;
  std::vector<ava::agent::ToolTimelineEntry> tool_events;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .on_tool_event =
          [&](ava::agent::ToolTimelineEntry const& entry) {
            tool_events.push_back(entry);
            if (entry.call_id == "call_1" && entry.status == ava::agent::ToolTimelineStatus::Success)
            {
              cancel_after_first_tool = true;
            }
          },
      .cancel_requested = [&cancel_after_first_tool] { return cancel_after_first_tool; },
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });

  auto result = loop.run_turn("read both but cancel after first", store, provider, transport);
  expect(!result && result.error().message().find("canceled") != std::string::npos, "agent loop reports cancellation after the first sequential tool dispatch");
  expect(tool_events.size() == 2 && tool_events[0].call_id == "call_1" && tool_events[0].status == ava::agent::ToolTimelineStatus::Running &&
             tool_events[1].call_id == "call_1" && tool_events[1].status == ava::agent::ToolTimelineStatus::Success,
         "agent loop does not publish events for later provider tool calls after cancellation");

  auto entries = store.load();
  std::size_t tool_calls = 0;
  std::size_t tool_results = 0;
  bool saw_second_tool = false;
  if (entries)
  {
    for (auto const& entry : *entries)
    {
      if (entry.type == ava::session::EntryType::ToolCall)
      {
        ++tool_calls;
        saw_second_tool = saw_second_tool || ava::core::json::string_field(entry.data_json, "call_id").value_or("") == "call_2";
      }
      if (entry.type == ava::session::EntryType::ToolResult)
      {
        ++tool_results;
        saw_second_tool = saw_second_tool || ava::core::json::string_field(entry.data_json, "call_id").value_or("") == "call_2";
      }
    }
  }
  expect(entries && tool_calls == 1 && tool_results == 1 && !saw_second_tool,
         "agent loop cancellation stops launching later tools under current sequential dispatch behavior");
}

void test_agent_loop_tool_delta_dedupes_and_rejects_empty_tool_ids()
{
  ava::provider::OpenAIProvider const provider("https://api.example.test");

  {
    auto const root = temp_root() / "agent-delta-before-start";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    {
      std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
      file << "dedupe content";
    }
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "delta-before-start"});
    ava::tests::FakeTransport transport({sse_response("data: "
                                                      "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
                                                      "\\\"note.txt\\\"}\"}\n\n"
                                                      "data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
                                                      "data: {\"type\":\"response.function_call.done\",\"call_id\":\"call_1\"}\n\n"
                                                      "data: [DONE]\n\n"),
                                         sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"done\"}\n\n"
                                                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .append_entry = append_route_for_test(store),
        .session_read_authority = read_authority_for_test(store),
    });
    auto result = loop.run_turn("read note", store, provider, transport);
    expect(result && result->tool_calls == 1 && result->tool_timeline.size() == 1 &&
               result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success && result->tool_timeline.front().name == "read_file",
           "agent loop deduplicates tool deltas that arrive before tool start events");

    auto entries = store.load();
    std::size_t tool_calls = 0;
    std::size_t tool_results = 0;
    if (entries)
    {
      for (auto const& entry : *entries)
      {
        if (entry.type == ava::session::EntryType::ToolCall)
          ++tool_calls;
        if (entry.type == ava::session::EntryType::ToolResult)
          ++tool_results;
      }
    }
    expect(entries && tool_calls == 1 && tool_results == 1,
           "same-iteration start/delta/end fragments merge into one finalized provider call and one ACP-compatible lifecycle id");
  }

  {
    auto const root = temp_root() / "agent-cross-iteration-duplicate-call-id";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    {
      std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
      file << "duplicate id content";
    }
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "duplicate-call-id"});
    auto const repeated_call = sse_response(
        "data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_reused\",\"name\":\"read_file\"}\n\n"
        "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_reused\",\"delta\":\"{\\\"path\\\":\\\"note.txt\\\"}\"}\n\n"
        "data: {\"type\":\"response.function_call.done\",\"call_id\":\"call_reused\"}\n\n"
        "data: [DONE]\n\n");
    ava::tests::FakeTransport transport({repeated_call, repeated_call});
    std::vector<ava::agent::ToolTimelineEntry> tool_events;
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .on_tool_event = [&tool_events](auto const& event) { tool_events.push_back(event); },
        .append_entry = append_route_for_test(store),
        .session_read_authority = read_authority_for_test(store),
    });
    auto result = loop.run_turn("reuse a call id", store, provider, transport);
    auto entries = store.load();
    std::size_t tool_calls = 0;
    std::size_t tool_results = 0;
    if (entries)
    {
      for (auto const& entry : *entries)
      {
        tool_calls += entry.type == ava::session::EntryType::ToolCall ? 1U : 0U;
        tool_results += entry.type == ava::session::EntryType::ToolResult ? 1U : 0U;
      }
    }
    auto validation = entries ? ava::session::validate_session_replay(*entries) : ava::session::SessionReplayValidation{};
    expect(!result && result.error().category() == ava::core::ErrorCategory::Provider && result.error().message().find("reused") != std::string::npos &&
               result.error().format().find("call_reused") != std::string::npos && tool_calls == 1 && tool_results == 1 && tool_events.size() == 2 &&
               validation.ok(),
           "cross-iteration provider call-id reuse is rejected before a duplicate lifecycle, dispatch, or session record");
  }

  {
    auto const root = temp_root() / "agent-cross-prompt-duplicate-call-id";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    {
      std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
      file << "persistent duplicate id content";
    }
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "cross-prompt-duplicate"});
    auto const tool_call = sse_response(
        "data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_persistent\",\"name\":\"read_file\"}\n\n"
        "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_persistent\",\"delta\":\"{\\\"path\\\":\\\"note.txt\\\"}\"}\n\n"
        "data: {\"type\":\"response.function_call.done\",\"call_id\":\"call_persistent\"}\n\n"
        "data: [DONE]\n\n");
    ava::tests::FakeTransport transport({tool_call,
                                         sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"first done\"}\n\n"
                                                      "data: [DONE]\n\n"),
                                         tool_call});
    std::vector<ava::provider::StreamEvent> stream_events;
    auto options = ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .on_stream_event = [&stream_events](auto const& event) -> ava::core::VoidResult {
          stream_events.push_back(event);
          return {};
        },
        .append_entry = append_route_for_test(store),
        .session_read_authority = read_authority_for_test(store),
    };
    ava::agent::AgentLoop first_loop(options);
    auto first = first_loop.run_turn("first prompt", store, provider, transport);
    auto const events_after_first = stream_events.size();
    ava::agent::AgentLoop second_loop(std::move(options));
    auto second = second_loop.run_turn("second prompt", store, provider, transport);
    auto entries = store.load();
    std::size_t tool_calls = 0;
    std::size_t tool_results = 0;
    if (entries)
    {
      for (auto const& entry : *entries)
      {
        tool_calls += entry.type == ava::session::EntryType::ToolCall ? 1U : 0U;
        tool_results += entry.type == ava::session::EntryType::ToolResult ? 1U : 0U;
      }
    }
    auto validation = entries ? ava::session::validate_session_replay(*entries) : ava::session::SessionReplayValidation{};
    expect(first && !second && second.error().category() == ava::core::ErrorCategory::Provider &&
               second.error().message().find("persistent session") != std::string::npos &&
               second.error().format().find("call_persistent") != std::string::npos && tool_calls == 1 && tool_results == 1 &&
               stream_events.size() == events_after_first && validation.ok(),
           "cross-prompt provider call-id reuse is rejected before duplicate updates, persistence, or dispatch");
  }

  {
    auto const root = temp_root() / "agent-empty-call-id";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "empty-call-id"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"\",\"name\":\"read_file\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .append_entry = append_route_for_test(store),
        .session_read_authority = read_authority_for_test(store),
    });
    auto result = loop.run_turn("read missing-id", store, provider, transport);
    auto entries = store.load();
    bool saw_tool_entry = false;
    if (entries)
    {
      for (auto const& entry : *entries)
      {
        saw_tool_entry = saw_tool_entry || entry.type == ava::session::EntryType::ToolCall || entry.type == ava::session::EntryType::ToolResult;
      }
    }
    expect(!result && result.error().message().find("empty") != std::string::npos && entries && !saw_tool_entry,
           "agent loop rejects empty provider tool call ids before session or timeline use");
  }
}

void test_agent_loop_model_command_deny_preflight_blocks_auto_allow_without_process()
{
  auto const test_root = temp_root();
  expect(::chmod(test_root.c_str(), S_IRWXU) == 0, "model command deny preflight test secures its test-root ancestor");
  auto const root = test_root / "agent-model-command-preflight-deny";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  expect(::chmod(root.c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0,
         "model command deny preflight fixture keeps sealed planning roots owner-only");

  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "model-preflight-deny"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(tool_call_sse("call_auto_allow", "bash", R"({"command":"ls"})") + "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"persistent deny handled\"}\n\n"
                                                    "data: [DONE]\n\n")});
  auto collector = std::make_shared<TraceCollector>();
  auto observation = std::make_shared<ava::observability::RunObservation>(collector);
  int interactive_prompts = 0;
  int deny_preflights = 0;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [&interactive_prompts](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++interactive_prompts;
        return ava::permissions::PermissionResolution::Allow;
      },
      .command_deny_preflight =
          [&deny_preflights](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++deny_preflights;
        expect(prompt.operation == ava::permissions::Operation::RunCommand && prompt.command == "ls" && prompt.command_metadata &&
                   prompt.command_metadata->level == ava::command::CommandLevel::Standard &&
                   prompt.command_metadata->containment_status == ava::permissions::CommandContainmentStatus::NotRequired,
               "model command deny preflight receives the auto-Allow standard command's sealed metadata");
        ava::permissions::PermissionResolutionDecision denied(ava::permissions::PermissionResolution::Deny, "external persistent Deny");
        denied.resolution_source = "persistent_rule";
        denied.rule_id = "rule_model_deny";
        denied.authoritative = true;
        return denied;
      },
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
      .observation = observation,
  });

  auto result = loop.run_turn("run inspection", store, provider, transport);
  bool process_started = false;
  {
    std::lock_guard lock(collector->mutex);
    process_started = std::ranges::any_of(collector->events, [](ava::observability::TraceEvent const& event) {
      return event.type == ava::observability::TraceEventType::ProcessStart;
    });
  }
  expect(result && result->final_text == "persistent deny handled" && deny_preflights == 1 && interactive_prompts == 0 &&
             result->tool_timeline.size() == 1 && result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error && !process_started,
         "an authoritative model-command Deny preflight overrides Standard auto-Allow before prompt or process side effects");
}

void test_agent_loop_model_command_rejects_authority_workspace_before_permission_or_process()
{
  auto const test_root = temp_root();
  expect(::chmod(test_root.c_str(), S_IRWXU) == 0, "model authority-root test secures its test-root ancestor");
  auto const root = test_root / "agent-model-authority-root";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "ava-authority";
  std::filesystem::create_directories(workspace);
  expect(::chmod(root.c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0,
         "model authority-root fixture keeps sealed planning roots owner-only");

  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "model-authority-root"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(tool_call_sse("call_authority", "bash", R"({"command":"ls"})") + "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"authority rejected\"}\n\n"
                                                    "data: [DONE]\n\n")});
  auto collector = std::make_shared<TraceCollector>();
  auto observation = std::make_shared<ava::observability::RunObservation>(collector);
  int prompts = 0;
  int preflights = 0;
  std::vector<std::filesystem::path> duplicate_roots(65, workspace);
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .ava_authority_roots = std::move(duplicate_roots),
      .permission_resolver = [&prompts](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++prompts;
        return ava::permissions::PermissionResolution::Allow;
      },
      .command_deny_preflight = [&preflights](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++preflights;
        return ava::permissions::PermissionResolution::Allow;
      },
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
      .observation = observation,
  });

  auto result = loop.run_turn("inspect authority", store, provider, transport);
  bool process_started = false;
  {
    std::lock_guard lock(collector->mutex);
    process_started = std::ranges::any_of(collector->events, [](ava::observability::TraceEvent const& event) {
      return event.type == ava::observability::TraceEventType::ProcessStart;
    });
  }
  auto const detail = result && !result->tool_timeline.empty() ? result->tool_timeline.front().result_summary : std::string{};
  expect(result && result->final_text == "authority rejected" && prompts == 0 && preflights == 0 && !process_started &&
             detail.find("must not overlap with any AVA authority root") != std::string::npos,
         "model ToolContexts deduplicate bounded AVA authority roots and reject overlapping workspaces before prompts or processes");
}

void test_agent_loop_truncates_tool_context()
{
  auto const root = temp_root() / "agent-tool-truncate";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream large(workspace / "large.txt", std::ios::binary | std::ios::trunc);
    large << std::string(12 * 1024, 'x');
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "truncate"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_large\",\"name\":\"read_file\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_large\",\"delta\":\"{\\\"path\\\":"
                                                    "\\\"large.txt\\\"}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"ok\"}\n\n"
                                                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .max_tool_result_context_bytes = 8 * 1024,
      .append_entry = append_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("read large", store, provider, transport);
  expect(result && transport.requests().size() == 2 && transport.requests()[1].body.find("tool result content truncated") != std::string::npos,
         "agent loop truncates native tool results before OpenAI continuation");
}

}  // namespace

void run_agent_loop_tests()
{
  test_agent_loop_text_only_turn();
  test_agent_loop_model_capability_gating();
  test_agent_loop_rejects_persistent_store_without_append_route();
  test_agent_loop_rejects_replaced_history_before_provider_use();
  test_agent_loop_image_attachment_load_failure_records_error();
  test_agent_loop_usage_and_cost_persistence();
  test_agent_loop_tool_turn_and_continuation();
  test_agent_loop_task_subagent_runs_child_session();
  test_agent_loop_task_subagent_propagates_authority_roots_to_foreground_and_background_children();
  test_agent_loop_task_subagent_recovers_torn_child_before_resume();
  test_subagent_config_loads_project_definitions();
  test_agent_loop_custom_subagent_definition_controls_prompt_and_tools();
  test_agent_loop_background_task_starts_child_session();
  test_agent_loop_background_task_failure_records_parent_and_child_errors();
  test_agent_loop_background_task_cancel_requests_child_cancellation();
  test_agent_loop_background_task_requires_registry_owner();
  test_background_job_registry_worker_exception_marks_failed();
  test_background_job_registry_enforces_running_limit();
  test_background_job_registry_coerces_non_terminal_completion_to_failed();
  test_background_job_registry_bounds_snapshot_text();
  test_background_job_registry_normalizes_terminal_completion_fields();
  test_background_job_registry_join_finished_is_concurrency_safe();
  test_background_job_registry_prunes_retained_finished_jobs();
  test_background_job_registry_destructor_stops_running_jobs();
  test_agent_loop_permission_resolver_threads_to_tools();
  test_agent_loop_question_resolver_threads_to_tools();
  test_agent_loop_non_stream_response();
  test_agent_loop_non_stream_error_prevents_tool_dispatch();
  test_agent_loop_stream_unended_documented_function_prevents_dispatch();
  test_agent_loop_stream_post_terminal_function_prevents_dispatch();
  test_agent_loop_compaction_status_metadata();
  test_agent_loop_replays_steering_after_mid_turn_auto_compaction();
  test_agent_loop_context_overflow_retry_skips_duplicate_auto_compaction();
  test_agent_loop_multiple_tools_and_denied_continuation();
  test_agent_loop_parallel_read_search_preserves_provider_order_and_replay();
  test_agent_loop_parallel_read_search_zero_max_workers_clamps_to_one();
  test_agent_loop_parallel_read_search_falls_back_for_ask_preflight();
  test_agent_loop_parallel_read_search_active_cancellation_stops_unstarted_slots();
  test_agent_loop_parallel_read_search_cancellation_stops_later_barrier();
  test_agent_loop_cancellation_stops_later_sequential_tools();
  test_agent_loop_tool_delta_dedupes_and_rejects_empty_tool_ids();
  test_agent_loop_model_command_deny_preflight_blocks_auto_allow_without_process();
  test_agent_loop_model_command_rejects_authority_workspace_before_permission_or_process();
  test_agent_loop_truncates_tool_context();
}
