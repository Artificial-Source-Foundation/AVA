#include "sys.h"
#include "ava/config/provider_config_internal.h"
#include "ava/core/strict_json.h"

#include <cctype>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ava::config {
namespace provider_config_detail {

ava::core::Result<ProviderProtocol> parse_protocol(Json const& value)
{
  if (!value.is_string())
    return std::unexpected(config_error("provider protocol must be a string", "protocol"));
  auto const text = value.get<std::string>();
  if (text == "openai_chat_completions")
    return ProviderProtocol::OpenAIChatCompletions;
  if (text == "openai_responses")
    return ProviderProtocol::OpenAIResponses;
  if (text == "anthropic_messages")
    return ProviderProtocol::AnthropicMessages;
  return std::unexpected(config_error("provider protocol is not supported", "protocol"));
}

ava::core::Result<ProviderAuthMode> parse_auth_mode(Json const& value)
{
  if (!value.is_string())
    return std::unexpected(config_error("provider auth must be a string", "auth"));
  auto const text = value.get<std::string>();
  if (text == "api_key")
    return ProviderAuthMode::ApiKey;
  if (text == "none")
    return ProviderAuthMode::None;
  return std::unexpected(config_error("provider auth is not supported", "auth"));
}

ava::core::VoidResult reject_unknown_fields(Json const& object, std::set<std::string> const& allowed, std::string_view context)
{
  if (!object.is_object())
    return std::unexpected(config_error(std::string(context) + " must be an object"));
  for (auto const& [key, value] : object.items())
  {
    (void)value;
    if (!allowed.contains(key))
      return std::unexpected(config_error(std::string(context) + " contains an unsupported member", key));
  }
  return {};
}

ava::core::Result<UserProviderDefinition> parse_one_provider(Json const& object)
{
  static std::set<std::string> const allowed{"id", "display_name", "protocol", "base_url", "request_path", "auth", "api_key_env", "compatibility"};
  if (auto unknown = reject_unknown_fields(object, allowed, "provider entry"); !unknown)
    return std::unexpected(std::move(unknown.error()));

  if (!object.contains("id") || !object["id"].is_string())
    return std::unexpected(config_error("provider id is required", "id"));
  auto const id = object["id"].get<std::string>();
  if (!is_valid_provider_id(id))
    return std::unexpected(config_error("provider id is invalid", "id"));

  if (!object.contains("display_name") || !object["display_name"].is_string())
    return std::unexpected(config_error("provider display_name is required", "display_name"));
  auto const display_name = object["display_name"].get<std::string>();
  if (!is_valid_display_name(display_name))
    return std::unexpected(config_error("provider display_name is invalid", "display_name"));

  if (!object.contains("protocol"))
    return std::unexpected(config_error("provider protocol is required", "protocol"));
  auto protocol = parse_protocol(object["protocol"]);
  if (!protocol)
    return std::unexpected(std::move(protocol.error()));

  if (!object.contains("base_url") || !object["base_url"].is_string())
    return std::unexpected(config_error("provider base_url is required", "base_url"));
  auto parsed_base = parse_and_validate_base_url(object["base_url"].get<std::string>());
  if (!parsed_base)
    return std::unexpected(std::move(parsed_base.error()));

  std::string request_path;
  if (object.contains("request_path"))
  {
    if (!object["request_path"].is_string())
      return std::unexpected(config_error("provider request_path must be a string", "request_path"));
    request_path = object["request_path"].get<std::string>();
    if (auto valid = validate_request_path(request_path, "request_path"); !valid)
      return std::unexpected(std::move(valid.error()));
  }
  else
  {
    request_path = std::string(default_request_path_for(*protocol));
  }

  ProviderAuthMode auth = ProviderAuthMode::ApiKey;
  if (object.contains("auth"))
  {
    auto parsed_auth = parse_auth_mode(object["auth"]);
    if (!parsed_auth)
      return std::unexpected(std::move(parsed_auth.error()));
    auth = *parsed_auth;
  }

  std::string api_key_env;
  bool const has_api_key_env = object.contains("api_key_env");
  if (has_api_key_env)
  {
    if (!object["api_key_env"].is_string())
      return std::unexpected(config_error("provider api_key_env must be a string", "api_key_env"));
    api_key_env = object["api_key_env"].get<std::string>();
  }

  if (auth == ProviderAuthMode::None)
  {
    if (has_api_key_env)
      return std::unexpected(config_error("provider auth none rejects api_key_env", "api_key_env"));
  }
  else
  {
    if (!has_api_key_env)
      api_key_env = default_api_key_env_for_provider_id(id);
    if (!is_valid_shell_env_name(api_key_env))
      return std::unexpected(config_error("provider api_key_env is invalid", "api_key_env"));
  }

  UserProviderCompatibility compatibility;
  if (object.contains("compatibility"))
  {
    auto const& compat = object["compatibility"];
    static std::set<std::string> const compat_allowed{"include_stream_usage"};
    if (auto unknown = reject_unknown_fields(compat, compat_allowed, "provider compatibility"); !unknown)
      return std::unexpected(std::move(unknown.error()));
    if (compat.contains("include_stream_usage"))
    {
      if (!compat["include_stream_usage"].is_boolean())
        return std::unexpected(config_error("provider compatibility.include_stream_usage must be a boolean", "include_stream_usage"));
      compatibility.include_stream_usage = compat["include_stream_usage"].get<bool>();
      if (compatibility.include_stream_usage && *protocol != ProviderProtocol::OpenAIChatCompletions)
        return std::unexpected(config_error("include_stream_usage is only valid for openai_chat_completions", "include_stream_usage"));
    }
  }

  UserProviderDefinition definition;
  definition.id = id;
  definition.display_name = display_name;
  definition.protocol = *protocol;
  definition.base_url = std::move(parsed_base->canonical_base);
  definition.request_path = std::move(request_path);
  definition.endpoint = join_endpoint(definition.base_url, definition.request_path);
  definition.auth = auth;
  definition.api_key_env = std::move(api_key_env);
  definition.compatibility = compatibility;
  return definition;
}

}  // namespace provider_config_detail

