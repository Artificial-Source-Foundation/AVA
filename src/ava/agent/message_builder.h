#pragma once

#include "ava/agent/history_projection.h"
#include "ava/session/session_store.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <vector>

namespace ava::agent {

struct BuiltProviderMessages
{
  std::vector<ava::provider::ChatMessage> messages;
  bool used_compacted_context = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::Result<BuiltProviderMessages> build_messages(ava::session::SessionReadAuthority read_authority, MessageBuildOptions options = {});

[[nodiscard]] ava::core::Result<std::vector<ava::provider::ChatMessage>> build_provider_messages_from_entries(
    std::vector<ava::session::SessionEntry> const& entries, MessageBuildOptions options = MessageBuildOptions{});

struct PreparedContextUsage
{
  std::size_t tokens = 0;
  // The next request includes content added since the last provider input
  // measurement. That delta (or the entire unmeasured request) is estimated.
  bool estimated = true;
  std::optional<std::size_t> provider_input_tokens = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Uses the same bounded, target-specific message projection as provider replay.
// Cumulative session usage is deliberately not an input to context pressure.
[[nodiscard]] auto prepared_context_usage(std::vector<ava::session::SessionEntry> const& entries, std::string_view system_prompt,
                                          MessageBuildOptions const& options = {}) -> ava::core::Result<PreparedContextUsage>;

}  // namespace ava::agent
