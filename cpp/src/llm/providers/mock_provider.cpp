#include "ava/llm/providers/mock_provider.hpp"

#include <utility>

#include "ava/llm/pricing.hpp"

namespace ava::llm {

MockProvider::MockProvider(std::string model, std::vector<std::string> responses)
    : model_(std::move(model)) {
  for(auto& response : responses) {
    responses_.push_back(LlmResponse{
        .content = std::move(response),
        .tool_calls = {},
        .usage = std::nullopt,
        .thinking = std::nullopt,
    });
  }
}

MockProvider::MockProvider(std::string model, std::vector<LlmResponse> responses)
    : model_(std::move(model)), responses_(responses.begin(), responses.end()) {}

std::string MockProvider::model_name() const {
  return model_;
}

ProviderKind MockProvider::provider_kind() const {
  return ProviderKind::OpenAI;
}

std::size_t MockProvider::estimate_tokens(std::string_view input) const {
  return ava::llm::estimate_tokens(input);
}

double MockProvider::estimate_cost(std::size_t input_tokens, std::size_t output_tokens) const {
  return static_cast<double>(input_tokens + output_tokens) * 0.0000005;
}

LlmResponse MockProvider::generate(
    const std::vector<ChatMessage>& /*messages*/,
    const std::vector<types::Tool>& /*tools*/,
    ThinkingConfig /*thinking*/
) const {
  std::scoped_lock lock(mutex_);
  if(responses_.empty()) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = "mock",
        .message = "mock provider has no queued responses",
    });
  }

  auto next = responses_.front();
  responses_.pop_front();
  return next;
}

std::vector<types::StreamChunk> MockProvider::generate_stream(
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    ThinkingConfig thinking
) const {
  std::vector<types::StreamChunk> chunks;
  (void)stream_generate(messages, tools, thinking, [&](const types::StreamChunk& chunk) {
    chunks.push_back(chunk);
    return true;
  });
  return chunks;
}

Provider::StreamDispatchResult MockProvider::stream_generate(
    const std::vector<ChatMessage>& messages,
    const std::vector<types::Tool>& tools,
    ThinkingConfig thinking,
    const StreamChunkSink& on_chunk
) const {
  const auto response = generate(messages, tools, thinking);

  if(!response.content.empty()) {
    if(on_chunk && !on_chunk(types::StreamChunk::text(response.content))) {
      return StreamDispatchResult::Completed;
    }
  }

  if(response.thinking.has_value() && !response.thinking->empty()) {
    types::StreamChunk thinking_chunk;
    thinking_chunk.thinking = response.thinking;
    if(on_chunk && !on_chunk(thinking_chunk)) {
      return StreamDispatchResult::Completed;
    }
  }

  for(std::size_t index = 0; index < response.tool_calls.size(); ++index) {
    const auto& tool_call = response.tool_calls.at(index);
    types::StreamChunk tool_chunk;
    tool_chunk.tool_call = types::StreamToolCall{
        .index = index,
        .id = tool_call.id,
        .name = tool_call.name,
        .arguments_delta = tool_call.arguments.dump(),
    };
    if(on_chunk && !on_chunk(tool_chunk)) {
      return StreamDispatchResult::Completed;
    }
  }

  if(response.usage.has_value()) {
    if(on_chunk && !on_chunk(types::StreamChunk::with_usage(*response.usage))) {
      return StreamDispatchResult::Completed;
    }
  } else {
    if(on_chunk && !on_chunk(types::StreamChunk::finished())) {
      return StreamDispatchResult::Completed;
    }
  }

  return StreamDispatchResult::Completed;
}

}  // namespace ava::llm
