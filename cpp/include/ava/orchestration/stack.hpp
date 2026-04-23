#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#include "ava/types/session.hpp"

namespace ava::orchestration {

// Milestone 8: lightweight stack DTO contracts only.
struct AgentStackConfig {
  std::filesystem::path data_dir{"."};
  std::optional<std::filesystem::path> config_dir;
  std::optional<std::string> provider;
  std::optional<std::string> model;
  std::size_t max_turns{0};
  double max_budget_usd{0.0};
  bool auto_approve{false};
  bool non_interactive_approvals{false};
  std::optional<std::filesystem::path> working_dir;
};

struct AgentRunResult {
  bool success{false};
  std::size_t turns{0};
  std::optional<ava::types::SessionRecord> session;
  std::optional<std::string> error;
};

}  // namespace ava::orchestration
