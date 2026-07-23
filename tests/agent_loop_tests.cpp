#include "sys.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/observability/run_observer.h"
#include "ava/app/command_jobs.h"
#include "ava/agent/agent_loop.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/agent/assistant_turn.h"
#include "ava/agent/history_projection.h"
#include "ava/agent/message_builder.h"
#include "ava/agent/mode.h"
#include "ava/agent/stream_bridge.h"
#include "ava/agent/usage_accounting.h"
#include "ava/config/model_config.h"
#include "ava/session/assistant_output.h"
#include "ava/session/record.h"
#include "ava/session/session_metadata.h"
#include "ava/session/session_store.h"
#include "ava/session/validation.h"
#include "ava/permissions/permission.h"
#include "ava/permissions/permission_rules.h"
#include "ava/provider/anthropic_provider.h"
#include "ava/provider/openai_compatible_provider.h"
#include "ava/provider/openai_provider.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
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

void test_legacy_provider_text_runs_preserve_v4_order()
{
  auto event = [](ava::provider::StreamEventType type) {
    auto value = ava::provider::StreamEvent{};
    value.type = type;
    return value;
  };
  auto text = [&](std::string value) {
    auto delta = event(ava::provider::StreamEventType::TextDelta);
    delta.text = std::move(value);
    return delta;
  };
  auto done = [&](ava::provider::ProviderFinishReason reason) {
    auto terminal = event(ava::provider::StreamEventType::Done);
    terminal.finish_reason = reason;
    return terminal;
  };
  auto tool = [&](ava::provider::StreamEventType type, std::string id, std::string name = {}, std::string arguments = {}) {
    auto value = event(type);
    value.tool_call_id = std::move(id);
    value.tool_name = std::move(name);
    value.text = std::move(arguments);
    return value;
  };

  auto const anthropic_streaming = ava::agent::parse_assistant_turn(
      {text("before"), text(" "), tool(ava::provider::StreamEventType::ToolCallStart, "call_anthropic", "read_file"),
       tool(ava::provider::StreamEventType::ToolCallDelta, "call_anthropic", "", R"({"path":"before.txt"})"),
       tool(ava::provider::StreamEventType::ToolCallEnd, "call_anthropic"), text("after"), done(ava::provider::ProviderFinishReason::ToolCalls)},
      {});
  auto const gemini_non_stream = ava::agent::parse_assistant_turn(
      {text("before"), tool(ava::provider::StreamEventType::ToolCallStart, "call_gemini", "read_file"),
       tool(ava::provider::StreamEventType::ToolCallDelta, "call_gemini", "", R"({"path":"after.txt"})"),
       tool(ava::provider::StreamEventType::ToolCallEnd, "call_gemini"), text("after"), done(ava::provider::ProviderFinishReason::ToolCalls)},
      {});
  auto reasoning_start = event(ava::provider::StreamEventType::ReasoningStart);
  reasoning_start.reasoning_format = "anthropic_thinking";
  auto reasoning_delta = event(ava::provider::StreamEventType::ReasoningDelta);
  reasoning_delta.text = "inspect";
  reasoning_delta.reasoning_format = "anthropic_thinking";
  auto reasoning_end = event(ava::provider::StreamEventType::ReasoningEnd);
  reasoning_end.reasoning_format = "anthropic_thinking";
  auto const anthropic_reasoning = ava::agent::parse_assistant_turn(
      {text("before"), reasoning_start, reasoning_delta, reasoning_end, text("after"), done(ava::provider::ProviderFinishReason::Completed)}, {});

  auto native_start = event(ava::provider::StreamEventType::TextStart);
  native_start.provider_item_id = "msg_native";
  native_start.provider_output_index = 1;
  native_start.assistant_phase = ava::provider::AssistantPhase::Commentary;
  auto native_delta = native_start;
  native_delta.type = ava::provider::StreamEventType::TextDelta;
  native_delta.text = "native";
  auto native_end = native_start;
  native_end.type = ava::provider::StreamEventType::TextEnd;
  auto const mixed_native = ava::agent::parse_assistant_turn(
      {text("before"), native_start, native_delta, native_end, text("after"), done(ava::provider::ProviderFinishReason::Completed)}, {});

  auto text_tool_text = [](ava::core::Result<ava::agent::ParsedAssistantTurn> const& turn, std::string_view call_id) {
    if (!turn || turn->ordered_items.size() != 3)
      return false;
    auto const* before = std::get_if<ava::agent::AssistantTextItem>(&turn->ordered_items[0].item);
    auto const* function = std::get_if<ava::agent::AssistantFunctionCallItem>(&turn->ordered_items[1].item);
    auto const* after = std::get_if<ava::agent::AssistantTextItem>(&turn->ordered_items[2].item);
    return before && function && after && before->text == "before" + std::string(call_id == "call_anthropic" ? " " : "") && function->tool_call.id == call_id &&
           after->text == "after";
  };
  auto reasoning_ordered = [&] {
    if (!anthropic_reasoning || anthropic_reasoning->ordered_items.size() != 3)
      return false;
    auto const* before = std::get_if<ava::agent::AssistantTextItem>(&anthropic_reasoning->ordered_items[0].item);
    auto const* reasoning = std::get_if<ava::agent::AssistantReasoningItem>(&anthropic_reasoning->ordered_items[1].item);
    auto const* after = std::get_if<ava::agent::AssistantTextItem>(&anthropic_reasoning->ordered_items[2].item);
    return before && reasoning && after && before->text == "before" && reasoning->reasoning.text == "inspect" && after->text == "after";
  };
  auto mixed_ordered = [&] {
    if (!mixed_native || mixed_native->ordered_items.size() != 3)
      return false;
    auto const* before = std::get_if<ava::agent::AssistantTextItem>(&mixed_native->ordered_items[0].item);
    auto const* native = std::get_if<ava::agent::AssistantTextItem>(&mixed_native->ordered_items[1].item);
    auto const* after = std::get_if<ava::agent::AssistantTextItem>(&mixed_native->ordered_items[2].item);
    return before && native && after && before->text == "before" && native->text == "native" && native->metadata.provider_item_id == "msg_native" &&
           after->text == "after";
  };
  expect(text_tool_text(anthropic_streaming, "call_anthropic") && text_tool_text(gemini_non_stream, "call_gemini") && reasoning_ordered() && mixed_ordered(),
         "legacy Anthropic/Gemini-style deltas form contiguous text runs across tool, reasoning, and native text lifecycles");

  auto store = ava::session::SessionStore::create_ephemeral(create_empty_root("legacy-text-run-v4-order"));
  auto target = store ? ava::session::SessionAppendTarget::create_ephemeral(*store)
                      : ava::core::Result<std::shared_ptr<ava::session::SessionAppendTarget>>(std::unexpected(store.error()));
  if (!store || !target || !anthropic_streaming || !gemini_non_stream || !anthropic_reasoning || !mixed_native)
    return;
  auto append_batch = [append_target = *target](std::vector<ava::session::SessionEntry> entries) { return append_target->append_batch(std::move(entries)); };
  auto append_entry = [append_target = *target](ava::session::SessionEntry entry) { return append_target->append(std::move(entry)); };
  auto persisted_anthropic = ava::agent::append_assistant_turn(append_batch, *anthropic_streaming, "anthropic", "claude-test", {}, std::nullopt);
  auto anthropic_result =
      persisted_anthropic
          ? ava::agent::append_tool_result(append_entry, {.call_id = "call_anthropic", .name = "read_file", .success = true, .result_text = "anthropic result"},
                                           persisted_anthropic->function_output_entry_ids_by_call_id.at("call_anthropic"))
          : ava::core::VoidResult(std::unexpected(persisted_anthropic.error()));
  auto persisted_gemini = ava::agent::append_assistant_turn(append_batch, *gemini_non_stream, "gemini", "gemini-test", {}, std::nullopt);
  auto gemini_result = persisted_gemini ? ava::agent::append_tool_result(
                                              append_entry, {.call_id = "call_gemini", .name = "read_file", .success = true, .result_text = "gemini result"},
                                              persisted_gemini->function_output_entry_ids_by_call_id.at("call_gemini"))
                                        : ava::core::VoidResult(std::unexpected(persisted_gemini.error()));
  auto persisted_reasoning = ava::agent::append_assistant_turn(append_batch, *anthropic_reasoning, "anthropic", "claude-test", {}, std::nullopt);
  auto persisted_mixed = ava::agent::append_assistant_turn(append_batch, *mixed_native, "openai", "gpt-test", {}, std::nullopt);
  auto entries = store->load();
  auto projection = entries ? ava::session::classify_assistant_output(*entries) : ava::session::AssistantOutputProjection{};
  auto messages = entries ? ava::agent::build_provider_messages_from_entries(
                                *entries, ava::agent::MessageBuildOptions{.target = ava::agent::HistoryReplayTarget{.provider_id = "openai",
                                                                                                                    .model_id = "gpt-test",
                                                                                                                    .api_family = "openai_responses",
                                                                                                                    .reasoning_format = "openai_responses",
                                                                                                                    .supports_tools = true,
                                                                                                                    .supports_images = false}})
                          : ava::core::Result<std::vector<ava::provider::ChatMessage>>{};
  auto const persisted_order = projection.turns.size() == 4 && projection.turns[0].items.size() == 3 && projection.turns[1].items.size() == 3 &&
                               projection.turns[2].items.size() == 3 && projection.turns[3].items.size() == 3 &&
                               std::holds_alternative<ava::session::AssistantOutputText>(projection.turns[0].items[0].item.payload) &&
                               std::holds_alternative<ava::session::AssistantOutputFunctionCall>(projection.turns[0].items[1].item.payload) &&
                               std::holds_alternative<ava::session::AssistantOutputText>(projection.turns[0].items[2].item.payload) &&
                               std::holds_alternative<ava::session::AssistantOutputText>(projection.turns[1].items[0].item.payload) &&
                               std::holds_alternative<ava::session::AssistantOutputFunctionCall>(projection.turns[1].items[1].item.payload) &&
                               std::holds_alternative<ava::session::AssistantOutputText>(projection.turns[1].items[2].item.payload) &&
                               std::holds_alternative<ava::session::AssistantOutputText>(projection.turns[2].items[0].item.payload) &&
                               std::holds_alternative<ava::session::AssistantOutputReasoning>(projection.turns[2].items[1].item.payload) &&
                               std::holds_alternative<ava::session::AssistantOutputText>(projection.turns[2].items[2].item.payload) &&
                               std::holds_alternative<ava::session::AssistantOutputText>(projection.turns[3].items[0].item.payload) &&
                               std::holds_alternative<ava::session::AssistantOutputText>(projection.turns[3].items[1].item.payload) &&
                               std::holds_alternative<ava::session::AssistantOutputText>(projection.turns[3].items[2].item.payload);
  auto const replay_order =
      messages && messages->size() == 6 && (*messages)[0].content_parts.size() == 3 && (*messages)[1].content_parts.size() == 1 &&
      (*messages)[2].content_parts.size() == 3 && (*messages)[3].content_parts.size() == 1 && (*messages)[4].content_parts.size() == 2 &&
      (*messages)[5].content_parts.size() == 3 && (*messages)[0].content_parts[0].text == "before " &&
      (*messages)[0].content_parts[1].type == ava::provider::ContentPartType::ToolUse && (*messages)[0].content_parts[2].text == "after" &&
      (*messages)[2].content_parts[0].text == "before" && (*messages)[2].content_parts[1].type == ava::provider::ContentPartType::ToolUse &&
      (*messages)[2].content_parts[2].text == "after" && (*messages)[4].content_parts[0].text == "before" && (*messages)[4].content_parts[1].text == "after" &&
      (*messages)[5].content_parts[0].text == "before" && (*messages)[5].content_parts[1].text == "native" && (*messages)[5].content_parts[2].text == "after";
  expect(
      persisted_anthropic && anthropic_result && persisted_gemini && gemini_result && persisted_reasoning && persisted_mixed && persisted_order && replay_order,
      "legacy text runs retain their v4 persisted sequence while unknown mixed-source reasoning projects portably");
}

void test_legacy_reasoning_replay_requires_exact_entry_source()
{
  using ava::session::EntryType;
  using ava::session::SessionEntry;

  auto const kimi_start = SessionEntry{
      .id = "legacy_kimi_start",
      .parent_id = "",
      .type = EntryType::SessionStart,
      .timestamp = "2026-07-24T00:00:00Z",
      .data_json = R"({"provider":"kimi","model":"kimi-k2-thinking","api_family":"openai_chat_completions","reasoning_format":"reasoning_content"})"};
  auto const assistant = SessionEntry{.id = "legacy_reasoning_answer",
                                      .parent_id = "",
                                      .type = EntryType::AssistantMessage,
                                      .timestamp = "2026-07-24T00:00:02Z",
                                      .data_json = R"({"text":"VISIBLE_LEGACY_ANSWER"})"};
  auto reasoning = [](std::string provider, std::string model, std::string text) {
    return SessionEntry{.id = "legacy_reasoning_block",
                        .parent_id = "",
                        .type = EntryType::ReasoningBlock,
                        .timestamp = "2026-07-24T00:00:01Z",
                        .data_json = "{\"provider\":\"" + provider + "\",\"model\":\"" + model + "\",\"format\":\"reasoning_content\",\"text\":\"" + text +
                                     "\",\"signature\":\"LEGACY_SIGNATURE_CANARY\",\"redacted_data\":\"LEGACY_REDACTED_CANARY\","
                                     "\"redacted\":false}"};
  };
  auto const kimi_target = ava::agent::HistoryReplayTarget{.provider_id = "kimi",
                                                           .model_id = "kimi-k2-thinking",
                                                           .api_family = "openai_chat_completions",
                                                           .reasoning_format = "reasoning_content",
                                                           .supports_tools = false,
                                                           .supports_images = false};
  ava::provider::OpenAICompatibleProvider const kimi_provider(ava::provider::OpenAICompatibleProviderOptions{
      .base_url = "https://compat.example.test", .provider_name = "Kimi", .reasoning_format = "reasoning_content", .preserve_reasoning_content = true});
  auto kimi_body = [&](SessionEntry block) {
    auto messages =
        ava::agent::build_provider_messages_from_entries({kimi_start, std::move(block), assistant}, ava::agent::MessageBuildOptions{.target = kimi_target});
    if (!messages)
      return std::string{};
    auto request = kimi_provider.build_request(
        ava::provider::ProviderRequest{
            .provider_id = "kimi", .model_id = "kimi-k2-thinking", .system_prompt = "", .messages = std::move(*messages), .tools_json = {}, .stream = false},
        "token");
    return request ? request->body : std::string{};
  };

  auto const cross_provider_body = kimi_body(reasoning("deepseek", "deepseek-reasoner", "LEGACY_CROSS_PROVIDER_REASONING"));
  auto const cross_model_body = kimi_body(reasoning("kimi", "deepseek-reasoner", "LEGACY_CROSS_MODEL_REASONING"));
  auto const exact_body = kimi_body(reasoning("kimi", "kimi-k2-thinking", "LEGACY_EXACT_REASONING"));
  auto omits_legacy_private = [](std::string const& body) {
    return !body.empty() && body.find("LEGACY_CROSS_") == std::string::npos && body.find("LEGACY_SIGNATURE_CANARY") == std::string::npos &&
           body.find("LEGACY_REDACTED_CANARY") == std::string::npos;
  };
  expect(
      omits_legacy_private(cross_provider_body) && omits_legacy_private(cross_model_body) &&
          cross_provider_body.find("VISIBLE_LEGACY_ANSWER") != std::string::npos && cross_model_body.find("VISIBLE_LEGACY_ANSWER") != std::string::npos &&
          exact_body.find("LEGACY_EXACT_REASONING") != std::string::npos,
      "legacy reasoning_content replay requires the block provider/model to match the complete snapshot and target, while an exact control replays natively");

  auto cross_provider_compaction_entries =
      std::vector<SessionEntry>{kimi_start, reasoning("deepseek", "deepseek-reasoner", "LEGACY_COMPACTION_REASONING"), assistant};
  cross_provider_compaction_entries.push_back(
      SessionEntry{.id = "legacy_reasoning_compaction",
                   .parent_id = "",
                   .type = EntryType::Compaction,
                   .timestamp = "2026-07-24T00:00:03Z",
                   .data_json = R"({"summary":"LEGACY_COMPACTION_PRIVATE_CANARY","provider":"kimi","model":"kimi-k2-thinking"})"});
  auto const cross_provider_compaction =
      ava::agent::build_provider_messages_from_entries(cross_provider_compaction_entries, ava::agent::MessageBuildOptions{.target = kimi_target});
  expect(cross_provider_compaction && cross_provider_compaction->size() == 1 &&
             cross_provider_compaction->front().content ==
                 "Earlier compacted provider history was omitted because exact replay compatibility could not be proven." &&
             cross_provider_compaction->front().content.find("LEGACY_COMPACTION_PRIVATE_CANARY") == std::string::npos,
         "legacy compaction exact-source proof rejects a same-format reasoning block whose own provider/model contradicts the snapshot");

  std::vector<SessionEntry> const openai_entries = {
      SessionEntry{.id = "legacy_openai_start",
                   .parent_id = "",
                   .type = EntryType::SessionStart,
                   .timestamp = "2026-07-24T00:00:00Z",
                   .data_json = R"({"provider":"openai","model":"gpt-5.5","api_family":"openai_responses","reasoning_format":"openai_responses"})"},
      SessionEntry{
          .id = "legacy_openai_reasoning",
          .parent_id = "",
          .type = EntryType::ReasoningBlock,
          .timestamp = "2026-07-24T00:00:01Z",
          .data_json =
              R"({"provider":"anthropic","model":"claude-test","format":"openai_responses","text":"OPENAI_LEGACY_REASONING_CANARY","signature":"OPENAI_LEGACY_SIGNATURE_CANARY","redacted_data":"OPENAI_LEGACY_REDACTED_CANARY","native_item_json":"{\"id\":\"rs_legacy_cross_source\",\"type\":\"reasoning\",\"summary\":[],\"encrypted_content\":\"OPENAI_LEGACY_NATIVE_CANARY\"}","redacted":false})"},
      assistant};
  auto openai_messages = ava::agent::build_provider_messages_from_entries(
      openai_entries, ava::agent::MessageBuildOptions{.target = ava::agent::HistoryReplayTarget{.provider_id = "openai",
                                                                                                .model_id = "gpt-5.5",
                                                                                                .api_family = "openai_responses",
                                                                                                .reasoning_format = "openai_responses",
                                                                                                .supports_tools = false,
                                                                                                .supports_images = false}});
  ava::provider::OpenAIProvider const openai_provider("https://api.example.test");
  auto openai_request =
      openai_messages
          ? openai_provider.build_request(
                ava::provider::ProviderRequest{
                    .provider_id = "openai", .model_id = "gpt-5.5", .system_prompt = "", .messages = *openai_messages, .tools_json = {}, .stream = false},
                "token")
          : ava::core::Result<ava::provider::HttpRequest>{std::unexpected(openai_messages.error())};
  auto const openai_body = openai_request ? openai_request->body : std::string{};
  expect(
      openai_request && openai_body.find("VISIBLE_LEGACY_ANSWER") != std::string::npos && openai_body.find("OPENAI_LEGACY_") == std::string::npos &&
          openai_body.find("rs_legacy_cross_source") == std::string::npos,
      "OpenAI Responses generated history omits native reasoning text, signature, redacted data, and native item metadata from a contradictory legacy block");
}

