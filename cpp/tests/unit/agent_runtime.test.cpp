#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/agent/agent.hpp"

namespace {

class EchoTool final : public ava::tools::Tool {
public:
  [[nodiscard]] std::string name() const override { return "echo"; }
  [[nodiscard]] std::string description() const override { return "Echoes input"; }
  [[nodiscard]] nlohmann::json parameters() const override {
    return nlohmann::json{{"type", "object"}, {"properties", {{"input", {{"type", "string"}}}}}};
  }

  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json& args) const override {
    return ava::types::ToolResult{
        .call_id = "",
        .content = args.value("input", std::string{}),
        .is_error = false,
    };
  }
};

class FailingTool final : public ava::tools::Tool {
public:
  [[nodiscard]] std::string name() const override { return "fail"; }
  [[nodiscard]] std::string description() const override { return "Fails deterministically"; }
  [[nodiscard]] nlohmann::json parameters() const override { return nlohmann::json{{"type", "object"}}; }

  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json&) const override {
    throw std::runtime_error("deterministic tool failure");
  }
};

class ScriptedProvider final : public ava::llm::Provider {
public:
  explicit ScriptedProvider(std::vector<ava::llm::LlmResponse> scripted)
      : scripted_(scripted.begin(), scripted.end()) {}

  [[nodiscard]] std::string model_name() const override { return "scripted"; }
  [[nodiscard]] std::size_t estimate_tokens(std::string_view input) const override { return input.size(); }
  [[nodiscard]] double estimate_cost(std::size_t, std::size_t) const override { return 0.0; }
  [[nodiscard]] bool supports_tools() const override { return true; }

  [[nodiscard]] ava::llm::LlmResponse generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    if(scripted_.empty()) {
      throw std::runtime_error("scripted provider exhausted");
    }
    auto next = scripted_.front();
    scripted_.pop_front();
    return next;
  }

  [[nodiscard]] std::vector<ava::types::StreamChunk> generate_stream(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    return {};
  }

  [[nodiscard]] StreamDispatchResult stream_generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig,
      const StreamChunkSink&
  ) const override {
    return StreamDispatchResult::Unsupported;
  }

private:
  mutable std::deque<ava::llm::LlmResponse> scripted_;
};

class CapturingProvider final : public ava::llm::Provider {
public:
  explicit CapturingProvider(std::vector<ava::llm::LlmResponse> scripted)
      : scripted_(scripted.begin(), scripted.end()) {}

  [[nodiscard]] std::string model_name() const override { return "capturing"; }
  [[nodiscard]] std::size_t estimate_tokens(std::string_view input) const override { return input.size(); }
  [[nodiscard]] double estimate_cost(std::size_t, std::size_t) const override { return 0.0; }
  [[nodiscard]] bool supports_tools() const override { return true; }

  [[nodiscard]] ava::llm::LlmResponse generate(
      const std::vector<ava::llm::ChatMessage>& messages,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    captured_messages.push_back(messages);
    if(scripted_.empty()) {
      throw std::runtime_error("capturing provider exhausted");
    }
    auto next = scripted_.front();
    scripted_.pop_front();
    return next;
  }

  [[nodiscard]] std::vector<ava::types::StreamChunk> generate_stream(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    return {};
  }

  [[nodiscard]] StreamDispatchResult stream_generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig,
      const StreamChunkSink&
  ) const override {
    return StreamDispatchResult::Unsupported;
  }

  mutable std::vector<std::vector<ava::llm::ChatMessage>> captured_messages;

private:
  mutable std::deque<ava::llm::LlmResponse> scripted_;
};

class StreamScriptedProvider final : public ava::llm::Provider {
public:
  explicit StreamScriptedProvider(std::vector<std::vector<ava::types::StreamChunk>> scripted_stream)
      : scripted_stream_(scripted_stream.begin(), scripted_stream.end()) {}

  [[nodiscard]] std::string model_name() const override { return "stream-scripted"; }
  [[nodiscard]] std::size_t estimate_tokens(std::string_view input) const override { return input.size(); }
  [[nodiscard]] double estimate_cost(std::size_t, std::size_t) const override { return 0.0; }
  [[nodiscard]] bool supports_tools() const override { return true; }

  [[nodiscard]] ava::llm::LlmResponse generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    throw std::runtime_error("unexpected non-stream call");
  }

  [[nodiscard]] std::vector<ava::types::StreamChunk> generate_stream(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    throw std::runtime_error("unexpected materialized stream call");
  }

  [[nodiscard]] StreamDispatchResult stream_generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig,
      const StreamChunkSink& on_chunk
  ) const override {
    if(scripted_stream_.empty()) {
      throw std::runtime_error("scripted stream provider exhausted");
    }
    auto next = scripted_stream_.front();
    scripted_stream_.pop_front();
    for(const auto& chunk : next) {
      if(on_chunk && !on_chunk(chunk)) {
        break;
      }
    }
    return StreamDispatchResult::Completed;
  }

 private:
  mutable std::deque<std::vector<ava::types::StreamChunk>> scripted_stream_;
};

