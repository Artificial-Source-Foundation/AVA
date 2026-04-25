#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/config/paths.hpp"
#include "ava/config/trust.hpp"
#include "ava/llm/factory.hpp"
#include "ava/orchestration/orchestration.hpp"
#include "ava/session/session.hpp"

namespace {

void set_env_var(const std::string& key, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(key.c_str(), value.c_str());
#else
  setenv(key.c_str(), value.c_str(), 1);
#endif
}

void unset_env_var(const std::string& key) {
#if defined(_WIN32)
  _putenv_s(key.c_str(), "");
#else
  unsetenv(key.c_str());
#endif
}

struct ScopedEnvVar {
  std::string key;
  std::optional<std::string> old_value;

  ScopedEnvVar(std::string key_value, std::string value)
      : key(std::move(key_value)) {
    if(const char* current = std::getenv(key.c_str()); current != nullptr) {
      old_value = std::string(current);
    }
    set_env_var(key, value);
  }

  ~ScopedEnvVar() {
    if(old_value.has_value()) {
      set_env_var(key, *old_value);
    } else {
      unset_env_var(key);
    }
  }
};

class StaticTool final : public ava::tools::Tool {
 public:
  explicit StaticTool(std::string tool_name) : name_(std::move(tool_name)) {}

  [[nodiscard]] std::string name() const override { return name_; }
  [[nodiscard]] std::string description() const override { return "tool:" + name_; }
  [[nodiscard]] nlohmann::json parameters() const override { return nlohmann::json{{"type", "object"}}; }
  [[nodiscard]] ava::types::ToolResult execute(const nlohmann::json&) const override {
    return ava::types::ToolResult{.call_id = "", .content = name_, .is_error = false};
  }

 private:
  std::string name_;
};

class RecordingSpawner final : public ava::orchestration::TaskSpawner {
 public:
  ava::orchestration::TaskResult spawn(const std::string& prompt) override {
    last_prompt = prompt;
    return ava::orchestration::TaskResult{
        .output = "spawn:" + prompt,
        .error = std::nullopt,
        .session_id = "session-1",
        .messages = {},
    };
  }

  std::string last_prompt;
};

class BlockingStreamProvider final : public ava::llm::Provider {
 public:
  BlockingStreamProvider(std::shared_ptr<std::atomic<bool>> entered_stream, std::shared_ptr<std::atomic<bool>> stop_stream)
      : entered_stream_(std::move(entered_stream)), stop_stream_(std::move(stop_stream)) {}

  [[nodiscard]] std::string model_name() const override { return "blocking-mock"; }
  [[nodiscard]] std::size_t estimate_tokens(std::string_view input) const override { return input.size(); }
  [[nodiscard]] double estimate_cost(std::size_t, std::size_t) const override { return 0.0; }

  [[nodiscard]] ava::llm::LlmResponse generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    return ava::llm::LlmResponse{.content = "blocking fallback"};
  }

  [[nodiscard]] std::vector<ava::types::StreamChunk> generate_stream(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    return {ava::types::StreamChunk::text("blocking fallback")};
  }

  [[nodiscard]] ava::llm::Provider::StreamDispatchResult stream_generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig,
      const StreamChunkSink& on_chunk
  ) const override {
    entered_stream_->store(true, std::memory_order_release);
    while(!stop_stream_->load(std::memory_order_acquire)) {
      if(on_chunk && !on_chunk(ava::types::StreamChunk::text("tick"))) {
        return StreamDispatchResult::Completed;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return StreamDispatchResult::Completed;
  }

 private:
  std::shared_ptr<std::atomic<bool>> entered_stream_;
  std::shared_ptr<std::atomic<bool>> stop_stream_;
};

class ThrowingStreamProvider final : public ava::llm::Provider {
 public:
  [[nodiscard]] std::string model_name() const override { return "throwing-mock"; }
  [[nodiscard]] std::size_t estimate_tokens(std::string_view input) const override { return input.size(); }
  [[nodiscard]] double estimate_cost(std::size_t, std::size_t) const override { return 0.0; }

  [[nodiscard]] ava::llm::LlmResponse generate(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    throw std::runtime_error("provider exploded");
  }

  [[nodiscard]] std::vector<ava::types::StreamChunk> generate_stream(
      const std::vector<ava::llm::ChatMessage>&,
      const std::vector<ava::types::Tool>&,
      ava::llm::ThinkingConfig
  ) const override {
    throw std::runtime_error("provider exploded");
  }
};

class ScriptedMcpTransport final : public ava::mcp::McpTransport {
 public:
  struct State {
    std::deque<ava::mcp::JsonRpcMessage> inbound;
    std::deque<ava::mcp::JsonRpcMessage> outbound;
    bool closed{false};
  };

  explicit ScriptedMcpTransport(std::shared_ptr<State> state)
      : state_(std::move(state)) {}

  void send(const ava::mcp::JsonRpcMessage& message) override {
    if(state_->closed) {
      throw std::runtime_error("scripted MCP transport is closed");
    }
    state_->outbound.push_back(message);
  }

  [[nodiscard]] ava::mcp::JsonRpcMessage receive() override {
    if(state_->closed) {
      throw std::runtime_error("scripted MCP transport is closed");
    }
    if(state_->inbound.empty()) {
      throw std::runtime_error("scripted MCP transport has no inbound messages");
    }
    auto message = state_->inbound.front();
    state_->inbound.pop_front();
    return message;
  }

  void close() override {
    state_->closed = true;
  }

 private:
  std::shared_ptr<State> state_;
};

std::filesystem::path temp_root_for_test() {
  const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  return std::filesystem::temp_directory_path() / ("ava_cpp_orchestration_test_" + unique);
}

}  // namespace

TEST_CASE("runtime profile classifies read-only specialist agents", "[ava_orchestration]") {
  REQUIRE(ava::orchestration::MAX_AGENT_DEPTH == 3);
  REQUIRE(
      ava::orchestration::runtime_profile_for("review") == ava::orchestration::SubAgentRuntimeProfile::ReadOnly
  );
  REQUIRE(
      ava::orchestration::runtime_profile_for("general") == ava::orchestration::SubAgentRuntimeProfile::Full
  );
}

TEST_CASE("read-only runtime profile keeps only safe investigation tools", "[ava_orchestration]") {
  ava::tools::ToolRegistry registry;
  registry.register_tool(std::make_unique<StaticTool>("read"));
  registry.register_tool(std::make_unique<StaticTool>("write"));
  registry.register_tool(std::make_unique<StaticTool>("bash"));
  registry.register_tool(std::make_unique<StaticTool>("custom_mutating_tool"));

  const auto read_only = ava::orchestration::apply_runtime_profile_to_registry(
      registry,
      ava::orchestration::SubAgentRuntimeProfile::ReadOnly
  );

  REQUIRE(read_only.size() == 1);
  REQUIRE(read_only.front().name == "read");

  const auto full = ava::orchestration::apply_runtime_profile_to_registry(
      registry,
      ava::orchestration::SubAgentRuntimeProfile::Full
  );
  REQUIRE(full.size() == 4);
}

TEST_CASE("read-only runtime tool names are sorted and reusable", "[ava_orchestration]") {
  const auto names = ava::orchestration::read_only_runtime_tool_names();

  REQUIRE(std::is_sorted(names.begin(), names.end()));
  REQUIRE(names == std::vector<std::string>{"git", "git_read", "glob", "grep", "read"});
  REQUIRE(std::find(names.begin(), names.end(), "read") != names.end());
  REQUIRE(std::find(names.begin(), names.end(), "grep") != names.end());
  REQUIRE(std::find(names.begin(), names.end(), "write") == names.end());
}

TEST_CASE("subagent system prompt adds read-only guidance", "[ava_orchestration]") {
  const auto review_prompt = ava::orchestration::build_subagent_system_prompt("review");
  REQUIRE_THAT(review_prompt, Catch::Matchers::ContainsSubstring("read-only specialist mode"));

  const auto general_prompt = ava::orchestration::build_subagent_system_prompt("general");
  REQUIRE_THAT(general_prompt, Catch::Matchers::ContainsSubstring("Stay focused on the delegated task"));
}

TEST_CASE("effective subagent catalog merges builtins and overrides", "[ava_orchestration]") {
  ava::config::AgentsConfig config;
  config.defaults.model = "openai/gpt-5.3-codex";
  config.agents.push_back({
      "review",
      ava::config::AgentOverride{
          .enabled = true,
          .temperature = 0.1F,
      },
  });
  config.agents.push_back({
      "scout",
      ava::config::AgentOverride{
          .enabled = false,
      },
  });
  config.agents.push_back({
      "custom-specialist",
      ava::config::AgentOverride{
          .description = "custom from config",
          .max_turns = 7U,
      },
  });

  const auto defs = ava::orchestration::effective_subagent_definitions(config);

  REQUIRE(std::find_if(defs.begin(), defs.end(), [](const auto& def) { return def.id == "general" && def.built_in; }) != defs.end());
  REQUIRE(std::find_if(defs.begin(), defs.end(), [](const auto& def) {
            return def.id == "review"
                && def.runtime_profile == ava::orchestration::SubAgentRuntimeProfile::ReadOnly
                && def.configured;
          })
          != defs.end());
  REQUIRE(std::find_if(defs.begin(), defs.end(), [](const auto& def) { return def.id == "scout"; }) == defs.end());
  REQUIRE(std::find_if(defs.begin(), defs.end(), [](const auto& def) {
            return def.id == "custom-specialist" && !def.built_in && def.max_turns == 7U;
          })
          != defs.end());
}

TEST_CASE("parse_model_spec follows provider and model-catalog seam", "[ava_orchestration]") {
  const auto explicit_known = ava::orchestration::parse_model_spec("openai/gpt-5-mini");
  REQUIRE(explicit_known.provider == "openai");
  REQUIRE(explicit_known.model == "gpt-5-mini");

  const auto explicit_cli = ava::orchestration::parse_model_spec("cli:claude/opus");
  REQUIRE(explicit_cli.provider == "cli:claude");
  REQUIRE(explicit_cli.model == "opus");

  const auto empty_known = ava::orchestration::parse_model_spec("openai/");
  REQUIRE(empty_known.provider == "openrouter");
  REQUIRE(empty_known.model == "openai/");

  const auto empty_cli = ava::orchestration::parse_model_spec("cli:/");
  REQUIRE(empty_cli.provider == "openrouter");
  REQUIRE(empty_cli.model == "cli:/");

  const auto from_catalog = ava::orchestration::parse_model_spec("opus");
  REQUIRE(from_catalog.provider == "anthropic");
  REQUIRE(from_catalog.model == "claude-opus-4-6");

  const auto fallback = ava::orchestration::parse_model_spec("unknown-model");
  REQUIRE(fallback.provider == "openrouter");
  REQUIRE(fallback.model == "unknown-model");
}