void test_v4_no_tools_fallback_survives_native_content_serializers()
{
  using ava::session::AssistantOutputFunctionCall;
  using ava::session::AssistantOutputItem;
  using ava::session::AssistantOutputItemKind;
  using ava::session::AssistantOutputReasoning;
  using ava::session::AssistantOutputText;
  using ava::session::AssistantOutputTextPhase;
  using ava::session::AssistantTurnCommit;
  using ava::session::EntryType;
  using ava::session::SessionEntry;

  auto item_entry = [](std::string id, AssistantOutputItem item) {
    auto data = ava::session::serialize_assistant_output_item_data_json(item);
    return SessionEntry{
        .id = std::move(id), .parent_id = "", .type = EntryType::AssistantOutputItem, .timestamp = "2026-07-24T00:00:00Z", .data_json = data.value_or("{}")};
  };
  std::vector<SessionEntry> entries = {
      item_entry("portable_commentary",
                 AssistantOutputItem{.assistant_turn_id = "turn_no_tools",
                                     .sequence = 0,
                                     .kind = AssistantOutputItemKind::Text,
                                     .provider_item_id = "msg_private_commentary",
                                     .provider_output_index = 0,
                                     .payload = AssistantOutputText{.text = "VISIBLE_COMMENTARY", .assistant_phase = AssistantOutputTextPhase::Commentary}}),
      item_entry(
          "portable_reasoning",
          AssistantOutputItem{
              .assistant_turn_id = "turn_no_tools",
              .sequence = 1,
              .kind = AssistantOutputItemKind::Reasoning,
              .provider_item_id = "rs_private_no_tools",
              .provider_output_index = 1,
              .payload =
                  AssistantOutputReasoning{
                      .text = "PRIVATE_REASONING_CANARY",
                      .format = "openai_responses",
                      .redacted = false,
                      .signature = "PRIVATE_SIGNATURE_CANARY",
                      .redacted_data = "PRIVATE_REDACTED_CANARY",
                      .native_item_json = R"({"id":"rs_private_no_tools","type":"reasoning","summary":[],"encrypted_content":"PRIVATE_NATIVE_CANARY"})"}}),
      item_entry("portable_function", AssistantOutputItem{.assistant_turn_id = "turn_no_tools",
                                                          .sequence = 2,
                                                          .kind = AssistantOutputItemKind::FunctionCall,
                                                          .provider_item_id = "fc_private_no_tools",
                                                          .provider_output_index = 2,
                                                          .payload = AssistantOutputFunctionCall{.call_id = "call_private_no_tools",
                                                                                                 .name = "read_file",
                                                                                                 .arguments_json = R"({"path":"README.md"})"}}),
      item_entry("portable_answer",
                 AssistantOutputItem{.assistant_turn_id = "turn_no_tools",
                                     .sequence = 3,
                                     .kind = AssistantOutputItemKind::Text,
                                     .provider_item_id = "msg_private_answer",
                                     .provider_output_index = 3,
                                     .payload = AssistantOutputText{.text = "VISIBLE_ANSWER", .assistant_phase = AssistantOutputTextPhase::FinalAnswer}})};
  auto commit_data = ava::session::serialize_assistant_turn_commit_data_json(AssistantTurnCommit{.assistant_turn_id = "turn_no_tools",
                                                                                                 .item_count = 4,
                                                                                                 .provider = "openai",
                                                                                                 .model = "gpt-5.5",
                                                                                                 .api_family = "openai_responses",
                                                                                                 .reasoning_format = "openai_responses",
                                                                                                 .finish_reason = "tool_calls",
                                                                                                 .usage_json = std::nullopt});
  entries.push_back(SessionEntry{.id = "portable_commit",
                                 .parent_id = "",
                                 .type = EntryType::AssistantTurnCommit,
                                 .timestamp = "2026-07-24T00:00:01Z",
                                 .data_json = commit_data.value_or("{}")});
  entries.push_back(SessionEntry{
      .id = "portable_result",
      .parent_id = "",
      .type = EntryType::ToolResult,
      .timestamp = "2026-07-24T00:00:02Z",
      .data_json =
          R"({"assistant_output_entry_id":"portable_function","call_id":"call_private_no_tools","name":"read_file","success":true,"result":"VISIBLE_TOOL_RESULT"})"});

  auto messages_for = [&](ava::agent::HistoryReplayTarget target) {
    return ava::agent::build_provider_messages_from_entries(entries, ava::agent::MessageBuildOptions{.target = std::move(target)});
  };
  auto openai_messages = messages_for(ava::agent::HistoryReplayTarget{.provider_id = "openai",
                                                                      .model_id = "gpt-5.5",
                                                                      .api_family = "openai_responses",
                                                                      .reasoning_format = "openai_responses",
                                                                      .supports_tools = false,
                                                                      .supports_images = false});
  auto anthropic_messages = messages_for(ava::agent::HistoryReplayTarget{.provider_id = "anthropic",
                                                                         .model_id = "claude-test",
                                                                         .api_family = "anthropic_messages",
                                                                         .reasoning_format = "anthropic_thinking",
                                                                         .supports_tools = false,
                                                                         .supports_images = false});
  auto compatible_messages = messages_for(ava::agent::HistoryReplayTarget{.provider_id = "kimi",
                                                                          .model_id = "kimi-k2-thinking",
                                                                          .api_family = "openai_chat_completions",
                                                                          .reasoning_format = "reasoning_content",
                                                                          .supports_tools = false,
                                                                          .supports_images = false});

  ava::provider::OpenAIProvider const openai("https://api.example.test");
  ava::provider::AnthropicProvider const anthropic("https://api.example.test");
  ava::provider::OpenAICompatibleProvider const compatible(ava::provider::OpenAICompatibleProviderOptions{
      .base_url = "https://api.example.test", .provider_name = "Kimi", .reasoning_format = "reasoning_content", .preserve_reasoning_content = true});
  auto request_for = [](std::string provider_id, std::string model_id, std::vector<ava::provider::ChatMessage> messages) {
    return ava::provider::ProviderRequest{.provider_id = std::move(provider_id),
                                          .model_id = std::move(model_id),
                                          .system_prompt = "",
                                          .messages = std::move(messages),
                                          .tools_json = {},
                                          .stream = false,
                                          .max_output_tokens = std::nullopt,
                                          .reasoning = std::nullopt,
                                          .system_prompt_cache_ttl = ""};
  };
  auto openai_request = openai_messages ? openai.build_request(request_for("openai", "gpt-5.5", *openai_messages), "token")
                                        : ava::core::Result<ava::provider::HttpRequest>{std::unexpected(openai_messages.error())};
  auto anthropic_request = anthropic_messages ? anthropic.build_request(request_for("anthropic", "claude-test", *anthropic_messages), "token")
                                              : ava::core::Result<ava::provider::HttpRequest>{std::unexpected(anthropic_messages.error())};
  auto compatible_request = compatible_messages ? compatible.build_request(request_for("kimi", "kimi-k2-thinking", *compatible_messages), "token")
                                                : ava::core::Result<ava::provider::HttpRequest>{std::unexpected(compatible_messages.error())};

  auto has_portable_turn = [](std::string const& body) {
    auto const commentary = body.find("VISIBLE_COMMENTARY");
    auto const call = body.find("Tool call (read_file)");
    auto const answer = body.find("VISIBLE_ANSWER");
    auto const result = body.find("VISIBLE_TOOL_RESULT");
    return commentary != std::string::npos && call != std::string::npos && answer != std::string::npos && result != std::string::npos && commentary < call &&
           call < answer && answer < result && body.find("Tool call (read_file)", call + 1) == std::string::npos &&
           body.find("README.md") != std::string::npos && body.find("call_private_no_tools") == std::string::npos && body.find("PRIVATE_") == std::string::npos;
  };
  auto const openai_body = openai_request ? openai_request->body : std::string{};
  auto const anthropic_body = anthropic_request ? anthropic_request->body : std::string{};
  auto const compatible_body = compatible_request ? compatible_request->body : std::string{};
  expect(openai_request && anthropic_request && compatible_request && has_portable_turn(openai_body) && has_portable_turn(anthropic_body) &&
             has_portable_turn(compatible_body) && openai_body.find(R"("type":"function_call")") == std::string::npos &&
             openai_body.find(R"("type":"function_call_output")") == std::string::npos && anthropic_body.find(R"("type":"tool_use")") == std::string::npos &&
             anthropic_body.find(R"("type":"tool_result")") == std::string::npos && compatible_body.find(R"("tool_calls")") == std::string::npos &&
             compatible_body.find(R"("role":"tool")") == std::string::npos,
         "OpenAI Responses, Anthropic Messages, and OpenAI-compatible bodies retain ordered no-tools call/result fallback text without private or native tool "
         "data");
}

