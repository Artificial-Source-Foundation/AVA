#include "ava/types/thinking.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace ava::types {

ThinkingLevel cycle_thinking_level(ThinkingLevel value) {
  switch(value) {
    case ThinkingLevel::Off:
      return ThinkingLevel::Low;
    case ThinkingLevel::Low:
      return ThinkingLevel::Medium;
    case ThinkingLevel::Medium:
      return ThinkingLevel::High;
    case ThinkingLevel::High:
      return ThinkingLevel::Max;
    case ThinkingLevel::Max:
      return ThinkingLevel::Off;
  }
  return ThinkingLevel::Off;
}

ThinkingLevel cycle_thinking_level_binary(ThinkingLevel value) {
  return value == ThinkingLevel::Off ? ThinkingLevel::High : ThinkingLevel::Off;
}

std::string_view thinking_level_label(ThinkingLevel value) {
  switch(value) {
    case ThinkingLevel::Off:
      return "off";
    case ThinkingLevel::Low:
      return "low";
    case ThinkingLevel::Medium:
      return "med";
    case ThinkingLevel::High:
      return "high";
    case ThinkingLevel::Max:
      return "xhigh";
  }
  return "off";
}

std::optional<ThinkingLevel> thinking_level_from_string_loose(std::string_view value) {
  std::string lowered(value);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  static constexpr std::array<std::pair<std::string_view, ThinkingLevel>, 15> kMap{ {
      {"off", ThinkingLevel::Off},
      {"none", ThinkingLevel::Off},
      {"0", ThinkingLevel::Off},
      {"low", ThinkingLevel::Low},
      {"l", ThinkingLevel::Low},
      {"1", ThinkingLevel::Low},
      {"minimal", ThinkingLevel::Low},
      {"medium", ThinkingLevel::Medium},
      {"med", ThinkingLevel::Medium},
      {"m", ThinkingLevel::Medium},
      {"2", ThinkingLevel::Medium},
      {"high", ThinkingLevel::High},
      {"h", ThinkingLevel::High},
      {"3", ThinkingLevel::High},
      {"max", ThinkingLevel::Max},
  } };

  for(const auto& [alias, level] : kMap) {
    if(alias == lowered) {
      return level;
    }
  }
  if(lowered == "x" || lowered == "xhigh" || lowered == "4") {
    return ThinkingLevel::Max;
  }
  return std::nullopt;
}

}  // namespace ava::types