TEST_CASE("stack and task DTO contracts are usable in leaf tests", "[ava_orchestration]") {
  ava::orchestration::AgentStackConfig stack_config;
  stack_config.max_turns = 5;
  stack_config.auto_approve = true;
  REQUIRE(stack_config.max_turns == 5);
  REQUIRE(stack_config.auto_approve);

  ava::orchestration::AgentRunResult run_result;
  run_result.success = true;
  run_result.turns = 2;
  REQUIRE(run_result.success);
  REQUIRE(run_result.turns == 2);

  ava::orchestration::NoopTaskSpawner noop;
  const auto noop_result = noop.spawn("hello");
  REQUIRE(noop_result.output == std::optional<std::string>{"hello"});
  REQUIRE(!noop_result.error.has_value());
  REQUIRE(noop_result.ok());
  REQUIRE(noop_result.session_id == "noop-session");

  RecordingSpawner spawner;
  const auto named = spawner.spawn_named("review", "inspect");
  REQUIRE(named.output == std::optional<std::string>{"spawn:inspect"});
  REQUIRE(!named.error.has_value());
  REQUIRE(spawner.last_prompt == "inspect");

  const auto named_again = spawner.spawn_named("review", "inspect-bg");
  REQUIRE(named_again.session_id == "session-1");
}

TEST_CASE("interactive bridge tracks approval/question/plan request lifecycle", "[ava_orchestration]") {
  ava::orchestration::InteractiveBridge bridge(
      "run-42",
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::ApprovalRequestPayload&) {
        return ava::orchestration::ApprovalResolution{
            .approval = ava::tools::ToolApproval{.kind = ava::tools::ToolApprovalKind::Allowed},
            .state = ava::control_plane::InteractiveRequestState::Resolved,
        };
      },
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::QuestionRequestPayload&) {
        return ava::orchestration::QuestionResolution{
            .answer = std::optional<std::string>{"yes"},
            .state = ava::control_plane::InteractiveRequestState::Resolved,
        };
      },
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::PlanRequestPayload&) {
        return ava::orchestration::PlanResolution{
            .accepted = true,
            .state = ava::control_plane::InteractiveRequestState::Resolved,
        };
      }
  );

  const auto approval = bridge.request_approval(
      ava::types::ToolCall{.id = "call-1", .name = "write", .arguments = nlohmann::json::object()},
      ava::tools::PermissionInspection{
          .action = ava::tools::PermissionAction::Ask,
          .reason = "approval",
          .risk_level = "high",
      }
  );
  REQUIRE(approval.kind == ava::tools::ToolApprovalKind::Allowed);

  const auto answer = bridge.request_question("continue?", {"yes", "no"});
  REQUIRE(answer == std::optional<std::string>{"yes"});

  const auto accepted = bridge.request_plan(nlohmann::json{{"steps", nlohmann::json::array()}});
  REQUIRE(accepted);

  REQUIRE_FALSE(bridge.approval_requests().current_pending().has_value());
  REQUIRE_FALSE(bridge.question_requests().current_pending().has_value());
  REQUIRE_FALSE(bridge.plan_requests().current_pending().has_value());

  const auto approval_state = bridge.approval_requests().request_by_id("approval-1");
  REQUIRE(approval_state.has_value());
  REQUIRE(approval_state->run_id == std::optional<std::string>{"run-42"});
  REQUIRE(approval_state->state == ava::control_plane::InteractiveRequestState::Resolved);

  const auto question_state = bridge.question_requests().request_by_id("question-1");
  REQUIRE(question_state.has_value());
  REQUIRE(question_state->run_id == std::optional<std::string>{"run-42"});
  REQUIRE(question_state->state == ava::control_plane::InteractiveRequestState::Resolved);

  const auto plan_state = bridge.plan_requests().request_by_id("plan-1");
  REQUIRE(plan_state.has_value());
  REQUIRE(plan_state->run_id == std::optional<std::string>{"run-42"});
  REQUIRE(plan_state->state == ava::control_plane::InteractiveRequestState::Resolved);
}

TEST_CASE("interactive bridge fails closed when resolvers are missing", "[ava_orchestration]") {
  ava::orchestration::InteractiveBridge bridge("run-closed");

  const auto approval = bridge.request_approval(
      ava::types::ToolCall{.id = "call-1", .name = "write", .arguments = nlohmann::json::object()},
      ava::tools::PermissionInspection{
          .action = ava::tools::PermissionAction::Ask,
          .reason = "approval",
          .risk_level = "high",
      }
  );
  REQUIRE(approval.kind == ava::tools::ToolApprovalKind::Rejected);
  REQUIRE(approval.reason.has_value());

  REQUIRE_FALSE(bridge.request_question("proceed?", {"yes", "no"}).has_value());
  REQUIRE_FALSE(bridge.request_plan(nlohmann::json{{"steps", nlohmann::json::array()}}));

  const auto approval_state = bridge.approval_requests().request_by_id("approval-1");
  REQUIRE(approval_state.has_value());
  REQUIRE(approval_state->state == ava::control_plane::InteractiveRequestState::Cancelled);

  const auto question_state = bridge.question_requests().request_by_id("question-1");
  REQUIRE(question_state.has_value());
  REQUIRE(question_state->state == ava::control_plane::InteractiveRequestState::Cancelled);

  const auto plan_state = bridge.plan_requests().request_by_id("plan-1");
  REQUIRE(plan_state.has_value());
  REQUIRE(plan_state->state == ava::control_plane::InteractiveRequestState::Cancelled);
}

TEST_CASE("interactive bridge cancels pending request when resolver throws", "[ava_orchestration]") {
  ava::orchestration::InteractiveBridge bridge(
      "run-throw",
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::ApprovalRequestPayload&) -> ava::orchestration::ApprovalResolution {
        throw std::runtime_error("resolver exploded");
      }
  );

  REQUIRE_THROWS_WITH(
      bridge.request_approval(
          ava::types::ToolCall{.id = "call-1", .name = "write", .arguments = nlohmann::json::object()},
          ava::tools::PermissionInspection{
              .action = ava::tools::PermissionAction::Ask,
              .reason = "approval",
              .risk_level = "high",
          }
      ),
      Catch::Matchers::ContainsSubstring("resolver exploded")
  );

  REQUIRE_FALSE(bridge.approval_requests().current_pending().has_value());
  const auto state = bridge.approval_requests().request_by_id("approval-1");
  REQUIRE(state.has_value());
  REQUIRE(state->state == ava::control_plane::InteractiveRequestState::Cancelled);
}

TEST_CASE("interactive bridge cancels question and plan requests when resolvers throw", "[ava_orchestration]") {
  ava::orchestration::InteractiveBridge bridge(
      "run-throw-secondary",
      nullptr,
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::QuestionRequestPayload&) -> ava::orchestration::QuestionResolution {
        throw std::runtime_error("question resolver exploded");
      },
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::PlanRequestPayload&) -> ava::orchestration::PlanResolution {
        throw std::runtime_error("plan resolver exploded");
      }
  );

  REQUIRE_THROWS_WITH(
      bridge.request_question("continue?", {"yes", "no"}),
      Catch::Matchers::ContainsSubstring("question resolver exploded")
  );
  REQUIRE_THROWS_WITH(
      bridge.request_plan(nlohmann::json{{"steps", nlohmann::json::array()}}),
      Catch::Matchers::ContainsSubstring("plan resolver exploded")
  );

  const auto question_state = bridge.question_requests().request_by_id("question-1");
  REQUIRE(question_state.has_value());
  REQUIRE(question_state->state == ava::control_plane::InteractiveRequestState::Cancelled);

  const auto plan_state = bridge.plan_requests().request_by_id("plan-1");
  REQUIRE(plan_state.has_value());
  REQUIRE(plan_state->state == ava::control_plane::InteractiveRequestState::Cancelled);
}

TEST_CASE("interactive bridge rejects resolver returning pending terminal state", "[ava_orchestration]") {
  ava::orchestration::InteractiveBridge bridge(
      "run-pending",
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::ApprovalRequestPayload&) {
        return ava::orchestration::ApprovalResolution{
            .approval = ava::tools::ToolApproval{.kind = ava::tools::ToolApprovalKind::Allowed},
            .state = ava::control_plane::InteractiveRequestState::Pending,
        };
      }
  );

  REQUIRE_THROWS_WITH(
      bridge.request_approval(
          ava::types::ToolCall{.id = "call-1", .name = "write", .arguments = nlohmann::json::object()},
          ava::tools::PermissionInspection{
              .action = ava::tools::PermissionAction::Ask,
              .reason = "approval",
              .risk_level = "high",
          }
      ),
      Catch::Matchers::ContainsSubstring("invalid terminal state")
  );

  const auto state = bridge.approval_requests().request_by_id("approval-1");
  REQUIRE(state.has_value());
  REQUIRE(state->state == ava::control_plane::InteractiveRequestState::Cancelled);
}

TEST_CASE("interactive bridge rejects question and plan pending terminal states", "[ava_orchestration]") {
  ava::orchestration::InteractiveBridge bridge(
      "run-pending-secondary",
      nullptr,
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::QuestionRequestPayload&) {
        return ava::orchestration::QuestionResolution{
            .answer = std::optional<std::string>{"yes"},
            .state = ava::control_plane::InteractiveRequestState::Pending,
        };
      },
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::PlanRequestPayload&) {
        return ava::orchestration::PlanResolution{
            .accepted = true,
            .state = ava::control_plane::InteractiveRequestState::Pending,
        };
      }
  );

  REQUIRE_THROWS_WITH(
      bridge.request_question("continue?", {"yes", "no"}),
      Catch::Matchers::ContainsSubstring("invalid terminal state")
  );
  REQUIRE_THROWS_WITH(
      bridge.request_plan(nlohmann::json{{"steps", nlohmann::json::array()}}),
      Catch::Matchers::ContainsSubstring("invalid terminal state")
  );

  const auto question_state = bridge.question_requests().request_by_id("question-1");
  REQUIRE(question_state.has_value());
  REQUIRE(question_state->state == ava::control_plane::InteractiveRequestState::Cancelled);

  const auto plan_state = bridge.plan_requests().request_by_id("plan-1");
  REQUIRE(plan_state.has_value());
  REQUIRE(plan_state->state == ava::control_plane::InteractiveRequestState::Cancelled);
}

TEST_CASE("interactive bridge adapter settlements fail closed for stale requests", "[ava_orchestration]") {
  ava::orchestration::InteractiveBridge bridge("run-adapter");

  const auto approval = bridge.register_approval_for_adapter();
  const auto resolved = bridge.approve_from_adapter(approval.request_id);
  REQUIRE(resolved.has_value());
  REQUIRE(resolved->state == ava::control_plane::InteractiveRequestState::Resolved);

  const auto record = bridge.adapter_resolution_for(approval.request_id);
  REQUIRE(record.has_value());
  REQUIRE(record->kind == ava::control_plane::InteractiveRequestKind::Approval);
  REQUIRE(record->state == ava::control_plane::InteractiveRequestState::Resolved);
  REQUIRE(record->approval.has_value());
  REQUIRE(record->approval->kind == ava::tools::ToolApprovalKind::Allowed);

  REQUIRE_FALSE(bridge.reject_from_adapter(approval.request_id, "too late").has_value());
  REQUIRE_FALSE(bridge.approve_from_adapter("approval-missing").has_value());
}

