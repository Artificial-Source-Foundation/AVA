#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/llm/factory.hpp"
#include "ava/orchestration/orchestration.hpp"
#include "ava/session/session.hpp"

namespace {

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

TEST_CASE("read-only runtime profile filters restricted tools", "[ava_orchestration]") {
  ava::tools::ToolRegistry registry;
  registry.register_tool(std::make_unique<StaticTool>("read"));
  registry.register_tool(std::make_unique<StaticTool>("write"));
  registry.register_tool(std::make_unique<StaticTool>("bash"));

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
  REQUIRE(full.size() == 3);
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
  REQUIRE(from_catalog.model == "claude-opus-4.6");

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

TEST_CASE("native blocking task spawner runs child sessions", "[ava_orchestration]") {
  const auto root = temp_root_for_test();
  std::filesystem::create_directories(root);

  auto parent_registry = std::make_shared<ava::tools::ToolRegistry>();
  parent_registry->register_tool(std::make_unique<StaticTool>("read"));
  parent_registry->register_tool(std::make_unique<StaticTool>("write"));
  parent_registry->register_tool(std::make_unique<StaticTool>("bash"));

  ava::orchestration::NativeBlockingTaskSpawner spawner(ava::orchestration::NativeTaskSpawnerOptions{
      .session_db_path = root / "sessions.db",
      .workspace_root = root,
      .provider = "mock",
      .model = "mock-model",
      .max_turns = 4,
      .max_spawns = 2,
      .auto_approve = true,
      .parent_depth = 1,
      .parent_session_id = "parent-session",
      .parent_agent_type = "general",
      .parent_registry = parent_registry,
      .agents_config = ava::config::AgentsConfig{},
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
  REQUIRE(observed.model == "claude-opus-4.6");

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