void test_request_time_history_projection_preserves_only_exact_native_replay()
{
  using ava::session::AssistantOutputFunctionCall;
  using ava::session::AssistantOutputItem;
  using ava::session::AssistantOutputItemKind;
  using ava::session::AssistantOutputReasoning;
  using ava::session::AssistantOutputText;
  using ava::session::AssistantOutputTextPhase;
  using ava::session::AssistantTurnCommit;
  using ava::session::EntryType;
  using ava::session::SessionEntry;

  auto item_entry = [](std::string id, AssistantOutputItem item) {
    auto data = ava::session::serialize_assistant_output_item_data_json(item);
    return SessionEntry{
        .id = std::move(id), .parent_id = "", .type = EntryType::AssistantOutputItem, .timestamp = "2026-07-24T00:00:00Z", .data_json = data.value_or("{}")};
  };
  auto commit_entry = [](AssistantTurnCommit commit) {
    auto data = ava::session::serialize_assistant_turn_commit_data_json(commit);
    return SessionEntry{.id = "commit_projection",
                        .parent_id = "",
                        .type = EntryType::AssistantTurnCommit,
                        .timestamp = "2026-07-24T00:00:01Z",
                        .data_json = data.value_or("{}")};
  };

  std::vector<SessionEntry> entries = {
      item_entry(
          "reasoning_projection",
          AssistantOutputItem{
              .assistant_turn_id = "turn_projection",
              .sequence = 0,
              .kind = AssistantOutputItemKind::Reasoning,
              .provider_item_id = "rs_source_canary",
              .provider_output_index = 0,
              .payload =
                  AssistantOutputReasoning{
                      .text = "PRIVATE_REASONING_CANARY",
                      .format = "openai_responses",
                      .redacted = false,
                      .signature = "PRIVATE_SIGNATURE_CANARY",
                      .redacted_data = "PRIVATE_REDACTED_CANARY",
                      .native_item_json = R"({"id":"rs_source_canary","type":"reasoning","summary":[],"encrypted_content":"PRIVATE_ENCRYPTED_CANARY"})"}}),
      item_entry("function_projection", AssistantOutputItem{.assistant_turn_id = "turn_projection",
                                                            .sequence = 1,
                                                            .kind = AssistantOutputItemKind::FunctionCall,
                                                            .provider_item_id = "fc_source_canary",
                                                            .provider_output_index = 1,
                                                            .payload = AssistantOutputFunctionCall{.call_id = "call_source_canary",
                                                                                                   .name = "read_file",
                                                                                                   .arguments_json = R"({"path":"README.md"})"}}),
      item_entry("text_projection",
                 AssistantOutputItem{.assistant_turn_id = "turn_projection",
                                     .sequence = 2,
                                     .kind = AssistantOutputItemKind::Text,
                                     .provider_item_id = "msg_source_canary",
                                     .provider_output_index = 2,
                                     .payload = AssistantOutputText{.text = "VISIBLE_ANSWER", .assistant_phase = AssistantOutputTextPhase::FinalAnswer}}),
      commit_entry(AssistantTurnCommit{.assistant_turn_id = "turn_projection",
                                       .item_count = 3,
                                       .provider = "openai",
                                       .model = "gpt-5.5",
                                       .api_family = "openai_responses",
                                       .reasoning_format = "openai_responses",
                                       .finish_reason = "tool_calls",
                                       .usage_json = std::nullopt}),
      SessionEntry{
          .id = "result_projection",
          .parent_id = "",
          .type = EntryType::ToolResult,
          .timestamp = "2026-07-24T00:00:02Z",
          .data_json =
              R"({"assistant_output_entry_id":"function_projection","call_id":"call_source_canary","name":"read_file","success":true,"result":"VISIBLE_TOOL_RESULT"})"}};

  auto target = ava::agent::HistoryReplayTarget{.provider_id = "openai",
                                                .model_id = "gpt-5.5",
                                                .api_family = "openai_responses",
                                                .reasoning_format = "openai_responses",
                                                .supports_tools = true,
                                                .supports_images = true};
  auto exact = ava::agent::build_provider_messages_from_entries(entries, ava::agent::MessageBuildOptions{.target = target});
  bool exact_native = exact && exact->size() == 2 && (*exact)[0].content_parts.size() == 3 && (*exact)[1].content_parts.size() == 1;
  if (exact_native)
  {
    auto const& reasoning = (*exact)[0].content_parts[0];
    auto const& function = (*exact)[0].content_parts[1];
    auto const& answer = (*exact)[0].content_parts[2];
    auto const& result = (*exact)[1].content_parts[0];
    exact_native = reasoning.type == ava::provider::ContentPartType::Reasoning && reasoning.provider_item_id == "rs_source_canary" &&
                   reasoning.reasoning_native_item_json.find("PRIVATE_ENCRYPTED_CANARY") != std::string::npos &&
                   function.type == ava::provider::ContentPartType::ToolUse && function.provider_item_id == "fc_source_canary" &&
                   function.tool_call_id == "call_source_canary" && answer.provider_item_id == "msg_source_canary" &&
                   result.tool_call_id == "call_source_canary";
  }
  expect(exact_native, "an exact source provider/model/API/reasoning target preserves native reasoning and provider/tool identities");

  auto duplicate_provider_id_entries = entries;
  auto const duplicate_id_offset = duplicate_provider_id_entries[2].data_json.find("msg_source_canary");
  if (duplicate_id_offset != std::string::npos)
    duplicate_provider_id_entries[2].data_json.replace(duplicate_id_offset, std::string("msg_source_canary").size(), "rs_source_canary");
  auto duplicate_provider_ids =
      ava::agent::build_provider_messages_from_entries(duplicate_provider_id_entries, ava::agent::MessageBuildOptions{.target = target});
  expect(!duplicate_provider_ids, "duplicate provider item identities are a hard v4 classification error rather than partial native replay");

  auto malformed_provider_id_entries = entries;
  auto const malformed_id_offset = malformed_provider_id_entries[2].data_json.find("msg_source_canary");
  if (malformed_id_offset != std::string::npos)
    malformed_provider_id_entries[2].data_json.replace(malformed_id_offset, std::string("msg_source_canary").size(), "");
  auto malformed_provider_id =
      ava::agent::build_provider_messages_from_entries(malformed_provider_id_entries, ava::agent::MessageBuildOptions{.target = target});
  expect(!malformed_provider_id, "a malformed provider item identity is a hard v4 classification error rather than silent native replay");

  auto missing_target = ava::agent::build_provider_messages_from_entries(entries);
  std::string missing_target_fields;
  if (missing_target)
  {
    for (auto const& message : *missing_target)
    {
      missing_target_fields += message.content;
      for (auto const& part : message.content_parts)
        missing_target_fields += part.text + part.tool_call_id + part.provider_item_id + part.reasoning_signature + part.reasoning_native_item_json;
    }
  }
  expect(missing_target && missing_target_fields.find("VISIBLE_ANSWER") != std::string::npos &&
             missing_target_fields.find("VISIBLE_TOOL_RESULT") != std::string::npos && missing_target_fields.find("PRIVATE_") == std::string::npos &&
             missing_target_fields.find("source_canary") == std::string::npos &&
             std::ranges::all_of(*missing_target,
                                 [](auto const& message) {
                                   return std::ranges::none_of(message.content_parts, [](auto const& part) {
                                     return part.type == ava::provider::ContentPartType::Reasoning || part.type == ava::provider::ContentPartType::ToolUse ||
                                            part.type == ava::provider::ContentPartType::ToolResult || !part.provider_item_id.empty();
                                   });
                                 }),
         "an omitted request target is force-portable and can never activate exact native replay");

  auto forced_portable = ava::agent::build_provider_messages_from_entries(
      entries, ava::agent::MessageBuildOptions{.target = target, .replay_mode = ava::agent::HistoryReplayMode::ForcePortable});
  expect(forced_portable && forced_portable->size() == 2 && forced_portable->front().content_parts.size() == 2 &&
             forced_portable->front().content_parts.front().type == ava::provider::ContentPartType::ToolUse &&
             forced_portable->front().content_parts.front().tool_call_id != "call_source_canary" &&
             std::ranges::none_of(forced_portable->front().content_parts,
                                  [](auto const& part) { return part.type == ava::provider::ContentPartType::Reasoning || !part.provider_item_id.empty(); }),
         "explicit ForcePortable preserves known tool capability while still preventing native reasoning and source identities");

  auto incomplete_target = target;
  incomplete_target.api_family.clear();
  auto incomplete = ava::agent::build_provider_messages_from_entries(entries, ava::agent::MessageBuildOptions{.target = incomplete_target});
  expect(incomplete && std::ranges::all_of(*incomplete,
                                           [](auto const& message) {
                                             return std::ranges::none_of(message.content_parts, [](auto const& part) {
                                               return part.type == ava::provider::ContentPartType::Reasoning ||
                                                      part.type == ava::provider::ContentPartType::ToolUse ||
                                                      part.type == ava::provider::ContentPartType::ToolResult || !part.provider_item_id.empty();
                                             });
                                           }),
         "an incomplete target is conservatively force-portable and supplies no unproven tool or image capability");

  target.model_id = "gpt-5.6-sol";
  auto portable = ava::agent::build_provider_messages_from_entries(entries, ava::agent::MessageBuildOptions{.target = target});
  bool portable_pair = portable && portable->size() == 2 && (*portable)[0].content_parts.size() == 2 && (*portable)[1].content_parts.size() == 1;
  std::string portable_fields;
  if (portable_pair)
  {
    auto const& function = (*portable)[0].content_parts[0];
    auto const& answer = (*portable)[0].content_parts[1];
    auto const& result = (*portable)[1].content_parts[0];
    portable_pair = function.type == ava::provider::ContentPartType::ToolUse && result.type == ava::provider::ContentPartType::ToolResult &&
                    !function.tool_call_id.empty() && function.tool_call_id == result.tool_call_id && function.tool_call_id != "call_source_canary" &&
                    function.provider_item_id.empty() && answer.text == "VISIBLE_ANSWER" && answer.provider_item_id.empty() &&
                    result.text == "VISIBLE_TOOL_RESULT";
    for (auto const& message : *portable)
    {
      portable_fields += message.content;
      for (auto const& part : message.content_parts)
      {
        portable_fields +=
            part.text + part.tool_call_id + part.provider_item_id + part.reasoning_signature + part.reasoning_redacted_data + part.reasoning_native_item_json;
      }
    }
  }
  expect(portable_pair && portable_fields.find("PRIVATE_") == std::string::npos && portable_fields.find("source_canary") == std::string::npos,
         "a model switch keeps visible answer/tool semantics with paired request-local IDs and drops every private/source identity canary");

  target.model_id = "gpt-5.5";
  target.api_family = "openai_chat_completions";
  auto cross_api = ava::agent::build_provider_messages_from_entries(entries, ava::agent::MessageBuildOptions{.target = target});
  expect(cross_api && cross_api->size() == 2 && cross_api->front().content_parts.size() == 2 &&
             cross_api->front().content_parts.front().type == ava::provider::ContentPartType::ToolUse &&
             cross_api->front().content_parts.front().tool_call_id != "call_source_canary" &&
             std::ranges::none_of(cross_api->front().content_parts,
                                  [](auto const& part) { return part.type == ava::provider::ContentPartType::Reasoning || !part.provider_item_id.empty(); }),
         "a same-provider, same-model API-family change forces portable replay rather than sending endpoint-native identities");

  target.api_family = "openai_responses";
  target.reasoning_format = "reasoning_content";
  auto cross_reasoning_format = ava::agent::build_provider_messages_from_entries(entries, ava::agent::MessageBuildOptions{.target = target});
  expect(cross_reasoning_format && cross_reasoning_format->size() == 2 && cross_reasoning_format->front().content_parts.size() == 2 &&
             std::ranges::none_of(cross_reasoning_format->front().content_parts,
                                  [](auto const& part) { return part.type == ava::provider::ContentPartType::Reasoning || !part.provider_item_id.empty(); }),
         "an incompatible reasoning format drops reasoning and forces the containing turn through portable replay");

  target.reasoning_format = "openai_responses";
  auto exact_after_portable = ava::agent::build_provider_messages_from_entries(entries, ava::agent::MessageBuildOptions{.target = target});
  expect(exact_after_portable && exact_after_portable->size() == 2 && exact_after_portable->front().content_parts.size() == 3 &&
             exact_after_portable->front().content_parts[0].reasoning_native_item_json.find("PRIVATE_ENCRYPTED_CANARY") != std::string::npos &&
             exact_after_portable->front().content_parts[1].tool_call_id == "call_source_canary" &&
             exact_after_portable->back().content_parts.front().tool_call_id == "call_source_canary",
         "A-to-B-to-A request projection is copy-only: returning to the exact target can safely reconstruct native replay from unchanged session records");
  target.model_id = "gpt-5.6-sol";

  std::vector<SessionEntry> const synthetic_collision_entries = {
      SessionEntry{.id = "legacy_collision_call",
                   .parent_id = "",
                   .type = EntryType::ToolCall,
                   .timestamp = "2026-07-24T00:00:00Z",
                   .data_json = R"({"call_id":"ava_history_tool_1","name":"read_file","arguments":"{\"path\":\"README.md\"}"})"},
      SessionEntry{.id = "legacy_collision_result",
                   .parent_id = "",
                   .type = EntryType::ToolResult,
                   .timestamp = "2026-07-24T00:00:01Z",
                   .data_json = R"({"call_id":"ava_history_tool_1","name":"read_file","success":true,"result":"collision-safe"})"}};
  auto synthetic_collision = ava::agent::build_provider_messages_from_entries(synthetic_collision_entries, ava::agent::MessageBuildOptions{.target = target});
  expect(synthetic_collision && synthetic_collision->size() == 2 && synthetic_collision->front().content_parts.size() == 1 &&
             synthetic_collision->back().content_parts.size() == 1 && synthetic_collision->front().content_parts.front().tool_call_id == "ava_history_tool_2" &&
             synthetic_collision->back().content_parts.front().tool_call_id == "ava_history_tool_2" &&
             synthetic_collision->front().content.find("ava_history_tool_1") == std::string::npos &&
             synthetic_collision->back().content.find("ava_history_tool_1") == std::string::npos,
         "request-local tool IDs skip every persisted source ID so a source cannot collide with or leak through the synthetic namespace");

  target.supports_tools = false;
  auto text_tools = ava::agent::build_provider_messages_from_entries(entries, ava::agent::MessageBuildOptions{.target = target});
  std::string text_tool_projection;
  if (text_tools)
    for (auto const& message : *text_tools) text_tool_projection += message.content;
  expect(text_tools &&
             std::ranges::all_of(*text_tools,
                                 [](auto const& message) {
                                   return std::ranges::none_of(message.content_parts, [](auto const& part) {
                                     return part.type == ava::provider::ContentPartType::ToolUse || part.type == ava::provider::ContentPartType::ToolResult;
                                   });
                                 }) &&
             text_tool_projection.find("Tool call") != std::string::npos && text_tool_projection.find("Tool result") != std::string::npos &&
             text_tool_projection.find("VISIBLE_TOOL_RESULT") != std::string::npos && text_tool_projection.find("call_source_canary") == std::string::npos,
         "a no-tools target receives a labelled textual call/result pair without source or synthetic call IDs");

  auto unknown_commit = commit_entry(AssistantTurnCommit{.assistant_turn_id = "turn_projection",
                                                         .item_count = 3,
                                                         .provider = "openai",
                                                         .model = "gpt-5.5",
                                                         .finish_reason = "tool_calls",
                                                         .usage_json = std::nullopt});
  auto unknown_entries = entries;
  unknown_entries[3] = std::move(unknown_commit);
  target.model_id = "gpt-5.5";
  target.supports_tools = true;
  auto unknown = ava::agent::build_provider_messages_from_entries(unknown_entries, ava::agent::MessageBuildOptions{.target = target});
  expect(unknown && !unknown->empty() &&
             std::ranges::none_of(unknown->front().content_parts,
                                  [](auto const& part) {
                                    return part.type == ava::provider::ContentPartType::Reasoning || !part.provider_item_id.empty() ||
                                           part.tool_call_id == "call_source_canary";
                                  }),
         "a child-style committed turn without snapshot or commit API provenance is always portable");

  auto contradictory_entries = entries;
  contradictory_entries.insert(contradictory_entries.begin(),
                               SessionEntry{.id = "contradictory_start",
                                            .parent_id = "",
                                            .type = EntryType::SessionStart,
                                            .timestamp = "2026-07-23T23:59:59Z",
                                            .data_json = R"({"provider":"anthropic","model":"claude-sonnet-4-5","original_cwd":"/tmp"})"});
  auto contradictory = ava::agent::build_provider_messages_from_entries(contradictory_entries, ava::agent::MessageBuildOptions{.target = target});
  expect(contradictory && !contradictory->empty() &&
             std::ranges::none_of(contradictory->front().content_parts,
                                  [](auto const& part) {
                                    return part.type == ava::provider::ContentPartType::Reasoning || !part.provider_item_id.empty() ||
                                           part.tool_call_id == "call_source_canary";
                                  }),
         "a commit cannot claim native replay authority when even a partial session snapshot contradicts its provider and model");

  auto changed_before_result_entries = entries;
  changed_before_result_entries.insert(
      changed_before_result_entries.begin() + 4,
      SessionEntry{
          .id = "changed_before_result",
          .parent_id = "",
          .type = EntryType::ModelChange,
          .timestamp = "2026-07-24T00:00:01Z",
          .data_json =
              R"({"previous_provider":"openai","previous_model":"gpt-5.5","provider":"anthropic","model":"claude-sonnet-4-5","api_family":"anthropic_messages","reasoning_format":"anthropic_thinking"})"});
  auto changed_before_result =
      ava::agent::build_provider_messages_from_entries(changed_before_result_entries, ava::agent::MessageBuildOptions{.target = target});
  expect(changed_before_result && !changed_before_result->empty() &&
             std::ranges::none_of(changed_before_result->front().content_parts,
                                  [](auto const& part) {
                                    return part.type == ava::provider::ContentPartType::Reasoning || !part.provider_item_id.empty() ||
                                           part.tool_call_id == "call_source_canary";
                                  }),
         "a v4 result recorded across a contradictory model snapshot is projected portably rather than retaining native call identities");

  auto legacy_compaction_entries = entries;
  legacy_compaction_entries.push_back(SessionEntry{.id = "older_portable_compaction_projection",
                                                   .parent_id = "",
                                                   .type = EntryType::Compaction,
                                                   .timestamp = "2026-07-24T00:00:02Z",
                                                   .data_json = R"({"summary":"OLDER_PORTABLE_SUMMARY","history_projection":"portable-v1"})"});
  legacy_compaction_entries.push_back(SessionEntry{
      .id = "legacy_compaction_projection",
      .parent_id = "",
      .type = EntryType::Compaction,
      .timestamp = "2026-07-24T00:00:03Z",
      .data_json = R"({"summary":"PRIVATE_LEGACY_COMPACTION_CANARY","instructions":"PRIVATE_LEGACY_INSTRUCTIONS","provider":"openai","model":"gpt-5.5"})"});
  auto legacy_compaction = ava::agent::build_provider_messages_from_entries(legacy_compaction_entries, ava::agent::MessageBuildOptions{.target = target});
  expect(legacy_compaction && legacy_compaction->size() == 1 &&
             legacy_compaction->front().content == "Earlier compacted provider history was omitted because exact replay compatibility could not be proven." &&
             legacy_compaction->front().content.find("PRIVATE_LEGACY") == std::string::npos,
         "an unmarked legacy compaction omits all replay-bearing material when its own provenance is unknown, even after an older portable checkpoint");

  std::vector<SessionEntry> const exact_legacy_compaction_entries = {
      SessionEntry{
          .id = "exact_legacy_start",
          .parent_id = "",
          .type = EntryType::SessionStart,
          .timestamp = "2026-07-24T00:00:00Z",
          .data_json =
              R"({"provider":"openai","model":"gpt-5.5","api_family":"openai_responses","reasoning_format":"openai_responses","original_cwd":"/tmp"})"},
      SessionEntry{.id = "exact_legacy_answer",
                   .parent_id = "",
                   .type = EntryType::AssistantMessage,
                   .timestamp = "2026-07-24T00:00:01Z",
                   .data_json = R"({"text":"represented answer"})"},
      SessionEntry{.id = "exact_legacy_compaction",
                   .parent_id = "",
                   .type = EntryType::Compaction,
                   .timestamp = "2026-07-24T00:00:02Z",
                   .data_json = R"({"summary":"EXACT_LEGACY_SUMMARY","provider":"openai","model":"gpt-5.5"})"}};
  auto exact_legacy_compaction =
      ava::agent::build_provider_messages_from_entries(exact_legacy_compaction_entries, ava::agent::MessageBuildOptions{.target = target});
  expect(exact_legacy_compaction && exact_legacy_compaction->size() == 1 &&
             exact_legacy_compaction->front().content.find("EXACT_LEGACY_SUMMARY") != std::string::npos,
         "an unmarked legacy compaction retains represented summary data only when its complete source range and boundary are exact-compatible");

  legacy_compaction_entries.back().data_json =
      R"({"summary":"PORTABLE_COMPACTION_SUMMARY","instructions":"portable carry","provider":"openai","model":"gpt-5.5","history_projection":"portable-v1"})";
  auto marked_compaction = ava::agent::build_provider_messages_from_entries(legacy_compaction_entries);
  expect(marked_compaction && marked_compaction->size() == 1 && marked_compaction->front().content.find("PORTABLE_COMPACTION_SUMMARY") != std::string::npos &&
             marked_compaction->front().content.find("portable carry") != std::string::npos,
         "a portable-v1 compaction remains replayable even when the request target is omitted and therefore forced portable");

  auto unresolved_entries = entries;
  unresolved_entries.pop_back();
  auto unresolved = ava::agent::build_provider_messages_from_entries(unresolved_entries, ava::agent::MessageBuildOptions{.target = target});
  expect(!unresolved && unresolved.error().message().find("tool result") != std::string::npos,
         "an unresolved committed v4 function still fails at request time instead of inventing a portable completion");
}

