#pragma once

#include "ava/llm/message_transform.hpp"
#include "ava/llm/provider.hpp"

namespace ava::llm {

class SharedProvider {
public:
  explicit SharedProvider(ProviderPtr provider, bool normalize = true);

  [[nodiscard]] const Provider& inner() const;
  [[nodiscard]] ProviderKind provider_kind() const;

  [[nodiscard]] LlmResponse generate(
      const std::vector<ChatMessage>& messages,
      const std::vector<types::Tool>& tools = {},
      ThinkingConfig thinking = ThinkingConfig::disabled()
  ) const;

  [[nodiscard]] std::vector<types::StreamChunk> generate_stream(
      const std::vector<ChatMessage>& messages,
      const std::vector<types::Tool>& tools = {},
      ThinkingConfig thinking = ThinkingConfig::disabled()
  ) const;

private:
  [[nodiscard]] std::vector<ChatMessage> maybe_normalize(const std::vector<ChatMessage>& messages) const;

  ProviderPtr provider_;
  bool normalize_{true};
};

}  // namespace ava::llm
