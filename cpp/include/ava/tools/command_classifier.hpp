#pragma once

#include <string>

namespace ava::tools {

enum class RiskLevel {
  Safe,
  Low,
  Medium,
  High,
  Critical,
};

struct CommandClassification {
  RiskLevel risk_level{RiskLevel::Low};
  std::string reason{"low-risk command"};
};

[[nodiscard]] std::string risk_level_to_string(RiskLevel level);
[[nodiscard]] CommandClassification classify_bash_command(const std::string& command);

}  // namespace ava::tools