class EmptyStreamProvider final : public ava::llm::Provider {
 public:
  [[nodiscard]] std::string model_name() const override { return "empty-stream"; }
  [[nodiscard]] std::size_t estimate_tokens(std::string_view input) const override { return input.size(); }
  [[nodiscard]] double estimate_cost(std::size_t, std::size_t) const override { return 0.0; }

  [[nodiscard]] ava::llm::LlmResponse generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    ++generate_calls;
    return ava::llm::LlmResponse{.content = "fallback", .tool_calls = {}, .usage = std::nullopt, .thinking = std::nullopt};
  }

  [[nodiscard]] std::vector<ava::types::StreamChunk> generate_stream(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    return {};
  }

  [[nodiscard]] StreamDispatchResult stream_generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig,
      const StreamChunkSink&
  ) const override {
    ++stream_calls;
    return StreamDispatchResult::Completed;
  }

  mutable int generate_calls{0};
  mutable int stream_calls{0};
};

class ToolCallThenCancelProvider final : public ava::llm::Provider {
 public:
  explicit ToolCallThenCancelProvider(bool& cancelled) : cancelled_(cancelled) {}

  [[nodiscard]] std::string model_name() const override { return "tool-call-then-cancel"; }
  [[nodiscard]] std::size_t estimate_tokens(std::string_view input) const override { return input.size(); }
  [[nodiscard]] double estimate_cost(std::size_t, std::size_t) const override { return 0.0; }
  [[nodiscard]] bool supports_tools() const override { return true; }

  [[nodiscard]] ava::llm::LlmResponse generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    throw std::runtime_error("unexpected non-stream call");
  }

  [[nodiscard]] std::vector<ava::types::StreamChunk> generate_stream(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    throw std::runtime_error("unexpected materialized stream call");
  }

  [[nodiscard]] StreamDispatchResult stream_generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig,
      const StreamChunkSink& on_chunk
  ) const override {
    if(on_chunk) {
      (void)on_chunk(ava::types::StreamChunk{.tool_call = ava::types::StreamToolCall{
                                                 .index = 0,
                                                 .id = "call_empty_text",
                                                 .name = "echo",
                                                 .arguments_delta = "{\"input\":\"x\"}",
                                             }});
    }
    cancelled_ = true;
    return StreamDispatchResult::Completed;
  }

 private:
  bool& cancelled_;
};

class ToolCallCancelledDuringStreamProvider final : public ava::llm::Provider {
 public:
  explicit ToolCallCancelledDuringStreamProvider(bool& cancelled) : cancelled_(cancelled) {}

  [[nodiscard]] std::string model_name() const override { return "tool-call-cancelled-during-stream"; }
  [[nodiscard]] std::size_t estimate_tokens(std::string_view input) const override { return input.size(); }
  [[nodiscard]] double estimate_cost(std::size_t, std::size_t) const override { return 0.0; }
  [[nodiscard]] bool supports_tools() const override { return true; }

  [[nodiscard]] ava::llm::LlmResponse generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    throw std::runtime_error("unexpected non-stream call");
  }

  [[nodiscard]] std::vector<ava::types::StreamChunk> generate_stream(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    throw std::runtime_error("unexpected materialized stream call");
  }

  [[nodiscard]] StreamDispatchResult stream_generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig,
      const StreamChunkSink& on_chunk
  ) const override {
    if(on_chunk) {
      (void)on_chunk(ava::types::StreamChunk{.tool_call = ava::types::StreamToolCall{
                                                 .index = 0,
                                                 .id = "call_cancelled_during_stream",
                                                 .name = "echo",
                                                 .arguments_delta = "{\"input\":\"x\"}",
                                             }});
      cancelled_ = true;
      (void)on_chunk(ava::types::StreamChunk::finished());
    }
    return StreamDispatchResult::Completed;
  }

 private:
  bool& cancelled_;
};

class UnsupportedCancellableProvider final : public ava::llm::Provider {
 public:
  [[nodiscard]] std::string model_name() const override { return "unsupported-cancellable"; }
  [[nodiscard]] std::size_t estimate_tokens(std::string_view input) const override { return input.size(); }
  [[nodiscard]] double estimate_cost(std::size_t, std::size_t) const override { return 0.0; }

  [[nodiscard]] ava::llm::LlmResponse generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    ++generate_calls;
    return ava::llm::LlmResponse{.content = "should-not-run", .tool_calls = {}, .usage = std::nullopt, .thinking = std::nullopt};
  }

  [[nodiscard]] std::vector<ava::types::StreamChunk> generate_stream(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    return {};
  }

  [[nodiscard]] StreamDispatchResult stream_generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig,
      const StreamChunkSink&
  ) const override {
    return StreamDispatchResult::Unsupported;
  }

  mutable int generate_calls{0};
};

class ThrowingStreamProvider final : public ava::llm::Provider {
 public:
  [[nodiscard]] std::string model_name() const override { return "throwing-stream"; }
  [[nodiscard]] std::size_t estimate_tokens(std::string_view input) const override { return input.size(); }
  [[nodiscard]] double estimate_cost(std::size_t, std::size_t) const override { return 0.0; }

