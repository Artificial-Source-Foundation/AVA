#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "ava/llm/provider.hpp"

namespace ava::agent::response {

struct ToolCallAccumulator {
  std::size_t index{0};
  std::string id;
  std::string name;
  std::string arguments_json;

  [[nodiscard]] bool is_complete() const;
  [[nodiscard]] std::optional<ava::types::ToolCall> to_tool_call() const;
};

void accumulate_tool_call(std::vector<ToolCallAccumulator>& accumulators, const ava::types::StreamToolCall& delta);
[[nodiscard]] std::vector<ava::types::ToolCall> finalize_tool_calls(std::vector<ToolCallAccumulator> accumulators);

[[nodiscard]] std::vector<ava::types::ToolCall> parse_tool_calls_from_content(const std::string& content);
[[nodiscard]] std::vector<ava::types::ToolCall> coalesce_tool_calls(const ava::llm::LlmResponse& response);

}  // namespace ava::agent::response
