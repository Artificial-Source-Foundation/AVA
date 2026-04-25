#pragma once

#include <string>

#include "ava/config/credentials.hpp"
#include "ava/llm/provider.hpp"

namespace ava::llm {

class AnthropicProvider final : public Provider {
public:
  AnthropicProvider(std::string model, std::string api_key, std::string base_url, std::string anthropic_version);

  [[nodiscard]] static AnthropicProvider from_credential(
      const std::string& model,
      const ava::config::ProviderCredential& credential
  );

  [[nodiscard]] std::string model_name() const override;
  [[nodiscard]] ProviderKind provider_kind() const override;
  [[nodiscard]] ProviderCapabilities capabilities() const override;

  [[nodiscard]] std::size_t estimate_tokens(std::string_view input) const override;
  [[nodiscard]] double estimate_cost(std::size_t input_tokens, std::size_t output_tokens) const override;

  [[nodiscard]] bool supports_tools() const override;

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
  [[nodiscard]] std::string messages_url() const;

  std::string model_;
  std::string api_key_;
  std::string base_url_;
  std::string anthropic_version_;
};

}  // namespace ava::llm
