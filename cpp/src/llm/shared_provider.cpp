#include "ava/llm/shared_provider.hpp"

#include <utility>

namespace ava::llm {

SharedProvider::SharedProvider(ProviderPtr provider, bool normalize)
    : provider_(std::move(provider)), normalize_(normalize) {}

const Provider& SharedProvider::inner() const {
  return *provider_;
}

ProviderKind SharedProvider::provider_kind() const {
  return provider_->provider_kind();
}

LlmResponse SharedProvider::generate(
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    ThinkingConfig thinking
) const {
  return provider_->generate(maybe_normalize(messages), tools, thinking);
}

std::vector<types::StreamChunk> SharedProvider::generate_stream(
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    ThinkingConfig thinking
) const {
  return provider_->generate_stream(maybe_normalize(messages), tools, thinking);
}

std::vector<ChatMessage> SharedProvider::maybe_normalize(const std::vector<ChatMessage>& messages) const {
  if(!normalize_) {
    return messages;
  }
  return normalize_messages(messages, provider_->provider_kind());
}

}  // namespace ava::llm