  [[nodiscard]] ava::llm::LlmResponse generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    return ava::llm::LlmResponse{.content = "unused", .tool_calls = {}, .usage = std::nullopt, .thinking = std::nullopt};
  }

  [[nodiscard]] std::vector<ava::types::StreamChunk> generate_stream(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    return {};
  }

  [[nodiscard]] StreamDispatchResult stream_generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig,
      const StreamChunkSink&
  ) const override {
    throw std::runtime_error("malformed provider stream");
  }
};

}  // namespace

TEST_CASE("message queue routes and drains tiers", "[ava_agent]") {
  ava::agent::MessageQueue queue;
  queue.enqueue(ava::agent::QueuedMessage{.text = "s1", .tier = ava::types::MessageTier::steering()});
  queue.enqueue(ava::agent::QueuedMessage{.text = "f1", .tier = ava::types::MessageTier::follow_up()});
  queue.enqueue(ava::agent::QueuedMessage{.text = "p1", .tier = ava::types::MessageTier::post_complete(2)});

  REQUIRE(queue.has_steering());
  REQUIRE(queue.has_follow_up());
  REQUIRE(queue.has_post_complete());
  REQUIRE((queue.pending_count() == std::tuple<std::size_t, std::size_t, std::size_t>{1, 1, 1}));

  REQUIRE(queue.drain_steering() == std::vector<std::string>{"s1"});
  REQUIRE(queue.drain_follow_up() == std::vector<std::string>{"f1"});
  const auto [group, messages] = queue.next_post_complete_group();
  REQUIRE(group == 2);
  REQUIRE(messages == std::vector<std::string>{"p1"});
}

TEST_CASE("response helpers parse envelope and stream tool deltas", "[ava_agent]") {
  const auto parsed = ava::agent::response::parse_tool_calls_from_content(
      R"({"tool_calls":[{"id":"call_1","name":"echo","arguments":{"input":"hello"}}]})"
  );
  REQUIRE(parsed.size() == 1);
  REQUIRE(parsed.front().name == "echo");
  REQUIRE(parsed.front().arguments.at("input") == "hello");

  std::vector<ava::agent::response::ToolCallAccumulator> accumulators;
  ava::agent::response::accumulate_tool_call(
      accumulators,
      ava::types::StreamToolCall{
          .index = 0,
          .id = std::string{"call_stream"},
          .name = std::string{"echo"},
          .arguments_delta = std::string{"{\"input\":\"he"},
      }
  );
  ava::agent::response::accumulate_tool_call(
      accumulators,
      ava::types::StreamToolCall{
          .index = 0,
          .id = std::nullopt,
          .name = std::nullopt,
          .arguments_delta = std::string{"llo\"}"},
      }
  );

  const auto finalized = ava::agent::response::finalize_tool_calls(std::move(accumulators));
  REQUIRE(finalized.size() == 1);
  REQUIRE(finalized.front().id == "call_stream");
  REQUIRE(finalized.front().arguments.at("input") == "hello");
}

TEST_CASE("streamed zero-argument tool calls finalize with empty arguments", "[ava_agent]") {
  std::vector<ava::agent::response::ToolCallAccumulator> accumulators;
  ava::agent::response::accumulate_tool_call(
      accumulators,
      ava::types::StreamToolCall{.index = 0, .id = std::string{"call_zero"}, .name = std::string{"status"}}
  );

  const auto finalized = ava::agent::response::finalize_tool_calls(std::move(accumulators));
  REQUIRE(finalized.size() == 1);
  REQUIRE(finalized.front().id == "call_zero");
  REQUIRE(finalized.front().name == "status");
  REQUIRE(finalized.front().arguments == nlohmann::json::object());
}

TEST_CASE("agent runtime executes tool calls and completes", "[ava_agent]") {
  ScriptedProvider provider({
      ava::llm::LlmResponse{
          .content = "I'll use a tool first",
          .tool_calls = {ava::types::ToolCall{.id = "call_1", .name = "echo", .arguments = nlohmann::json{{"input", "tool-output"}}}},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
      ava::llm::LlmResponse{
          .content = "Done: tool-output",
          .tool_calls = {},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
  });

  ava::tools::ToolRegistry tools;
  tools.register_tool(std::make_unique<EchoTool>());

  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 4});

  ava::types::SessionRecord session{
      .id = "session_1",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  std::vector<ava::agent::AgentEventKind> events;
  const auto result = runtime.run(
      session,
      ava::agent::AgentRunInput{.goal = "say hello"},
      [&](const ava::agent::AgentEvent& event) {
        events.push_back(event.kind);
      }
  );

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Completed);
  REQUIRE(result.final_response == "Done: tool-output");
  REQUIRE(session.messages.size() == 4);
  REQUIRE(session.messages.at(0).role == "user");
  REQUIRE(session.messages.at(2).role == "tool");
  REQUIRE(session.messages.at(2).tool_call_id == std::optional<std::string>{"call_1"});
  REQUIRE(session.messages.at(2).tool_results.at(0).at("call_id") == "call_1");
  REQUIRE(events.back() == ava::agent::AgentEventKind::Completion);
}

