#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"

#include "ava/config/model_config.h"

#include "ava/session/session_store.h"

#include "ava/permissions/permission.h"

#include "ava/provider/openai_provider.h"

#include "ava/core/ids.h"
#include "ava/core/json.h"

#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

ava::provider::HttpResponse sse_response(std::string const& body)
{
  return ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = body};
}

class OverflowOnceProvider final : public ava::provider::Provider {
 public:
  explicit OverflowOnceProvider(std::string base_url) : delegate_(std::move(base_url)) {}

  [[nodiscard]] ava::core::Result<ava::provider::HttpRequest> build_request(
      ava::provider::ProviderRequest const& request, std::string_view access_token) const override
  {
    ++build_calls_;
    if (build_calls_ == 1) {
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::Provider, "context window exceeds token limit"));
    }
    return delegate_.build_request(request, access_token);
  }

  [[nodiscard]] std::unique_ptr<ava::provider::StreamParser> create_stream_parser() const override
  {
    return delegate_.create_stream_parser();
  }

  [[nodiscard]] ava::core::Result<std::vector<ava::provider::StreamEvent>> parse_response(
      ava::provider::HttpResponse const& response, bool stream) const override
  {
    return delegate_.parse_response(response, stream);
  }

 private:
  ava::provider::OpenAIProvider delegate_;
  mutable int build_calls_ = 0;
};