void test_request_time_history_projection_reserves_images_for_active_turn()
{
  auto image_entry = [](int index, std::size_t byte_size) {
    auto const id = "img_" + std::to_string(index);
    return ava::session::SessionEntry{
        .id = "user_" + std::to_string(index),
        .parent_id = "",
        .type = ava::session::EntryType::UserMessage,
        .timestamp = "2026-07-24T00:00:00Z",
        .data_json = "{\"text\":\"image " + std::to_string(index) + "\",\"attachments\":[{\"id\":\"" + id +
                     "\",\"type\":\"image\",\"mime_type\":\"image/png\",\"byte_size\":" + std::to_string(byte_size) +
                     ",\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"storage_path\":\"attachments/" + id + ".png\"}]}"};
  };

  std::vector<ava::session::SessionEntry> entries;
  for (int index = 0; index < 17; ++index) entries.push_back(image_entry(index, 1));
  auto target = ava::agent::HistoryReplayTarget{.provider_id = "openai",
                                                .model_id = "gpt-image",
                                                .api_family = "openai_responses",
                                                .reasoning_format = "openai_responses",
                                                .supports_tools = true,
                                                .supports_images = true};
  auto projected =
      ava::agent::build_provider_messages_from_entries(entries, ava::agent::MessageBuildOptions{.target = target, .active_turn_user_entry_ids = {"user_16"}});
  std::size_t image_parts = 0;
  if (projected)
  {
    for (auto const& message : *projected)
      image_parts += static_cast<std::size_t>(
          std::ranges::count_if(message.content_parts, [](auto const& part) { return part.type == ava::provider::ContentPartType::Image; }));
  }
  expect(projected && projected->size() == 17 && image_parts == 16 &&
             projected->front().content.find("[historical image omitted: mime=image/png bytes=1]") != std::string::npos &&
             projected->front().content.find("img_0") == std::string::npos && projected->front().content_parts.size() == 2 &&
             projected->front().content_parts.back().type == ava::provider::ContentPartType::Text &&
             projected->front().content_parts.back().text == "[historical image omitted: mime=image/png bytes=1]" &&
             projected->back().content.find("id=img_16") != std::string::npos && projected->back().content_parts.size() == 2,
         "historical image count selection is deterministic, reserves capacity for the active image, and omits historical attachment identity");

  std::vector<ava::session::SessionEntry> aggregate = {image_entry(30, 15 * 1024 * 1024), image_entry(31, 15 * 1024 * 1024), image_entry(32, 15 * 1024 * 1024),
                                                       image_entry(33, 1 * 1024 * 1024)};
  auto aggregate_projection =
      ava::agent::build_provider_messages_from_entries(aggregate, ava::agent::MessageBuildOptions{.target = target, .active_turn_user_entry_ids = {"user_33"}});
  std::size_t aggregate_image_parts = 0;
  if (aggregate_projection)
  {
    for (auto const& message : *aggregate_projection)
      aggregate_image_parts += static_cast<std::size_t>(
          std::ranges::count_if(message.content_parts, [](auto const& part) { return part.type == ava::provider::ContentPartType::Image; }));
  }
  expect(aggregate_projection && aggregate_image_parts == 3 &&
             aggregate_projection->front().content.find("[historical image omitted: mime=image/png bytes=15728640]") != std::string::npos &&
             (*aggregate_projection)[1].content.find("[historical image: mime=image/png bytes=15728640]") != std::string::npos &&
             (*aggregate_projection)[2].content.find("[historical image: mime=image/png bytes=15728640]") != std::string::npos,
         "historical image aggregate-byte admission keeps the newest compatible images after reserving the active image bytes");

  std::vector<ava::session::SessionEntry> oversized = {image_entry(20, 6 * 1024 * 1024), image_entry(21, 6 * 1024 * 1024)};
  target.provider_id = "anthropic";
  target.model_id = "claude-image";
  target.api_family = "anthropic_messages";
  target.reasoning_format = "anthropic_thinking";
  auto anthropic =
      ava::agent::build_provider_messages_from_entries(oversized, ava::agent::MessageBuildOptions{.target = target, .active_turn_user_entry_ids = {"user_21"}});
  expect(anthropic && anthropic->size() == 2 && anthropic->front().content_parts.size() == 2 &&
             anthropic->front().content_parts.back().text == "[historical image omitted: mime=image/png bytes=6291456]" &&
             anthropic->front().content.find("[historical image omitted: mime=image/png bytes=6291456]") != std::string::npos &&
             anthropic->front().content.find("img_20") == std::string::npos && anthropic->back().content_parts.size() == 2,
         "provider-specific per-image policy replaces an incompatible historical image while never spending active-turn capacity on history");

  target.supports_images = false;
  auto text_only = ava::agent::build_provider_messages_from_entries(oversized, ava::agent::MessageBuildOptions{.target = target});
  expect(text_only && std::ranges::all_of(*text_only,
                                          [](auto const& message) {
                                            return std::ranges::none_of(message.content_parts,
                                                                        [](auto const& part) { return part.type == ava::provider::ContentPartType::Image; }) &&
                                                   message.content.find("[historical image omitted: mime=image/png bytes=6291456]") != std::string::npos &&
                                                   message.content.find("img_") == std::string::npos &&
                                                   message.content.find("attachments/") == std::string::npos;
                                          }),
         "a text-only switched target receives only MIME-and-size historical image placeholders without attachment identifiers or paths");
}

void test_agent_loop_assistant_turn_lifecycle_validation()
{
  auto event = [](ava::provider::StreamEventType type) {
    auto value = ava::provider::StreamEvent{};
    value.type = type;
    return value;
  };
  auto text_start = event(ava::provider::StreamEventType::TextStart);
  text_start.provider_item_id = "msg_lifecycle";
  text_start.provider_output_index = 1;
  text_start.assistant_phase = ava::provider::AssistantPhase::Commentary;
  auto text_delta = event(ava::provider::StreamEventType::TextDelta);
  text_delta.text = "working";
  text_delta.provider_item_id = "msg_lifecycle";
  text_delta.provider_output_index = 1;
  text_delta.assistant_phase = ava::provider::AssistantPhase::Commentary;
  auto text_end = text_start;
  text_end.type = ava::provider::StreamEventType::TextEnd;
  auto call_start = event(ava::provider::StreamEventType::ToolCallStart);
  call_start.tool_call_id = "call_lifecycle";
  call_start.tool_name = "read_file";
  call_start.provider_item_id = "fc_lifecycle";
  call_start.provider_output_index = 2;
  auto call_delta = call_start;
  call_delta.type = ava::provider::StreamEventType::ToolCallDelta;
  call_delta.text = "{}";
  call_delta.tool_name.clear();
  auto call_end = call_start;
  call_end.type = ava::provider::StreamEventType::ToolCallEnd;
  call_end.tool_name.clear();
  auto done = event(ava::provider::StreamEventType::Done);
  done.finish_reason = ava::provider::ProviderFinishReason::ToolCalls;
  auto const complete = ava::agent::parse_assistant_turn({text_start, text_delta, text_end, call_start, call_delta, call_end, done}, {});

  auto incomplete_start = event(ava::provider::StreamEventType::ToolCallStart);
  incomplete_start.tool_call_id = "call_incomplete";
  incomplete_start.tool_name = "read_file";
  incomplete_start.provider_item_id = "fc_incomplete";
  auto const incomplete_call = ava::agent::parse_assistant_turn({incomplete_start, done}, {});
  auto legacy_call_start = event(ava::provider::StreamEventType::ToolCallStart);
  legacy_call_start.tool_call_id = "call_legacy";
  legacy_call_start.tool_name = "read_file";
  auto const legacy_call = ava::agent::parse_assistant_turn({legacy_call_start, done}, {});
  auto reordered_text_start = text_start;
  reordered_text_start.provider_item_id = "msg_index_one";
  reordered_text_start.provider_output_index = 1;
  auto reordered_text_delta = text_delta;
  reordered_text_delta.provider_item_id = "msg_index_one";
  reordered_text_delta.provider_output_index = 1;
  auto reordered_text_end = text_end;
  reordered_text_end.provider_item_id = "msg_index_one";
  reordered_text_end.provider_output_index = 1;
  auto reordered_reasoning_start = event(ava::provider::StreamEventType::ReasoningStart);
  reordered_reasoning_start.provider_item_id = "rs_index_zero";
  reordered_reasoning_start.provider_output_index = 0;
  auto reordered_reasoning_end = reordered_reasoning_start;
  reordered_reasoning_end.type = ava::provider::StreamEventType::ReasoningEnd;
  auto completed_done = done;
  completed_done.finish_reason = ava::provider::ProviderFinishReason::Completed;
  auto const reordered = ava::agent::parse_assistant_turn(
      {reordered_text_start, reordered_text_delta, reordered_text_end, reordered_reasoning_start, reordered_reasoning_end, completed_done}, {});
  auto const reordered_first_is_reasoning =
      reordered && !reordered->ordered_items.empty() && std::holds_alternative<ava::agent::AssistantReasoningItem>(reordered->ordered_items.front().item);
  expect(complete && complete->ordered_items.size() == 2 && complete->text == "working" && complete->tool_calls.size() == 1 &&
             complete->tool_calls[0].id == "call_lifecycle" && !incomplete_call && legacy_call && legacy_call->tool_calls.size() == 1 &&
             reordered_first_is_reasoning,
         "agent turn assembly orders fully indexed native items by output_index, rejects incomplete native calls, and retains legacy tool turns");
}

void test_usage_accounting_saturates_without_signed_overflow()
{
  ava::provider::TokenUsage ordinary_usage{};
  ordinary_usage.input_tokens = 17;
  ordinary_usage.output_tokens = 25;
  auto const ordinary = ava::agent::with_total_tokens(ordinary_usage);
  ava::provider::TokenUsage saturated_usage{};
  saturated_usage.input_tokens = LLONG_MAX - 3;
  saturated_usage.output_tokens = 10;
  auto const saturated = ava::agent::with_total_tokens(saturated_usage);
  ava::provider::TokenUsage aggregate_usage{};
  aggregate_usage.input_tokens = LLONG_MAX - 2;
  aggregate_usage.output_tokens = 10;
  aggregate_usage.estimated_total_bytes = LLONG_MAX - 1;
  std::optional<ava::provider::TokenUsage> aggregate = aggregate_usage;
  ava::provider::TokenUsage increment{};
  increment.input_tokens = 9;
  increment.output_tokens = 5;
  increment.estimated_total_bytes = 7;
  ava::agent::accumulate_usage(aggregate, increment);
  expect(ordinary.total_tokens && *ordinary.total_tokens == 42 && saturated.total_tokens && *saturated.total_tokens == LLONG_MAX && aggregate &&
             aggregate->input_tokens && *aggregate->input_tokens == LLONG_MAX && aggregate->output_tokens && *aggregate->output_tokens == 15 &&
             aggregate->estimated_total_bytes && *aggregate->estimated_total_bytes == LLONG_MAX,
         "usage accounting preserves ordinary sums and saturates every near-LLONG_MAX aggregate without signed overflow");
}

void test_agent_loop_text_only_turn()
{
  auto const root = create_empty_root("agent-text");

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
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
      .api_family = "openai_responses",
      .reasoning_format = "openai_responses",
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
  auto const projection = entries ? ava::session::classify_assistant_output(*entries) : ava::session::AssistantOutputProjection{};
  expect(entries && entries->size() == 3 && (*entries)[0].type == ava::session::EntryType::UserMessage && projection.turns.size() == 1 &&
             projection.turns.front().items.size() == 1 && projection.turns.front().commit.api_family == "openai_responses" &&
             projection.turns.front().commit.reasoning_format == "openai_responses" &&
             std::get<ava::session::AssistantOutputText>(projection.turns.front().items.front().item.payload).text == "hello user",
         "agent loop persists text-only provider output with explicit source API/reasoning provenance as one committed v4 assistant turn");
}

void test_agent_loop_uses_established_session_read_limits()
{
  auto const root = create_empty_root("agent-established-read-limits");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "established-read-limits"});

  for (std::size_t index = 0; index < 10; ++index)
  {
    auto appended = append_session_entry_for_test(store, ava::session::SessionEntry{.id = "large-history-" + std::to_string(index),
                                                                                    .parent_id = "",
                                                                                    .type = ava::session::EntryType::Error,
                                                                                    .timestamp = ava::session::now_timestamp(),
                                                                                    .data_json = "{\"message\":\"" + std::string(900U * 1024U, 'x') + "\"}"});
    expect(appended.has_value(), "large legacy-unbounded agent history fixture appends");
    if (!appended)
      return;
  }

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport legacy_transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"legacy reads succeed\"}\n\ndata: [DONE]\n\n")});
  ava::agent::AgentLoop legacy_loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto legacy_result = legacy_loop.run_turn("read large legacy history", store, provider, legacy_transport);

  ava::tests::FakeTransport bounded_transport({});
  ava::agent::AgentLoop bounded_loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
      .session_read_limits = ava::session::SessionReadLimits{.max_file_bytes = 8U * 1024U * 1024U, .max_line_bytes = 1024U * 1024U, .max_entries = 64},
  });
  auto bounded_result = bounded_loop.run_turn("bounded read must fail", store, provider, bounded_transport);
  expect(legacy_result && legacy_transport.requests().size() == 1 && !bounded_result &&
             bounded_result.error().message().find("bounded read limit") != std::string::npos && bounded_transport.requests().empty(),
         "AgentLoop preserves legacy-unbounded history seeding while an explicit runtime read policy remains bounded");
}

void test_agent_loop_authority_policy_applies_between_provider_iterations()
{
  auto const root = create_empty_root("agent-authority-policy-between-iterations");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "tool content";
  }

  auto store = ava::session::SessionStore::create_ephemeral(workspace);
  auto const limits = ava::session::SessionReadLimits{.max_file_bytes = 4096, .max_line_bytes = 2048, .max_entries = 1};
  auto target = store ? ava::session::SessionAppendTarget::create_ephemeral(*store, limits)
                      : ava::core::Result<std::shared_ptr<ava::session::SessionAppendTarget>>(std::unexpected(store.error()));
  auto authority = target ? (*target)->read_authority() : ava::core::Result<ava::session::SessionReadAuthority>(std::unexpected(target.error()));
  if (!store || !target || !authority)
  {
    expect(false, "policy-bound provider-iteration fixture creates its ephemeral append and read authorities");
    return;
  }

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_policy\",\"type\":\"function_call\","
                    "\"call_id\":\"call_policy\",\"name\":\"read_file\",\"arguments\":\"\"}}\n\n"
                    "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"fc_policy\",\"delta\":\"{\\\"path\\\":\\\"note.txt\\\"}\"}\n\n"
                    "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_policy\",\"type\":\"function_call\","
                    "\"call_id\":\"call_policy\",\"name\":\"read_file\",\"arguments\":\"{\\\"path\\\":\\\"note.txt\\\"}\"}}\n\n"
                    "data: {\"type\":\"response.completed\",\"response\":{}}\n\n")});
  auto append_target = *target;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .append_entry = [append_target](ava::session::SessionEntry const& entry) { return append_target->append(entry); },
      .append_batch = [append_target](std::vector<ava::session::SessionEntry> entries) { return append_target->append_batch(std::move(entries)); },
      .session_read_authority = std::move(*authority),
  });
  auto result = loop.run_turn("read note", *store, provider, transport);
  expect(!result && result.error().message().find("entry count") != std::string::npos && transport.requests().size() == 1,
         "the policy bound to the read authority rejects grown history before the second provider iteration despite AgentLoop's default explicit limits");
}