TEST_CASE("agent runtime records non-permission tool errors and continues", "[ava_agent]") {
  ScriptedProvider provider({
      ava::llm::LlmResponse{
          .content = "I'll try a failing tool",
          .tool_calls = {ava::types::ToolCall{.id = "call_fail_1", .name = "fail", .arguments = nlohmann::json::object()}},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
      ava::llm::LlmResponse{
          .content = "Recovered from the tool failure",
          .tool_calls = {},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
  });

  ava::tools::ToolRegistry tools;
  tools.register_tool(std::make_unique<FailingTool>());

  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 4});
  ava::types::SessionRecord session{
      .id = "session_tool_error",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  std::vector<ava::types::ToolResult> tool_results;
  const auto result = runtime.run(
      session,
      ava::agent::AgentRunInput{.goal = "try fail"},
      [&](const ava::agent::AgentEvent& event) {
        if(event.tool_result.has_value()) {
          tool_results.push_back(*event.tool_result);
        }
      }
  );

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Completed);
  REQUIRE(result.final_response == "Recovered from the tool failure");
  REQUIRE(tool_results.size() == 1);
  REQUIRE(tool_results.front().call_id == "call_fail_1");
  REQUIRE(tool_results.front().is_error);
  REQUIRE(tool_results.front().content.find("deterministic tool failure") != std::string::npos);
  REQUIRE(session.messages.size() == 4);
  REQUIRE(session.messages.at(2).role == "tool");
  REQUIRE(session.messages.at(2).tool_call_id == std::optional<std::string>{"call_fail_1"});
  const auto payload = nlohmann::json::parse(session.messages.at(2).content);
  REQUIRE(payload.at("call_id") == "call_fail_1");
  REQUIRE(payload.at("is_error") == true);
}

TEST_CASE("agent runtime appends after sparse generated message ids", "[ava_agent]") {
  ScriptedProvider provider({
      ava::llm::LlmResponse{
          .content = "continued",
          .tool_calls = {},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
  });

  ava::tools::ToolRegistry tools;
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 1});

  ava::types::SessionRecord session{
      .id = "session_sparse_ids",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {
          ava::types::SessionMessage{
              .id = "m7_msg_1",
              .role = "user",
              .content = "earlier",
              .timestamp = "2026-01-01T00:00:00Z",
              .parent_id = std::nullopt,
          },
          ava::types::SessionMessage{
              .id = "m7_msg_5",
              .role = "assistant",
              .content = "older branch head",
              .timestamp = "2026-01-01T00:00:01Z",
              .parent_id = std::optional<std::string>{"m7_msg_1"},
          },
      },
      .branch_head = std::optional<std::string>{"m7_msg_5"},
  };

  const auto result = runtime.run(session, ava::agent::AgentRunInput{.goal = "continue"});

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Completed);
  REQUIRE(session.messages.size() == 4);
  REQUIRE(session.messages.at(2).id == "m7_msg_6");
  REQUIRE(session.messages.at(3).id == "m7_msg_7");
}

TEST_CASE("agent runtime appends and prompts from active branch head", "[ava_agent]") {
  CapturingProvider provider({
      ava::llm::LlmResponse{
          .content = "active branch response",
          .tool_calls = {},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
  });

  ava::tools::ToolRegistry tools;
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 1});

  ava::types::SessionRecord session{
      .id = "session_active_branch",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {
          ava::types::SessionMessage{
              .id = "m7_msg_1",
              .role = "user",
              .content = "root question",
              .timestamp = "2026-01-01T00:00:00Z",
              .parent_id = std::nullopt,
          },
          ava::types::SessionMessage{
              .id = "m7_msg_2",
              .role = "assistant",
              .content = "active assistant",
              .timestamp = "2026-01-01T00:00:01Z",
              .parent_id = std::optional<std::string>{"m7_msg_1"},
          },
          ava::types::SessionMessage{
              .id = "m7_msg_3",
              .role = "user",
              .content = "inactive fork",
              .timestamp = "2026-01-01T00:00:02Z",
              .parent_id = std::optional<std::string>{"m7_msg_1"},
          },
      },
      .branch_head = std::optional<std::string>{"m7_msg_2"},
  };

  const auto result = runtime.run(session, ava::agent::AgentRunInput{.goal = "continue active", .stream = false});

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Completed);
  REQUIRE(session.messages.size() == 5);
  REQUIRE(session.messages.at(3).role == "user");
  REQUIRE(session.messages.at(3).content == "continue active");
  REQUIRE(session.messages.at(3).parent_id == std::optional<std::string>{"m7_msg_2"});
  REQUIRE(session.messages.at(4).role == "assistant");
  REQUIRE(session.messages.at(4).parent_id == std::optional<std::string>{session.messages.at(3).id});
  REQUIRE(session.branch_head == std::optional<std::string>{session.messages.at(4).id});

  REQUIRE(provider.captured_messages.size() == 1);
  std::vector<std::string> prompt_contents;
  for(const auto& message : provider.captured_messages.front()) {
    prompt_contents.push_back(message.content);
  }
  REQUIRE(std::find(prompt_contents.begin(), prompt_contents.end(), "root question") != prompt_contents.end());
  REQUIRE(std::find(prompt_contents.begin(), prompt_contents.end(), "active assistant") != prompt_contents.end());
  REQUIRE(std::find(prompt_contents.begin(), prompt_contents.end(), "continue active") != prompt_contents.end());
  REQUIRE(std::find(prompt_contents.begin(), prompt_contents.end(), "inactive fork") == prompt_contents.end());
}