TEST_CASE("interactive bridge settle is idempotent for same terminal state and rejects mismatched resettle", "[ava_orchestration]") {
  ava::orchestration::InteractiveBridge bridge(
      "run-idempotent",
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::ApprovalRequestPayload&) {
        return ava::orchestration::ApprovalResolution{
            .approval = ava::tools::ToolApproval{.kind = ava::tools::ToolApprovalKind::Allowed},
            .state = ava::control_plane::InteractiveRequestState::Resolved,
        };
      }
  );

  const auto approval = bridge.request_approval(
      ava::types::ToolCall{.id = "call-1", .name = "write", .arguments = nlohmann::json::object()},
      ava::tools::PermissionInspection{
          .action = ava::tools::PermissionAction::Ask,
          .reason = "approval",
          .risk_level = "high",
      }
  );
  REQUIRE(approval.kind == ava::tools::ToolApprovalKind::Allowed);

  REQUIRE_NOTHROW(bridge.settle_request_for_testing(
      ava::control_plane::InteractiveRequestKind::Approval,
      "approval-1",
      ava::control_plane::InteractiveRequestState::Resolved
  ));

  REQUIRE_THROWS_WITH(
      bridge.settle_request_for_testing(
          ava::control_plane::InteractiveRequestKind::Approval,
          "approval-1",
          ava::control_plane::InteractiveRequestState::TimedOut
      ),
      Catch::Matchers::ContainsSubstring("timeout transition failed")
  );
}

TEST_CASE("interactive bridge terminal cancelled/timedout outcomes are fail-closed", "[ava_orchestration]") {
  ava::orchestration::InteractiveBridge cancelled_bridge(
      "run-cancelled",
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::ApprovalRequestPayload&) {
        return ava::orchestration::ApprovalResolution{
            .approval = ava::tools::ToolApproval{.kind = ava::tools::ToolApprovalKind::Allowed},
            .state = ava::control_plane::InteractiveRequestState::Cancelled,
        };
      },
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::QuestionRequestPayload&) {
        return ava::orchestration::QuestionResolution{
            .answer = std::optional<std::string>{"yes"},
            .state = ava::control_plane::InteractiveRequestState::Cancelled,
        };
      },
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::PlanRequestPayload&) {
        return ava::orchestration::PlanResolution{
            .accepted = true,
            .state = ava::control_plane::InteractiveRequestState::Cancelled,
        };
      }
  );

  const auto cancelled_approval = cancelled_bridge.request_approval(
      ava::types::ToolCall{.id = "call-1", .name = "write", .arguments = nlohmann::json::object()},
      ava::tools::PermissionInspection{
          .action = ava::tools::PermissionAction::Ask,
          .reason = "approval",
          .risk_level = "high",
      }
  );
  REQUIRE(cancelled_approval.kind == ava::tools::ToolApprovalKind::Rejected);
  REQUIRE_FALSE(cancelled_bridge.request_question("continue?", {"yes", "no"}).has_value());
  REQUIRE_FALSE(cancelled_bridge.request_plan(nlohmann::json{{"steps", nlohmann::json::array()}}));

  ava::orchestration::InteractiveBridge timed_out_bridge(
      "run-timeout",
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::ApprovalRequestPayload&) {
        return ava::orchestration::ApprovalResolution{
            .approval = ava::tools::ToolApproval{.kind = ava::tools::ToolApprovalKind::Allowed},
            .state = ava::control_plane::InteractiveRequestState::TimedOut,
        };
      },
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::QuestionRequestPayload&) {
        return ava::orchestration::QuestionResolution{
            .answer = std::optional<std::string>{"yes"},
            .state = ava::control_plane::InteractiveRequestState::TimedOut,
        };
      },
      [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::PlanRequestPayload&) {
        return ava::orchestration::PlanResolution{
            .accepted = true,
            .state = ava::control_plane::InteractiveRequestState::TimedOut,
        };
      }
  );

  const auto timed_out_approval = timed_out_bridge.request_approval(
      ava::types::ToolCall{.id = "call-2", .name = "write", .arguments = nlohmann::json::object()},
      ava::tools::PermissionInspection{
          .action = ava::tools::PermissionAction::Ask,
          .reason = "approval",
          .risk_level = "high",
      }
  );
  REQUIRE(timed_out_approval.kind == ava::tools::ToolApprovalKind::Rejected);
  REQUIRE_FALSE(timed_out_bridge.request_question("continue?", {"yes", "no"}).has_value());
  REQUIRE_FALSE(timed_out_bridge.request_plan(nlohmann::json{{"steps", nlohmann::json::array()}}));
}

TEST_CASE("run controller issues unique run leases and cooperative cancellation state", "[ava_orchestration]") {
  ava::orchestration::RunController controller("session-abc");

  const auto first = controller.begin_run();
  const auto second = controller.begin_run();

  REQUIRE(first.run_id == "session-abc:run:1");
  REQUIRE(second.run_id == "session-abc:run:2");
  REQUIRE_FALSE(first.token.is_cancelled());
  REQUIRE_FALSE(first.handle.is_cancelled());

  first.handle.cancel();
  REQUIRE(first.token.is_cancelled());
  REQUIRE(first.handle.is_cancelled());
  REQUIRE_FALSE(second.token.is_cancelled());

  ava::orchestration::RunCancellationToken default_token;
  ava::orchestration::RunCancellationHandle default_handle;
  REQUIRE_FALSE(default_token.is_cancelled());
  REQUIRE_FALSE(default_token.is_deadline_expired());
  REQUIRE_FALSE(default_handle);
  REQUIRE_FALSE(default_handle.is_cancelled());
  REQUIRE_FALSE(default_handle.is_deadline_expired());
  default_handle.cancel();
  REQUIRE_FALSE(default_handle.is_cancelled());

  const auto future = controller.begin_run(std::chrono::steady_clock::now() + std::chrono::milliseconds(20));
  REQUIRE(future.handle);
  REQUIRE_FALSE(future.token.is_deadline_expired());
  REQUIRE_FALSE(future.token.is_cancelled());
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  REQUIRE(future.token.is_deadline_expired());
  REQUIRE(future.token.is_cancelled());

  const auto expired = controller.begin_run(std::chrono::steady_clock::now() - std::chrono::milliseconds(1));
  REQUIRE(expired.token.is_deadline_expired());
  REQUIRE(expired.handle.is_deadline_expired());
  REQUIRE(expired.token.is_cancelled());
}

TEST_CASE("runtime composition interactive bridge correlates approvals to active run id", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  std::optional<ava::control_plane::InteractiveRequestHandle> observed_handle;
  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .resume_latest = false,
      .session_id = std::nullopt,
      .selection = ava::orchestration::RuntimeSelectionOptions{
          .provider = "mock",
          .model = "mock-run-id",
          .max_turns = 2,
          .max_turns_explicit = true,
      },
      .auto_approve = false,
      .allowed_tools = std::nullopt,
      .system_prompt_preamble = std::nullopt,
      .approval_resolver = [&](const ava::control_plane::InteractiveRequestHandle& handle,
                               const ava::orchestration::ApprovalRequestPayload&) {
        observed_handle = handle;
        return ava::orchestration::ApprovalResolution{
            .approval = ava::tools::ToolApproval{.kind = ava::tools::ToolApprovalKind::Allowed},
            .state = ava::control_plane::InteractiveRequestState::Resolved,
        };
      },
      .question_resolver = nullptr,
      .plan_resolver = nullptr,
      .provider_override = ava::llm::create_mock_provider(
          "mock-run-id",
          std::vector<ava::llm::LlmResponse>{ava::llm::LlmResponse{
              .content = "calling write",
              .tool_calls = {ava::types::ToolCall{
                  .id = "call-1",
                  .name = "write",
                  .arguments = nlohmann::json{{"path", (root / "approval-test.txt").string()}, {"content", "ok"}},
              }},
              .usage = std::nullopt,
              .thinking = std::nullopt,
          },
                                             ava::llm::LlmResponse{
                                                 .content = "done",
                                                 .tool_calls = {},
                                                 .usage = std::nullopt,
                                                 .thinking = std::nullopt,
                                             }}
      ),
      .provider_factory = nullptr,
      .credentials_override = std::nullopt,
  });

  const auto run_lease = composition.run_controller->begin_run();
  composition.interactive_bridge->set_run_id(run_lease.run_id);
  const auto run_result = composition.runtime->run(
      composition.session,
      ava::agent::AgentRunInput{
          .goal = "exercise approval",
          .queue = &composition.queue,
          .run_id = run_lease.run_id,
          .is_cancelled = [&] {
            return run_lease.token.is_cancelled();
          },
          .stream = true,
      }
  );
  composition.interactive_bridge->set_run_id(std::nullopt);

  REQUIRE(run_result.reason == ava::agent::AgentCompletionReason::Completed);

  REQUIRE(observed_handle.has_value());
  REQUIRE(observed_handle->run_id == std::optional<std::string>{run_lease.run_id});
  REQUIRE(observed_handle->request_id == "approval-1");

  const auto stored = composition.interactive_bridge->approval_requests().request_by_id(observed_handle->request_id);
  REQUIRE(stored.has_value());
  REQUIRE(stored->run_id == std::optional<std::string>{run_lease.run_id});
  REQUIRE(stored->state == ava::control_plane::InteractiveRequestState::Resolved);

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner persists terminal metadata when child runtime errors", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "throwing-mock",
      .max_turns = 4,
      .max_spawns = 1,
      .auto_approve = true,
      .parent_depth = 1,
      .parent_session_id = "parent-session",
      .provider_factory = [](const ava::orchestration::ResolvedRuntimeSelection&) {
        return std::make_shared<ThrowingStreamProvider>();
      },
  });

  const auto result = spawner.spawn_named("review", "inspect failure");
  REQUIRE_FALSE(result.ok());
  REQUIRE(result.error.has_value());
  REQUIRE_THAT(*result.error, Catch::Matchers::ContainsSubstring("provider exploded"));

  const auto summaries = spawner.child_terminal_summaries();
  REQUIRE(summaries.size() == 1);
  REQUIRE(summaries.front().completion_reason == "error");
  REQUIRE(summaries.front().error == std::optional<std::string>{"provider exploded"});
  REQUIRE(spawner.active_child_runs().empty());

  ava::session::SessionManager sessions(root / "sessions.db");
  const auto stored = sessions.get(summaries.front().session_id);
  REQUIRE(stored.has_value());
  const auto& metadata = stored->metadata["orchestration"]["subagent_run"];
  REQUIRE(metadata["run_id"] == summaries.front().run_id);
  REQUIRE(metadata["agent_type"] == "review");
  REQUIRE(metadata["depth"] == 2);
  REQUIRE(metadata["completion_reason"] == "error");
  REQUIRE(metadata["watchdog_timed_out"] == false);
  REQUIRE(metadata["turns_used"] == summaries.front().turns_used);
  REQUIRE(metadata["error"] == "provider exploded");

  std::filesystem::remove_all(root);
}

