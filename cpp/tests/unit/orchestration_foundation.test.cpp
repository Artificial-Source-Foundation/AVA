#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/orchestration/orchestration.hpp"

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
        .text = "spawn:" + prompt,
        .session_id = "session-1",
        .messages = {},
    };
  }

  std::string last_prompt;
};

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
  REQUIRE(noop_result.text == "hello");
  REQUIRE(noop_result.session_id == "noop-session");

  RecordingSpawner spawner;
  const auto named = spawner.spawn_named("review", "inspect");
  REQUIRE(named.text == "spawn:inspect");
  REQUIRE(spawner.last_prompt == "inspect");

  const auto named_again = spawner.spawn_named("review", "inspect-bg");
  REQUIRE(named_again.session_id == "session-1");
}
