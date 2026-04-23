#include "ava/config/agents.hpp"

namespace ava::config {

const std::vector<BuiltinAgentTemplate>& builtin_agent_templates() {
  static const std::vector<BuiltinAgentTemplate> templates{
      {"build", "Build-and-test specialist for compile and CI failures.", 20U, std::nullopt},
      {"explore", "Read-first explorer for quick repo reconnaissance.", 5U, std::nullopt},
      {"general", "General-purpose coding helper for delegated implementation work.", 12U, std::nullopt},
      {"plan", "Planning-focused architect for structure-first task breakdown.", 10U, 0.3F},
      {"review", "Targeted reviewer for bugs, security, and performance issues.", 15U, 0.2F},
      {"scout", "Low-cost scout for quick read-only investigation.", 5U, std::nullopt},
      {"subagent", "Default delegated helper alias (same intent as general).", 12U, std::nullopt},
      {"task", "Focused execution worker for delegated implementation slices.", 10U, std::nullopt},
      {"worker", "Execution-heavy worker for larger delegated coding tasks.", 15U, std::nullopt},
  };
  return templates;
}

}  // namespace ava::config
