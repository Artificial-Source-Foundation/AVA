#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::config {

struct AgentDefaults {
  std::optional<std::string> model;
  std::optional<std::size_t> max_turns;
  bool enabled{true};
};

struct AgentOverride {
  std::optional<std::string> description;
  std::optional<bool> enabled;
  std::optional<std::string> model;
  std::optional<std::size_t> max_turns;
  std::optional<float> temperature;
  std::optional<std::string> provider;
  std::optional<std::vector<std::string>> allowed_tools;
  std::optional<double> max_budget_usd;
};

struct AgentsConfig {
  AgentDefaults defaults{};
  std::vector<std::pair<std::string, AgentOverride>> agents;
};

struct BuiltinAgentTemplate {
  std::string_view id;
  std::string_view description;
  std::optional<std::size_t> max_turns;
  std::optional<float> temperature;
};

[[nodiscard]] const std::vector<BuiltinAgentTemplate>& builtin_agent_templates();

}  // namespace ava::config