void test_agent_loop_text_only_turn()
{
  auto const root = temp_root() / "agent-text";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "text"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"hello user\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .openai_oauth = true,
                                                          .openai_account_id = "acct_123"});
  auto result = loop.run_turn("hi", store, provider, transport);
  expect(result && result->final_text == "hello user" && result->tool_calls == 0 &&
             result->initial_context_messages == 1 && !result->used_compacted_context && result->tool_iterations == 0 &&
             result->stop_reason == "completed",
         "agent loop returns text-only provider response with status metadata");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("read_file") != std::string::npos,
         "agent loop includes tool schemas in provider request");
  expect(transport.requests().size() == 1 &&
             transport.requests()[0].url == "https://chatgpt.com/backend-api/codex/responses" &&
             transport.requests()[0].headers.at("ChatGPT-Account-Id") == "acct_123",
         "agent loop routes OpenAI OAuth turns through Codex endpoint");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("\"store\":false") != std::string::npos,
         "agent loop disables Codex response storage for OpenAI OAuth turns");
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
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "capabilities"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"plain\"}"}});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "text-only-model",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .stream = true,
                                                          .model_supports_tools = false,
                                                          .model_supports_streaming = false});
  auto result = loop.run_turn("hi", store, provider, transport);
  expect(result && result->final_text == "plain", "agent loop accepts text-only model response");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("read_file") == std::string::npos &&
             transport.requests()[0].body.find("write_file") == std::string::npos,
         "agent loop omits tool definitions for models without tool support");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("\"stream\":false") != std::string::npos,
         "agent loop disables streaming for models without streaming support");
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

  ava::session::SessionStore exact_store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "exact"});
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
                                                                .model_pricing = pricing});
  auto exact_result = exact_loop.run_turn("hi", exact_store, provider, exact_transport);
  expect(exact_result && exact_result->usage && !exact_result->usage->estimated && exact_result->cost_usd &&
             *exact_result->cost_usd > 0.049L && *exact_result->cost_usd < 0.051L,
         "agent loop calculates cost from provider usage when pricing is known");
  auto exact_entries = exact_store.load();
  expect(exact_entries && exact_entries->size() == 2 &&
             (*exact_entries)[1].data_json.find("\"source\":\"provider\"") != std::string::npos &&
             (*exact_entries)[1].data_json.find("\"cost_usd\":0.05") != std::string::npos,
         "agent loop persists exact provider usage and known cost on assistant messages");

  ava::session::SessionStore unknown_price_store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "unknown-price"});
  ava::tests::FakeTransport unknown_price_transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"unknown\"}\n\n"
                    "data: {\"type\":\"response.completed\",\"response\":{\"usage\":{\"input_tokens\":1,"
                    "\"output_tokens\":1,\"total_tokens\":2}}}\n\n")});
  ava::agent::AgentLoop unknown_price_loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                                        .mode = ava::agent::Mode::Build,
                                                                        .provider_id = "openai",
                                                                        .model_id = "unknown-model",
                                                                        .system_prompt = "system prompt",
                                                                        .access_token = "token"});
  auto unknown_price_result = unknown_price_loop.run_turn("hi", unknown_price_store, provider, unknown_price_transport);
  auto unknown_price_entries = unknown_price_store.load();
  expect(unknown_price_result && unknown_price_entries &&
             (*unknown_price_entries)[1].data_json.find("cost_usd") == std::string::npos,
         "agent loop does not persist fake cost when model pricing is unknown");

  ava::session::SessionStore estimated_store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "estimated"});
  ava::tests::FakeTransport estimated_transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"estimated\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop estimated_loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                                    .mode = ava::agent::Mode::Build,
                                                                    .provider_id = "openai",
                                                                    .model_id = "gpt-5.5",
                                                                    .system_prompt = "system prompt",
                                                                    .access_token = "token",
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
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "tool"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::config::ModelPricing const pricing{.input_per_million = 10.0L,
                                          .output_per_million = 20.0L,
                                          .cache_read_per_million = std::nullopt,
                                          .cache_write_per_million = std::nullopt,
                                          .reasoning_per_million = std::nullopt};
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
           "\\\"note.txt\\\"}\"}\n\n"
           "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_1\"}\n\n"
           "data: {\"type\":\"response.completed\",\"response\":{\"usage\":{\"input_tokens\":10,"
           "\"output_tokens\":2,\"total_tokens\":12}}}\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"read it\"}\n\n"
                    "data: {\"type\":\"response.completed\",\"response\":{\"usage\":{\"input_tokens\":5,"
                    "\"output_tokens\":3,\"total_tokens\":8}}}\n\n")});
  std::vector<ava::agent::ToolTimelineEntry> tool_events;
  ava::agent::AgentLoop loop(
      ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                   .mode = ava::agent::Mode::Build,
                                   .provider_id = "openai",
                                   .model_id = "gpt-5.5",
                                   .system_prompt = "system prompt",
                                   .access_token = "token",
                                   .on_tool_event = [&tool_events](auto const& entry) { tool_events.push_back(entry); },
                                   .model_pricing = pricing});
  auto result = loop.run_turn("read note", store, provider, transport);
  expect(result && result->final_text == "read it" && result->tool_calls == 1 && result->provider_iterations == 2 &&
             result->initial_context_messages == 1 && result->tool_iterations == 1 &&
             result->stop_reason == "completed",
         "agent loop runs one sequential tool call then continues to final answer with status metadata");
  expect(transport.requests().size() == 2 && transport.requests()[1].body.find("tool content") != std::string::npos,
         "agent loop sends persisted tool result as continuation context");
  expect(result && result->tool_timeline.size() == 1 &&
             result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success &&
             result->tool_timeline.front().name == "read_file" &&
             result->tool_timeline.front().argument_summary.find("path=note.txt") != std::string::npos &&
             result->tool_timeline.front().argument_summary.find('{') == std::string::npos &&
             result->tool_timeline.front().result_summary.find("tool content") == std::string::npos &&
             result->tool_timeline.front().result_summary.find("bytes") != std::string::npos &&
             result->tool_timeline.front().structured_result_json.find("\"status\":\"success\"") != std::string::npos &&
             result->tool_timeline.front().content_type == "application/json",
         "agent loop returns safe compact tool timeline summaries and structured result metadata");
  expect(tool_events.size() == 2 && tool_events.front().status == ava::agent::ToolTimelineStatus::Running &&
             tool_events.back().status == ava::agent::ToolTimelineStatus::Success,
         "agent loop publishes running and completed tool timeline events");
  expect(result && result->usage && !result->usage->estimated && result->usage->input_tokens == 15 &&
             result->usage->output_tokens == 5 && result->usage->total_tokens == 20 && result->cost_usd &&
             *result->cost_usd > 0.00024L && *result->cost_usd < 0.00026L,
         "agent loop accumulates usage and cost across provider iterations");

  auto entries = store.load();
  expect(entries.has_value(), "agent tool turn session loads");
  if (!entries) return;
  bool saw_tool_call = false;
  bool saw_tool_result = false;
  bool saw_structured_tool_result = false;
  bool saw_final_assistant = false;
  for (auto const& entry : *entries) {
    saw_tool_call = saw_tool_call || entry.type == ava::session::EntryType::ToolCall;
    saw_tool_result = saw_tool_result || entry.type == ava::session::EntryType::ToolResult;
    saw_structured_tool_result =
        saw_structured_tool_result ||
        (entry.type == ava::session::EntryType::ToolResult &&
         entry.data_json.find("\"structured_result\":{\"schema_version\":1") != std::string::npos &&
         entry.data_json.find("\"content_type\":\"application/json\"") != std::string::npos);
    saw_final_assistant = saw_final_assistant || (entry.type == ava::session::EntryType::AssistantMessage &&
                                                  entry.data_json.find("read it") != std::string::npos);
  }
  expect(saw_tool_call && saw_tool_result && saw_structured_tool_result && saw_final_assistant,
         "agent loop persists assistant, tool call, and semantic structured tool result entries");
}