TEST_CASE("agent runtime stops on simple stuck loop", "[ava_agent]") {
  ScriptedProvider provider({
      ava::llm::LlmResponse{
          .content = "",
          .tool_calls = {ava::types::ToolCall{.id = "call_1", .name = "echo", .arguments = nlohmann::json{{"input", "same"}}}},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
      ava::llm::LlmResponse{
          .content = "",
          .tool_calls = {ava::types::ToolCall{.id = "call_2", .name = "echo", .arguments = nlohmann::json{{"input", "same"}}}},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
      ava::llm::LlmResponse{
          .content = "",
          .tool_calls = {ava::types::ToolCall{.id = "call_3", .name = "echo", .arguments = nlohmann::json{{"input", "same"}}}},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
  });

  ava::tools::ToolRegistry tools;
  tools.register_tool(std::make_unique<EchoTool>());

  ava::agent::AgentConfig config;
  config.max_turns = 6;
  config.stuck.repeated_tool_call_limit = 1;

  ava::agent::AgentRuntime runtime(provider, tools, config);

  ava::types::SessionRecord session{
      .id = "session_stuck",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  const auto result = runtime.run(session, ava::agent::AgentRunInput{.goal = "loop"});
  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Stuck);
}

TEST_CASE("stuck detector resets repeated response state on empty turns", "[ava_agent]") {
  ava::agent::StuckDetector detector;

  REQUIRE(detector.check("same response", {}).kind == ava::agent::StuckActionKind::Continue);
  REQUIRE(detector.check("", {}).kind == ava::agent::StuckActionKind::Continue);
  REQUIRE(detector.check("same response", {}).kind == ava::agent::StuckActionKind::Continue);
}

TEST_CASE("stuck detector resets repeated tool state on multi tool turns", "[ava_agent]") {
  ava::agent::StuckDetectorConfig config;
  config.repeated_tool_call_limit = 1;
  ava::agent::StuckDetector detector{config};

  const auto single = std::vector<ava::types::ToolCall>{
      ava::types::ToolCall{.id = "call_1", .name = "echo", .arguments = nlohmann::json{{"input", "same"}}},
  };
  const auto multi = std::vector<ava::types::ToolCall>{
      ava::types::ToolCall{.id = "call_2", .name = "echo", .arguments = nlohmann::json{{"input", "same"}}},
      ava::types::ToolCall{.id = "call_3", .name = "echo", .arguments = nlohmann::json{{"input", "other"}}},
  };

  REQUIRE(detector.check("", single).kind == ava::agent::StuckActionKind::Continue);
  REQUIRE(detector.check("", multi).kind == ava::agent::StuckActionKind::Continue);
  REQUIRE(detector.check("", single).kind == ava::agent::StuckActionKind::Continue);
}

TEST_CASE("tool message fallback preserves raw content for unexpected json shape", "[ava_agent]") {
  ScriptedProvider provider({
      ava::llm::LlmResponse{
          .content = "final answer",
          .tool_calls = {},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
  });

  ava::tools::ToolRegistry tools;
  ava::agent::AgentRuntime runtime(provider, tools);

  ava::types::SessionRecord session{
      .id = "session_tool_json",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {
          ava::types::SessionMessage{
              .id = "tool_1",
              .role = "tool",
              .content = R"({"unexpected":"shape"})",
              .timestamp = "2026-01-01T00:00:00Z",
              .parent_id = std::nullopt,
          },
      },
      .branch_head = std::string{"tool_1"},
  };

  const auto result = runtime.run(session, ava::agent::AgentRunInput{.goal = "continue"});
  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Completed);
}

TEST_CASE("agent runtime ignores throwing event sinks and still returns results", "[ava_agent]") {
  ScriptedProvider provider({
      ava::llm::LlmResponse{
          .content = "final answer",
          .tool_calls = {},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
  });

  ava::tools::ToolRegistry tools;
  ava::agent::AgentRuntime runtime(provider, tools);

  ava::types::SessionRecord session{
      .id = "session_sink_throw",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  const auto result = runtime.run(
      session,
      ava::agent::AgentRunInput{.goal = "go"},
      [](const ava::agent::AgentEvent&) {
        throw std::runtime_error("sink failure");
      }
  );

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Completed);
  REQUIRE(result.final_response == "final answer");
}

TEST_CASE("inject message skips current turn tool execution", "[ava_agent]") {
  ScriptedProvider provider({
      ava::llm::LlmResponse{
          .content = "",
          .tool_calls = {ava::types::ToolCall{.id = "call_1", .name = "echo", .arguments = nlohmann::json{{"input", "same"}}}},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
      ava::llm::LlmResponse{
          .content = "",
          .tool_calls = {ava::types::ToolCall{.id = "call_2", .name = "echo", .arguments = nlohmann::json{{"input", "same"}}}},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
      ava::llm::LlmResponse{
          .content = "recovered",
          .tool_calls = {},
          .usage = std::nullopt,
          .thinking = std::nullopt,
      },
  });

  ava::tools::ToolRegistry tools;
  tools.register_tool(std::make_unique<EchoTool>());

  ava::agent::AgentConfig config;
  config.max_turns = 4;
  config.stuck.repeated_tool_call_limit = 1;

  ava::agent::AgentRuntime runtime(provider, tools, config);

  ava::types::SessionRecord session{
      .id = "session_inject",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  const auto result = runtime.run(session, ava::agent::AgentRunInput{.goal = "loop"});
  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Completed);

  std::size_t tool_messages = 0;
  for(const auto& message : session.messages) {
    if(message.role == "tool") {
      ++tool_messages;
    }
  }
  REQUIRE(tool_messages == 1);
}

TEST_CASE("agent runtime emits streaming assistant deltas with run_id", "[ava_agent]") {
  StreamScriptedProvider provider({
      std::vector<ava::types::StreamChunk>{
          ava::types::StreamChunk::text("Hello "),
          ava::types::StreamChunk::text("world"),
          ava::types::StreamChunk::finished(),
      },
  });

  ava::tools::ToolRegistry tools;
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 2});

  ava::types::SessionRecord session{
      .id = "session_stream",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  std::vector<ava::agent::AgentEvent> events;
  const auto result = runtime.run(
      session,
      ava::agent::AgentRunInput{
          .goal = "stream",
          .queue = nullptr,
          .run_id = "run-stream-1",
          .is_cancelled = [] {
            return false;
          },
          .stream = true,
      },
      [&](const ava::agent::AgentEvent& event) {
        events.push_back(event);
      }
  );

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Completed);
  REQUIRE(result.final_response == "Hello world");

  const auto delta_count = std::count_if(events.begin(), events.end(), [](const auto& event) {
    return event.kind == ava::agent::AgentEventKind::AssistantResponseDelta;
  });
  REQUIRE(delta_count == 2);
  REQUIRE(std::all_of(events.begin(), events.end(), [](const auto& event) {
    return event.run_id == std::optional<std::string>{"run-stream-1"};
  }));
}

TEST_CASE("agent runtime exits cooperatively when cancelled during streaming", "[ava_agent]") {
  StreamScriptedProvider provider({
      std::vector<ava::types::StreamChunk>{
          ava::types::StreamChunk::text("partial"),
          ava::types::StreamChunk::text(" ignored"),
          ava::types::StreamChunk::finished(),
      },
  });

  ava::tools::ToolRegistry tools;
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 2});

  ava::types::SessionRecord session{
      .id = "session_cancel",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  bool cancelled = false;
  const auto result = runtime.run(
      session,
      ava::agent::AgentRunInput{
          .goal = "cancel",
          .queue = nullptr,
          .run_id = "run-cancel-1",
          .is_cancelled = [&] {
            return cancelled;
          },
          .stream = true,
      },
      [&](const ava::agent::AgentEvent& event) {
        if(event.kind == ava::agent::AgentEventKind::AssistantResponseDelta) {
          cancelled = true;
        }
      }
  );

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Cancelled);
  REQUIRE(result.final_response == "agent run cancelled");
  REQUIRE(std::any_of(session.messages.begin(), session.messages.end(), [](const auto& message) {
    return message.role == "assistant" && message.content == "partial";
  }));
}

TEST_CASE("agent runtime records error metadata on exception path", "[ava_agent]") {
  ScriptedProvider provider({});
  ava::tools::ToolRegistry tools;
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 2});

  ava::types::SessionRecord session{
      .id = "session_error_meta",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  const auto result = runtime.run(session, ava::agent::AgentRunInput{.goal = "boom", .stream = false});
  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Error);
  REQUIRE(session.metadata["agent"]["last_completion_reason"] == "error");
  REQUIRE(session.metadata["agent"].contains("last_error"));
}

