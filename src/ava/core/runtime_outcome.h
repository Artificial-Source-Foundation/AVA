#pragma once

#include "ava/debug/print_members_on.h"

#include <array>
#include <string_view>

namespace ava::core {

// Closed, provider-neutral outcome for a completed runtime turn. Protocol
// adapters must map this enum exhaustively rather than interpreting provider
// strings.
enum class RuntimeTerminalOutcome
{
  Completed,
  MaxTokens,
  MaxTurnRequests,
  Refusal,
  Cancelled,
  Error,
};

struct RuntimeTerminalOutcomeCatalogEntry
{
  RuntimeTerminalOutcome outcome;
  std::string_view name;
  AVA_DEBUG_PRINT_MEMBERS_ON
};

inline constexpr std::array kRuntimeTerminalOutcomeCatalog{
    RuntimeTerminalOutcomeCatalogEntry{RuntimeTerminalOutcome::Completed, "completed"},
    RuntimeTerminalOutcomeCatalogEntry{RuntimeTerminalOutcome::MaxTokens, "max_tokens"},
    RuntimeTerminalOutcomeCatalogEntry{RuntimeTerminalOutcome::MaxTurnRequests, "max_turn_requests"},
    RuntimeTerminalOutcomeCatalogEntry{RuntimeTerminalOutcome::Refusal, "refusal"},
    RuntimeTerminalOutcomeCatalogEntry{RuntimeTerminalOutcome::Cancelled, "cancelled"},
    RuntimeTerminalOutcomeCatalogEntry{RuntimeTerminalOutcome::Error, "error"},
};

[[nodiscard]] constexpr std::string_view to_string(RuntimeTerminalOutcome outcome) noexcept
{
  for (auto const& entry : kRuntimeTerminalOutcomeCatalog)
    if (entry.outcome == outcome)
      return entry.name;
  return "error";
}

}  // namespace ava::core
