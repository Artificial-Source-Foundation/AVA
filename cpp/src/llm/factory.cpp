#include "ava/llm/factory.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "ava/llm/message_transform.hpp"
#include "ava/llm/providers/anthropic_provider.hpp"
#include "ava/llm/providers/mock_provider.hpp"
#include "ava/llm/providers/openai_provider.hpp"
#include "ava/core/string_utils.hpp"

namespace ava::llm {
namespace {

[[nodiscard]] std::string join_provider_list(std::vector<std::string> providers) {

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

[[nodiscard]] bool contains_provider(const std::vector<std::string>& providers, std::string_view provider) {
  return std::find(providers.begin(), providers.end(), provider) != providers.end();
}

[[nodiscard]] std::optional<std::string> env_string(const char* name) {
  if(const char* value = std::getenv(name); value != nullptr) {
    std::string trimmed(value);
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                  }));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                  }).base(),
                  trimmed.end());
    if(!trimmed.empty()) {
      return trimmed;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> env_api_key(const char* name) {
  auto value = env_string(name);
  if(value.has_value() && !ava::config::is_placeholder_api_key(*value)) {
    return value;
  }
  return std::nullopt;
}

[[nodiscard]] std::string to_upper_underscore(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    if(ch == '-') {
      return '_';
    }
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

void clear_oauth_fields(ava::config::ProviderCredential& credential) {
  credential.oauth_token.reset();
  credential.oauth_refresh_token.reset();
  credential.oauth_expires_at.reset();
  credential.oauth_account_id.reset();
}

void apply_api_key(ava::config::ProviderCredential& credential, const std::string& api_key) {
  if(api_key.empty() || ava::config::is_placeholder_api_key(api_key)) {
    return;
  }
  credential.api_key = api_key;
  clear_oauth_fields(credential);
}

void apply_non_empty_string(
    std::optional<std::string>& target,
    const std::optional<std::string>& source
) {
  if(source.has_value() && !source->empty()) {
    target = source;
  }
}

[[nodiscard]] std::string read_text_file(const std::string& path) {
  errno = 0;
  std::ifstream input(path);
  if(!input.is_open()) {
    const std::string reason = errno == 0 ? std::string{} : std::string{": "} + std::strerror(errno);
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = "mock",
        .message = "failed to open AVA mock response file: " + path + reason,
    });
  }

  std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  if(input.bad()) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = "mock",
        .message = "failed to read AVA mock response file: " + path,
    });
  }
  return content;
}

[[nodiscard]] types::TokenUsage parse_token_usage(const nlohmann::json& json) {
  return types::TokenUsage{
      .input_tokens = json.value("input_tokens", std::size_t{0}),
      .output_tokens = json.value("output_tokens", std::size_t{0}),
      .cache_read_tokens = json.value("cache_read_tokens", std::size_t{0}),
      .cache_creation_tokens = json.value("cache_creation_tokens", std::size_t{0}),
  };
}

[[nodiscard]] std::vector<types::ToolCall> parse_tool_calls(const nlohmann::json& json) {
  if(!json.is_array()) {
    throw std::runtime_error("tool_calls must be an array");
  }

  std::vector<types::ToolCall> calls;
  calls.reserve(json.size());
  for(const auto& item : json) {
    if(!item.is_object()) {
      throw std::runtime_error("tool_calls entries must be objects");
    }
    calls.push_back(types::ToolCall{
        .id = item.at("id").get<std::string>(),
        .name = item.at("name").get<std::string>(),
        .arguments = item.value("arguments", nlohmann::json::object()),
    });
  }
  return calls;
}

[[nodiscard]] std::vector<LlmResponse> parse_mock_responses(std::string_view payload) {
  try {
    const auto root = nlohmann::json::parse(payload);
    if(!root.is_array()) {
      throw std::runtime_error("mock response payload must be a JSON array");
    }

    std::vector<LlmResponse> responses;
    responses.reserve(root.size());
    for(const auto& item : root) {
      if(item.is_string()) {
        responses.push_back(LlmResponse{.content = item.get<std::string>()});
        continue;
      }
      if(!item.is_object()) {
        throw std::runtime_error("mock response entries must be objects or strings");
      }

      LlmResponse response;
      response.content = item.value("content", std::string{});
      if(const auto calls = item.find("tool_calls"); calls != item.end() && !calls->is_null()) {
        response.tool_calls = parse_tool_calls(*calls);
      }
      if(const auto usage = item.find("usage"); usage != item.end() && usage->is_object()) {
        response.usage = parse_token_usage(*usage);
      }
      if(const auto thinking = item.find("thinking"); thinking != item.end() && thinking->is_string()) {
        response.thinking = thinking->get<std::string>();
      }
      responses.push_back(std::move(response));
    }

    return responses;
  } catch(const ProviderException&) {
    throw;
  } catch(const std::exception& error) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = "mock",
        .message = std::string{"failed to parse AVA mock responses: "} + error.what(),
    });
  }
}