TEST_CASE("agent runtime treats empty stream as completed stream without fallback generate", "[ava_agent]") {
  EmptyStreamProvider provider;
  ava::tools::ToolRegistry tools;
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 2});

  ava::types::SessionRecord session{
      .id = "session_empty_stream",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  const auto result = runtime.run(session, ava::agent::AgentRunInput{.goal = "stream-empty", .stream = true});
  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Completed);
  REQUIRE(provider.stream_calls == 1);
  REQUIRE(provider.generate_calls == 0);
}

TEST_CASE("agent runtime honours cancellation before unsupported-stream fallback generate", "[ava_agent]") {
  UnsupportedCancellableProvider provider;
  ava::tools::ToolRegistry tools;
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 2});

  ava::types::SessionRecord session{
      .id = "session_cancel_before_fallback",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  const auto result = runtime.run(
      session,
      ava::agent::AgentRunInput{
          .goal = "cancel",
          .is_cancelled = [] {
            return true;
          },
          .stream = true,
      }
  );

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Cancelled);
  REQUIRE(provider.generate_calls == 0);
}

TEST_CASE("agent runtime reassembles streamed tool calls and executes tools", "[ava_agent]") {
  StreamScriptedProvider provider({
      std::vector<ava::types::StreamChunk>{
          ava::types::StreamChunk::text("use tool"),
          ava::types::StreamChunk{.tool_call = ava::types::StreamToolCall{.index = 0, .id = "call_stream", .name = "echo", .arguments_delta = "{\"input\":\"hel"}},
          ava::types::StreamChunk{.tool_call = ava::types::StreamToolCall{.index = 0, .id = std::nullopt, .name = std::nullopt, .arguments_delta = "lo\"}"}},
          ava::types::StreamChunk::finished(),
      },
      std::vector<ava::types::StreamChunk>{
          ava::types::StreamChunk::text("done"),
          ava::types::StreamChunk::finished(),
      },
  });

  ava::tools::ToolRegistry tools;
  tools.register_tool(std::make_unique<EchoTool>());
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 3});

  ava::types::SessionRecord session{
      .id = "session_stream_tool",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  const auto result = runtime.run(session, ava::agent::AgentRunInput{.goal = "tool", .stream = true});
  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Completed);

  std::size_t tool_messages = 0;
  for(const auto& message : session.messages) {
    if(message.role == "tool") {
      ++tool_messages;
      const auto payload = nlohmann::json::parse(message.content);
      REQUIRE(payload["call_id"] == "call_stream");
      REQUIRE(payload["content"] == "hello");
    }
  }
  REQUIRE(tool_messages == 1);
}

