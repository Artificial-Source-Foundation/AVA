#pragma once

#include <cstddef>
#include <vector>

#include "ava/core/result.h"
#include "ava/provider/provider.h"
#include "ava/session/session_store.h"

namespace ava::agent {

struct BuiltProviderMessages {
  std::vector<ava::provider::ChatMessage> messages;
  bool used_compacted_context = false;
};

struct MessageBuildOptions {
  std::size_t max_tool_result_context_bytes = 8 * 1024;
};

[[nodiscard]] ava::core::Result<BuiltProviderMessages> build_messages(const ava::session::SessionStore& store,
                                                                      std::size_t max_tool_result_context_bytes);

[[nodiscard]] ava::core::Result<std::vector<ava::provider::ChatMessage>> build_provider_messages_from_entries(
    const std::vector<ava::session::SessionEntry>& entries, MessageBuildOptions options = MessageBuildOptions{});

}  // namespace ava::agent
