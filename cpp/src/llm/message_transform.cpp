#include "ava/llm/message_transform.hpp"

#include "ava/config/credentials.hpp"

namespace ava::llm {
namespace {

[[nodiscard]] std::string strip_tag_block(const std::string& text, std::string_view tag) {
  const std::string open = "<" + std::string(tag) + ">";
  const std::string close = "</" + std::string(tag) + ">";

  std::string output;
  std::string remaining = text;
  output.reserve(text.size());

  while(true) {
    const auto start = remaining.find(open);
    if(start == std::string::npos) {
      output += remaining;
      break;
    }

    output += remaining.substr(0, start);
    const auto end = remaining.find(close, start);
    if(end == std::string::npos) {
      output += remaining.substr(start);
      break;
    }

    remaining = remaining.substr(end + close.size());
  }

  return output;
}

[[nodiscard]] std::string strip_thinking_blocks(const std::string& content) {
  auto value = strip_tag_block(content, "thinking");
  value = strip_tag_block(value, "antThinking");
  for(std::size_t pos = 0; (pos = value.find("\n\n\n", pos)) != std::string::npos;) {
    value.replace(pos, 3, "\n\n");
  }
  return value;
}

}  // namespace

bool supports_thinking_blocks(ProviderKind kind) {
  return kind == ProviderKind::Anthropic || kind == ProviderKind::Gemini;
}

bool is_openai_compatible(ProviderKind kind) {
  return kind == ProviderKind::OpenAI || kind == ProviderKind::OpenRouter || kind == ProviderKind::Copilot
         || kind == ProviderKind::Ollama || kind == ProviderKind::Inception;
}

ProviderKind provider_kind_from_name(const std::string& provider_name) {
  const auto normalized = normalize_provider_alias(provider_name);
  if(normalized == "anthropic" || normalized == "alibaba" || normalized == "kimi" || normalized == "minimax") {
    return ProviderKind::Anthropic;
  }
  if(normalized == "gemini") {
    return ProviderKind::Gemini;
  }
  if(normalized == "ollama") {
    return ProviderKind::Ollama;
  }
  if(normalized == "openrouter") {
    return ProviderKind::OpenRouter;
  }
  if(normalized == "copilot") {
    return ProviderKind::Copilot;
  }
  if(normalized == "inception") {
    return ProviderKind::Inception;
  }
  return ProviderKind::OpenAI;
}

std::string normalize_provider_alias(const std::string& provider_name) {
  return ava::config::normalize_provider_name(provider_name);
}

std::vector<ChatMessage> normalize_messages(const std::vector<ChatMessage>& messages, ProviderKind target) {
  std::vector<ChatMessage> output = messages;
  if(supports_thinking_blocks(target)) {
    return output;
  }

  for(auto& message : output) {
    message.content = strip_thinking_blocks(message.content);
  }
  return output;
}

}  // namespace ava::llm
