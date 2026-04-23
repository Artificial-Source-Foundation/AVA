#include "ava/llm/factory.hpp"

#include <algorithm>

#include "ava/llm/message_transform.hpp"
#include "ava/llm/providers/mock_provider.hpp"
#include "ava/llm/providers/openai_provider.hpp"

namespace ava::llm {

bool is_known_provider(std::string_view provider_name) {
  const auto normalized = normalize_provider_alias(std::string(provider_name));
  if(normalized == "mock") {
    return true;
  }

  const auto& providers = ava::config::known_providers();
  return std::find(providers.begin(), providers.end(), normalized) != providers.end();
}

std::optional<std::string> base_url_for_provider(std::string_view provider_name) {
  return ava::config::default_base_url_for_provider(std::string(provider_name));
}

ProviderPtr create_provider(
    const std::string& provider_name,
    const std::string& model,
    const ava::config::CredentialStore& credentials
) {
  const auto normalized = normalize_provider_alias(provider_name);

  if(normalized == "mock") {
    return create_mock_provider(model, {});
  }

  auto credential = credentials.get(provider_name);
  if(!credential.has_value() && normalized != provider_name) {
    credential = credentials.get(normalized);
  }

  if(normalized == "openai") {
    if(!credential.has_value()) {
      throw ProviderException(ProviderError{
          .kind = ProviderErrorKind::AuthFailure,
          .provider = "openai",
          .message = "missing api key",
      });
    }
    return std::make_shared<OpenAiProvider>(OpenAiProvider::from_credential(model, *credential));
  }

  if(
      normalized == "anthropic" || normalized == "gemini" || normalized == "openrouter" || normalized == "ollama"
      || normalized == "copilot" || normalized == "inception" || normalized == "alibaba" || normalized == "zai"
      || normalized == "kimi" || normalized == "minimax"
  ) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = normalized,
        .message = "provider entrypoint exists but is not implemented in C++ Milestone 5",
    });
  }

  throw ProviderException(ProviderError{
      .kind = ProviderErrorKind::Unknown,
      .provider = provider_name,
      .message = "unknown provider",
  });
}

ProviderPtr create_mock_provider(const std::string& model, std::vector<std::string> responses) {
  return std::make_shared<MockProvider>(model, std::move(responses));
}

ProviderPtr create_mock_provider(const std::string& model, std::vector<LlmResponse> responses) {
  return std::make_shared<MockProvider>(model, std::move(responses));
}

}  // namespace ava::llm