TEST_CASE("shared runtime composition assembles reusable blocking runtime", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .resume_latest = false,
      .session_id = std::nullopt,
      .selection = ava::orchestration::RuntimeSelectionOptions{
          .provider = "mock",
          .model = "mock-model",
          .max_turns = 4,
          .max_turns_explicit = true,
      },
      .auto_approve = true,
      .allowed_tools = std::vector<std::string>{"glob", "read"},
      .system_prompt_preamble = std::nullopt,
      .provider_override = ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"done"}),
      .provider_factory = nullptr,
      .credentials_override = std::nullopt,
  });

  REQUIRE(composition.selection.provider == "mock");
  REQUIRE(composition.selection.model == "mock-model");
  REQUIRE(composition.interactive_bridge != nullptr);
  REQUIRE(composition.registry->has_tool("read"));
  REQUIRE(!composition.registry->has_tool("write"));

  const auto prompt = composition.runtime->build_system_prompt();
  REQUIRE_THAT(prompt, Catch::Matchers::ContainsSubstring("- read:"));
  REQUIRE_THAT(prompt, Catch::Matchers::ContainsSubstring("- glob:"));

  const auto run_result = composition.runtime->run(
      composition.session,
      ava::agent::AgentRunInput{.goal = "summarize", .queue = &composition.queue}
  );

  REQUIRE(run_result.reason == ava::agent::AgentCompletionReason::Completed);
  REQUIRE(run_result.final_response == "done");

  composition.save_session();
  const auto reloaded = composition.sessions->get(composition.session.id);
  REQUIRE(reloaded.has_value());

  std::filesystem::remove_all(root);
}

TEST_CASE("runtime composition wires MCP tools into the shared runtime path", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto mcp_state = std::make_shared<ScriptedMcpTransport::State>();
  mcp_state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  mcp_state->inbound.push_back(ava::mcp::make_result(
      2,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", "echo"}, {"description", "Echo"}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
  ));
  mcp_state->inbound.push_back(ava::mcp::make_result(
      3,
      nlohmann::json{{"content", nlohmann::json::array({nlohmann::json{{"type", "text"}, {"text", "mcp-runtime"}}})}, {"isError", false}}
  ));

  ava::mcp::McpServerConfig mcp_server;
  mcp_server.name = "mock";
  mcp_server.transport_type = ava::mcp::TransportType::Stdio;
  mcp_server.stdio.command = "unused";

  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .resume_latest = false,
      .session_id = std::nullopt,
      .selection = ava::orchestration::RuntimeSelectionOptions{
          .provider = "mock",
          .model = "mock-mcp-runtime",
          .max_turns = 4,
          .max_turns_explicit = true,
      },
      .auto_approve = true,
      .allowed_tools = std::nullopt,
      .system_prompt_preamble = std::nullopt,
      .approval_resolver = [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::ApprovalRequestPayload&) {
        return ava::orchestration::ApprovalResolution{
            .approval = ava::tools::ToolApproval::allowed(),
            .state = ava::control_plane::InteractiveRequestState::Resolved,
        };
      },
      .provider_override = ava::llm::create_mock_provider(
          "mock-mcp-runtime",
          std::vector<ava::llm::LlmResponse>{
              ava::llm::LlmResponse{
                  .content = "use MCP tool",
                  .tool_calls = {ava::types::ToolCall{
                      .id = "call_mcp_runtime",
                      .name = "mcp_mock_echo",
                      .arguments = nlohmann::json{{"text", "mcp-runtime"}},
                  }},
                  .usage = std::nullopt,
                  .thinking = std::nullopt,
              },
              ava::llm::LlmResponse{
                  .content = "done",
                  .tool_calls = {},
                  .usage = std::nullopt,
                  .thinking = std::nullopt,
              },
          }
      ),
      .provider_factory = nullptr,
      .credentials_override = std::nullopt,
      .mcp_config_override = ava::mcp::McpConfig{.servers = {mcp_server}},
      .mcp_transport_factory = [mcp_state](const ava::mcp::McpServerConfig&) {
        return std::make_unique<ScriptedMcpTransport>(mcp_state);
      },
  });

  REQUIRE(composition.registry->has_tool("mcp_mock_echo"));
  const auto source = composition.registry->tool_source("mcp_mock_echo");
  REQUIRE(source.has_value());
  REQUIRE(source->kind == ava::tools::ToolSourceKind::MCP);
  REQUIRE(source->detail == "mock");

  const auto run_result = composition.runtime->run(
      composition.session,
      ava::agent::AgentRunInput{.goal = "exercise MCP bridge", .queue = &composition.queue}
  );
  REQUIRE(run_result.reason == ava::agent::AgentCompletionReason::Completed);
  REQUIRE(run_result.final_response == "done");

  REQUIRE(mcp_state->outbound.size() == 4);
  REQUIRE(mcp_state->outbound.at(0).method == std::optional<std::string>{"initialize"});
  REQUIRE(mcp_state->outbound.at(1).method == std::optional<std::string>{"notifications/initialized"});
  REQUIRE(mcp_state->outbound.at(2).method == std::optional<std::string>{"tools/list"});
  REQUIRE(mcp_state->outbound.at(3).method == std::optional<std::string>{"tools/call"});
  REQUIRE(mcp_state->outbound.at(3).params.at("name") == "echo");

  REQUIRE(composition.session.messages.size() == 4);
  REQUIRE(composition.session.messages.at(2).role == "tool");
  const auto tool_payload = nlohmann::json::parse(composition.session.messages.at(2).content);
  REQUIRE(tool_payload.at("is_error") == false);
  REQUIRE(tool_payload.at("content") == "mcp-runtime");

  std::filesystem::remove_all(root);
}

TEST_CASE("runtime composition tolerates MCP server initialization failure", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto stable_state = std::make_shared<ScriptedMcpTransport::State>();
  stable_state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  stable_state->inbound.push_back(ava::mcp::make_result(
      2,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", "echo"}, {"description", "Echo"}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
  ));

  ava::mcp::McpServerConfig stable;
  stable.name = "stable";
  stable.transport_type = ava::mcp::TransportType::Stdio;
  stable.stdio.command = "unused";

  ava::mcp::McpServerConfig broken = stable;
  broken.name = "broken";

  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .resume_latest = false,
      .session_id = std::nullopt,
      .selection = ava::orchestration::RuntimeSelectionOptions{
          .provider = "mock",
          .model = "mock-mcp-failure-tolerant",
          .max_turns = 2,
          .max_turns_explicit = true,
      },
      .auto_approve = true,
      .allowed_tools = std::nullopt,
      .system_prompt_preamble = std::nullopt,
      .provider_override = ava::llm::create_mock_provider("mock-mcp-failure-tolerant", std::vector<std::string>{"done"}),
      .provider_factory = nullptr,
      .credentials_override = std::nullopt,
      .mcp_config_override = ava::mcp::McpConfig{.servers = {stable, broken}},
      .mcp_transport_factory = [stable_state](const ava::mcp::McpServerConfig& server) {
        if(server.name == "broken") {
          throw std::runtime_error("broken MCP server failed to start");
        }
        return std::make_unique<ScriptedMcpTransport>(stable_state);
      },
  });

  REQUIRE(composition.mcp_manager != nullptr);
  REQUIRE(composition.mcp_manager->server_count() == 1);
  REQUIRE(composition.mcp_manager->has_server("stable"));
  REQUIRE_FALSE(composition.mcp_manager->has_server("broken"));

  const auto broken_report = composition.mcp_manager->server_report("broken");
  REQUIRE(broken_report.has_value());
  REQUIRE_FALSE(broken_report->connected);
  REQUIRE(broken_report->error.has_value());
  REQUIRE_THAT(*broken_report->error, Catch::Matchers::ContainsSubstring("failed to start"));

  REQUIRE(composition.registry->has_tool("mcp_stable_echo"));
  REQUIRE_FALSE(composition.registry->has_tool("mcp_broken_echo"));

  const auto run_result = composition.runtime->run(
      composition.session,
      ava::agent::AgentRunInput{.goal = "continue with remaining MCP servers", .queue = &composition.queue}
  );
  REQUIRE(run_result.reason == ava::agent::AgentCompletionReason::Completed);
  REQUIRE(run_result.final_response == "done");

  std::filesystem::remove_all(root);
}

TEST_CASE("runtime composition isolates MCP server with blank tool names", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto stable_state = std::make_shared<ScriptedMcpTransport::State>();
  stable_state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  stable_state->inbound.push_back(ava::mcp::make_result(
      2,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", "echo"}, {"description", "Echo"}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
  ));

  auto malformed_state = std::make_shared<ScriptedMcpTransport::State>();
  malformed_state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  malformed_state->inbound.push_back(ava::mcp::make_result(
      2,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", "   "}, {"description", "Malformed"}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
  ));

  ava::mcp::McpServerConfig stable;
  stable.name = "stable";
  stable.transport_type = ava::mcp::TransportType::Stdio;
  stable.stdio.command = "unused";

  ava::mcp::McpServerConfig malformed = stable;
  malformed.name = "malformed";

  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .resume_latest = false,
      .session_id = std::nullopt,
      .selection = ava::orchestration::RuntimeSelectionOptions{
          .provider = "mock",
          .model = "mock-mcp-blank-tool-name",
          .max_turns = 2,
          .max_turns_explicit = true,
      },
      .auto_approve = true,
      .allowed_tools = std::nullopt,
      .system_prompt_preamble = std::nullopt,
      .provider_override = ava::llm::create_mock_provider("mock-mcp-blank-tool-name", std::vector<std::string>{"done"}),
      .provider_factory = nullptr,
      .credentials_override = std::nullopt,
      .mcp_config_override = ava::mcp::McpConfig{.servers = {stable, malformed}},
      .mcp_transport_factory = [stable_state, malformed_state](const ava::mcp::McpServerConfig& server) {
        if(server.name == "stable") {
          return std::make_unique<ScriptedMcpTransport>(stable_state);
        }
        if(server.name == "malformed") {
          return std::make_unique<ScriptedMcpTransport>(malformed_state);
        }
        throw std::runtime_error("unexpected MCP server: " + server.name);
      },
  });

  REQUIRE(composition.mcp_manager != nullptr);
  REQUIRE(composition.mcp_manager->server_count() == 1);
  REQUIRE(composition.mcp_manager->has_server("stable"));
  REQUIRE_FALSE(composition.mcp_manager->has_server("malformed"));

  const auto malformed_report = composition.mcp_manager->server_report("malformed");
  REQUIRE(malformed_report.has_value());
  REQUIRE_FALSE(malformed_report->connected);
  REQUIRE(malformed_report->error.has_value());
  REQUIRE_THAT(*malformed_report->error, Catch::Matchers::ContainsSubstring("empty or blank"));

  REQUIRE(composition.registry->has_tool("mcp_stable_echo"));
  REQUIRE_FALSE(composition.registry->has_tool("mcp_malformed_unnamed"));

  const auto run_result = composition.runtime->run(
      composition.session,
      ava::agent::AgentRunInput{.goal = "keep running with healthy MCP servers", .queue = &composition.queue}
  );
  REQUIRE(run_result.reason == ava::agent::AgentCompletionReason::Completed);
  REQUIRE(run_result.final_response == "done");

  REQUIRE(stable_state->outbound.size() == 3);
  REQUIRE(malformed_state->outbound.size() == 3);

  std::filesystem::remove_all(root);
}

