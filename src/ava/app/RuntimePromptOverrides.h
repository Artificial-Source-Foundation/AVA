#pragma once

#include "ava/debug/print_members_on.h"

#include <optional>
#include <string>
#include <vector>

namespace ava::app {

// Carry user-supplied overrides for the assembled system prompt: a full replacement and/or appended fragments.
struct RuntimePromptOverrides
{
  std::optional<std::string> system_prompt = std::nullopt;
  std::vector<std::string> append_system_prompts;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app
