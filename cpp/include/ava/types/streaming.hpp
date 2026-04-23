#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace ava::types {

struct TokenUsage {
  std::size_t input_tokens{0};
  std::size_t output_tokens{0};
  std::size_t cache_read_tokens{0};
  std::size_t cache_creation_tokens{0};
};

struct StreamToolCall {
  std::size_t index{0};
  std::optional<std::string> id;
  std::optional<std::string> name;
  std::optional<std::string> arguments_delta;
};

struct StreamChunk {
  std::optional<std::string> content;
  std::optional<StreamToolCall> tool_call;
  std::optional<TokenUsage> usage;
  std::optional<std::string> thinking;
  bool done{false};

  [[nodiscard]] static StreamChunk text(std::string value) {
    StreamChunk chunk;
    chunk.content = std::move(value);
    return chunk;
  }

  [[nodiscard]] static StreamChunk finished() {
    StreamChunk chunk;
    chunk.done = true;
    return chunk;
  }

  [[nodiscard]] static StreamChunk with_usage(TokenUsage token_usage) {
    StreamChunk chunk;
    chunk.usage = std::move(token_usage);
    chunk.done = true;
    return chunk;
  }
};

}  // namespace ava::types