void test_agent_loop_permission_resolver_threads_to_tools()
{
  auto const root = temp_root() / "agent-permission-resolver";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside via agent";
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "resolver"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_outside\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_outside\",\"delta\":\"{"
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
      .permission_resolver = [&prompts, &outside_path](ava::permissions::PermissionPrompt const& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++prompts;
        expect(prompt.target_path == outside_path, "agent loop resolver sees tool target path");
        return ava::permissions::PermissionResolution::Allow;
      }});
  auto result = loop.run_turn("read outside", store, provider, transport);
  expect(result && result->final_text == "used resolver" && prompts == 1 && result->tool_timeline.size() == 1 &&
             result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success,
         "agent loop threads permission resolver into tool dispatcher");
  expect(
      transport.requests().size() == 2 && transport.requests()[1].body.find("outside via agent") != std::string::npos,
      "agent loop continuation includes resolver-approved tool result");
  auto resolver_entries = store.load();
  auto resolver_audits =
      resolver_entries ? permission_entries(*resolver_entries) : std::vector<ava::session::SessionEntry>{};
  auto const resolver_permission_request_id =
      resolver_audits.size() >= 2
          ? ava::core::json::string_field(resolver_audits[0].data_json, "permission_request_id").value_or("")
          : "";
  expect(resolver_audits.size() == 2 && resolver_permission_request_id.starts_with("permreq_") &&
             ava::core::json::string_field(resolver_audits[0].data_json, "action") == "ask" &&
             ava::core::json::string_field(resolver_audits[0].data_json, "resolution_source") == "policy" &&
             ava::core::json::string_field(resolver_audits[1].data_json, "permission_request_id") ==
                 resolver_permission_request_id &&
             ava::core::json::string_field(resolver_audits[1].data_json, "resolution") == "allow" &&
             ava::core::json::string_field(resolver_audits[1].data_json, "resolution_source") == "resolver",
         "agent loop persists linked ask and resolver permission audit entries");

  {
    auto const bash_root = temp_root() / "agent-bash-ask-allow";
    std::filesystem::remove_all(bash_root, remove_error);
    auto const bash_workspace = bash_root / "workspace";
    std::filesystem::create_directories(bash_workspace);
    ava::session::SessionStore bash_store(ava::session::SessionStoreOptions{
        .root_dir = bash_root / "sessions", .workspace_dir = bash_workspace, .session_id = "bash-allow"});
    ava::tests::FakeTransport bash_transport(
        {sse_response(
             "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
             "data: "
             "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_bash\",\"delta\":\"{"
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
        .permission_resolver = [&bash_allow_prompts](ava::permissions::PermissionPrompt const& prompt)
            -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          ++bash_allow_prompts;
          expect(prompt.operation == ava::permissions::Operation::RunCommand,
                 "agent bash allow resolver sees run command");
          expect(prompt.command == "true", "agent bash allow resolver sees command text");
          return ava::permissions::PermissionResolution::Allow;
        }});
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash allowed" && bash_allow_prompts == 1 &&
               bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success,
           "agent loop allows bash Ask decisions when resolver allows once");
  }

  {
    auto const bash_root = temp_root() / "agent-bash-ask-deny";
    std::filesystem::remove_all(bash_root, remove_error);
    auto const bash_workspace = bash_root / "workspace";
    std::filesystem::create_directories(bash_workspace);
    ava::session::SessionStore bash_store(ava::session::SessionStoreOptions{
        .root_dir = bash_root / "sessions", .workspace_dir = bash_workspace, .session_id = "bash-deny"});
    ava::tests::FakeTransport bash_transport(
        {sse_response(
             "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
             "data: "
             "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_bash\",\"delta\":\"{"
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
        .permission_resolver = [&bash_deny_prompts](ava::permissions::PermissionPrompt const& prompt)
            -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          ++bash_deny_prompts;
          expect(prompt.operation == ava::permissions::Operation::RunCommand,
                 "agent bash deny resolver sees run command");
          return ava::permissions::PermissionResolution::Deny;
        }});
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash denied" && bash_deny_prompts == 1 &&
               bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error,
           "agent loop records denied bash Ask decisions as failed tool results and continues");
    auto bash_entries = bash_store.load();
    auto bash_audits = bash_entries ? permission_entries(*bash_entries) : std::vector<ava::session::SessionEntry>{};
    expect(bash_audits.size() == 2 && ava::core::json::string_field(bash_audits[1].data_json, "command") == "true" &&
               ava::core::json::string_field(bash_audits[1].data_json, "resolution") == "deny" &&
               ava::core::json::string_field(bash_audits[1].data_json, "resolution_source") == "resolver",
           "agent loop persists resolver-denied command permission audit entries");
  }

  {
    auto const bash_root = temp_root() / "agent-bash-ask-fail";
    std::filesystem::remove_all(bash_root, remove_error);
    auto const bash_workspace = bash_root / "workspace";
    std::filesystem::create_directories(bash_workspace);
    ava::session::SessionStore bash_store(ava::session::SessionStoreOptions{
        .root_dir = bash_root / "sessions", .workspace_dir = bash_workspace, .session_id = "bash-fail"});
    ava::tests::FakeTransport bash_transport(
        {sse_response(
             "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
             "data: "
             "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_bash\",\"delta\":\"{"
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
        .permission_resolver = [&bash_fail_prompts](ava::permissions::PermissionPrompt const& prompt)
            -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          ++bash_fail_prompts;
          expect(prompt.operation == ava::permissions::Operation::RunCommand,
                 "agent bash fail resolver sees run command");
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
        }});
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash resolver failed" && bash_fail_prompts == 1 &&
               bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error &&
               bash_transport.requests().size() == 2,
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
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "question-resolver"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_question\",\"name\":\"question\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_question\",\"delta\":\"{"
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
      .question_resolver =
          [&prompts](ava::agent::QuestionPrompt const& prompt) -> ava::core::Result<ava::agent::QuestionAnswer> {
        ++prompts;
        expect(prompt.question == "Pick one?" && prompt.options.size() == 2,
               "agent loop question resolver receives provider prompt");
        return ava::agent::QuestionAnswer{.selected_options = {"B"}, .custom_text = ""};
      }});
  auto result = loop.run_turn("ask", store, provider, transport);
  expect(result && result->final_text == "question answered" && prompts == 1 && result->tool_timeline.size() == 1 &&
             result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success,
         "agent loop threads question resolver into tool dispatcher");
  expect(transport.requests().size() == 2 &&
             transport.requests()[1].body.find("\\\"selected_options\\\":[\\\"B\\\"]") != std::string::npos,
         "agent loop continuation includes serialized question answer");
}