TEST_CASE("agent runtime suppresses empty assistant response for tool-call-only stream", "[ava_agent]") {
  StreamScriptedProvider provider({
      std::vector<ava::types::StreamChunk>{
          ava::types::StreamChunk{.tool_call = ava::types::StreamToolCall{.index = 0, .id = "call_stream_only", .name = "echo", .arguments_delta = "{\"input\":\"hello\"}"}},
          ava::types::StreamChunk::finished(),
      },
      std::vector<ava::types::StreamChunk>{
          ava::types::StreamChunk::text("done"),
          ava::types::StreamChunk::finished(),
      },
  });

  ava::tools::ToolRegistry tools;
  tools.register_tool(std::make_unique<EchoTool>());
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 3});

  ava::types::SessionRecord session{
      .id = "session_stream_tool_only",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  std::vector<ava::agent::AgentEvent> events;
  const auto result = runtime.run(
      session,
      ava::agent::AgentRunInput{.goal = "tool only", .stream = true},
      [&](const ava::agent::AgentEvent& event) {
        events.push_back(event);
      }
  );

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Completed);
  REQUIRE(std::none_of(events.begin(), events.end(), [](const auto& event) {
    return event.kind == ava::agent::AgentEventKind::AssistantResponse && event.message.empty();
  }));
  REQUIRE(std::count_if(events.begin(), events.end(), [](const auto& event) {
            return event.kind == ava::agent::AgentEventKind::ToolCall;
          }) == 1);
}

TEST_CASE("agent runtime cancels before tool execution after streamed assistant text", "[ava_agent]") {
  StreamScriptedProvider provider({
      std::vector<ava::types::StreamChunk>{
          ava::types::StreamChunk::text("thinking"),
          ava::types::StreamChunk{.tool_call = ava::types::StreamToolCall{.index = 0, .id = "call_1", .name = "echo", .arguments_delta = "{\"input\":\"x\"}"}},
          ava::types::StreamChunk::finished(),
      },
  });

  ava::tools::ToolRegistry tools;
  tools.register_tool(std::make_unique<EchoTool>());
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 2});

  ava::types::SessionRecord session{
      .id = "session_cancel_before_tool",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  bool cancelled = false;
  const auto result = runtime.run(
      session,
      ava::agent::AgentRunInput{
          .goal = "cancel before tool",
          .is_cancelled = [&] {
            return cancelled;
          },
          .stream = true,
      },
      [&](const ava::agent::AgentEvent& event) {
        if(event.kind == ava::agent::AgentEventKind::AssistantResponseDelta) {
          cancelled = true;
        }
      }
  );

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Cancelled);
  REQUIRE(std::none_of(session.messages.begin(), session.messages.end(), [](const auto& message) {
    return message.role == "tool";
  }));
  REQUIRE(std::any_of(session.messages.begin(), session.messages.end(), [](const auto& message) {
    return message.role == "assistant" && message.content == "thinking";
  }));
}

TEST_CASE("agent runtime preserves tool-call-only assistant message when cancelled before tool execution", "[ava_agent]") {
  bool cancelled = false;
  ToolCallThenCancelProvider provider(cancelled);

  ava::tools::ToolRegistry tools;
  tools.register_tool(std::make_unique<EchoTool>());
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 2});

  ava::types::SessionRecord session{
      .id = "session_cancel_tool_call_only",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  const auto result = runtime.run(
      session,
      ava::agent::AgentRunInput{
          .goal = "cancel before empty-text tool call",
          .is_cancelled = [&] {
            return cancelled;
          },
          .stream = true,
      }
  );

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Cancelled);
  REQUIRE(std::none_of(session.messages.begin(), session.messages.end(), [](const auto& message) {
    return message.role == "tool";
  }));
  const auto assistant = std::find_if(session.messages.begin(), session.messages.end(), [](const auto& message) {
    return message.role == "assistant" && message.content.empty() && !message.tool_calls.empty();
  });
  REQUIRE(assistant != session.messages.end());
  REQUIRE(assistant->tool_calls.at(0).at("id") == "call_empty_text");
}

