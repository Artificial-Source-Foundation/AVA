#pragma once

#include <string>
#include <vector>

#include "ava/llm/provider.hpp"

namespace ava::llm {

[[nodiscard]] bool supports_thinking_blocks(ProviderKind kind);
[[nodiscard]] bool is_openai_compatible(ProviderKind kind);
[[nodiscard]] ProviderKind provider_kind_from_name(const std::string& provider_name);
[[nodiscard]] std::string normalize_provider_alias(const std::string& provider_name);

[[nodiscard]] std::vector<ChatMessage> normalize_messages(
    const std::vector<ChatMessage>& messages,
    ProviderKind target
);

}  // namespace ava::llm