[[nodiscard]] std::optional<std::vector<LlmResponse>> mock_responses_from_environment() {
  if(const auto file = env_string("AVA_MOCK_PROVIDER_RESPONSES_FILE"); file.has_value()) {
    return parse_mock_responses(read_text_file(*file));
  }
  if(const auto payload = env_string("AVA_MOCK_PROVIDER_RESPONSES"); payload.has_value()) {
    return parse_mock_responses(*payload);
  }
  return std::nullopt;
}

[[noreturn]] void throw_missing_credential(const std::string& provider, const std::string& requested_provider) {
  std::string message = "missing api key for provider '" + provider + "'";
  if(provider != requested_provider) {
    message += " (requested alias '" + requested_provider + "')";
  }

  throw ProviderException(ProviderError{
      .kind = ProviderErrorKind::AuthFailure,
      .provider = provider,
      .message = std::move(message),
  });
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

std::vector<std::string> implemented_provider_names() {
  return {"alibaba", "anthropic", "inception", "kimi", "minimax", "mock", "openai", "openrouter", "zai"};
}

std::vector<std::string> deferred_provider_names() {
  std::vector<std::string> deferred;
  for(const auto& provider : ava::config::known_providers()) {
    if(!contains_provider(implemented_provider_names(), provider)) {
      deferred.push_back(provider);
    }
  }
  std::sort(deferred.begin(), deferred.end());
  return deferred;
}

[[nodiscard]] std::optional<ava::config::ProviderCredential> credential_for_provider(
    const ava::config::CredentialStore& credentials,
    const std::string& requested_provider,
    const std::string& credential_provider,
    const std::string& normalized
) {
  if(normalized == credential_provider) {
    if(requested_provider != credential_provider) {
      if(auto raw = credentials.get(requested_provider); raw.has_value()) {
        return raw;
      }
    }
    return credentials.get(credential_provider);
  }

  std::optional<ava::config::ProviderCredential> credential;
  if(const auto canonical = credentials.providers.find(normalized); canonical != credentials.providers.end()) {
    credential = canonical->second;
  }

  auto apply_alias_credential = [&](const ava::config::ProviderCredential& alias_credential) {
    if(!credential.has_value()) {
      credential = ava::config::ProviderCredential{};
    }

    const bool alias_has_real_api_key =
        !alias_credential.api_key.empty() && !ava::config::is_placeholder_api_key(alias_credential.api_key);

    apply_api_key(*credential, alias_credential.api_key);
    apply_non_empty_string(credential->base_url, alias_credential.base_url);
    apply_non_empty_string(credential->org_id, alias_credential.org_id);
    if(!alias_has_real_api_key) {
      apply_non_empty_string(credential->oauth_token, alias_credential.oauth_token);
      apply_non_empty_string(credential->oauth_refresh_token, alias_credential.oauth_refresh_token);
      if(alias_credential.oauth_expires_at.has_value()) {
        credential->oauth_expires_at = alias_credential.oauth_expires_at;
      }
      apply_non_empty_string(credential->oauth_account_id, alias_credential.oauth_account_id);
    }
    if(alias_credential.litellm_compatible.has_value()) {
      credential->litellm_compatible = alias_credential.litellm_compatible;
    }
    if(alias_credential.loop_prone.has_value()) {
      credential->loop_prone = alias_credential.loop_prone;
    }
  };

  if(const auto alias = credentials.providers.find(requested_provider); alias != credentials.providers.end()) {
    apply_alias_credential(alias->second);
  }
  if(
      requested_provider != credential_provider
      && (credentials.providers.find(requested_provider) == credentials.providers.end())
  ) {
    if(const auto alias = credentials.providers.find(credential_provider); alias != credentials.providers.end()) {
      apply_alias_credential(alias->second);
    }
  }

  if(const auto standard = ava::config::standard_env_var(normalized); standard.has_value()) {
    if(const auto api_key = env_api_key(standard->c_str()); api_key.has_value()) {
      if(!credential.has_value()) {
        credential = ava::config::ProviderCredential{};
      }
      apply_api_key(*credential, *api_key);
    }
  }

  const auto canonical_env = "AVA_" + to_upper_underscore(normalized) + "_API_KEY";
  if(const auto api_key = env_api_key(canonical_env.c_str()); api_key.has_value()) {
    if(!credential.has_value()) {
      credential = ava::config::ProviderCredential{};
    }
    apply_api_key(*credential, *api_key);
  }

  const auto alias_env = "AVA_" + to_upper_underscore(credential_provider) + "_API_KEY";
  if(const auto api_key = env_api_key(alias_env.c_str()); api_key.has_value()) {
    if(!credential.has_value()) {
      credential = ava::config::ProviderCredential{};
    }
    apply_api_key(*credential, *api_key);
  }
  return credential;
}

std::optional<ava::config::ProviderCredential> resolve_credential_for_provider_for_testing(
    const ava::config::CredentialStore& credentials,
    const std::string& provider_name
) {
  const auto credential_provider = ava::core::lowercase_ascii(provider_name);
  const auto normalized = normalize_provider_alias(credential_provider);
  return credential_for_provider(credentials, provider_name, credential_provider, normalized);
}

ProviderPtr create_provider(
    const std::string& provider_name,
    const std::string& model,
    const ava::config::CredentialStore& credentials
) {
  const auto requested_provider = provider_name;
  const auto credential_provider = ava::core::lowercase_ascii(provider_name);
  const auto normalized = normalize_provider_alias(credential_provider);

  if(normalized == "mock") {
    if(auto responses = mock_responses_from_environment(); responses.has_value()) {
      return create_mock_provider(model, std::move(*responses));
    }
    return create_mock_provider(model, std::vector<std::string>{});
  }

  auto credential = credential_for_provider(credentials, requested_provider, credential_provider, normalized);

  if(normalized == "openai") {
    if(!credential.has_value()) {
      throw_missing_credential("openai", requested_provider);
    }
    return std::make_shared<OpenAiProvider>(OpenAiProvider::from_credential(model, *credential));
  }

  if(normalized == "openrouter" || normalized == "inception" || normalized == "zai") {
    if(!credential.has_value()) {
      throw_missing_credential(normalized, requested_provider);
    }
    const auto api_key = credential->effective_api_key();
    if(!api_key.has_value()) {
      throw_missing_credential(normalized, requested_provider);
    }
    const auto default_base_url = ava::config::default_base_url_for_provider(requested_provider)
                                      .value_or("https://api.openai.com");
    const auto kind = normalized == "openrouter"   ? ProviderKind::OpenRouter
                      : normalized == "inception" ? ProviderKind::Inception
                                                    : ProviderKind::OpenAI;
    return std::make_shared<OpenAiProvider>(
        model,
        *api_key,
        credential->base_url.value_or(default_base_url),
        credential->org_id,
        kind,
        normalized
    );
  }

  if(normalized == "anthropic") {
    if(!credential.has_value()) {
      throw_missing_credential("anthropic", requested_provider);
    }

    return std::make_shared<AnthropicProvider>(AnthropicProvider::from_credential(model, *credential));
  }

  if(normalized == "alibaba" || normalized == "kimi" || normalized == "minimax") {
    if(!credential.has_value()) {
      throw_missing_credential(normalized, requested_provider);
    }
    const auto api_key = credential->effective_api_key();
    if(!api_key.has_value()) {
      throw_missing_credential(normalized, requested_provider);
    }
    const auto default_base_url = ava::config::default_base_url_for_provider(requested_provider)
                                      .value_or("https://api.anthropic.com");
    return std::make_shared<AnthropicProvider>(
        model,
        *api_key,
        credential->base_url.value_or(default_base_url),
        "2023-06-01",
        normalized
    );
  }

  const auto deferred = deferred_provider_names();
  if(contains_provider(deferred, normalized)) {
    throw ProviderException(ProviderError{
        .kind = ProviderErrorKind::Unknown,
        .provider = normalized,
        .message = "provider '" + normalized
                   + "' is recognized but not implemented in the C++ provider runtime; implemented providers: "
                   + join_provider_list(implemented_provider_names()),
    });
  }

  std::string message = "unknown provider '" + requested_provider + "'";
  if(normalized != credential_provider) {
    message += " (normalized to '" + normalized + "')";
  }
  auto known = ava::config::known_providers();
  known.push_back("mock");
  message += "; known providers: " + join_provider_list(std::move(known));

  throw ProviderException(ProviderError{
      .kind = ProviderErrorKind::Unknown,
      .provider = requested_provider,
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
