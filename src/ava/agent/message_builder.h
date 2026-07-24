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

}  // namespace ava::agent
