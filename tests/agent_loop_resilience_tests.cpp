#include "sys.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/session/session_store.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/provider.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace {

ava::provider::HttpResponse sse_response(std::string const& body)
{
  return ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = body};
}

class CallbackTransport final : public ava::provider::Transport
{
 public:
  CallbackTransport(std::vector<ava::provider::HttpResponse> responses, std::function<void()> after_send)
      : responses_(std::move(responses)), after_send_(std::move(after_send))
  {
  }

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override
  {
    requests_.push_back(request);
    if (responses_.empty())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "callback transport has no response"));
    }
    auto response = responses_.front();
    responses_.erase(responses_.begin());
    if (after_send_)
      after_send_();
    return response;
  }

  [[nodiscard]] std::vector<ava::provider::HttpRequest> const& requests() const noexcept { return requests_; }

 private:
  std::vector<ava::provider::HttpResponse> responses_;
  std::function<void()> after_send_;
  std::vector<ava::provider::HttpRequest> requests_;
};

void test_agent_loop_cancellation_boundaries()
{
  ava::provider::OpenAIProvider const provider("https://api.example.test");

  {
    auto const root = temp_root() / "agent-cancel-before-turn-start";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "cancel-before-turn-start"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"should not send\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .cancel_requested = [] { return true; },
        .append_entry = append_route_for_test(store),
    });
    auto result = loop.run_turn("cancel now", store, provider, transport);
    auto entries = store.load();
    bool saw_user_message = false;
    bool saw_cancel = false;
    if (entries)
    {
      for (auto const& entry : *entries)
      {
        saw_user_message = saw_user_message || entry.type == ava::session::EntryType::UserMessage;
        saw_cancel = saw_cancel || (entry.type == ava::session::EntryType::Cancel && entry.data_json.find("before_turn_start") != std::string::npos);
      }
    }
    expect(!result && result.error().message() == "agent loop canceled" && transport.requests().empty() && entries && saw_cancel && !saw_user_message,
           "agent loop cancellation before turn start avoids persisting the user message");
  }

  {
    auto const root = temp_root() / "agent-cancel-before-provider";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "cancel-before-provider"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"should not send\"}\n\n"
                      "data: [DONE]\n\n")});
    int cancel_checks = 0;
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .cancel_requested =
            [&cancel_checks] {
              ++cancel_checks;
              return cancel_checks >= 2;
            },
        .append_entry = append_route_for_test(store),
    });
    auto result = loop.run_turn("cancel before provider", store, provider, transport);
    auto entries = store.load();
    bool const saw_cancel = entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
                              return entry.type == ava::session::EntryType::Cancel && entry.data_json.find("before_provider_call") != std::string::npos;
                            });
    bool const saw_user_message =
        entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) { return entry.type == ava::session::EntryType::UserMessage; });
    expect(!result && result.error().message() == "agent loop canceled" && transport.requests().empty() && saw_cancel && saw_user_message,
           "agent loop cancellation before provider call avoids transport send and records cancel boundary");
  }

  {
    auto const root = temp_root() / "agent-cancel-before-tool";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    {
      std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
      file << "must not read";
    }
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "cancel-before-tool"});
    bool cancel = false;
    CallbackTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_read\",\"name\":\"read_file\"}\n\n"
                                              "data: "
                                              "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_read\",\"delta\":\"{\\\"path\\\":"
                                              "\\\"note.txt\\\"}\"}\n\n"
                                              "data: [DONE]\n\n")},
                                [&cancel] { cancel = true; });
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .cancel_requested = [&cancel] { return cancel; },
        .append_entry = append_route_for_test(store),
    });
    auto result = loop.run_turn("read then cancel", store, provider, transport);
    auto entries = store.load();
    bool saw_tool_entry = false;
    bool saw_cancel = false;
    if (entries)
    {
      for (auto const& entry : *entries)
      {
        saw_tool_entry = saw_tool_entry || entry.type == ava::session::EntryType::ToolCall || entry.type == ava::session::EntryType::ToolResult;
        saw_cancel = saw_cancel || (entry.type == ava::session::EntryType::Cancel && (entry.data_json.find("before_tool_dispatch") != std::string::npos ||
                                                                                      entry.data_json.find("during_provider_request") != std::string::npos ||
                                                                                      entry.data_json.find("after_provider_call") != std::string::npos));
      }
    }
    expect(!result && result.error().message() == "agent loop canceled" && transport.requests().size() == 1 && entries && saw_cancel && !saw_tool_entry,
           "agent loop cancellation before tool dispatch avoids tool call/result entries");
  }

  {
    auto const root = temp_root() / "agent-cancel-during-bash-tool";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "cancel-during-bash-tool"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
                      "data: "
                      "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_bash\",\"delta\":\"{\\\"command\\\":"
                      "\\\"sleep 2\\\",\\\"timeout_ms\\\":5000}\"}\n\n"
                      "data: [DONE]\n\n")});
    bool bash_started = false;
    int bash_cancel_checks = 0;
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .on_tool_event =
            [&bash_started](auto const& entry) {
              if (entry.status == ava::agent::ToolTimelineStatus::Running && entry.name == "bash")
              {
                bash_started = true;
              }
            },
        .cancel_requested =
            [&bash_started, &bash_cancel_checks] {
              if (!bash_started)
                return false;
              ++bash_cancel_checks;
              return bash_cancel_checks >= 3;
            },
        .append_entry = append_route_for_test(store),
    });
    auto result = loop.run_turn("sleep then cancel", store, provider, transport);
    auto entries = store.load();
    bool const saw_canceled_tool_result = entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
                                            return entry.type == ava::session::EntryType::ToolResult &&
                                                   entry.data_json.find("\"status\":\"canceled\"") != std::string::npos &&
                                                   entry.data_json.find("\"canceled\":true") != std::string::npos;
                                          });
    bool const saw_after_tool_cancel =
        entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
          return entry.type == ava::session::EntryType::Cancel && entry.data_json.find("after_tool_dispatch") != std::string::npos;
        });
    expect(
        !result && result.error().message() == "agent loop canceled" && transport.requests().size() == 1 && saw_canceled_tool_result && saw_after_tool_cancel,
        "agent loop propagates cancellation into bash tools and persists a canceled tool result");
  }
}