void test_agent_loop_model_capability_gating()
{
  auto const root = create_empty_root("agent-capabilities");

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
      .append_batch = append_batch_route_for_test(store),
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
  auto const root = create_empty_root("agent-missing-append-route");

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
  expect(!result && result.error().message().find("authority routes") != std::string::npos && transport.requests().empty() &&
             !std::filesystem::exists(store.session_path()),
         "persistent AgentLoop without a bound append route fails before provider work or session mutation");
}

void test_agent_loop_rejects_replaced_history_before_provider_use()
{
  auto const root = create_empty_root("agent-history-path-replacement");

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
      .append_batch = append_batch_route_for_test(store),
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
  auto const root = create_empty_root("agent-image-load-failure");

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
      .append_batch = append_batch_route_for_test(store),
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
  auto const root = create_empty_root("agent-usage");

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
                                                                .append_batch = append_batch_route_for_test(exact_store),
                                                                .session_read_authority = read_authority_for_test(exact_store),
                                                                .model_pricing = pricing});
  auto exact_result = exact_loop.run_turn("hi", exact_store, provider, exact_transport);
  expect(exact_result && exact_result->usage && !exact_result->usage->estimated && exact_result->cost_usd && *exact_result->cost_usd > 0.049L &&
             *exact_result->cost_usd < 0.051L,
         "agent loop calculates cost from provider usage when pricing is known");
  auto exact_entries = exact_store.load();
  auto const exact_projection = exact_entries ? ava::session::classify_assistant_output(*exact_entries) : ava::session::AssistantOutputProjection{};
  expect(exact_entries && exact_projection.turns.size() == 1 && exact_projection.turns.front().commit.usage_json &&
             exact_projection.turns.front().commit.usage_json->find("\"source\":\"provider\"") != std::string::npos &&
             exact_projection.turns.front().commit.usage_json->find("\"cost_usd\":0.05") != std::string::npos,
         "agent loop persists exact provider usage and known cost on the committed v4 assistant turn");

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
      .append_batch = append_batch_route_for_test(unknown_price_store),
      .session_read_authority = read_authority_for_test(unknown_price_store),
  });
  auto unknown_price_result = unknown_price_loop.run_turn("hi", unknown_price_store, provider, unknown_price_transport);
  auto unknown_price_entries = unknown_price_store.load();
  auto const unknown_price_projection =
      unknown_price_entries ? ava::session::classify_assistant_output(*unknown_price_entries) : ava::session::AssistantOutputProjection{};
  expect(unknown_price_result && unknown_price_projection.turns.size() == 1 && unknown_price_projection.turns.front().commit.usage_json &&
             unknown_price_projection.turns.front().commit.usage_json->find("cost_usd") == std::string::npos,
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
                                                                    .append_batch = append_batch_route_for_test(estimated_store),
                                                                    .session_read_authority = read_authority_for_test(estimated_store),
                                                                    .model_pricing = pricing});
  auto estimated_result = estimated_loop.run_turn("hi", estimated_store, provider, estimated_transport);
  auto estimated_entries = estimated_store.load();
  auto const estimated_projection = estimated_entries ? ava::session::classify_assistant_output(*estimated_entries) : ava::session::AssistantOutputProjection{};
  auto const estimated_usage = estimated_projection.turns.empty() ? std::optional<std::string>{} : estimated_projection.turns.front().commit.usage_json;
  expect(estimated_result && estimated_result->usage && estimated_result->usage->estimated && estimated_usage &&
             estimated_usage->find("\"source\":\"estimated\"") != std::string::npos &&
             estimated_usage->find("\"estimation_method\":\"byte_count\"") != std::string::npos &&
             estimated_usage->find("\"estimated_input_bytes\":") != std::string::npos && estimated_usage->find("cost_usd") == std::string::npos,
         "agent loop estimates byte usage without persisting fake cost when provider usage is unavailable");
}

void test_agent_loop_tool_turn_and_continuation()
{
  auto const root = create_empty_root("agent-tool");

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
                                                          .append_batch = append_batch_route_for_test(store),
                                                          .session_read_authority = read_authority_for_test(store),
                                                          .model_pricing = pricing,
                                                          .api_family = "openai_responses",
                                                          .reasoning_format = "openai_responses"});
  auto result = loop.run_turn("read note", store, provider, transport);
  expect(result && result->final_text == "read it" && result->tool_calls == 1 && result->provider_iterations == 2 && result->initial_context_messages == 1 &&
             result->tool_iterations == 1 && result->outcome == ava::core::RuntimeTerminalOutcome::Completed,
         "agent loop runs one sequential tool call then continues to final answer with status metadata");
  expect(transport.requests().size() == 2 && transport.requests()[1].body.find("tool content") != std::string::npos &&
             transport.requests()[1].body.find(private_reasoning_item_json) != std::string::npos &&
             transport.requests()[1].body.find(R"({"type":"function_call","id":"fc_1","call_id":"call_1","name":"read_file",)") != std::string::npos &&
             transport.requests()[1].body.find(R"({"type":"function_call_output","call_id":"call_1",)") != std::string::npos &&
             transport.requests()[1].body.find(private_reasoning_item_json) <
                 transport.requests()[1].body.find(R"({"type":"function_call","id":"fc_1","call_id":"call_1",)") &&
             transport.requests()[1].body.find(R"({"type":"function_call","id":"fc_1","call_id":"call_1",)") <
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
  auto const projection = ava::session::classify_assistant_output(*entries);
  bool saw_bound_function = false;
  bool saw_private_reasoning_item = false;
  bool saw_final_text = false;
  bool saw_bound_tool_result = false;
  for (auto const& turn : projection.turns)
  {
    for (auto const& item : turn.items)
    {
      if (auto const* function = std::get_if<ava::session::AssistantOutputFunctionCall>(&item.item.payload))
        saw_bound_function = saw_bound_function || (function->call_id == "call_1" && item.item.provider_item_id == std::optional<std::string>{"fc_1"});
      if (auto const* reasoning = std::get_if<ava::session::AssistantOutputReasoning>(&item.item.payload))
        saw_private_reasoning_item =
            saw_private_reasoning_item ||
            (reasoning->native_item_json && reasoning->native_item_json->find("\"encrypted_content\":\"ciphertext-tool\"") != std::string::npos);
      if (auto const* text = std::get_if<ava::session::AssistantOutputText>(&item.item.payload))
        saw_final_text = saw_final_text || text->text == "read it";
    }
  }
  for (auto const& entry : *entries)
  {
    saw_bound_tool_result = saw_bound_tool_result ||
                            (entry.type == ava::session::EntryType::ToolResult && entry.data_json.find("\"assistant_output_entry_id\":") != std::string::npos &&
                             entry.data_json.find("\"structured_result\":{\"schema_version\":1") != std::string::npos);
  }
  expect(saw_bound_function && saw_bound_tool_result && saw_private_reasoning_item && saw_final_text,
         "agent loop persists private reasoning, exact function identity, bound result, and final text in committed v4 turns");
}

void test_agent_loop_task_subagent_runs_child_session()
{
  auto const root = create_empty_root("agent-task-subagent");
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
                                                          .append_batch = append_batch_route_for_test(store),
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
    expect(transport.requests()[1].body.find("\"name\":\"task\"") == std::string::npos &&
               transport.requests()[1].body.find("\"name\":\"job\"") == std::string::npos,
           "child provider request hides recursive task and job tool access");
    expect(transport.requests()[2].body.find("child result") != std::string::npos, "parent continuation receives child task result context");
  }

  auto entries = store.load();
  expect(entries.has_value(), "task parent session loads");
  bool saw_task_call = false;
  bool saw_task_result = false;
  bool saw_task_permission = false;
  if (entries)
  {
    auto const projection = ava::session::classify_assistant_output(*entries);
    for (auto const& turn : projection.turns)
    {
      for (auto const& item : turn.items)
      {
        if (auto const* function = std::get_if<ava::session::AssistantOutputFunctionCall>(&item.item.payload))
          saw_task_call = saw_task_call || function->name == "task";
      }
    }
    for (auto const& entry : *entries)
    {
      saw_task_result =
          saw_task_result ||
          (entry.type == ava::session::EntryType::ToolResult && entry.data_json.find("\\\"tool\\\":\\\"task\\\"") != std::string::npos &&
           entry.data_json.find("child result") != std::string::npos && entry.data_json.find("\"assistant_output_entry_id\":") != std::string::npos);
      saw_task_permission = saw_task_permission ||
                            (entry.type == ava::session::EntryType::PermissionDecision && entry.data_json.find("\"operation\":\"task\"") != std::string::npos &&
                             entry.data_json.find("\"resolution\":\"allow\"") != std::string::npos);
    }
  }
  expect(saw_task_call && saw_task_result && saw_task_permission,
         "task parent session persists committed task function, bound result, and permission decision");
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
        if (entry.type == ava::session::EntryType::AssistantTurnCommit)
        {
          auto const projection = ava::session::classify_assistant_output(*child_entries);
          for (auto const& turn : projection.turns)
            for (auto const& item : turn.items)
              if (auto const* text = std::get_if<ava::session::AssistantOutputText>(&item.item.payload))
                saw_child_answer = saw_child_answer || text->text == "child result";
        }
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

void test_agent_loop_child_rejects_unadvertised_task_and_job_calls()
{
  auto const root = temp_root() / "agent-child-rejects-job-controls";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "simulated-child"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(tool_call_sse("call_nested_task", "task", R"({"description":"nested","prompt":"must not run","subagent_type":"general"})") +
                    "data: [DONE]\n\n"),
       sse_response(tool_call_sse("call_nested_job", "job", R"({"action":"list"})") + "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"child controls rejected\"}\n\ndata: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "child system prompt",
      .access_token = "token",
      .tool_visibility = {.excluded_tools = {"task", "job"}},
      .permission_resolver = [](auto const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
      .trace_context =
          {.run_id = {}, .turn_id = {}, .session_id = {}, .provider_id = {}, .parent_run_id = {}, .parent_turn_id = {}, .parent_session_id = "parent-session"},
  });
  auto result = loop.run_turn("malicious child tool calls", store, provider, transport);
  auto const requests = transport.requests();
  bool const schemas_hidden = requests.size() == 3 && requests.front().body.find("\"name\":\"task\"") == std::string::npos &&
                              requests.front().body.find("\"name\":\"job\"") == std::string::npos;
  expect(result && result->final_text == "child controls rejected" && result->tool_calls == 2 && requests.size() == 3,
         "child malicious task/job calls return bounded tool errors and the child continues");
  expect(requests.size() == 3 && requests[1].body.find("unknown tool") != std::string::npos, "child malicious task call cannot start a recursive subagent");
  expect(requests.size() == 3 && requests[2].body.find("unknown tool") != std::string::npos, "child malicious job call cannot reach a coordinator");
  expect(schemas_hidden, "child provider schema hides both task and job controls");
}

void test_agent_loop_coordinated_foreground_uses_fresh_worker_and_preserves_result_accounting()
{
  auto const root = temp_root() / "agent-task-coordinated-foreground";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  std::filesystem::permissions(root, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "parent-coordinated"});
  auto coordinator_result = ava::agent::SubagentCoordinator::create({.ava_state_dir = root / "state"});
  expect(coordinator_result.has_value(), coordinator_result ? "coordinated foreground fixture creates coordinator"
                                                            : "coordinated foreground fixture creates coordinator: " + coordinator_result.error().format());
  if (!coordinator_result)
    return;
  auto coordinator = *coordinator_result;
  ava::provider::OpenAIProvider const parent_provider("https://api.example.test");
  ava::tests::FakeTransport parent_transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                           "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                           "\\\"description\\\":\\\"Fresh child\\\",\\\"prompt\\\":\\\"Return fresh child result.\\\","
                                                           "\\\"subagent_type\\\":\\\"general\\\"}\"}\n\n"
                                                           "data: [DONE]\n\n"),
                                              sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"parent received fresh child\"}\n\n"
                                                           "data: [DONE]\n\n")});
  auto const full_child_summary = std::string(17U * 1024U, 'x') + "FULL_FOREGROUND_TAIL";
  auto child_responses = std::make_shared<std::vector<ava::provider::HttpResponse>>(std::initializer_list<ava::provider::HttpResponse>{
      sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"" + full_child_summary + "\"}\n\ndata: [DONE]\n\n")});
  auto child_requests = std::make_shared<std::vector<ava::provider::HttpRequest>>();
  auto child_mutex = std::make_shared<std::mutex>();
  auto resume_state = std::make_shared<BlockingBackgroundTransport::State>();
  auto provider_creations = std::make_shared<std::atomic<unsigned>>(0);
  auto transport_creations = std::make_shared<std::atomic<unsigned>>(0);
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
      .background_provider_factory = [provider_creations]() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        provider_creations->fetch_add(1, std::memory_order_relaxed);
        std::unique_ptr<ava::provider::Provider> provider = std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
        return provider;
      },
      .background_transport_factory = [child_responses, child_requests, child_mutex, resume_state,
                                       transport_creations]() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
        auto const creation = transport_creations->fetch_add(1, std::memory_order_relaxed) + 1;
        if (creation == 1)
        {
          std::unique_ptr<ava::provider::Transport> transport = std::make_unique<SharedFakeTransport>(child_responses, child_requests, child_mutex);
          return transport;
        }
        std::unique_ptr<ava::provider::Transport> transport = std::make_unique<BlockingBackgroundTransport>(
            resume_state, sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"resumed child result\"}\n\n"
                                       "data: [DONE]\n\n"));
        return transport;
      },
      .subagent_coordinator = coordinator,
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });

  auto result = loop.run_turn("delegate through coordinator", store, parent_provider, parent_transport);
  auto jobs = coordinator->list(store.session_id());
  expect(result && result->final_text == "parent received fresh child" && parent_transport.requests().size() == 2 && jobs.size() == 1,
         "coordinated foreground child returns synchronously without using the parent transport");
  expect(provider_creations->load(std::memory_order_relaxed) == 1 && transport_creations->load(std::memory_order_relaxed) == 1 && child_requests->size() == 1,
         "coordinated foreground owns exactly one fresh provider and transport worker");
  if (child_requests->size() == 1)
    expect(child_requests->front().body.find("\"name\":\"task\"") == std::string::npos &&
               child_requests->front().body.find("\"name\":\"job\"") == std::string::npos,
           "coordinated child provider request cannot expose recursive task/job execution");
  expect(jobs.size() == 1 && jobs.front().job.execution == ava::agent::SubagentExecutionState::Completed &&
             jobs.front().job.delivery == ava::agent::SubagentDeliveryState::Direct && jobs.front().job.summary &&
             jobs.front().job.summary->size() == 16U * 1024U && jobs.front().job.summary_truncated && jobs.front().job.provider_iterations == 1 &&
             jobs.front().job.tool_calls == 0 && jobs.front().job.tool_iterations == 0,
         "coordinated foreground durably bounds summary text while preserving direct accounting metadata");
  auto parent_entries = store.load();
  bool const persisted_full_result =
      parent_entries && std::ranges::any_of(*parent_entries, [](ava::session::SessionEntry const& entry) {
        return entry.type == ava::session::EntryType::ToolResult && entry.data_json.find("FULL_FOREGROUND_TAIL") != std::string::npos;
      });
  expect(persisted_full_result && parent_transport.requests().size() == 2 &&
             parent_transport.requests()[1].body.find("\\\"provider_iterations\\\":1") != std::string::npos,
         "foreground task result preserves the exact untruncated final text and accounting before normal provider-context limiting");

  if (jobs.empty())
    return;
  auto const task_id = jobs.front().job.identity.task_id;
  ava::tests::FakeTransport resume_parent_transport(
      {sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_resume\",\"name\":\"task\"}\n\n"
                    "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_resume\",\"delta\":\"{"
                    "\\\"description\\\":\\\"Resume fresh child\\\",\\\"prompt\\\":\\\"Continue and wait.\\\","
                    "\\\"subagent_type\\\":\\\"general\\\",\\\"task_id\\\":\\\"" +
                    task_id +
                    "\\\"}\"}\n\n"
                    "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"parent received resumed promotion\"}\n\n"
                    "data: [DONE]\n\n")});
  auto resumed_parent =
      std::async(std::launch::async, [&] { return loop.run_turn("resume coordinated child", store, parent_provider, resume_parent_transport); });
  expect(resume_state->wait_for_request(std::chrono::milliseconds(1000)), "completed child session starts a later foreground worker with the same task_id");
  auto resumed_jobs = coordinator->list(store.session_id());
  auto resumed_job =
      std::ranges::find_if(resumed_jobs, [](auto const& snapshot) { return snapshot.job.execution == ava::agent::SubagentExecutionState::Running; });
  expect(resumed_jobs.size() == 2 && resumed_job != resumed_jobs.end() && resumed_job->job.identity.task_id == task_id &&
             resumed_job->job.identity.child_session_id == jobs.front().job.identity.child_session_id &&
             resumed_job->job.identity.job_id != jobs.front().job.identity.job_id &&
             resumed_job->job.identity.delivery_id != jobs.front().job.identity.delivery_id,
         "foreground resume reuses child identity sequentially with fresh job and delivery identities");
  if (resumed_job == resumed_jobs.end())
  {
    resume_state->release_success();
    static_cast<void>(resumed_parent.get());
    return;
  }
  auto const resumed_job_id = resumed_job->job.identity.job_id;
  auto promoted_resume = coordinator->promote(store.session_id(), resumed_job_id);
  bool const resumed_parent_woke = resumed_parent.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
  if (!resumed_parent_woke)
    resume_state->release_success();
  auto resumed_result = resumed_parent.get();
  expect(promoted_resume && promoted_resume->job.was_promoted && resumed_parent_woke && resumed_result &&
             resumed_result->final_text == "parent received resumed promotion",
         "a resumed foreground run can be promoted and wakes the parent without restarting");
  resume_state->release_success();
  auto resumed_terminal = coordinator->wait(store.session_id(), resumed_job_id, std::chrono::seconds(1));
  expect(resumed_terminal && resumed_terminal->job.execution == ava::agent::SubagentExecutionState::Completed &&
             resumed_terminal->job.delivery == ava::agent::SubagentDeliveryState::Pending && provider_creations->load(std::memory_order_relaxed) == 2 &&
             transport_creations->load(std::memory_order_relaxed) == 2,
         "promoted resumed worker completes once with pending delivery and fresh provider/transport ownership");
}

