#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <memory>
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

private:
  mutable std::deque<ava::llm::LlmResponse> scripted_;
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
  REQUIRE(events.back() == ava::agent::AgentEventKind::Completion);
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
