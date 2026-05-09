#pragma once

#include "ava/session/session_store.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <cstddef>
#include <vector>

namespace ava::agent {

struct BuiltProviderMessages
{
  std::vector<ava::provider::ChatMessage> messages;
  bool used_compacted_context = false;
};

struct MessageBuildOptions
{
  std::size_t max_tool_result_context_bytes = 8 * 1024;
};

[[nodiscard]] ava::core::Result<BuiltProviderMessages> build_messages(ava::session::SessionStore const& store, std::size_t max_tool_result_context_bytes);

[[nodiscard]] ava::core::Result<std::vector<ava::provider::ChatMessage>> build_provider_messages_from_entries(
    std::vector<ava::session::SessionEntry> const& entries, MessageBuildOptions options = MessageBuildOptions{});

}  // namespace ava::agent
