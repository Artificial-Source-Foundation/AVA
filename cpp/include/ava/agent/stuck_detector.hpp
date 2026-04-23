#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ava/types/tool.hpp"

namespace ava::agent {

struct StuckDetectorConfig {
  std::size_t empty_response_limit{2};
  std::size_t repeated_response_limit{3};
  std::size_t repeated_tool_call_limit{3};
};

enum class StuckActionKind {
  Continue,
  InjectMessage,
  Stop,
};

struct StuckAction {
  StuckActionKind kind{StuckActionKind::Continue};
  std::string message;
};

class StuckDetector {
public:
  explicit StuckDetector(StuckDetectorConfig config = {});

  [[nodiscard]] StuckAction check(const std::string& response_text, const std::vector<ava::types::ToolCall>& tool_calls);

private:
  [[nodiscard]] static std::string tool_signature(const ava::types::ToolCall& tool_call);

  StuckDetectorConfig config_;
  std::size_t consecutive_empty_{0};
  std::size_t consecutive_same_response_{0};
  std::size_t consecutive_same_tool_call_{0};
  std::string last_response_;
  std::string last_tool_signature_;
  bool nudge_sent_{false};
};

}  // namespace ava::agent
