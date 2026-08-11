#include "sys.h"
#include "ava/config/provider_config.h"
#include "ava/config/provider_profiles.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace ava::config {

std::string_view to_string(ProviderProtocol protocol) noexcept
{
  switch (protocol)
  {
    case ProviderProtocol::OpenAIChatCompletions:
      return "openai_chat_completions";
    case ProviderProtocol::OpenAIResponses:
      return "openai_responses";
    case ProviderProtocol::AnthropicMessages:
      return "anthropic_messages";
  }
  return "unknown";
}

std::string_view to_string(ProviderAuthMode auth) noexcept
{
  switch (auth)
  {
    case ProviderAuthMode::ApiKey:
      return "api_key";
    case ProviderAuthMode::None:
      return "none";
  }
  return "unknown";
}

std::string_view default_request_path_for(ProviderProtocol protocol) noexcept
{
  switch (protocol)
  {
    case ProviderProtocol::OpenAIChatCompletions:
      return "/v1/chat/completions";
    case ProviderProtocol::OpenAIResponses:
      return "/v1/responses";
    case ProviderProtocol::AnthropicMessages:
      return "/v1/messages";
  }
  return "/v1/chat/completions";
}

std::string default_api_key_env_for_provider_id(std::string_view provider_id)
{
  std::string key;
  key.reserve(provider_id.size() + std::string_view("_API_KEY").size());
  for (char const ch : provider_id)
  {
    auto const uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0)
      key.push_back(static_cast<char>(std::toupper(uch)));
    else if (ch == '-' || ch == '_')
      key.push_back('_');
  }
  key += "_API_KEY";
  return key;
}

ava::core::VoidResult validate_user_provider_ids_against_reserved(std::span<UserProviderDefinition const> definitions,
                                                                  std::span<std::string_view const> reserved_provider_ids)
{
  for (auto const& definition : definitions)
  {
    for (auto const reserved : reserved_provider_ids)
    {
      if (definition.id == reserved)
      {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Configuration, "user provider id collides with a reserved provider id")
                                   .with_context("field", "id")
                                   .with_context("provider_id", definition.id));
      }
    }
  }
  return {};
}

ava::core::VoidResult validate_user_provider_ids_against_builtins(std::span<UserProviderDefinition const> definitions)
{
  auto const builtins = builtin_provider_profiles();
  std::vector<std::string_view> reserved;
  reserved.reserve(builtins.size());
  for (auto const& profile : builtins) reserved.push_back(profile.provider_id);
  return validate_user_provider_ids_against_reserved(definitions, reserved);
}

}  // namespace ava::config
