#pragma once

#include <optional>
#include <string_view>

namespace ava::types {

enum class ThinkingLevel {
  Off,
  Low,
  Medium,
  High,
  Max,
};

[[nodiscard]] ThinkingLevel cycle_thinking_level(ThinkingLevel value);
[[nodiscard]] ThinkingLevel cycle_thinking_level_binary(ThinkingLevel value);
[[nodiscard]] std::string_view thinking_level_label(ThinkingLevel value);
[[nodiscard]] std::optional<ThinkingLevel> thinking_level_from_string_loose(std::string_view value);

}  // namespace ava::types