TEST_CASE("runtime composition skips project-local MCP config when workspace is untrusted", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ScopedEnvVar home("HOME", (root / "home").string());
  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "xdg-config").string());

  const auto global_config_path = ava::config::mcp_config_path();
  std::filesystem::create_directories(global_config_path.parent_path());
  {
    std::ofstream output(global_config_path);
    output << nlohmann::json{
                  {"servers", nlohmann::json::array({nlohmann::json{
                                  {"name", "global"},
                                  {"transport", nlohmann::json{{"type", "stdio"}, {"command", "global-server"}}},
                              }})},
              }
                  .dump(2);
  }

  const auto project_config_path = ava::config::project_mcp_config_path(root);
  std::filesystem::create_directories(project_config_path.parent_path());
  {
    std::ofstream output(project_config_path);
    output << nlohmann::json{
                  {"servers", nlohmann::json::array({nlohmann::json{
                                  {"name", "project"},
                                  {"transport", nlohmann::json{{"type", "stdio"}, {"command", "project-server"}}},
                              }})},
              }
                  .dump(2);
  }

  ava::config::clear_trust_cache_for_tests();
  REQUIRE_FALSE(ava::config::is_project_trusted(root));

  auto global_state = std::make_shared<ScriptedMcpTransport::State>();
  global_state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  global_state->inbound.push_back(ava::mcp::make_result(
      2,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", "echo"}, {"description", "Echo"}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
  ));

  auto project_state = std::make_shared<ScriptedMcpTransport::State>();
  project_state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  project_state->inbound.push_back(ava::mcp::make_result(
      2,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", "echo"}, {"description", "Echo"}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
  ));

  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .resume_latest = false,
      .session_id = std::nullopt,
      .selection = ava::orchestration::RuntimeSelectionOptions{
          .provider = "mock",
          .model = "mock-untrusted-mcp-config",
          .max_turns = 2,
          .max_turns_explicit = true,
      },
      .auto_approve = true,
      .allowed_tools = std::nullopt,
      .system_prompt_preamble = std::nullopt,
      .provider_override = ava::llm::create_mock_provider("mock-untrusted-mcp-config", std::vector<std::string>{"done"}),
      .provider_factory = nullptr,
      .credentials_override = std::nullopt,
      .load_global_mcp_config = true,
      .mcp_transport_factory = [global_state, project_state](const ava::mcp::McpServerConfig& server) {
        if(server.name == "global") {
          return std::make_unique<ScriptedMcpTransport>(global_state);
        }
        if(server.name == "project") {
          return std::make_unique<ScriptedMcpTransport>(project_state);
        }
        throw std::runtime_error("unexpected MCP server: " + server.name);
      },
  });

  REQUIRE(composition.mcp_manager != nullptr);
  REQUIRE(composition.mcp_manager->server_count() == 1);
  REQUIRE(composition.mcp_manager->has_server("global"));
  REQUIRE_FALSE(composition.mcp_manager->has_server("project"));
  REQUIRE(composition.registry->has_tool("mcp_global_echo"));
  REQUIRE_FALSE(composition.registry->has_tool("mcp_project_echo"));
  REQUIRE(global_state->outbound.size() == 3);
  REQUIRE(project_state->outbound.empty());

  std::filesystem::remove_all(root);
  ava::config::clear_trust_cache_for_tests();
}

TEST_CASE("runtime composition loads project-local MCP config when workspace is trusted", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ScopedEnvVar home("HOME", (root / "home").string());
  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "xdg-config").string());

  const auto global_config_path = ava::config::mcp_config_path();
  std::filesystem::create_directories(global_config_path.parent_path());
  {
    std::ofstream output(global_config_path);
    output << nlohmann::json{
                  {"servers", nlohmann::json::array({nlohmann::json{
                                  {"name", "global"},
                                  {"transport", nlohmann::json{{"type", "stdio"}, {"command", "global-server"}}},
                              }})},
              }
                  .dump(2);
  }

  const auto project_config_path = ava::config::project_mcp_config_path(root);
  std::filesystem::create_directories(project_config_path.parent_path());
  {
    std::ofstream output(project_config_path);
    output << nlohmann::json{
                  {"servers", nlohmann::json::array({nlohmann::json{
                                  {"name", "project"},
                                  {"transport", nlohmann::json{{"type", "stdio"}, {"command", "project-server"}}},
                              }})},
              }
                  .dump(2);
  }

  ava::config::clear_trust_cache_for_tests();
  ava::config::trust_project(root);
  REQUIRE(ava::config::is_project_trusted(root));

  auto global_state = std::make_shared<ScriptedMcpTransport::State>();
  global_state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  global_state->inbound.push_back(ava::mcp::make_result(
      2,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", "echo"}, {"description", "Echo"}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
  ));

  auto project_state = std::make_shared<ScriptedMcpTransport::State>();
  project_state->inbound.push_back(ava::mcp::make_result(
      1,
      nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
  ));
  project_state->inbound.push_back(ava::mcp::make_result(
      2,
      nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", "echo"}, {"description", "Echo"}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
  ));

  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .resume_latest = false,
      .session_id = std::nullopt,
      .selection = ava::orchestration::RuntimeSelectionOptions{
          .provider = "mock",
          .model = "mock-trusted-mcp-config",
          .max_turns = 2,
          .max_turns_explicit = true,
      },
      .auto_approve = true,
      .allowed_tools = std::nullopt,
      .system_prompt_preamble = std::nullopt,
      .provider_override = ava::llm::create_mock_provider("mock-trusted-mcp-config", std::vector<std::string>{"done"}),
      .provider_factory = nullptr,
      .credentials_override = std::nullopt,
      .load_global_mcp_config = true,
      .mcp_transport_factory = [global_state, project_state](const ava::mcp::McpServerConfig& server) {
        if(server.name == "global") {
          return std::make_unique<ScriptedMcpTransport>(global_state);
        }
        if(server.name == "project") {
          return std::make_unique<ScriptedMcpTransport>(project_state);
        }
        throw std::runtime_error("unexpected MCP server: " + server.name);
      },
  });

  REQUIRE(composition.mcp_manager != nullptr);
  REQUIRE(composition.mcp_manager->server_count() == 2);
  REQUIRE(composition.mcp_manager->has_server("global"));
  REQUIRE(composition.mcp_manager->has_server("project"));
  REQUIRE(composition.registry->has_tool("mcp_global_echo"));
  REQUIRE(composition.registry->has_tool("mcp_project_echo"));
  REQUIRE(global_state->outbound.size() == 3);
  REQUIRE(project_state->outbound.size() == 3);

  std::filesystem::remove_all(root);
  ava::config::clear_trust_cache_for_tests();
}

TEST_CASE("runtime composition deduplicates MCP names and keeps project overrides deterministic", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ScopedEnvVar home("HOME", (root / "home").string());
  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "xdg-config").string());

  const auto global_config_path = ava::config::mcp_config_path();
  std::filesystem::create_directories(global_config_path.parent_path());
  {
    std::ofstream output(global_config_path);
    output << nlohmann::json{
                  {"servers",
                   nlohmann::json::array(
                       {
                           nlohmann::json{{"name", "shared"}, {"transport", nlohmann::json{{"type", "stdio"}, {"command", "global-shared-v1"}}}},
                           nlohmann::json{{"name", "shared"}, {"transport", nlohmann::json{{"type", "stdio"}, {"command", "global-shared-v2"}}}},
                           nlohmann::json{{"name", "global-only"}, {"transport", nlohmann::json{{"type", "stdio"}, {"command", "global-only"}}}},
                       }
                   )},
              }
                   .dump(2);
  }

  const auto project_config_path = ava::config::project_mcp_config_path(root);
  std::filesystem::create_directories(project_config_path.parent_path());
  {
    std::ofstream output(project_config_path);
    output << nlohmann::json{
                  {"servers",
                   nlohmann::json::array(
                       {
                           nlohmann::json{{"name", "shared"}, {"transport", nlohmann::json{{"type", "stdio"}, {"command", "project-shared-v1"}}}},
                           nlohmann::json{{"name", "shared"}, {"transport", nlohmann::json{{"type", "stdio"}, {"command", "project-shared-v2"}}}},
                           nlohmann::json{{"name", "project-only"}, {"transport", nlohmann::json{{"type", "stdio"}, {"command", "project-only"}}}},
                       }
                   )},
              }
                   .dump(2);
  }

  ava::config::clear_trust_cache_for_tests();
  ava::config::trust_project(root);
  REQUIRE(ava::config::is_project_trusted(root));

  const auto make_state = [] {
    auto state = std::make_shared<ScriptedMcpTransport::State>();
    state->inbound.push_back(ava::mcp::make_result(
        1,
        nlohmann::json{{"protocolVersion", "2024-11-05"}, {"capabilities", nlohmann::json{{"tools", nlohmann::json::object()}}}}
    ));
    state->inbound.push_back(ava::mcp::make_result(
        2,
        nlohmann::json{{"tools", nlohmann::json::array({nlohmann::json{{"name", "echo"}, {"description", "Echo"}, {"inputSchema", nlohmann::json{{"type", "object"}}}}})}}
    ));
    return state;
  };

  const auto shared_state = make_state();
  const auto global_only_state = make_state();
  const auto project_only_state = make_state();

  std::vector<std::string> transport_call_order;
  std::unordered_map<std::string, std::string> command_by_server;

  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .resume_latest = false,
      .session_id = std::nullopt,
      .selection = ava::orchestration::RuntimeSelectionOptions{
          .provider = "mock",
          .model = "mock-merged-mcp-config",
          .max_turns = 2,
          .max_turns_explicit = true,
      },
      .auto_approve = true,
      .allowed_tools = std::nullopt,
      .system_prompt_preamble = std::nullopt,
      .provider_override = ava::llm::create_mock_provider("mock-merged-mcp-config", std::vector<std::string>{"done"}),
      .provider_factory = nullptr,
      .credentials_override = std::nullopt,
      .load_global_mcp_config = true,
      .mcp_transport_factory = [&](const ava::mcp::McpServerConfig& server) {
        transport_call_order.push_back(server.name);
        command_by_server.insert_or_assign(server.name, server.stdio.command);
        if(server.name == "shared") {
          return std::make_unique<ScriptedMcpTransport>(shared_state);
        }
        if(server.name == "global-only") {
          return std::make_unique<ScriptedMcpTransport>(global_only_state);
        }
        if(server.name == "project-only") {
          return std::make_unique<ScriptedMcpTransport>(project_only_state);
        }
        throw std::runtime_error("unexpected MCP server: " + server.name);
      },
  });

  REQUIRE(composition.mcp_manager != nullptr);
  REQUIRE(composition.mcp_manager->server_count() == 3);
  REQUIRE(composition.mcp_manager->has_server("shared"));
  REQUIRE(composition.mcp_manager->has_server("global-only"));
  REQUIRE(composition.mcp_manager->has_server("project-only"));

  REQUIRE(command_by_server.at("shared") == "project-shared-v2");
  REQUIRE(command_by_server.at("global-only") == "global-only");
  REQUIRE(command_by_server.at("project-only") == "project-only");

  REQUIRE(std::count(transport_call_order.begin(), transport_call_order.end(), "shared") == 1);
  REQUIRE(std::count(transport_call_order.begin(), transport_call_order.end(), "global-only") == 1);
  REQUIRE(std::count(transport_call_order.begin(), transport_call_order.end(), "project-only") == 1);

  std::filesystem::remove_all(root);
  ava::config::clear_trust_cache_for_tests();
}