void test_agent_loop_foreground_promotion_wakes_parent_without_restarting_child()
{
  auto const root = temp_root() / "agent-task-foreground-promotion";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  std::filesystem::permissions(root, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "parent-promote"});
  auto coordinator_result = ava::agent::SubagentCoordinator::create({.ava_state_dir = root / "state"});
  expect(coordinator_result.has_value(),
         coordinator_result ? "promotion fixture creates coordinator" : "promotion fixture creates coordinator: " + coordinator_result.error().format());
  if (!coordinator_result)
    return;
  auto coordinator = *coordinator_result;
  auto child_state = std::make_shared<BlockingBackgroundTransport::State>();
  auto provider_creations = std::make_shared<std::atomic<unsigned>>(0);
  auto transport_creations = std::make_shared<std::atomic<unsigned>>(0);
  ava::provider::OpenAIProvider const parent_provider("https://api.example.test");
  ava::tests::FakeTransport parent_transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                           "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                           "\\\"description\\\":\\\"Promote child\\\",\\\"prompt\\\":\\\"Wait for promotion.\\\","
                                                           "\\\"subagent_type\\\":\\\"general\\\"}\"}\n\n"
                                                           "data: [DONE]\n\n"),
                                              sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"parent resumed after promotion\"}\n\n"
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
      .background_provider_factory = [provider_creations]() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        provider_creations->fetch_add(1, std::memory_order_relaxed);
        std::unique_ptr<ava::provider::Provider> provider = std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
        return provider;
      },
      .background_transport_factory = [child_state, transport_creations]() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
        transport_creations->fetch_add(1, std::memory_order_relaxed);
        std::unique_ptr<ava::provider::Transport> transport = std::make_unique<BlockingBackgroundTransport>(
            child_state, sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"late child result\"}\n\n"
                                      "data: [DONE]\n\n"));
        return transport;
      },
      .subagent_coordinator = coordinator,
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });

  auto parent = std::async(std::launch::async, [&] { return loop.run_turn("delegate then promote", store, parent_provider, parent_transport); });
  expect(child_state->wait_for_request(std::chrono::milliseconds(1000)), "foreground promotion child reaches its fresh transport");
  auto jobs = coordinator->list(store.session_id());
  expect(jobs.size() == 1, "foreground promotion publishes one stable job before waiting");
  ava::core::Result<ava::agent::SubagentCoordinatorJobSnapshot> promoted =
      std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "missing promotion job"));
  ava::core::Result<ava::app::CommandResult> active_command =
      std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "missing active promotion command"));
  if (!jobs.empty())
  {
    active_command = ava::app::run_jobs_command(coordinator, store.session_id(), "promote " + jobs.front().job.identity.job_id, true);
    promoted = coordinator->snapshot(store.session_id(), jobs.front().job.identity.job_id);
  }
  expect(active_command && !active_command->output.empty() && promoted && promoted->job.was_promoted &&
             promoted->job.execution == ava::agent::SubagentExecutionState::Running,
         "out-of-band active-run /jobs promote durably changes mode while preserving the running worker");
  bool const parent_woke = parent.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
  if (!parent_woke)
    child_state->release_success();
  auto parent_result = parent.get();
  expect(parent_woke && parent_result && parent_result->final_text == "parent resumed after promotion",
         "live foreground /jobs promote wakes the parent tool call without a restart or modal boundary");
  expect(provider_creations->load(std::memory_order_relaxed) == 1 && transport_creations->load(std::memory_order_relaxed) == 1,
         "promotion never restarts or replaces the child worker");
  if (parent_transport.requests().size() == 2 && promoted)
    expect(parent_transport.requests()[1].body.find(promoted->job.identity.job_id) != std::string::npos &&
               parent_transport.requests()[1].body.find("promoted") != std::string::npos,
           "promoted task result returns the same running job identity to the parent");
  child_state->release_success();
  if (promoted)
  {
    auto completed = coordinator->wait(store.session_id(), promoted->job.identity.job_id, std::chrono::seconds(1));
    expect(completed && completed->job.execution == ava::agent::SubagentExecutionState::Completed &&
               completed->job.delivery == ava::agent::SubagentDeliveryState::Pending && completed->job.summary == "late child result",
           "promoted worker continues unchanged and atomically records pending delivery at completion");
  }
}

void test_agent_loop_promoted_failure_persists_sanitized_child_error()
{
  auto const root = temp_root() / "agent-task-promoted-failure";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  std::filesystem::permissions(root, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
  auto const session_root = root / "sessions";
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent-promoted-failure"});
  auto coordinator_result = ava::agent::SubagentCoordinator::create({.ava_state_dir = root / "state"});
  if (!coordinator_result)
  {
    expect(false, "promoted failure fixture creates coordinator");
    return;
  }
  auto coordinator = *coordinator_result;
  auto child_state = std::make_shared<BlockingBackgroundTransport::State>();
  ava::provider::OpenAIProvider const parent_provider("https://api.example.test");
  ava::tests::FakeTransport parent_transport(
      {sse_response(tool_call_sse("call_promoted_failure", "task",
                                  R"({"description":"Promoted failure","prompt":"Fail after promotion.","subagent_type":"general","mode":"foreground"})") +
                    "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"parent saw promotion\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [](auto const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .background_provider_factory = []() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        std::unique_ptr<ava::provider::Provider> provider = std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
        return provider;
      },
      .background_transport_factory = [child_state]() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
        auto secret_body = std::string("{\"error\":{\"message\":\"credential=promoted-secret command=curl --token promoted-secret\"}}");
        std::unique_ptr<ava::provider::Transport> transport = std::make_unique<BlockingBackgroundTransport>(
            child_state, ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = std::move(secret_body)});
        return transport;
      },
      .subagent_coordinator = coordinator,
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });

  auto parent = std::async(std::launch::async, [&] { return loop.run_turn("delegate promoted failure", store, parent_provider, parent_transport); });
  expect(child_state->wait_for_request(std::chrono::milliseconds(1000)), "promoted failure child reaches transport");
  auto jobs = coordinator->list(store.session_id());
  if (jobs.empty())
  {
    child_state->release_success();
    static_cast<void>(parent.get());
    expect(false, "promoted failure publishes job");
    return;
  }
  auto promoted = coordinator->promote(store.session_id(), jobs.front().job.identity.job_id);
  expect(promoted && promoted->job.was_promoted, "promoted failure switches the running foreground job to background delivery");
  auto parent_result = parent.get();
  expect(parent_result && parent_result->final_text == "parent saw promotion", "promoted failure releases the parent before child terminal failure");
  child_state->release_success();
  auto failed = coordinator->wait(store.session_id(), jobs.front().job.identity.job_id, std::chrono::seconds(1));
  auto child_store = ava::session::SessionStore::open(workspace, jobs.front().job.identity.child_session_id, session_root);
  auto child_entries = child_store ? child_store->load()
                                   : ava::core::Result<std::vector<ava::session::SessionEntry>>(
                                         std::unexpected(ava::core::Error(ava::core::ErrorCategory::NotFound, "child unavailable")));
  bool const safe_error = child_entries && std::ranges::any_of(*child_entries, [](auto const& entry) {
                            return entry.type == ava::session::EntryType::Error && entry.data_json.find("subagent job failed") != std::string::npos &&
                                   entry.data_json.find("promoted-secret") == std::string::npos && entry.data_json.find("curl --token") == std::string::npos;
                          });
  expect(failed && failed->job.execution == ava::agent::SubagentExecutionState::Failed && failed->job.delivery == ava::agent::SubagentDeliveryState::Pending &&
             failed->job.error == "subagent job failed" && safe_error,
         "a promoted child failure persists one sanitized bounded child error and safe coordinator result");
}

void test_agent_loop_task_subagent_propagates_authority_roots_to_foreground_and_background_children()
{
  auto run_case = [](bool background) {
    auto const root = create_empty_root(background ? "agent-task-authority-background" : "agent-task-authority-foreground");
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
    ava::tests::FakeTransport transport(
        background ? std::vector<ava::provider::HttpResponse>{sse_response(tool_call_sse("call_task", "task", task_arguments) + "data: [DONE]\n\n"),
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
      background_responses = std::make_shared<std::vector<ava::provider::HttpResponse>>(std::vector<ava::provider::HttpResponse>{
          sse_response(child_bash), sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"child denied\"}\n\n"
                                                 "data: [DONE]\n\n")});
      background_requests = std::make_shared<std::vector<ava::provider::HttpRequest>>();
      background_mutex = std::make_shared<std::mutex>();
    }

    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
        .workspace_dir = workspace,
        .anchor_set = command_anchors_for_test(workspace, store.session_path().parent_path() / "spill"),
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .ava_authority_roots = {workspace},
        .permission_resolver =
            [&task_prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          ++task_prompts;
          expect(prompt.operation == ava::permissions::Operation::TaskRun, "authority-root child only prompts to authorize its parent task");
          return ava::permissions::PermissionResolution::Allow;
        },
        .background_provider_factory = background ? []() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
          std::unique_ptr<ava::provider::Provider> child = std::make_unique<ava::provider::OpenAIProvider>("https://api.example.test");
          return child;
        }
        : decltype(ava::agent::AgentLoopOptions{}.background_provider_factory){},
        .background_transport_factory =
            background ? [background_responses, background_requests, background_mutex]() -> ava::core::Result<std::unique_ptr<ava::provider::Transport>> {
          std::unique_ptr<ava::provider::Transport> child = std::make_unique<SharedFakeTransport>(background_responses, background_requests, background_mutex);
          return child;
        }
        : decltype(ava::agent::AgentLoopOptions{}.background_transport_factory){},
        .background_jobs = registry,
        .append_entry = append_route_for_test(store),
        .append_batch = append_batch_route_for_test(store),
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
      process_started = std::ranges::any_of(
          collector->events, [](ava::observability::TraceEvent const& event) { return event.type == ava::observability::TraceEventType::ProcessStart; });
    }
    auto const child_error_propagated =
        background ? child_requests.size() == 2 && child_requests[1].body.find("must not overlap with any AVA authority root") != std::string::npos
                   : transport.requests().size() == 4 && transport.requests()[2].body.find("must not overlap with any AVA authority root") != std::string::npos;
    expect(result && task_prompts == 1 && child_completed && child_error_propagated && !process_started,
           background ? "background child copies AVA authority roots before its AgentLoop starts and blocks overlapping model commands"
                      : "foreground child copies AVA authority roots before its AgentLoop starts and blocks overlapping model commands");
  };

  run_case(false);
  run_case(true);
}

void test_agent_loop_task_subagent_recovers_torn_child_before_resume()
{
  auto const root = create_empty_root("agent-task-subagent-torn-resume");

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
      .append_batch = append_batch_route_for_test(parent),
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
  auto const root = create_empty_root("subagent-config");

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
  auto const root = create_empty_root("agent-task-custom-subagent");

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
      .append_batch = append_batch_route_for_test(store),
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
  auto const root = create_empty_root("agent-task-background");

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
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
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
        .append_batch = append_batch_route_for_test(competing_parent),
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
      if (child_entries)
      {
        auto const projection = ava::session::classify_assistant_output(*child_entries);
        for (auto const& turn : projection.turns)
          for (auto const& item : turn.items)
            if (auto const* text = std::get_if<ava::session::AssistantOutputText>(&item.item.payload))
              saw_background_answer = saw_background_answer || text->text == "background child";
      }
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
  auto const root = create_empty_root("agent-task-background-failure");

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
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
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
                          return entry.type == ava::session::EntryType::Error && entry.data_json.find("subagent job failed") != std::string::npos &&
                                 entry.data_json.find("fake transport has no response") == std::string::npos;
                        });
    }
  }
  expect(saw_background_request, "background failure test reaches the child provider transport");
  expect(!saw_parent_error, "background task failures are not appended directly into the parent session");
  expect(saw_child_error, "background task failures are persisted in the child session with bounded sanitized content");
}