TEST_CASE("agent runtime preserves tool-call-only assistant message when cancelled during streaming", "[ava_agent]") {
  bool cancelled = false;
  ToolCallCancelledDuringStreamProvider provider(cancelled);

  ava::tools::ToolRegistry tools;
  tools.register_tool(std::make_unique<EchoTool>());
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 2});

  ava::types::SessionRecord session{
      .id = "session_cancel_tool_call_during_stream",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  const auto result = runtime.run(
      session,
      ava::agent::AgentRunInput{
          .goal = "cancel during empty-text tool call",
          .is_cancelled = [&] {
            return cancelled;
          },
          .stream = true,
      }
  );

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Cancelled);
  REQUIRE(std::none_of(session.messages.begin(), session.messages.end(), [](const auto& message) {
    return message.role == "tool";
  }));
  const auto assistant = std::find_if(session.messages.begin(), session.messages.end(), [](const auto& message) {
    return message.role == "assistant" && message.content.empty() && !message.tool_calls.empty();
  });
  REQUIRE(assistant != session.messages.end());
  REQUIRE(assistant->tool_calls.at(0).at("id") == "call_cancelled_during_stream");
}

TEST_CASE("agent runtime cancellation preserves session transcript integrity", "[ava_agent]") {
  StreamScriptedProvider provider({
      std::vector<ava::types::StreamChunk>{
          ava::types::StreamChunk::text("partial"),
          ava::types::StreamChunk::finished(),
      },
  });

  ava::tools::ToolRegistry tools;
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 2});

  ava::types::SessionRecord session{
      .id = "session_cancel_integrity",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {ava::types::SessionMessage{
          .id = "m7_msg_1",
          .role = "user",
          .content = "earlier",
          .timestamp = "2026-01-01T00:00:00Z",
          .parent_id = std::nullopt,
      }},
      .branch_head = std::optional<std::string>{"m7_msg_1"},
  };

  bool cancelled = false;
  const auto result = runtime.run(
      session,
      ava::agent::AgentRunInput{
          .goal = "cancel with prior transcript",
          .is_cancelled = [&] {
            return cancelled;
          },
          .stream = true,
      },
      [&](const ava::agent::AgentEvent& event) {
        if(event.kind == ava::agent::AgentEventKind::AssistantResponseDelta) {
          cancelled = true;
        }
      }
  );

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Cancelled);
  REQUIRE(session.messages.size() == 3);
  REQUIRE(session.messages.at(0).id == "m7_msg_1");
  REQUIRE(session.messages.at(0).role == "user");
  REQUIRE(session.messages.at(1).role == "user");
  REQUIRE(session.messages.at(1).content == "cancel with prior transcript");
  REQUIRE(session.messages.at(1).parent_id == std::optional<std::string>{"m7_msg_1"});
  REQUIRE(session.messages.at(2).role == "assistant");
  REQUIRE(session.messages.at(2).content == "partial");
  REQUIRE(session.messages.at(2).parent_id == std::optional<std::string>{session.messages.at(1).id});
  REQUIRE(session.branch_head == std::optional<std::string>{session.messages.at(2).id});
}

TEST_CASE("agent runtime emits terminal completion on exception paths", "[ava_agent]") {
  ThrowingStreamProvider provider;
  ava::tools::ToolRegistry tools;
  ava::agent::AgentRuntime runtime(provider, tools, ava::agent::AgentConfig{.max_turns = 2});

  ava::types::SessionRecord session{
      .id = "session_exception_completion",
      .created_at = "2026-01-01T00:00:00Z",
      .updated_at = "2026-01-01T00:00:00Z",
      .metadata = nlohmann::json::object(),
      .messages = {},
      .branch_head = std::nullopt,
  };

  std::vector<ava::agent::AgentEvent> events;
  const auto result = runtime.run(
      session,
      ava::agent::AgentRunInput{.goal = "boom", .stream = true},
      [&](const ava::agent::AgentEvent& event) {
        events.push_back(event);
      }
  );

  REQUIRE(result.reason == ava::agent::AgentCompletionReason::Error);
  const auto saw_error = std::any_of(events.begin(), events.end(), [](const auto& event) {
    return event.kind == ava::agent::AgentEventKind::Error;
  });
  const auto saw_completion_error = std::any_of(events.begin(), events.end(), [](const auto& event) {
    return event.kind == ava::agent::AgentEventKind::Completion
        && event.completion_reason == std::optional<ava::agent::AgentCompletionReason>{ava::agent::AgentCompletionReason::Error};
  });
  REQUIRE(saw_error);
  REQUIRE(saw_completion_error);
}