ava::core::Result<std::vector<UserProviderDefinition>> parse_user_provider_definitions(std::string_view content)
{
  using provider_config_detail::config_error;
  using provider_config_detail::kMaxStrictJsonDepth;
  using provider_config_detail::parse_one_provider;
  using provider_config_detail::reject_unknown_fields;

  if (content.size() > kMaxUserProviderConfigBytes)
    return std::unexpected(config_error("providers config is too large"));

  auto const strict = ava::core::validate_strict_json(content, kMaxStrictJsonDepth);
  if (strict != ava::core::StrictJsonStatus::Valid)
  {
    if (strict == ava::core::StrictJsonStatus::DuplicateObjectKey)
      return std::unexpected(config_error("providers config contains a duplicate member"));
    if (strict == ava::core::StrictJsonStatus::NestingTooDeep)
      return std::unexpected(config_error("providers config JSON nesting is too deep"));
    return std::unexpected(config_error("providers config is not valid bounded JSON"));
  }

  provider_config_detail::Json root;
  try
  {
    root = provider_config_detail::Json::parse(content);
  }
  catch (...)
  {
    return std::unexpected(config_error("providers config is not valid JSON"));
  }

  static std::set<std::string> const root_allowed{"version", "providers"};
  if (auto unknown = reject_unknown_fields(root, root_allowed, "providers config"); !unknown)
    return std::unexpected(std::move(unknown.error()));

  if (!root.contains("version") || !root["version"].is_number_integer() || root["version"].get<long long>() != 1)
    return std::unexpected(config_error("providers config requires version 1", "version"));
  if (!root.contains("providers") || !root["providers"].is_array())
    return std::unexpected(config_error("providers config requires a providers array", "providers"));

  auto const& providers = root["providers"];
  if (providers.size() > kMaxUserProviders)
    return std::unexpected(config_error("providers config exceeds the maximum number of providers", "providers"));

  std::vector<UserProviderDefinition> definitions;
  definitions.reserve(providers.size());
  std::set<std::string> seen_ids;
  for (auto const& entry : providers)
  {
    if (!entry.is_object())
      return std::unexpected(config_error("provider entry must be an object", "providers"));
    auto parsed = parse_one_provider(entry);
    if (!parsed)
      return std::unexpected(std::move(parsed.error()));
    if (!seen_ids.insert(parsed->id).second)
      return std::unexpected(config_error("providers config contains a duplicate provider id", "id").with_context("provider_id", parsed->id));
    definitions.push_back(std::move(*parsed));
  }
  return definitions;
}

}  // namespace ava::config