void test_agent_loop_background_task_cancel_requests_child_cancellation()
{
  auto const root = create_empty_root("agent-task-background-cancel");

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
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
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
  auto const root = create_empty_root("agent-task-background-no-registry");

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
      .append_batch = append_batch_route_for_test(store),
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

void test_agent_loop_coordinator_start_journal_failure_rolls_back_child()
{
  auto const root = temp_root() / "agent-task-background-journal-failure";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  ava::agent::SubagentCoordinatorOptions coordinator_options;
  coordinator_options.ava_state_dir = root / "state";
  coordinator_options.journal_append_preflight = [](ava::agent::JobJournalRecord const& record) -> ava::core::VoidResult {
    if (record.kind == ava::agent::JobJournalTransitionKind::Started)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "injected started journal failure"));
    return {};
  };
  auto coordinator = ava::agent::SubagentCoordinator::create(std::move(coordinator_options));
  expect(coordinator.has_value(), "journal-failure rollback fixture creates coordinator");
  if (!coordinator)
    return;
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto const session_root = root / "sessions";
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = session_root, .workspace_dir = workspace, .session_id = "parent-journal-failure"});
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_task\",\"name\":\"task\"}\n\n"
                                                    "data: "
                                                    "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_task\",\"delta\":\"{"
                                                    "\\\"description\\\":\\\"Reject start\\\",\\\"prompt\\\":\\\"Never run.\\\","
                                                    "\\\"subagent_type\\\":\\\"general\\\",\\\"background\\\":true}\"}\n\n"
                                                    "data: [DONE]\n\n"),
                                       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"start rejected\"}\n\n"
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
      .subagent_coordinator = *coordinator,
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });

  auto result = loop.run_turn("delegate rejected background", store, provider, transport);
  expect(result && result->final_text == "start rejected" && transport.requests().size() == 2,
         "parent continues after coordinator rejects background publication");
  bool saw_failure = transport.requests().size() == 2 && transport.requests()[1].body.find("injected started journal failure") != std::string::npos;
  std::size_t session_files = 0;
  if (std::filesystem::exists(session_root))
    for (auto const& entry : std::filesystem::recursive_directory_iterator(session_root))
      session_files += entry.is_regular_file() && entry.path().extension() == ".jsonl";
  expect(saw_failure, "coordinator start failure is returned to the parent tool continuation");
  expect(session_files == 1, "journal failure rolls back the newly created child session file");
  expect((*coordinator)->list("parent-journal-failure").empty(), "journal failure prevents live worker publication");
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
  auto const root = create_empty_root("agent-permission-resolver");

  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  expect(::chmod(root.c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0,
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
      .append_batch = append_batch_route_for_test(store),
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
    auto const bash_root = create_empty_root("agent-bash-ask-allow");

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
        .anchor_set = command_anchors_for_test(bash_workspace, bash_store.session_path().parent_path() / "spill"),
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
        .append_batch = append_batch_route_for_test(bash_store),
        .session_read_authority = read_authority_for_test(bash_store),
    });
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash allowed" && bash_allow_prompts == 1 && bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success,
           "agent loop allows bash Ask decisions when resolver allows once");
  }

  {
    auto const bash_root = create_empty_root("agent-bash-ask-deny");

    auto const bash_workspace = bash_root / "workspace";
    std::filesystem::create_directories(bash_workspace);
    expect(::chmod(bash_root.c_str(), S_IRWXU) == 0 && ::chmod(bash_workspace.c_str(), S_IRWXU) == 0,
           "agent bash deny workspace is owner-only for sealed planning");
    ava::session::SessionStore bash_store(
        ava::session::SessionStoreOptions{.root_dir = bash_root / "sessions", .workspace_dir = bash_workspace, .session_id = "bash-deny"});
    ava::tests::FakeTransport bash_transport({sse_response("data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
                                                           "data: "
                                                           "{\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_bash\",\"delta\":\"{"
                                                           "\\\"command\\\":\\\"printf AGENT_DENIED_COMMAND_SECRET_SENTINEL\\\"}\"}\n\n"
                                                           "data: [DONE]\n\n"),
                                              sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"bash denied\"}\n\n"
                                                           "data: [DONE]\n\n")});
    int bash_deny_prompts = 0;
    std::vector<ava::provider::StreamEvent> public_bash_stream_events;
    ava::agent::AgentLoop bash_loop(ava::agent::AgentLoopOptions{
        .workspace_dir = bash_workspace,
        .anchor_set = command_anchors_for_test(bash_workspace, bash_store.session_path().parent_path() / "spill"),
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .on_stream_event = [&public_bash_stream_events](ava::provider::StreamEvent const& event) -> ava::core::VoidResult {
          public_bash_stream_events.push_back(event);
          return {};
        },
        .permission_resolver =
            [&bash_deny_prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
          ++bash_deny_prompts;
          expect(prompt.operation == ava::permissions::Operation::RunCommand, "agent bash deny resolver sees run command");
          expect(prompt.command.find("AGENT_DENIED_COMMAND_SECRET_SENTINEL") != std::string::npos,
                 "agent bash deny resolver retains the exact command only in its local prompt");
          return ava::permissions::PermissionResolution::Deny;
        },
        .append_entry = append_route_for_test(bash_store),
        .append_batch = append_batch_route_for_test(bash_store),
        .session_read_authority = read_authority_for_test(bash_store),
    });
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash denied" && bash_deny_prompts == 1 && bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error,
           "agent loop records denied bash Ask decisions as failed tool results and continues");
    auto bash_entries = bash_store.load();
    auto bash_audits = bash_entries ? permission_entries(*bash_entries) : std::vector<ava::session::SessionEntry>{};
    auto const serialized_denial =
        bash_result && !bash_result->tool_timeline.empty() ? bash_result->tool_timeline.front().structured_result_json : std::string{};
    auto const continuation_request = bash_transport.requests().size() > 1 ? bash_transport.requests()[1].body : std::string{};
    auto const durable_secret_absent = bash_entries && std::ranges::all_of(*bash_entries, [](ava::session::SessionEntry const& entry) {
                                         return entry.data_json.find("AGENT_DENIED_COMMAND_SECRET_SENTINEL") == std::string::npos;
                                       });
    auto const public_stream_secret_absent = std::ranges::all_of(public_bash_stream_events, [](ava::provider::StreamEvent const& event) {
      return event.text.find("AGENT_DENIED_COMMAND_SECRET_SENTINEL") == std::string::npos;
    });
    expect(bash_audits.size() == 2 && ava::core::json::string_field(bash_audits[1].data_json, "command") == "<redacted one-shot command>" &&
               ava::core::json::string_field(bash_audits[1].data_json, "resolution") == "deny" &&
               ava::core::json::string_field(bash_audits[1].data_json, "resolution_source") == "resolver" && durable_secret_absent &&
               public_stream_secret_absent && serialized_denial.find("AGENT_DENIED_COMMAND_SECRET_SENTINEL") == std::string::npos &&
               continuation_request.find("AGENT_DENIED_COMMAND_SECRET_SENTINEL") == std::string::npos,
           "agent loop redacts denied command arguments from durable audits, public stream events, serialized tool errors, and continuation payloads");
  }

  {
    auto const bash_root = create_empty_root("agent-bash-ask-fail");

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
        .anchor_set = command_anchors_for_test(bash_workspace, bash_store.session_path().parent_path() / "spill"),
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
        .append_batch = append_batch_route_for_test(bash_store),
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
  auto const root = create_empty_root("agent-question-resolver");

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
      .append_batch = append_batch_route_for_test(store),
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
  auto const root = create_empty_root("agent-non-stream");

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
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("hi", store, provider, transport);
  expect(result && result->final_text == "plain response with data: literal", "agent loop parses non-stream response without sniffing data text");
  expect(!transport.requests().empty() && transport.requests()[0].body.find("\"stream\":false") != std::string::npos,
         "agent loop passes explicit non-stream request expectation");
}

void test_agent_loop_non_stream_error_prevents_tool_dispatch()
{
  auto const root = create_empty_root("agent-nonstream-provider-error");
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
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("read", store, provider, transport);
  auto entries = store.load();
  expect(!result && result.error().category() == ava::core::ErrorCategory::Provider && entries &&
             std::none_of(entries->begin(), entries->end(),
                          [](ava::session::SessionEntry const& entry) { return entry.type == ava::session::EntryType::ToolCall; }),
         "a non-stream OpenAI parser Error prevents dispatch of an earlier otherwise valid tool call");
}

void test_stream_bridge_redacts_untrusted_provider_error_event()
{
  std::vector<ava::provider::StreamEvent> published;
  ava::agent::AgentLoopOptions options;
  options.on_stream_event = [&published](ava::provider::StreamEvent const& event) -> ava::core::VoidResult {
    published.push_back(event);
    return {};
  };
  ava::provider::StreamEvent untrusted_error;
  untrusted_error.type = ava::provider::StreamEventType::Error;
  untrusted_error.error_message = "STREAM_BRIDGE_PROVIDER_CANARY";
  auto result = ava::agent::publish_stream_event(options, untrusted_error);
  auto parsed = ava::agent::parse_assistant_turn({untrusted_error}, {});
  expect(result && published.size() == 1 && published[0].error_message == "Provider streaming error" &&
             published[0].error_message.find("STREAM_BRIDGE_PROVIDER_CANARY") == std::string::npos && !parsed &&
             parsed.error().message() == "provider stream error" && parsed.error().format().find("STREAM_BRIDGE_PROVIDER_CANARY") == std::string::npos,
         "the stream bridge and assistant-turn parser redact arbitrary provider Error payloads before public or session handling");
}

void test_agent_loop_invalid_utf8_function_arguments_prevent_dispatch()
{
  auto const root = create_empty_root("agent-invalid-utf8-function-arguments");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "invalid-utf8-function-arguments"});
  std::string response =
      "{\"output\":[{\"id\":\"fc_invalid_utf8\",\"type\":\"function_call\",\"call_id\":\"call_invalid_utf8\",\"name\":\"read_file\",\"arguments\":\"{"
      "\\\"path\\\":\\\"";
  response.push_back(static_cast<char>(0xFF));
  response += "\\\"}\"}]}";
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = std::move(response)}});
  int permission_resolver_calls = 0;
  int tool_events = 0;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .stream = false,
      .on_tool_event = [&tool_events](ava::agent::ToolTimelineEntry const&) { ++tool_events; },
      .permission_resolver =
          [&permission_resolver_calls](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++permission_resolver_calls;
        return ava::permissions::PermissionResolution::Allow;
      },
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("read", store, provider, transport);
  auto entries = store.load();
  expect(!result && result.error().category() == ava::core::ErrorCategory::Provider && permission_resolver_calls == 0 && tool_events == 0 && entries &&
             std::none_of(entries->begin(), entries->end(),
                          [](ava::session::SessionEntry const& entry) { return entry.type == ava::session::EntryType::ToolCall; }),
         "invalid UTF-8 OpenAI function arguments are rejected before permission evaluation or tool dispatch");
}

void test_agent_loop_stream_unended_documented_function_prevents_dispatch()
{
  auto const root = create_empty_root("agent-stream-unended-documented-function");
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
      .append_batch = append_batch_route_for_test(store),
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
  auto const root = create_empty_root("agent-stream-post-terminal-function");
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
      .append_batch = append_batch_route_for_test(store),
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
  auto const root = create_empty_root("agent-compaction-status");

  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(
      ava::session::SessionStoreOptions{.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "compaction-status"});
  auto appended =
      append_session_entry_for_test(store, ava::session::SessionEntry{.id = "entry_compaction_status",
                                                                      .parent_id = "",
                                                                      .type = ava::session::EntryType::Compaction,
                                                                      .timestamp = ava::session::now_timestamp(),
                                                                      .data_json = "{\"summary\":\"older context\",\"history_projection\":\"portable-v1\"}"});
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
      .append_batch = append_batch_route_for_test(store),
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
  auto const root = create_empty_root("agent-steering-compaction-replay");

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
  auto append_batch = [target = *append_target](std::vector<ava::session::SessionEntry> entries) { return target->append_batch(std::move(entries)); };
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
      .append_batch = append_batch,
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
  auto const root = create_empty_root("agent-overflow-skip-auto");

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
  auto append_batch = [target = *append_target](std::vector<ava::session::SessionEntry> entries) { return target->append_batch(std::move(entries)); };
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
          auto appended =
              store.append(*append_lease, ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                     .parent_id = "",
                                                                     .type = ava::session::EntryType::Compaction,
                                                                     .timestamp = ava::session::now_timestamp(),
                                                                     .data_json = "{\"summary\":\"overflow summary\",\"history_projection\":\"portable-v1\"}"});
          if (!appended)
            return std::unexpected(std::move(appended.error()));
          return true;
        }
        if (trigger == "auto" && overflow_compacted)
        {
          auto appended =
              store.append(*append_lease, ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                     .parent_id = "",
                                                                     .type = ava::session::EntryType::Compaction,
                                                                     .timestamp = ava::session::now_timestamp(),
                                                                     .data_json = "{\"summary\":\"duplicate\",\"history_projection\":\"portable-v1\"}"});
          if (!appended)
            return std::unexpected(std::move(appended.error()));
          return true;
        }
        return false;
      },
      .append_entry = append_route,
      .append_batch = append_batch,
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
  auto const root = create_empty_root("agent-multi-tools");
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
      .append_batch = append_batch_route_for_test(store),
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
  auto const projection = entries ? ava::session::classify_assistant_output(*entries) : ava::session::AssistantOutputProjection{};
  std::vector<std::string> committed_call_ids;
  std::vector<std::string> bound_result_ids;
  std::string final_text;
  for (auto const& turn : projection.turns)
  {
    for (auto const& item : turn.items)
    {
      if (auto const* function = std::get_if<ava::session::AssistantOutputFunctionCall>(&item.item.payload))
        committed_call_ids.push_back(function->call_id);
      if (auto const* text = std::get_if<ava::session::AssistantOutputText>(&item.item.payload))
        final_text = text->text;
    }
  }
  if (entries)
    for (auto const& entry : *entries)
      if (entry.type == ava::session::EntryType::ToolResult && ava::core::json::field_value_start(entry.data_json, "assistant_output_entry_id"))
        bound_result_ids.push_back(ava::core::json::string_field(entry.data_json, "call_id").value_or(""));
  expect(entries && projection.turns.size() == 2 && committed_call_ids == std::vector<std::string>({"call_1", "call_2"}) &&
             bound_result_ids == std::vector<std::string>({"call_1", "call_2"}) && final_text == "done",
         "agent loop commits ordered multi-tool functions, binds results, and stores the continuation as v4 turns");

  auto const denied_root = create_empty_root("agent-denied-continuation");
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
      .append_batch = append_batch_route_for_test(denied_store),
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
  auto const root = create_empty_root("agent-parallel-read-search");

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
                                                          .append_batch = append_batch_route_for_test(store),
                                                          .session_read_authority = read_authority_for_test(store),
                                                          .parallel_read_search_tools = true,
                                                          .parallel_read_search_max_workers = 2,
                                                          .api_family = "openai_responses",
                                                          .reasoning_format = "openai_responses"});
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

  auto const projection = entries ? ava::session::classify_assistant_output(*entries) : ava::session::AssistantOutputProjection{};
  std::vector<std::string> committed_calls;
  std::vector<std::string> bound_results;
  for (auto const& turn : projection.turns)
    for (auto const& item : turn.items)
      if (auto const* function = std::get_if<ava::session::AssistantOutputFunctionCall>(&item.item.payload))
        committed_calls.push_back(function->call_id);
  if (entries)
    for (auto const& entry : *entries)
      if (entry.type == ava::session::EntryType::ToolResult && ava::core::json::field_value_start(entry.data_json, "assistant_output_entry_id"))
        bound_results.push_back(ava::core::json::string_field(entry.data_json, "call_id").value_or(""));
  expect(entries && projection.turns.size() == 2 && committed_calls == std::vector<std::string>({"glob_a", "glob_b"}) &&
             bound_results == std::vector<std::string>({"glob_a", "glob_b"}),
         "parallel read/search commits provider-ordered v4 functions and exact bound results");
  auto const continuation = transport.requests().size() >= 2 ? transport.requests()[1].body : std::string{};
  auto const continuation_a = continuation.find("glob_a");
  auto const continuation_b = continuation.find("glob_b");
  expect(transport.requests().size() == 2 && continuation_a != std::string::npos && continuation_b != std::string::npos && continuation_a < continuation_b,
         "parallel read/search continuation replay keeps provider-order tool results");
}

void test_agent_loop_parallel_read_search_zero_max_workers_clamps_to_one()
{
  auto const root = create_empty_root("agent-parallel-zero-workers");

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
                                                          .append_batch = append_batch_route_for_test(store),
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
  auto const root = create_empty_root("agent-parallel-ask-fallback");

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
        bool saw_committed_function_before_prompt = false;
        if (entries)
        {
          auto const projection = ava::session::classify_assistant_output(*entries);
          for (auto const& turn : projection.turns)
            for (auto const& item : turn.items)
              if (auto const* function = std::get_if<ava::session::AssistantOutputFunctionCall>(&item.item.payload))
                saw_committed_function_before_prompt = saw_committed_function_before_prompt || function->call_id == "outside_read";
        }
        expect(entries && saw_committed_function_before_prompt, "Ask fallback commits the v4 function before invoking the resolver");
        return ava::permissions::PermissionResolution::Allow;
      },
      .append_entry = append_route_for_test(store),
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
      .parallel_read_search_tools = true,
      .parallel_read_search_max_workers = 2});
  auto result = loop.run_turn("read outside and inside", store, provider, transport);
  expect(result && result->final_text == "done" && prompts == 1 && resolver_thread == main_thread,
         "Ask read/search calls stay on the sequential barrier path when parallel opt-in is enabled");

  auto entries = store.load();
  auto const projection = entries ? ava::session::classify_assistant_output(*entries) : ava::session::AssistantOutputProjection{};
  std::vector<std::string> functions;
  std::vector<std::string> bound_results;
  for (auto const& turn : projection.turns)
    for (auto const& item : turn.items)
      if (auto const* function = std::get_if<ava::session::AssistantOutputFunctionCall>(&item.item.payload))
        functions.push_back(function->call_id);
  if (entries)
    for (auto const& entry : *entries)
      if (entry.type == ava::session::EntryType::ToolResult && ava::core::json::field_value_start(entry.data_json, "assistant_output_entry_id"))
        bound_results.push_back(ava::core::json::string_field(entry.data_json, "call_id").value_or(""));
  expect(entries && functions == std::vector<std::string>({"outside_read", "inside_read"}) &&
             bound_results == std::vector<std::string>({"outside_read", "inside_read"}),
         "Ask fallback and later parallel-ready read retain committed function and result order");
}