void test_agent_loop_error_paths_and_bounds()
{
  ava::provider::OpenAIProvider const provider("https://api.example.test");

  {
    auto const root = temp_root() / "agent-provider-error";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "provider-error"});
    ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.error\",\"error\":{\"message\":\"bad request\"}}\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .append_entry = append_route_for_test(store),
    });
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("provider stream error") != std::string::npos, "agent loop returns provider error events");
  }

  {
    auto const root = temp_root() / "agent-empty-transport";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "empty-transport"});
    ava::tests::FakeTransport transport({});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .append_entry = append_route_for_test(store),
    });
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("fake transport has no response") != std::string::npos, "agent loop returns transport failures");
  }

  {
    auto const root = temp_root() / "agent-empty-response";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "empty-response"});
    ava::tests::FakeTransport transport({sse_response("")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .append_entry = append_route_for_test(store),
    });
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("empty") != std::string::npos, "agent loop returns empty provider responses");
  }

  {
    auto const root = temp_root() / "agent-event-bound";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "event-bound"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"a\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .max_provider_events = 1,
        .append_entry = append_route_for_test(store),
    });
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("event limit") != std::string::npos, "agent loop enforces provider event bounds");
  }

  {
    auto const root = temp_root() / "agent-text-bound";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "text-bound"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"hello\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .max_assistant_text_bytes = 3,
        .append_entry = append_route_for_test(store),
    });
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("text byte limit") != std::string::npos, "agent loop enforces assistant text byte bounds");
  }

  {
    auto const root = temp_root() / "agent-arg-bound";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "arg-bound"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
                      "data: "
                      "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
                      "\\\"note.txt\\\"}\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .max_tool_argument_bytes = 5,
        .append_entry = append_route_for_test(store),
    });
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("argument byte limit") != std::string::npos, "agent loop enforces tool argument byte bounds");
  }

  {
    auto const root = temp_root() / "agent-control-call-id";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "control-call-id"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_\\u0001bad\",\"name\":\"read_file\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .append_entry = append_route_for_test(store),
    });
    auto result = loop.run_turn("bad id", store, provider, transport);
    auto entries = store.load();
    bool saw_tool_entry = false;
    if (entries)
    {
      saw_tool_entry = std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
        return entry.type == ava::session::EntryType::ToolCall || entry.type == ava::session::EntryType::ToolResult;
      });
    }
    expect(!result && result.error().message().find("control byte") != std::string::npos && entries && !saw_tool_entry,
           "agent loop rejects provider tool call ids with control bytes before session or timeline use");
  }

  {
    auto const root = temp_root() / "agent-long-call-id";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(
        ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "long-call-id"});
    std::string const long_call_id(300, 'a');
    ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"item_id\":\"" + long_call_id +
                                                      "\",\"name\":\"read_file\"}\n\n"
                                                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .append_entry = append_route_for_test(store),
    });
    auto result = loop.run_turn("long id", store, provider, transport);
    expect(!result && result.error().message().find("too long") != std::string::npos, "agent loop rejects overlong provider tool call ids");
  }
}

void test_agent_loop_max_iteration_guard()
{
  auto const root = temp_root() / "agent-max";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "max"});
  auto tool_sse = [](std::string_view call_id) {
    return "data: {\"type\":\"response.function_call.added\",\"item_id\":\"" + std::string(call_id) +
           "\",\"name\":\"glob\"}\n\n"
           "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"" +
           std::string(call_id) +
           "\",\"delta\":\"{\\\"pattern\\\":\\\"**/*\\\"}\"}\n\n"
           "data: {\"type\":\"response.function_call.done\",\"item_id\":\"" +
           std::string(call_id) +
           "\"}\n\n"
           "data: [DONE]\n\n";
  };
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(tool_sse("call_glob_1")), sse_response(tool_sse("call_glob_2"))});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .max_tool_iterations = 2,
      .append_entry = append_route_for_test(store),
  });
  auto result = loop.run_turn("loop", store, provider, transport);
  expect(result && result->outcome == ava::core::RuntimeTerminalOutcome::MaxTurnRequests && result->provider_iterations == 2 && result->tool_iterations == 2 &&
             result->tool_calls == 2,
         "agent loop reports repeated tool use as the max-turn-requests terminal outcome");
  auto entries = store.load();
  expect(entries && std::ranges::none_of(*entries, [](ava::session::SessionEntry const& entry) { return entry.type == ava::session::EntryType::Error; }),
         "max-turn-requests is a terminal outcome rather than a persisted internal error");
}

}  // namespace

void run_agent_loop_resilience_tests()
{
  test_agent_loop_cancellation_boundaries();
  test_agent_loop_error_paths_and_bounds();
  test_agent_loop_max_iteration_guard();
}
