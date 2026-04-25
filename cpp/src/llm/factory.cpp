#include "ava/llm/factory.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include "ava/llm/message_transform.hpp"
#include "ava/llm/providers/anthropic_provider.hpp"
#include "ava/llm/providers/mock_provider.hpp"
#include "ava/llm/providers/openai_provider.hpp"

namespace ava::llm {
namespace {

[[nodiscard]] std::string known_provider_list() {
  auto providers = ava::config::known_providers();
  providers.push_back("mock");

  std::sort(providers.begin(), providers.end());
  providers.erase(std::unique(providers.begin(), providers.end()), providers.end());

  std::ostringstream out;
  for(std::size_t index = 0; index < providers.size(); ++index) {
    if(index > 0) {
      out << ", ";
    }
    out << providers.at(index);
  }

  return out.str();
}

}  // namespace

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
    return create_mock_provider(model, std::vector<std::string>{});
  }

  auto credential = credentials.get(provider_name);
  if(!credential.has_value() && normalized != provider_name) {
    credential = credentials.get(normalized);
  }

  if(normalized == "openai") {
    if(!credential.has_value()) {
      std::string message = "missing api key for provider 'openai'";
      if(normalized != provider_name) {
        message += " (requested alias '" + provider_name + "')";
      }

      throw ProviderException(ProviderError{
          .kind = ProviderErrorKind::AuthFailure,
          .provider = "openai",
          .message = std::move(message),
      });
    }
    return std::make_shared<OpenAiProvider>(OpenAiProvider::from_credential(model, *credential));
  }

  if(normalized == "anthropic") {
    if(!credential.has_value()) {
      std::string message = "missing api key for provider 'anthropic'";
      if(normalized != provider_name) {
        message += " (requested alias '" + provider_name + "')";
      }

      throw ProviderException(ProviderError{
          .kind = ProviderErrorKind::AuthFailure,
          .provider = "anthropic",
          .message = std::move(message),
      });
    }

    return std::make_shared<AnthropicProvider>(AnthropicProvider::from_credential(model, *credential));
  }

  if(
      normalized == "gemini" || normalized == "openrouter" || normalized == "ollama" || normalized == "copilot"
      || normalized == "inception" || normalized == "alibaba" || normalized == "zai" || normalized == "kimi"
      || normalized == "minimax"
  ) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = normalized,
        .message = "provider '" + normalized + "' is recognized but not implemented in C++ Milestone 23 scoped provider slice",
    });
  }

  std::string message = "unknown provider '" + provider_name + "'";
  if(normalized != provider_name) {
    message += " (normalized to '" + normalized + "')";
  }
  message += "; known providers: " + known_provider_list();

  throw ProviderException(ProviderError{
      .kind = ProviderErrorKind::Unknown,
      .provider = provider_name,
      .message = std::move(message),
  });
}

ProviderPtr create_mock_provider(const std::string& model, std::vector<std::string> responses) {
  return std::make_shared<MockProvider>(model, std::move(responses));
}

ProviderPtr create_mock_provider(const std::string& model, std::vector<LlmResponse> responses) {
  return std::make_shared<MockProvider>(model, std::move(responses));
}

}  // namespace ava::llm
