#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/config/agents.hpp"
#include "ava/tools/registry.hpp"
#include "ava/types/tool.hpp"

namespace ava::orchestration {

constexpr std::uint32_t MAX_AGENT_DEPTH = 3;

enum class SubAgentRuntimeProfile {
  Full,
  ReadOnly,
};

struct EffectiveSubagentDefinition {
  std::string id;
  std::optional<std::string> description;
  bool enabled{true};
  std::optional<std::string> model;
  std::optional<std::size_t> max_turns;
  std::optional<float> temperature;
  std::optional<std::string> provider;
  std::optional<std::vector<std::string>> allowed_tools;
  std::optional<double> max_budget_usd;
  SubAgentRuntimeProfile runtime_profile{SubAgentRuntimeProfile::Full};
  bool built_in{false};
  bool configured{false};
};

struct ParsedModelSpec {
  std::string provider;
  std::string model;
};

[[nodiscard]] std::vector<std::string> builtin_subagent_ids();
[[nodiscard]] std::vector<std::string> read_only_runtime_tool_names();
[[nodiscard]] SubAgentRuntimeProfile runtime_profile_for(std::string_view agent_type);

// Milestone 8 keeps this filtering non-mutating and profile-aware by producing
// a filtered tool listing from the current registry snapshot.
[[nodiscard]] std::vector<ava::types::Tool> apply_runtime_profile_to_registry(
    const ava::tools::ToolRegistry& registry,
    SubAgentRuntimeProfile profile
);

[[nodiscard]] std::string build_subagent_system_prompt(std::string_view agent_type);

[[nodiscard]] std::vector<EffectiveSubagentDefinition> effective_subagent_definitions(
    const ava::config::AgentsConfig& config
);

[[nodiscard]] ParsedModelSpec parse_model_spec(const std::string& spec);

}  // namespace ava::orchestration