TEST_CASE("runtime composition tolerates malformed MCP config by disabling MCP", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ScopedEnvVar home("HOME", (root / "home").string());
  ScopedEnvVar xdg_config("XDG_CONFIG_HOME", (root / "xdg-config").string());

  const auto global_config_path = ava::config::mcp_config_path();
  std::filesystem::create_directories(global_config_path.parent_path());
  {
    std::ofstream output(global_config_path);
    output << "{ invalid json";
  }

  std::atomic_size_t mcp_factory_calls{0};
  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .resume_latest = false,
      .session_id = std::nullopt,
      .selection = ava::orchestration::RuntimeSelectionOptions{
          .provider = "mock",
          .model = "mock-malformed-mcp-config",
          .max_turns = 2,
          .max_turns_explicit = true,
      },
      .auto_approve = true,
      .allowed_tools = std::nullopt,
      .system_prompt_preamble = std::nullopt,
      .provider_override = ava::llm::create_mock_provider("mock-malformed-mcp-config", std::vector<std::string>{"done"}),
      .provider_factory = nullptr,
      .credentials_override = std::nullopt,
      .load_global_mcp_config = true,
      .mcp_transport_factory = [&mcp_factory_calls](const ava::mcp::McpServerConfig&) -> std::unique_ptr<ava::mcp::McpTransport> {
        ++mcp_factory_calls;
        throw std::runtime_error("transport factory should not be called when MCP is disabled");
      },
  });

  REQUIRE(composition.mcp_manager != nullptr);
  REQUIRE(composition.mcp_manager->server_count() == 0);
  REQUIRE(composition.mcp_manager->tool_count() == 0);
  REQUIRE(mcp_factory_calls.load() == 0);
  REQUIRE_FALSE(composition.registry->has_tool("mcp_global_echo"));

  std::filesystem::remove_all(root);
  ava::config::clear_trust_cache_for_tests();
}

TEST_CASE("runtime composition exposes question and plan interactive bridge resolvers", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .resume_latest = false,
      .session_id = std::nullopt,
      .selection = ava::orchestration::RuntimeSelectionOptions{
          .provider = "mock",
          .model = "mock-model",
          .max_turns = 2,
          .max_turns_explicit = true,
      },
      .auto_approve = false,
      .allowed_tools = std::nullopt,
      .system_prompt_preamble = std::nullopt,
      .approval_resolver = nullptr,
      .question_resolver = [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::QuestionRequestPayload&) {
        return ava::orchestration::QuestionResolution{
            .answer = std::optional<std::string>{"option-a"},
            .state = ava::control_plane::InteractiveRequestState::Resolved,
        };
      },
      .plan_resolver = [](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::PlanRequestPayload&) {
        return ava::orchestration::PlanResolution{
            .accepted = true,
            .state = ava::control_plane::InteractiveRequestState::Resolved,
        };
      },
      .provider_override = ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"done"}),
      .provider_factory = nullptr,
      .credentials_override = std::nullopt,
  });

  REQUIRE(composition.interactive_bridge != nullptr);
  REQUIRE(composition.interactive_bridge->request_question("pick", {"a", "b"}) == std::optional<std::string>{"option-a"});
  REQUIRE(composition.interactive_bridge->request_plan(nlohmann::json{{"steps", nlohmann::json::array()}}));

  std::filesystem::remove_all(root);
}

TEST_CASE("runtime composition remains usable after move", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  std::optional<ava::orchestration::RuntimeComposition> moved;
  {
    auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
        .session_db_path = root / "sessions.db",
        .workspace_root = root,
        .resume_latest = false,
        .session_id = std::nullopt,
        .selection = ava::orchestration::RuntimeSelectionOptions{
            .provider = "mock",
            .model = "mock-model",
            .max_turns = 4,
            .max_turns_explicit = true,
        },
        .auto_approve = true,
        .allowed_tools = std::vector<std::string>{"glob", "read"},
        .system_prompt_preamble = std::nullopt,
        .provider_override = ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"done"}),
        .provider_factory = nullptr,
        .credentials_override = std::nullopt,
    });
    moved.emplace(std::move(composition));
  }

  REQUIRE(moved.has_value());
  const auto prompt = moved->runtime->build_system_prompt();
  REQUIRE_THAT(prompt, Catch::Matchers::ContainsSubstring("- read:"));
  REQUIRE_THAT(prompt, Catch::Matchers::ContainsSubstring("- glob:"));

  const auto run_result = moved->runtime->run(
      moved->session,
      ava::agent::AgentRunInput{.goal = "summarize", .queue = &moved->queue}
  );
  REQUIRE(run_result.reason == ava::agent::AgentCompletionReason::Completed);

  std::filesystem::remove_all(root);
}

TEST_CASE("read-only explicit allowed tools cannot bypass runtime profile", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto parent_registry = std::make_shared<ava::tools::ToolRegistry>();
  parent_registry->register_tool(std::make_unique<StaticTool>("read"));
  parent_registry->register_tool(std::make_unique<StaticTool>("write"));
  parent_registry->register_tool(std::make_unique<StaticTool>("bash"));

  ava::config::AgentsConfig config;
  config.agents.push_back({
      "review",
      ava::config::AgentOverride{.allowed_tools = std::vector<std::string>{"write"}},
  });

  ava::llm::LlmResponse scripted;
  scripted.content = "using write";
  scripted.tool_calls = std::vector<ava::types::ToolCall>{ava::types::ToolCall{
      .id = "call-1",
      .name = "write",
      .arguments = nlohmann::json{{"path", "note.txt"}, {"content", "x"}},
  }};

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 2,
      .max_spawns = 1,
      .auto_approve = true,
      .parent_registry = parent_registry,
      .agents_config = config,
      .provider_factory = [&](const ava::orchestration::ResolvedRuntimeSelection&) {
        return ava::llm::create_mock_provider("mock-model", std::vector<ava::llm::LlmResponse>{scripted});
      },
  });

  const auto result = spawner.spawn_named("review", "inspect");
  REQUIRE(!result.ok());
  REQUIRE(result.error.has_value());
  REQUIRE_THAT(*result.error, Catch::Matchers::ContainsSubstring("Tool not found: write"));

  std::filesystem::remove_all(root);
}

TEST_CASE("shared runtime composition rejects unknown allowed tool names", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  REQUIRE_THROWS_WITH(
      ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
          .session_db_path = root / "sessions.db",
          .workspace_root = root,
          .resume_latest = false,
          .session_id = std::nullopt,
          .selection = ava::orchestration::RuntimeSelectionOptions{
              .provider = "mock",
              .model = "mock-model",
              .max_turns = 4,
              .max_turns_explicit = true,
          },
          .auto_approve = true,
          .allowed_tools = std::vector<std::string>{"read", "nope"},
          .system_prompt_preamble = std::nullopt,
          .provider_override = ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"done"}),
          .provider_factory = nullptr,
          .credentials_override = std::nullopt,
      }),
      Catch::Matchers::ContainsSubstring("Unknown allowed tool name")
  );

  std::filesystem::remove_all(root);
}

TEST_CASE("provider factory receives runtime selection resolved from startup session context", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager sessions(root / "sessions.db");
  auto session = sessions.create();
  session.metadata["runtime"] = nlohmann::json{
      {"provider", "mock"},
      {"model", "session-model"},
      {"max_turns", 7},
  };
  sessions.save(session);

  ava::orchestration::ResolvedRuntimeSelection observed;
  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .resume_latest = true,
      .session_id = std::nullopt,
      .selection = ava::orchestration::RuntimeSelectionOptions{},
      .auto_approve = true,
      .allowed_tools = std::nullopt,
      .system_prompt_preamble = std::nullopt,
      .provider_override = nullptr,
      .provider_factory = [&](const ava::orchestration::ResolvedRuntimeSelection& selection) {
        observed = selection;
        return ava::llm::create_mock_provider(selection.model, {"done"});
      },
      .credentials_override = std::nullopt,
  });

  REQUIRE(observed.provider == "mock");
  REQUIRE(observed.model == "session-model");
  REQUIRE(observed.max_turns == 7);
  REQUIRE(composition.selection.model == "session-model");

  std::filesystem::remove_all(root);
}