void test_agent_loop_non_stream_response()
{
  auto const root = temp_root() / "agent-non-stream";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "nonstream"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200, .headers = {}, .body = "{\"output_text\":\"plain response with data: literal\"}"}});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .stream = false});
  auto result = loop.run_turn("hi", store, provider, transport);
  expect(result && result->final_text == "plain response with data: literal",
         "agent loop parses non-stream response without sniffing data text");
  expect(!transport.requests().empty() && transport.requests()[0].body.find("\"stream\":false") != std::string::npos,
         "agent loop passes explicit non-stream request expectation");
}

void test_agent_loop_compaction_status_metadata()
{
  auto const root = temp_root() / "agent-compaction-status";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "compaction-status"});
  auto appended = store.append(ava::session::SessionEntry{.id = "entry_compaction_status",
                                                          .parent_id = "",
                                                          .type = ava::session::EntryType::Compaction,
                                                          .timestamp = ava::session::now_timestamp(),
                                                          .data_json = "{\"summary\":\"older context\"}"});
  expect(appended.has_value(), "agent loop compaction metadata test seeds compaction entry");
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"after compaction\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token"});
  auto result = loop.run_turn("continue", store, provider, transport);
  expect(result && result->used_compacted_context && result->initial_context_messages == 2 &&
             result->stop_reason == "completed",
         "agent loop status metadata reports compacted initial provider context");
  expect(transport.requests().size() == 1 &&
             transport.requests()[0].body.find("Compacted prior conversation summary") != std::string::npos,
         "agent loop sends compacted context in initial provider request");
}

