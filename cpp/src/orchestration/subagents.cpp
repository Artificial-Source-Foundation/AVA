#include "ava/orchestration/subagents.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "ava/config/credentials.hpp"
#include "ava/config/model_spec.hpp"
#include "ava/config/model_registry.hpp"

namespace ava::orchestration {
namespace {

[[nodiscard]] const std::unordered_set<std::string>& read_only_restricted_tools() {
  static const std::unordered_set<std::string> restricted{"write", "edit", "bash", "web_fetch", "web_search"};
  return restricted;
}

[[nodiscard]] std::unordered_map<std::string, ava::config::AgentOverride> configured_overrides(const ava::config::AgentsConfig& config) {
  std::unordered_map<std::string, ava::config::AgentOverride> out;
  for(const auto& [id, definition] : config.agents) {
    out[id] = definition;
  }
  return out;
}

}  // namespace

std::vector<std::string> builtin_subagent_ids() {
  std::vector<std::string> ids;
  ids.reserve(ava::config::builtin_agent_templates().size());
  for(const auto& entry : ava::config::builtin_agent_templates()) {
    ids.emplace_back(entry.id);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

SubAgentRuntimeProfile runtime_profile_for(std::string_view agent_type) {
  if(agent_type == "plan" || agent_type == "explore" || agent_type == "scout" || agent_type == "review") {
    return SubAgentRuntimeProfile::ReadOnly;
  }
  return SubAgentRuntimeProfile::Full;
}

std::vector<ava::types::Tool> apply_runtime_profile_to_registry(
    const ava::tools::ToolRegistry& registry,
    SubAgentRuntimeProfile profile
) {
  auto visible = registry.list_tools();
  if(profile != SubAgentRuntimeProfile::ReadOnly) {
    return visible;
  }

  const auto restricted = read_only_restricted_tools();
  visible.erase(
      std::remove_if(visible.begin(), visible.end(), [&](const ava::types::Tool& tool) {
        return restricted.contains(tool.name);
      }),
      visible.end()
  );
  return visible;
}

std::string build_subagent_system_prompt(std::string_view agent_type) {
  std::string prompt = "You are the `";
  prompt += std::string(agent_type);
  prompt += "` sub-agent of AVA, an AI coding assistant. You have been given a specific task to complete autonomously. "
            "Work through it step by step using the available tools.\n\n"
            "## Rules\n"
            "- Tool calls must use the tool's exact JSON parameter names. Examples: `read` requires {\"path\": \"...\"}, "
            "`glob` requires {\"pattern\": \"...\"}, and `grep` requires {\"pattern\": \"...\", \"path\": \"...\"}.\n"
            "- Read files before modifying them.\n"
            "- Prefer focused, local changes over broad rewrites.\n"
            "- Be thorough but efficient -- you have a limited number of turns.\n"
            "- If a tool call fails validation, correct the arguments on the next attempt instead of repeating the same invalid call.\n"
            "- When your task is complete, provide a clear summary of what you did as your final response.\n"
            "- Do NOT call attempt_completion -- simply respond with your final answer when done.\n";

  if(runtime_profile_for(agent_type) == SubAgentRuntimeProfile::ReadOnly) {
    prompt += "\n## Runtime limits\n"
              "- You are running in read-only specialist mode. Do not edit files, run shell commands, or browse the web. "
              "Investigate with read, glob, grep, and git, then report back clearly.\n";
  } else {
    prompt += "\n## Runtime limits\n"
              "- Stay focused on the delegated task. Keep changes narrow and summarize the result clearly.\n";
  }

  return prompt;
}

std::vector<EffectiveSubagentDefinition> effective_subagent_definitions(const ava::config::AgentsConfig& config) {
  std::vector<EffectiveSubagentDefinition> definitions;
  auto overrides = configured_overrides(config);

  std::unordered_map<std::string, ava::config::BuiltinAgentTemplate> builtins;
  for(const auto& entry : ava::config::builtin_agent_templates()) {
    builtins.emplace(std::string(entry.id), entry);
  }

  std::vector<std::string> ids = builtin_subagent_ids();
  for(const auto& [id, _] : overrides) {
    if(std::find(ids.begin(), ids.end(), id) == ids.end()) {
      ids.push_back(id);
    }
  }
  std::sort(ids.begin(), ids.end());

  for(const auto& id : ids) {
    const auto built_in_it = builtins.find(id);
    const auto override_it = overrides.find(id);
    const auto has_builtin = built_in_it != builtins.end();
    const auto has_override = override_it != overrides.end();

    const auto enabled = has_override && override_it->second.enabled.has_value()
                             ? *override_it->second.enabled
                             : config.defaults.enabled;
    if(!enabled) {
      continue;
    }

    EffectiveSubagentDefinition definition;
    definition.id = id;
    definition.enabled = enabled;
    definition.runtime_profile = runtime_profile_for(id);
    definition.built_in = has_builtin;
    definition.configured = has_override;

    if(has_override && override_it->second.description.has_value()) {
      definition.description = override_it->second.description;
    } else if(has_builtin) {
      definition.description = std::string(built_in_it->second.description);
    }

    definition.model = has_override && override_it->second.model.has_value()
                           ? override_it->second.model
                           : config.defaults.model;

    if(has_override && override_it->second.max_turns.has_value()) {
      definition.max_turns = override_it->second.max_turns;
    } else if(config.defaults.max_turns.has_value()) {
      definition.max_turns = config.defaults.max_turns;
    } else if(has_builtin && built_in_it->second.max_turns.has_value()) {
      definition.max_turns = built_in_it->second.max_turns;
    }

    if(has_override && override_it->second.temperature.has_value()) {
      definition.temperature = override_it->second.temperature;
    } else if(has_builtin && built_in_it->second.temperature.has_value()) {
      definition.temperature = built_in_it->second.temperature;
    }

    if(has_override) {
      definition.provider = override_it->second.provider;
      definition.allowed_tools = override_it->second.allowed_tools;
      definition.max_budget_usd = override_it->second.max_budget_usd;
    }

    definitions.push_back(std::move(definition));
  }

  return definitions;
}

ParsedModelSpec parse_model_spec(const std::string& spec) {
  const auto parsed = ava::config::parse_model_spec(spec);
  return ParsedModelSpec{.provider = parsed.provider, .model = parsed.model};
}

}  // namespace ava::orchestration