void test_agent_loop_parallel_read_search_active_cancellation_stops_unstarted_slots()
{
  auto const root = create_empty_root("agent-parallel-active-cancel");

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
      .append_batch = append_batch_route_for_test(store),
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
  auto const projection = entries ? ava::session::classify_assistant_output(*entries) : ava::session::AssistantOutputProjection{};
  std::vector<std::string> committed_calls;
  std::vector<std::string> bound_results;
  bool saw_cancel_boundary = false;
  for (auto const& turn : projection.turns)
    for (auto const& item : turn.items)
      if (auto const* function = std::get_if<ava::session::AssistantOutputFunctionCall>(&item.item.payload))
        committed_calls.push_back(function->call_id);
  if (entries)
    for (auto const& entry : *entries)
    {
      saw_cancel_boundary = saw_cancel_boundary || entry.type == ava::session::EntryType::Cancel;
      if (entry.type == ava::session::EntryType::ToolResult && ava::core::json::field_value_start(entry.data_json, "assistant_output_entry_id"))
        bound_results.push_back(ava::core::json::string_field(entry.data_json, "call_id").value_or(""));
    }
  auto validation = entries ? ava::session::validate_session_replay(*entries) : ava::session::SessionReplayValidation{};
  expect(entries && committed_calls == std::vector<std::string>({"call_1", "call_2", "call_3", "call_4", "call_bash"}) &&
             bound_results == std::vector<std::string>({"call_1", "call_2", "call_3", "call_4", "call_bash"}) && saw_cancel_boundary &&
             transport.requests().size() == 1 && validation.ok(),
         "active parallel cancellation closes every committed binding without dispatching unlaunched slots");
}

void test_agent_loop_parallel_read_search_cancellation_stops_later_barrier()
{
  auto const root = create_empty_root("agent-parallel-cancel");

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
                                                          .append_batch = append_batch_route_for_test(store),
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
  auto const projection = entries ? ava::session::classify_assistant_output(*entries) : ava::session::AssistantOutputProjection{};
  std::vector<std::string> committed_calls;
  std::vector<std::string> bound_results;
  for (auto const& turn : projection.turns)
    for (auto const& item : turn.items)
      if (auto const* function = std::get_if<ava::session::AssistantOutputFunctionCall>(&item.item.payload))
        committed_calls.push_back(function->call_id);
  if (entries)
    for (auto const& entry : *entries)
      if (entry.type == ava::session::EntryType::ToolResult && ava::core::json::field_value_start(entry.data_json, "assistant_output_entry_id"))
        bound_results.push_back(ava::core::json::string_field(entry.data_json, "call_id").value_or(""));
  auto validation = entries ? ava::session::validate_session_replay(*entries) : ava::session::SessionReplayValidation{};
  expect(entries && committed_calls == std::vector<std::string>({"call_1", "call_2", "call_bash"}) &&
             bound_results == std::vector<std::string>({"call_1", "call_2", "call_bash"}) && transport.requests().size() == 1 && validation.ok(),
         "parallel cancellation retains the committed tool turn, closes its later barrier, and never executes it");
}

void test_agent_loop_cancellation_stops_later_sequential_tools()
{
  auto const root = create_empty_root("agent-multi-tools-cancel");

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
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });

  auto result = loop.run_turn("read both but cancel after first", store, provider, transport);
  expect(!result && result.error().message().find("canceled") != std::string::npos, "agent loop reports cancellation after the first sequential tool dispatch");
  expect(tool_events.size() == 2 && tool_events[0].call_id == "call_1" && tool_events[0].status == ava::agent::ToolTimelineStatus::Running &&
             tool_events[1].call_id == "call_1" && tool_events[1].status == ava::agent::ToolTimelineStatus::Success,
         "agent loop does not publish events for later provider tool calls after cancellation");

  auto entries = store.load();
  auto const projection = entries ? ava::session::classify_assistant_output(*entries) : ava::session::AssistantOutputProjection{};
  std::vector<std::string> committed_calls;
  std::vector<std::string> bound_results;
  for (auto const& turn : projection.turns)
    for (auto const& item : turn.items)
      if (auto const* function = std::get_if<ava::session::AssistantOutputFunctionCall>(&item.item.payload))
        committed_calls.push_back(function->call_id);
  if (entries)
    for (auto const& entry : *entries)
      if (entry.type == ava::session::EntryType::ToolResult && ava::core::json::field_value_start(entry.data_json, "assistant_output_entry_id"))
        bound_results.push_back(ava::core::json::string_field(entry.data_json, "call_id").value_or(""));
  auto validation = entries ? ava::session::validate_session_replay(*entries) : ava::session::SessionReplayValidation{};
  expect(entries && committed_calls == std::vector<std::string>({"call_1", "call_2"}) && bound_results == std::vector<std::string>({"call_1", "call_2"}) &&
             validation.ok(),
         "sequential cancellation retains the committed turn, stops dispatch, and closes its later binding");
}

void test_agent_loop_tool_delta_dedupes_and_rejects_empty_tool_ids()
{
  ava::provider::OpenAIProvider const provider("https://api.example.test");

  {
    auto const root = create_empty_root("agent-delta-before-start");

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
        .append_batch = append_batch_route_for_test(store),
        .session_read_authority = read_authority_for_test(store),
    });
    auto result = loop.run_turn("read note", store, provider, transport);
    expect(result && result->tool_calls == 1 && result->tool_timeline.size() == 1 &&
               result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success && result->tool_timeline.front().name == "read_file",
           "agent loop deduplicates tool deltas that arrive before tool start events");

    auto entries = store.load();
    auto const projection = entries ? ava::session::classify_assistant_output(*entries) : ava::session::AssistantOutputProjection{};
    std::size_t functions = 0;
    std::size_t bound_results = 0;
    for (auto const& turn : projection.turns)
      for (auto const& item : turn.items) functions += std::holds_alternative<ava::session::AssistantOutputFunctionCall>(item.item.payload);
    if (entries)
      for (auto const& entry : *entries)
        bound_results += entry.type == ava::session::EntryType::ToolResult && ava::core::json::field_value_start(entry.data_json, "assistant_output_entry_id");
    expect(entries && functions == 1 && bound_results == 1, "same-iteration start/delta/end fragments merge into one committed v4 function and bound result");
  }

  {
    auto const root = create_empty_root("agent-cross-iteration-duplicate-call-id");

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
        .append_batch = append_batch_route_for_test(store),
        .session_read_authority = read_authority_for_test(store),
    });
    auto result = loop.run_turn("reuse a call id", store, provider, transport);
    auto entries = store.load();
    auto const projection = entries ? ava::session::classify_assistant_output(*entries) : ava::session::AssistantOutputProjection{};
    std::size_t functions = 0;
    std::size_t bound_results = 0;
    for (auto const& turn : projection.turns)
      for (auto const& item : turn.items) functions += std::holds_alternative<ava::session::AssistantOutputFunctionCall>(item.item.payload);
    if (entries)
      for (auto const& entry : *entries)
        bound_results += entry.type == ava::session::EntryType::ToolResult && ava::core::json::field_value_start(entry.data_json, "assistant_output_entry_id");
    auto validation = entries ? ava::session::validate_session_replay(*entries) : ava::session::SessionReplayValidation{};
    expect(!result && result.error().category() == ava::core::ErrorCategory::Provider && result.error().message().find("reused") != std::string::npos &&
               result.error().format().find("call_reused") != std::string::npos && functions == 1 && bound_results == 1 && tool_events.size() == 2 &&
               validation.ok(),
           "cross-iteration provider call-id reuse is rejected before a duplicate lifecycle, dispatch, or session record");
  }

  {
    auto const root = create_empty_root("agent-cross-prompt-duplicate-call-id");

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
        .append_batch = append_batch_route_for_test(store),
        .session_read_authority = read_authority_for_test(store),
    };
    ava::agent::AgentLoop first_loop(options);
    auto first = first_loop.run_turn("first prompt", store, provider, transport);
    auto const events_after_first = stream_events.size();
    ava::agent::AgentLoop second_loop(std::move(options));
    auto second = second_loop.run_turn("second prompt", store, provider, transport);
    auto entries = store.load();
    auto const projection = entries ? ava::session::classify_assistant_output(*entries) : ava::session::AssistantOutputProjection{};
    std::size_t functions = 0;
    std::size_t bound_results = 0;
    for (auto const& turn : projection.turns)
      for (auto const& item : turn.items) functions += std::holds_alternative<ava::session::AssistantOutputFunctionCall>(item.item.payload);
    if (entries)
      for (auto const& entry : *entries)
        bound_results += entry.type == ava::session::EntryType::ToolResult && ava::core::json::field_value_start(entry.data_json, "assistant_output_entry_id");
    auto validation = entries ? ava::session::validate_session_replay(*entries) : ava::session::SessionReplayValidation{};
    expect(first && !second && second.error().category() == ava::core::ErrorCategory::Provider &&
               second.error().message().find("persistent session") != std::string::npos &&
               second.error().format().find("call_persistent") != std::string::npos && functions == 1 && bound_results == 1 &&
               stream_events.size() == events_after_first && validation.ok(),
           "cross-prompt provider call-id reuse is rejected before duplicate updates, persistence, or dispatch");
  }

  {
    auto const root = create_empty_root("agent-empty-call-id");

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
        .append_batch = append_batch_route_for_test(store),
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
      .anchor_set = command_anchors_for_test(workspace, store.session_path().parent_path() / "spill"),
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver =
          [&interactive_prompts](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
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
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
      .observation = observation,
  });

  auto result = loop.run_turn("run inspection", store, provider, transport);
  bool process_started = false;
  {
    std::lock_guard lock(collector->mutex);
    process_started = std::ranges::any_of(
        collector->events, [](ava::observability::TraceEvent const& event) { return event.type == ava::observability::TraceEventType::ProcessStart; });
  }
  expect(result && result->final_text == "persistent deny handled" && deny_preflights == 1 && interactive_prompts == 0 && result->tool_timeline.size() == 1 &&
             result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error && !process_started,
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
      .anchor_set = command_anchors_for_test(workspace, store.session_path().parent_path() / "spill"),
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
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
      .observation = observation,
  });

  auto result = loop.run_turn("inspect authority", store, provider, transport);
  bool process_started = false;
  {
    std::lock_guard lock(collector->mutex);
    process_started = std::ranges::any_of(
        collector->events, [](ava::observability::TraceEvent const& event) { return event.type == ava::observability::TraceEventType::ProcessStart; });
  }
  auto const detail = result && !result->tool_timeline.empty() ? result->tool_timeline.front().result_summary : std::string{};
  expect(result && result->final_text == "authority rejected" && prompts == 0 && preflights == 0 && !process_started &&
             detail.find("must not overlap with any AVA authority root") != std::string::npos,
         "model ToolContexts deduplicate bounded AVA authority roots and reject overlapping workspaces before prompts or processes");
}

void test_agent_loop_truncates_tool_context()
{
  auto const root = create_empty_root("agent-tool-truncate");

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
      .append_batch = append_batch_route_for_test(store),
      .session_read_authority = read_authority_for_test(store),
  });
  auto result = loop.run_turn("read large", store, provider, transport);
  expect(result && transport.requests().size() == 2 && transport.requests()[1].body.find("tool result content truncated") != std::string::npos,
         "agent loop truncates native tool results before OpenAI continuation");
}

void test_unresolved_committed_function_recovery_never_partially_appends_after_a_closed_window()
{
  auto store = ava::session::SessionStore::create_ephemeral(create_empty_root("unresolved-committed-function-recovery"));
  auto target = store ? ava::session::SessionAppendTarget::create_ephemeral(*store)
                      : ava::core::Result<std::shared_ptr<ava::session::SessionAppendTarget>>(std::unexpected(store.error()));
  if (!store || !target)
    return;

  auto function_turn = [](std::string call_id) {
    ava::agent::ParsedAssistantTurn turn;
    turn.ordered_items.push_back(ava::agent::OrderedAssistantItem{
        .sequence = 0,
        .item = ava::agent::AssistantFunctionCallItem{
            .metadata = {}, .tool_call = ava::agent::ProviderToolCall{.id = std::move(call_id), .name = "read_file", .arguments_json = "{}"}}});
    turn.finish_reason = ava::provider::ProviderFinishReason::ToolCalls;
    return turn;
  };
  auto append_batch = [append_target = *target](std::vector<ava::session::SessionEntry> entries) { return append_target->append_batch(std::move(entries)); };
  auto first = ava::agent::append_assistant_turn(append_batch, function_turn("closed_call"), "openai", "gpt-5.5", {}, std::nullopt);
  auto boundary = first ? (*target)->append(ava::session::SessionEntry{.id = "closed_window_user",
                                                                       .parent_id = "",
                                                                       .type = ava::session::EntryType::UserMessage,
                                                                       .timestamp = ava::session::now_timestamp(),
                                                                       .data_json = "{\"text\":\"later turn\"}"})
                        : ava::core::VoidResult(std::unexpected(first.error()));
  auto current = boundary ? ava::agent::append_assistant_turn(append_batch, function_turn("current_call"), "openai", "gpt-5.5", {}, std::nullopt)
                          : ava::core::Result<ava::agent::PersistedAssistantTurn>(std::unexpected(boundary.error()));
  auto authority = current ? (*target)->read_authority() : ava::core::Result<ava::session::SessionReadAuthority>(std::unexpected(current.error()));
  std::size_t synthetic_append_attempts = 0;
  ava::agent::SessionAppendSink sink = [append_target = *target, &synthetic_append_attempts](ava::session::SessionEntry entry) {
    ++synthetic_append_attempts;
    return append_target->append(std::move(entry));
  };
  auto const before = store->load();
  auto reconciled = authority
                        ? ava::agent::reconcile_unresolved_committed_function_calls(*authority, sink, ava::session::legacy_unbounded_session_read_limits())
                        : ava::core::VoidResult(std::unexpected(authority.error()));
  auto const after = store->load();
  expect(first && boundary && current && authority && !reconciled && reconciled.error().message().find("active EOF tool-result window") != std::string::npos &&
             synthetic_append_attempts == 0 && before && after && after->size() == before->size() &&
             std::ranges::equal(
                 *before, *after, {}, [](auto const& entry) { return entry.id; }, [](auto const& entry) { return entry.id; }),
         "closed unresolved v4 calls reject the whole recovery batch before any synthetic result can mutate a later current call");
}

}  // namespace

void run_agent_loop_tests()
{
  test_legacy_provider_text_runs_preserve_v4_order();
  test_legacy_reasoning_replay_requires_exact_entry_source();
  test_v4_no_tools_fallback_survives_native_content_serializers();
  test_request_time_history_projection_preserves_only_exact_native_replay();
  test_request_time_history_projection_reserves_images_for_active_turn();
  test_agent_loop_assistant_turn_lifecycle_validation();
  test_agent_loop_text_only_turn();
  test_agent_loop_uses_established_session_read_limits();
  test_agent_loop_authority_policy_applies_between_provider_iterations();
  test_agent_loop_model_capability_gating();
  test_agent_loop_rejects_persistent_store_without_append_route();
  test_agent_loop_rejects_replaced_history_before_provider_use();
  test_agent_loop_image_attachment_load_failure_records_error();
  test_usage_accounting_saturates_without_signed_overflow();
  test_agent_loop_usage_and_cost_persistence();
  test_agent_loop_tool_turn_and_continuation();
  test_agent_loop_task_subagent_runs_child_session();
  test_agent_loop_child_rejects_unadvertised_task_and_job_calls();
  test_agent_loop_coordinated_foreground_uses_fresh_worker_and_preserves_result_accounting();
  test_agent_loop_foreground_promotion_wakes_parent_without_restarting_child();
  test_agent_loop_promoted_failure_persists_sanitized_child_error();
  test_agent_loop_task_subagent_propagates_authority_roots_to_foreground_and_background_children();
  test_agent_loop_task_subagent_recovers_torn_child_before_resume();
  test_subagent_config_loads_project_definitions();
  test_agent_loop_custom_subagent_definition_controls_prompt_and_tools();
  test_agent_loop_background_task_starts_child_session();
  test_agent_loop_background_task_failure_records_parent_and_child_errors();
  test_agent_loop_background_task_cancel_requests_child_cancellation();
  test_agent_loop_background_task_requires_registry_owner();
  test_agent_loop_coordinator_start_journal_failure_rolls_back_child();
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
  test_stream_bridge_redacts_untrusted_provider_error_event();
  test_agent_loop_invalid_utf8_function_arguments_prevent_dispatch();
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
  test_unresolved_committed_function_recovery_never_partially_appends_after_a_closed_window();
  test_agent_loop_model_command_deny_preflight_blocks_auto_allow_without_process();
  test_agent_loop_model_command_rejects_authority_workspace_before_permission_or_process();
  test_agent_loop_truncates_tool_context();
}