void test_agent_loop_replays_steering_after_mid_turn_auto_compaction()
{
  auto const root = temp_root() / "agent-steering-compaction-replay";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "steering-replay"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"ok\"}\n\n"
                    "data: [DONE]\n\n")});
  int compact_calls = 0;
  bool steering_taken = false;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .take_steering_messages = [&steering_taken]() -> ava::core::Result<std::vector<std::string>> {
        if (steering_taken) return std::vector<std::string>{};
        steering_taken = true;
        return std::vector<std::string>{"mid-turn steering"};
      },
      .compact_context = [&compact_calls](ava::session::SessionStore& compact_store, std::string_view trigger,
                                          std::vector<std::string> const&) -> ava::core::Result<bool> {
        ++compact_calls;
        if (trigger == "auto" && compact_calls == 2) {
          auto appended = compact_store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                          .parent_id = "",
                                                                          .type = ava::session::EntryType::Compaction,
                                                                          .timestamp = ava::session::now_timestamp(),
                                                                          .data_json = "{\"summary\":\"mid turn\"}"});
          if (!appended) return std::unexpected(std::move(appended.error()));
          return true;
        }
        return false;
      }});

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
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "overflow-skip-auto"});
  OverflowOnceProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"retry ok\"}\n\n"
                    "data: [DONE]\n\n")});
  bool overflow_compacted = false;
  std::vector<std::string> triggers;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .compact_context = [&](ava::session::SessionStore& compact_store, std::string_view trigger,
                             std::vector<std::string> const&) -> ava::core::Result<bool> {
        triggers.push_back(std::string(trigger));
        if (trigger == "context_overflow") {
          overflow_compacted = true;
          auto appended =
              compact_store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                              .parent_id = "",
                                                              .type = ava::session::EntryType::Compaction,
                                                              .timestamp = ava::session::now_timestamp(),
                                                              .data_json = "{\"summary\":\"overflow summary\"}"});
          if (!appended) return std::unexpected(std::move(appended.error()));
          return true;
        }
        if (trigger == "auto" && overflow_compacted) {
          auto appended = compact_store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                          .parent_id = "",
                                                                          .type = ava::session::EntryType::Compaction,
                                                                          .timestamp = ava::session::now_timestamp(),
                                                                          .data_json = "{\"summary\":\"duplicate\"}"});
          if (!appended) return std::unexpected(std::move(appended.error()));
          return true;
        }
        return false;
      }});

  auto result = loop.run_turn("overflow prompt", store, provider, transport);
  auto entries = store.load();
  auto const compactions =
      entries
          ? static_cast<std::size_t>(std::ranges::count_if(*entries,
                                                           [](ava::session::SessionEntry const& entry) {
                                                             return entry.type == ava::session::EntryType::Compaction;
                                                           }))
          : 0;
  auto const user_messages =
      entries
          ? static_cast<std::size_t>(std::ranges::count_if(*entries,
                                                           [](ava::session::SessionEntry const& entry) {
                                                             return entry.type == ava::session::EntryType::UserMessage;
                                                           }))
          : 0;
  expect(result && result->final_text == "retry ok", "context overflow retry succeeds after compaction");
  expect(triggers == std::vector<std::string>({"auto", "auto", "context_overflow"}),
         "context overflow retry skips immediate duplicate auto compaction");
  expect(entries && compactions == 1 && user_messages == 2,
         "context overflow retry appends one compaction and replays the active prompt once");
  expect(transport.requests().size() == 1 &&
             transport.requests()[0].body.find("overflow summary") != std::string::npos &&
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
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "multi"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
           "\\\"one.txt\\\"}\"}\n\n"
           "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_1\"}\n\n"
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_2\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_2\",\"delta\":\"{\\\"path\\\":"
           "\\\"two.txt\\\"}\"}\n\n"
           "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_2\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"done\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token"});
  auto result = loop.run_turn("read both", store, provider, transport);
  expect(result && result->tool_calls == 2 && result->final_text == "done",
         "agent loop handles multiple tool calls before continuation");

  auto const denied_root = temp_root() / "agent-denied-continuation";
  std::filesystem::remove_all(denied_root, remove_error);
  auto const denied_workspace = denied_root / "workspace";
  std::filesystem::create_directories(denied_workspace);
  ava::session::SessionStore denied_store(ava::session::SessionStoreOptions{
      .root_dir = denied_root / "sessions", .workspace_dir = denied_workspace, .session_id = "denied"});
  ava::tests::FakeTransport denied_transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_write\",\"name\":\"write_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_write\",\"delta\":\"{\\\"path\\\":"
           "\\\"src/new.cpp\\\",\\\"content\\\":\\\"bad\\\"}\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"permission explained\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop denied_loop(ava::agent::AgentLoopOptions{.workspace_dir = denied_workspace,
                                                                 .mode = ava::agent::Mode::Plan,
                                                                 .provider_id = "openai",
                                                                 .model_id = "gpt-5.5",
                                                                 .system_prompt = "system prompt",
                                                                 .access_token = "token",
                                                                 .openai_oauth = true,
                                                                 .openai_account_id = "acct_123"});
  auto denied_result = denied_loop.run_turn("write source", denied_store, provider, denied_transport);
  expect(
      denied_result && denied_result->final_text == "permission explained" && denied_result->provider_iterations == 2,
      "agent loop continues after permission-denied tool results");
  expect(denied_result && denied_result->tool_timeline.size() == 1 &&
             denied_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error &&
             denied_result->tool_timeline.front().argument_summary.find("content=3 bytes") != std::string::npos &&
             denied_result->tool_timeline.front().argument_summary.find("bad") == std::string::npos &&
             denied_result->tool_timeline.front().result_summary.find("error:") == 0,
         "agent loop marks denied tool results as safe error timeline entries");
  expect(denied_transport.requests().size() == 2 &&
             denied_transport.requests()[1].body.find("permission_denied") != std::string::npos,
         "permission-denied tool result is framed into continuation context");
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
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "delta-before-start"});
    ava::tests::FakeTransport transport(
        {sse_response(
             "data: "
             "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
             "\\\"note.txt\\\"}\"}\n\n"
             "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
             "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_1\"}\n\n"
             "data: [DONE]\n\n"),
         sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"done\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("read note", store, provider, transport);
    expect(result && result->tool_calls == 1 && result->tool_timeline.size() == 1 &&
               result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success &&
               result->tool_timeline.front().name == "read_file",
           "agent loop deduplicates tool deltas that arrive before tool start events");

    auto entries = store.load();
    std::size_t tool_calls = 0;
    std::size_t tool_results = 0;
    if (entries) {
      for (auto const& entry : *entries) {
        if (entry.type == ava::session::EntryType::ToolCall) ++tool_calls;
        if (entry.type == ava::session::EntryType::ToolResult) ++tool_results;
      }
    }
    expect(entries && tool_calls == 1 && tool_results == 1, "deduped streamed tool call has one paired result");
  }

  {
    auto const root = temp_root() / "agent-empty-call-id";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "empty-call-id"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.function_call.added\",\"item_id\":\"\",\"name\":\"read_file\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("read missing-id", store, provider, transport);
    auto entries = store.load();
    bool saw_tool_entry = false;
    if (entries) {
      for (auto const& entry : *entries) {
        saw_tool_entry = saw_tool_entry || entry.type == ava::session::EntryType::ToolCall ||
                         entry.type == ava::session::EntryType::ToolResult;
      }
    }
    expect(!result && result.error().message().find("empty") != std::string::npos && entries && !saw_tool_entry,
           "agent loop rejects empty provider tool call ids before session or timeline use");
  }
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
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "truncate"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_large\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_large\",\"delta\":\"{\\\"path\\\":"
           "\\\"large.txt\\\"}\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"ok\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .max_tool_result_context_bytes = 8 * 1024});
  auto result = loop.run_turn("read large", store, provider, transport);
  expect(result && transport.requests().size() == 2 &&
             transport.requests()[1].body.find("tool result context truncated") != std::string::npos,
         "agent loop truncates tool results before provider continuation context");
}

}  // namespace

void run_agent_loop_tests()
{
  test_agent_loop_text_only_turn();
  test_agent_loop_model_capability_gating();
  test_agent_loop_usage_and_cost_persistence();
  test_agent_loop_tool_turn_and_continuation();
  test_agent_loop_permission_resolver_threads_to_tools();
  test_agent_loop_question_resolver_threads_to_tools();
  test_agent_loop_non_stream_response();
  test_agent_loop_compaction_status_metadata();
  test_agent_loop_replays_steering_after_mid_turn_auto_compaction();
  test_agent_loop_context_overflow_retry_skips_duplicate_auto_compaction();
  test_agent_loop_multiple_tools_and_denied_continuation();
  test_agent_loop_tool_delta_dedupes_and_rejects_empty_tool_ids();
  test_agent_loop_truncates_tool_context();
}
