#pragma once

#include <deque>
#include <mutex>

#include "ava/llm/provider.hpp"

namespace ava::llm {

class MockProvider final : public Provider {
public:
  MockProvider(std::string model, std::vector<std::string> responses);
  MockProvider(std::string model, std::vector<LlmResponse> responses);

  [[nodiscard]] std::string model_name() const override;
  [[nodiscard]] ProviderKind provider_kind() const override;

  [[nodiscard]] std::size_t estimate_tokens(std::string_view input) const override;
  [[nodiscard]] double estimate_cost(std::size_t input_tokens, std::size_t output_tokens) const override;

  [[nodiscard]] LlmResponse generate(
      const std::vector<ChatMessage>& messages,
      const std::vector<types::Tool>& tools,
      ThinkingConfig thinking
  ) const override;

  [[nodiscard]] std::vector<types::StreamChunk> generate_stream(
      const std::vector<ChatMessage>& messages,
      const std::vector<types::Tool>& tools,
      ThinkingConfig thinking
  ) const override;

  [[nodiscard]] StreamDispatchResult stream_generate(
      const std::vector<ChatMessage>& messages,
      const std::vector<types::Tool>& tools,
      ThinkingConfig thinking,
      const StreamChunkSink& on_chunk
  ) const override;

private:
  std::string model_;
  mutable std::mutex mutex_;
  mutable std::deque<LlmResponse> responses_;
};

}  // namespace ava::llm
