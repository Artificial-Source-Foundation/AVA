#pragma once

#include "ava/debug/print_members_on.h"

#include <optional>
#include <string>

namespace ava::app::runtime {

// Express a selected reasoning configuration: an opaque level, an optional token budget and a display label.
struct ReasoningSelection
{
  std::string level;
  std::optional<long long> budget_tokens = std::nullopt;
  std::string display;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