TEST_CASE("runtime composition resumes explicit session id and falls back to legacy headless metadata", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::session::SessionManager sessions(root / "sessions.db");
  auto first = sessions.create();
  first.metadata["runtime"] = nlohmann::json{{"provider", "mock"}, {"model", "latest-model"}, {"max_turns", 3}};
  sessions.save(first);

  auto explicit_session = sessions.create();
  explicit_session.metadata["headless"] = nlohmann::json{{"provider", "mock"}, {"model", "legacy-model"}, {"max_turns", 5}};
  sessions.save(explicit_session);

  ava::orchestration::ResolvedRuntimeSelection observed;
  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .resume_latest = false,
      .session_id = explicit_session.id,
      .selection = ava::orchestration::RuntimeSelectionOptions{},
      .auto_approve = true,
      .allowed_tools = std::nullopt,
      .system_prompt_preamble = std::nullopt,
      .provider_override = nullptr,
      .provider_factory = [&](const ava::orchestration::ResolvedRuntimeSelection& selection) {
        observed = selection;
        return ava::llm::create_mock_provider(selection.model, {"done"});
      },
      .credentials_override = std::nullopt,
  });

  REQUIRE(composition.startup_kind == ava::orchestration::SessionStartupKind::ContinueById);
  REQUIRE(composition.session.id == explicit_session.id);
  REQUIRE(observed.provider == "mock");
  REQUIRE(observed.model == "legacy-model");
  REQUIRE(observed.max_turns == 5);
  REQUIRE(composition.session.metadata["runtime"]["provider"] == "mock");
  REQUIRE(composition.session.metadata["runtime"]["model"] == "legacy-model");
  REQUIRE(composition.session.metadata["runtime"]["max_turns"] == 5);

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner runs child sessions", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto parent_registry = std::make_shared<ava::tools::ToolRegistry>();
  parent_registry->register_tool(std::make_unique<StaticTool>("read"));
  parent_registry->register_tool(std::make_unique<StaticTool>("write"));
  parent_registry->register_tool(std::make_unique<StaticTool>("bash"));
  std::vector<ava::agent::AgentEvent> events;

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 4,
      .max_spawns = 2,
      .auto_approve = true,
      .parent_depth = 1,
      .parent_run_id = "parent-run",
      .parent_session_id = "parent-session",
      .parent_agent_type = "general",
      .parent_call_id = "call-subagent",
      .parent_registry = parent_registry,
      .agents_config = ava::config::AgentsConfig{},
      .event_sink = [&](const ava::agent::AgentEvent& event) {
        events.push_back(event);
      },
      .provider_factory = [](const ava::orchestration::ResolvedRuntimeSelection&) {
        return ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"subagent-complete"});
      },
  });

  const auto result = spawner.spawn_named("review", "inspect config");
  REQUIRE(result.output == std::optional<std::string>{"subagent-complete"});
  REQUIRE(!result.error.has_value());
  REQUIRE(result.ok());
  REQUIRE(!result.session_id.empty());
  REQUIRE(!result.messages.empty());

  ava::session::SessionManager sessions(root / "sessions.db");
  const auto stored = sessions.get(result.session_id);
  REQUIRE(stored.has_value());
  REQUIRE(stored->metadata["orchestration"]["subagent_run"]["agent_type"] == "review");
  REQUIRE(stored->metadata["orchestration"]["subagent_run"]["runtime_profile"] == "read_only");
  REQUIRE(stored->metadata["orchestration"]["subagent_run"]["depth"] == 2);
  REQUIRE(stored->metadata["orchestration"]["subagent_run"]["parent_session_id"] == "parent-session");
  REQUIRE(events.size() == 1);
  REQUIRE(events.front().kind == ava::agent::AgentEventKind::SubagentComplete);
  REQUIRE(events.front().run_id == std::optional<std::string>{"parent-run"});
  REQUIRE(events.front().subagent_call_id == std::optional<std::string>{"call-subagent"});
  REQUIRE(events.front().subagent_session_id == std::optional<std::string>{result.session_id});
  REQUIRE(events.front().subagent_description == std::optional<std::string>{"inspect config"});
  REQUIRE(events.front().subagent_message_count == std::optional<std::size_t>{result.messages.size()});

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner requires parent correlation before subagent complete event", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto parent_registry = std::make_shared<ava::tools::ToolRegistry>();
  parent_registry->register_tool(std::make_unique<StaticTool>("read"));
  std::vector<ava::agent::AgentEvent> events;

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 4,
      .max_spawns = 1,
      .auto_approve = true,
      .parent_depth = 1,
      .parent_run_id = "parent-run",
      .parent_session_id = "parent-session",
      .parent_call_id = "   ",
      .parent_registry = parent_registry,
      .event_sink = [&](const ava::agent::AgentEvent& event) {
        events.push_back(event);
      },
      .provider_factory = [](const ava::orchestration::ResolvedRuntimeSelection&) {
        return ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"subagent-complete"});
      },
  });

  const auto result = spawner.spawn_named("review", "inspect config");
  REQUIRE(result.ok());
  REQUIRE(events.empty());

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner emits subagent complete when child reaches max turns", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);
  std::ofstream(root / "README.md") << "hello from child";
  std::vector<ava::agent::AgentEvent> events;

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 1,
      .max_spawns = 1,
      .auto_approve = true,
      .parent_depth = 1,
      .parent_run_id = "parent-run-max-turns",
      .parent_session_id = "parent-session",
      .parent_call_id = "call-subagent-max-turns",
      .event_sink = [&](const ava::agent::AgentEvent& event) {
        events.push_back(event);
      },
      .provider_factory = [](const ava::orchestration::ResolvedRuntimeSelection&) {
        return ava::llm::create_mock_provider(
            "mock-model",
            std::vector<ava::llm::LlmResponse>{ava::llm::LlmResponse{
                .content = "inspect file",
                .tool_calls = {ava::types::ToolCall{
                    .id = "call_read_before_max_turns",
                    .name = "read",
                    .arguments = nlohmann::json{{"path", "README.md"}},
                }},
            }}
        );
      },
  });

  const auto result = spawner.spawn_named("review", "inspect config");
  REQUIRE(result.ok());
  REQUIRE(result.child_run_summary.has_value());
  REQUIRE(result.child_run_summary->completion_reason == "max_turns");
  REQUIRE(events.size() == 1);
  REQUIRE(events.front().kind == ava::agent::AgentEventKind::SubagentComplete);
  REQUIRE(events.front().run_id == std::optional<std::string>{"parent-run-max-turns"});
  REQUIRE(events.front().subagent_call_id == std::optional<std::string>{"call-subagent-max-turns"});
  REQUIRE(events.front().subagent_session_id == std::optional<std::string>{result.session_id});
  REQUIRE(events.front().subagent_description == std::optional<std::string>{"inspect config"});
  REQUIRE(events.front().subagent_message_count == std::optional<std::size_t>{result.messages.size()});

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner exposes active child runs for cancellation", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);
  const auto entered_stream = std::make_shared<std::atomic<bool>>(false);
  const auto stop_stream = std::make_shared<std::atomic<bool>>(false);
  std::vector<ava::agent::AgentEvent> events;

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "blocking-mock",
      .max_turns = 4,
      .max_spawns = 1,
      .auto_approve = true,
      .parent_depth = 1,
      .parent_run_id = "parent-run-cancel",
      .parent_session_id = "parent-session",
      .parent_call_id = "call-subagent-cancel",
      .child_run_timeout = std::chrono::seconds(5),
      .event_sink = [&](const ava::agent::AgentEvent& event) {
        events.push_back(event);
      },
      .provider_factory = [&](const ava::orchestration::ResolvedRuntimeSelection&) {
        return std::make_shared<BlockingStreamProvider>(entered_stream, stop_stream);
      },
  });

  std::optional<ava::orchestration::TaskResult> result;
  std::thread child([&] {
    result = spawner.spawn_named("review", "inspect slowly");
  });

  std::vector<ava::orchestration::ChildRunInfo> active;
  for(int attempt = 0; attempt < 1000; ++attempt) {
    active = spawner.active_child_runs();
    if(entered_stream->load(std::memory_order_acquire) && !active.empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  if(active.empty()) {
    stop_stream->store(true, std::memory_order_release);
    child.join();
  }

  REQUIRE(active.size() == 1);
  REQUIRE(active.front().agent_type == "review");
  REQUIRE(active.front().parent_session_id == std::optional<std::string>{"parent-session"});
  REQUIRE(active.front().depth == 2);
  REQUIRE(spawner.active_child_run(active.front().run_id).has_value());
  REQUIRE(spawner.active_child_run(active.front().run_id)->run_id == active.front().run_id);
  REQUIRE_FALSE(spawner.active_child_run("missing-run").has_value());

  REQUIRE(spawner.cancel_child_run(active.front().run_id));
  REQUIRE_FALSE(spawner.cancel_child_run("missing-run"));
  child.join();

  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->ok());
  REQUIRE(result->error == std::optional<std::string>{"child run cancelled"});
  REQUIRE(result->child_run_summary.has_value());
  REQUIRE(result->child_run_summary->run_id == active.front().run_id);
  REQUIRE(result->child_run_summary->completion_reason == "cancelled");
  REQUIRE(result->child_run_summary->cancelled);
  REQUIRE_FALSE(result->child_run_summary->watchdog_timed_out);
  REQUIRE(spawner.active_child_runs().empty());
  const auto stored_summary = spawner.child_terminal_summary(active.front().run_id);
  REQUIRE(stored_summary.has_value());
  REQUIRE(stored_summary->completion_reason == "cancelled");
  REQUIRE(spawner.child_terminal_summaries().size() == 1);
  REQUIRE(events.empty());

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner propagates parent cancellation into child run", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);
  const auto entered_stream = std::make_shared<std::atomic<bool>>(false);
  const auto stop_stream = std::make_shared<std::atomic<bool>>(false);
  const auto parent_cancelled = std::make_shared<std::atomic<bool>>(false);

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "blocking-mock",
      .max_turns = 4,
      .max_spawns = 1,
      .auto_approve = true,
      .parent_depth = 1,
      .parent_run_id = "parent-run-propagate",
      .parent_session_id = "parent-session",
      .parent_call_id = "call-subagent-propagate",
      .parent_is_cancelled = [parent_cancelled] {
        return parent_cancelled->load(std::memory_order_acquire);
      },
      .child_run_timeout = std::chrono::seconds(5),
      .provider_factory = [&](const ava::orchestration::ResolvedRuntimeSelection&) {
        return std::make_shared<BlockingStreamProvider>(entered_stream, stop_stream);
      },
  });

  std::optional<ava::orchestration::TaskResult> result;
  std::thread child([&] {
    result = spawner.spawn_named("review", "inspect with propagated cancellation");
  });

  for(int attempt = 0; attempt < 1000; ++attempt) {
    if(entered_stream->load(std::memory_order_acquire) && !spawner.active_child_runs().empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  REQUIRE(entered_stream->load(std::memory_order_acquire));
  REQUIRE_FALSE(spawner.active_child_runs().empty());
  parent_cancelled->store(true, std::memory_order_release);
  child.join();

  REQUIRE(result.has_value());
  REQUIRE_FALSE(result->ok());
  REQUIRE(result->error == std::optional<std::string>{"child run cancelled"});
  REQUIRE(result->child_run_summary.has_value());
  REQUIRE(result->child_run_summary->completion_reason == "cancelled");
  REQUIRE(result->child_run_summary->cancelled);
  REQUIRE(spawner.active_child_runs().empty());

  ava::session::SessionManager sessions(root / "sessions.db");
  const auto stored = sessions.get(result->session_id);
  REQUIRE(stored.has_value());
  const auto metadata = stored->metadata.at("orchestration").at("subagent_run");
  REQUIRE(metadata.at("run_id") == result->child_run_summary->run_id);
  REQUIRE(metadata.at("completion_reason") == "cancelled");
  REQUIRE(metadata.at("watchdog_timed_out") == false);

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner watchdog timeout surfaces deterministic terminal summary", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);
  const auto entered_stream = std::make_shared<std::atomic<bool>>(false);
  const auto stop_stream = std::make_shared<std::atomic<bool>>(false);
  std::vector<ava::agent::AgentEvent> events;

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "blocking-mock",
      .max_turns = 4,
      .max_spawns = 1,
      .auto_approve = true,
      .parent_depth = 1,
      .parent_run_id = "parent-run-watchdog",
      .parent_session_id = "parent-session",
      .parent_call_id = "call-subagent-watchdog",
      .child_run_timeout = std::chrono::milliseconds(100),
      .event_sink = [&](const ava::agent::AgentEvent& event) {
        events.push_back(event);
      },
      .provider_factory = [&](const ava::orchestration::ResolvedRuntimeSelection&) {
        return std::make_shared<BlockingStreamProvider>(entered_stream, stop_stream);
      },
  });

  const auto result = spawner.spawn_named("review", "inspect until watchdog");
  REQUIRE(entered_stream->load(std::memory_order_acquire));
  REQUIRE_FALSE(result.ok());
  REQUIRE(result.error == std::optional<std::string>{"child run watchdog timeout"});
  REQUIRE(result.child_run_summary.has_value());
  REQUIRE(result.child_run_summary->completion_reason == "watchdog_timeout");
  REQUIRE(result.child_run_summary->cancelled);
  REQUIRE(result.child_run_summary->watchdog_timed_out);
  REQUIRE_FALSE(result.child_run_summary->run_id.empty());
  REQUIRE(spawner.active_child_runs().empty());

  ava::session::SessionManager sessions(root / "sessions.db");
  const auto stored = sessions.get(result.session_id);
  REQUIRE(stored.has_value());
  REQUIRE(stored->metadata["orchestration"]["subagent_run"]["run_id"] == result.child_run_summary->run_id);
  REQUIRE(stored->metadata["orchestration"]["subagent_run"]["completion_reason"] == "watchdog_timeout");
  REQUIRE(stored->metadata["orchestration"]["subagent_run"]["watchdog_timed_out"] == true);
  REQUIRE(events.empty());

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner enforces max depth", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 2,
      .max_spawns = 1,
      .auto_approve = true,
      .parent_depth = ava::orchestration::MAX_AGENT_DEPTH,
      .provider_factory = [](const ava::orchestration::ResolvedRuntimeSelection&) {
        return ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"never"});
      },
  });

  REQUIRE_THROWS_WITH(
      spawner.spawn_named("general", "too deep"),
      Catch::Matchers::ContainsSubstring("subagent depth limit exceeded")
  );

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner rejects disabled subagents", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::config::AgentsConfig config;
  config.agents.push_back({
      "review",
      ava::config::AgentOverride{.enabled = false},
  });

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 2,
      .max_spawns = 2,
      .auto_approve = true,
      .agents_config = config,
      .provider_factory = [](const ava::orchestration::ResolvedRuntimeSelection&) {
        return ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"unused"});
      },
  });

  REQUIRE_THROWS_WITH(
      spawner.spawn_named("review", "inspect"),
      Catch::Matchers::ContainsSubstring("disabled")
  );

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner distinguishes disabled defaults", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::config::AgentsConfig config;
  config.defaults.enabled = false;

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 2,
      .max_spawns = 1,
      .auto_approve = true,
      .agents_config = config,
      .provider_factory = [](const ava::orchestration::ResolvedRuntimeSelection&) {
        return ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"unused"});
      },
  });

  REQUIRE_THROWS_WITH(
      spawner.spawn_named("review", "inspect"),
      Catch::Matchers::ContainsSubstring("disabled by default configuration")
  );

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner enforces spawn budget", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 2,
      .max_spawns = 1,
      .auto_approve = true,
      .provider_factory = [](const ava::orchestration::ResolvedRuntimeSelection&) {
        return ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"first", "second"});
      },
  });

  const auto first = spawner.spawn_named("general", "one");
  REQUIRE(first.ok());
  REQUIRE(first.output == std::optional<std::string>{"first"});

  REQUIRE_THROWS_WITH(
      spawner.spawn_named("general", "two"),
      Catch::Matchers::ContainsSubstring("budget exhausted")
  );
  REQUIRE(spawner.spawn_count_for_testing() == 1);

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner spawn delegates to default subagent type", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 2,
      .max_spawns = 1,
      .auto_approve = true,
      .provider_factory = [](const ava::orchestration::ResolvedRuntimeSelection&) {
        return ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"default-complete"});
      },
  });

  const auto result = spawner.spawn("inspect with default type");
  REQUIRE(result.ok());
  REQUIRE(result.output == std::optional<std::string>{"default-complete"});
  REQUIRE(result.child_run_summary.has_value());
  REQUIRE(result.child_run_summary->agent_type == "subagent");

  ava::session::SessionManager sessions(root / "sessions.db");
  const auto stored = sessions.get(result.session_id);
  REQUIRE(stored.has_value());
  REQUIRE(stored->metadata["orchestration"]["subagent_run"]["agent_type"] == "subagent");

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner caps child max turns to parent ceiling", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::config::AgentsConfig config;
  config.agents.push_back({
      "review",
      ava::config::AgentOverride{.max_turns = 50},
  });

  std::size_t observed_turns = 0;
  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 3,
      .max_spawns = 2,
      .auto_approve = true,
      .agents_config = config,
      .provider_factory = [&](const ava::orchestration::ResolvedRuntimeSelection& selection) {
        observed_turns = selection.max_turns;
        return ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"done"});
      },
  });

  const auto result = spawner.spawn_named("review", "inspect");
  REQUIRE(result.ok());
  REQUIRE(observed_turns == 3);

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner clamps child max turns to at least one", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::config::AgentsConfig config;
  config.agents.push_back({
      "review",
      ava::config::AgentOverride{.max_turns = 0},
  });

  std::size_t observed_turns = 0;
  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 3,
      .max_spawns = 1,
      .auto_approve = true,
      .agents_config = config,
      .provider_factory = [&](const ava::orchestration::ResolvedRuntimeSelection& selection) {
        observed_turns = selection.max_turns;
        return ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"done"});
      },
  });

  const auto result = spawner.spawn_named("review", "inspect");
  REQUIRE(result.ok());
  REQUIRE(observed_turns == 1);

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner rejects empty agent type", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 2,
      .max_spawns = 1,
      .auto_approve = true,
      .provider_factory = [](const ava::orchestration::ResolvedRuntimeSelection&) {
        return ava::llm::create_mock_provider("mock-model", std::vector<std::string>{"unused"});
      },
  });

  REQUIRE_THROWS_WITH(
      spawner.spawn_named("", "inspect"),
      Catch::Matchers::ContainsSubstring("agent_type must not be empty")
  );

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner rejects conflicting model and provider config", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::config::AgentsConfig config;
  config.agents.push_back({
      "custom-specialist",
      ava::config::AgentOverride{.model = "openai/gpt-5-mini", .provider = "anthropic"},
  });

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = std::nullopt,
      .model = std::nullopt,
      .max_turns = 3,
      .max_spawns = 1,
      .auto_approve = true,
      .agents_config = config,
      .provider_factory = nullptr,
  });

  REQUIRE_THROWS_WITH(
      spawner.spawn_named("custom-specialist", "inspect"),
      Catch::Matchers::ContainsSubstring("conflicting subagent provider/model configuration")
  );

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner rejects alias model with conflicting inherited provider", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::config::AgentsConfig config;
  config.agents.push_back({
      "custom-specialist",
      ava::config::AgentOverride{.model = "opus"},
  });

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "openai",
      .model = "gpt-5-mini",
      .max_turns = 3,
      .max_spawns = 1,
      .auto_approve = true,
      .agents_config = config,
      .provider_factory = nullptr,
  });

  REQUIRE_THROWS_WITH(
      spawner.spawn_named("custom-specialist", "inspect"),
      Catch::Matchers::ContainsSubstring("model alias='opus' resolves to provider='anthropic'")
  );

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner resolves alias with explicit matching provider", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::config::AgentsConfig config;
  config.agents.push_back({
      "custom-specialist",
      ava::config::AgentOverride{.model = "opus", .provider = "anthropic"},
  });

  ava::orchestration::ResolvedRuntimeSelection observed;
  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = std::nullopt,
      .model = std::nullopt,
      .max_turns = 3,
      .max_spawns = 1,
      .auto_approve = true,
      .agents_config = config,
      .provider_factory = [&](const ava::orchestration::ResolvedRuntimeSelection& selection) {
        observed = selection;
        return ava::llm::create_mock_provider(selection.model, {"done"});
      },
  });

  const auto result = spawner.spawn_named("custom-specialist", "inspect");
  REQUIRE(result.ok());
  REQUIRE(observed.provider == "anthropic");
  REQUIRE(observed.model == "claude-opus-4-6");

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner rolls back budget on setup failure", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::config::AgentsConfig config;
  config.agents.push_back({
      "bad-specialist",
      ava::config::AgentOverride{.model = "opus", .provider = "openai"},
  });

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 3,
      .max_spawns = 1,
      .auto_approve = true,
      .agents_config = config,
      .provider_factory = [](const ava::orchestration::ResolvedRuntimeSelection& selection) {
        return ava::llm::create_mock_provider(selection.model, {"done"});
      },
  });

  REQUIRE_THROWS_WITH(
      spawner.spawn_named("bad-specialist", "inspect"),
      Catch::Matchers::ContainsSubstring("conflicting subagent provider/model configuration")
  );

  const auto second = spawner.spawn_named("general", "inspect");
  REQUIRE(second.ok());

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner fails on unknown provider without silent fallback", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::config::AgentsConfig config;
  config.agents.push_back({
      "custom-specialist",
      ava::config::AgentOverride{.provider = "unknown-provider"},
  });

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = std::nullopt,
      .model = std::nullopt,
      .max_turns = 3,
      .max_spawns = 1,
      .auto_approve = true,
      .agents_config = config,
      .provider_factory = nullptr,
  });

  REQUIRE_THROWS_WITH(
      spawner.spawn_named("custom-specialist", "inspect"),
      Catch::Matchers::ContainsSubstring("no default model known for provider")
  );

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner threads credentials override into child composition", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::orchestration::NativeBlockingTaskSpawner without_credentials(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "openai",
      .model = "gpt-5-mini",
      .max_turns = 1,
      .max_spawns = 1,
      .auto_approve = true,
      .credentials_override = ava::config::CredentialStore{},
  });

  REQUIRE_THROWS_WITH(
      without_credentials.spawn_named("general", "hello"),
      Catch::Matchers::ContainsSubstring("missing api key")
  );

  ava::config::CredentialStore credentials;
  credentials.set("openai", ava::config::ProviderCredential{.api_key = "test-key"});

  ava::orchestration::NativeBlockingTaskSpawner with_credentials(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "openai",
      .model = "gpt-5-mini",
      .max_turns = 1,
      .max_spawns = 1,
      .auto_approve = true,
      .credentials_override = credentials,
  });

  const auto result = with_credentials.spawn_named("general", "hello");
  if(result.error.has_value()) {
    REQUIRE(result.error->find("missing api key") == std::string::npos);
  }

  std::filesystem::remove_all(root);
}

