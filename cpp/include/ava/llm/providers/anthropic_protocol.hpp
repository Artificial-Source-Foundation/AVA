#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/llm/provider.hpp"

namespace ava::llm::anthropic {

[[nodiscard]] nlohmann::json build_messages_request(
    const std::string& model,
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    std::uint32_t max_tokens,
    ThinkingConfig thinking
);

[[nodiscard]] LlmResponse parse_messages_response(const nlohmann::json& payload);
[[nodiscard]] std::vector<nlohmann::json> tools_to_anthropic_format(const std::vector<types::Tool>& tools);

}  // namespace ava::llm::anthropic
