#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/llm/provider.hpp"

namespace ava::llm::openai {

[[nodiscard]] nlohmann::json build_chat_completions_request(
    const std::string& model,
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    bool stream,
    ThinkingConfig thinking
);

[[nodiscard]] LlmResponse parse_chat_completion_response(const nlohmann::json& payload);
[[nodiscard]] std::optional<types::StreamChunk> parse_stream_event(const nlohmann::json& payload);

[[nodiscard]] std::vector<nlohmann::json> tools_to_openai_format(const std::vector<types::Tool>& tools);

}  // namespace ava::llm::openai