TEST_CASE("native blocking task spawner threads interactive resolvers into child composition", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  bool approval_called = false;
  ava::llm::LlmResponse scripted;
  scripted.content = "running write";
  scripted.tool_calls = std::vector<ava::types::ToolCall>{ava::types::ToolCall{
      .id = "call-1",
      .name = "write",
      .arguments = nlohmann::json{{"path", (root / "child.txt").string()}, {"content", "x"}},
  }};

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 1,
      .max_spawns = 1,
      .auto_approve = false,
      .approval_resolver = [&](const ava::control_plane::InteractiveRequestHandle&, const ava::orchestration::ApprovalRequestPayload&) {
        approval_called = true;
        return ava::orchestration::ApprovalResolution{
            .approval = ava::tools::ToolApproval::rejected("blocked by propagated resolver"),
            .state = ava::control_plane::InteractiveRequestState::Resolved,
        };
      },
      .provider_factory = [&](const ava::orchestration::ResolvedRuntimeSelection&) {
        return ava::llm::create_mock_provider("mock-model", std::vector<ava::llm::LlmResponse>{scripted});
      },
  });

  const auto result = spawner.spawn_named("general", "try writing");
  REQUIRE(approval_called);
  REQUIRE(result.error.has_value());
  REQUIRE_THAT(*result.error, Catch::Matchers::ContainsSubstring("blocked by propagated resolver"));

  std::filesystem::remove_all(root);
}

TEST_CASE("task result reports error separately from output", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 2,
      .max_spawns = 1,
      .auto_approve = true,
      .provider_factory = [](const ava::orchestration::ResolvedRuntimeSelection&) {
        return ava::llm::create_mock_provider("mock-model", std::vector<std::string>{});
      },
  });

  const auto result = spawner.spawn_named("general", "will fail");
  REQUIRE(!result.ok());
  REQUIRE(!result.output.has_value());
  REQUIRE(result.error.has_value());

  std::filesystem::remove_all(root);
}
