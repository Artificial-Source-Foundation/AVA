#include "ava/config/auth.h"
#include "ava/config/auth_file_store.h"
#include "ava/config/auth_record.h"
#include "ava/config/openai_oauth.h"
#include "ava/core/json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace ava::config {
namespace {

constexpr long long kOAuthRefreshSkewSeconds = 300;

std::string env_key_from_provider_id(std::string_view provider_id)
{
  std::string key;
  key.reserve(provider_id.size() + std::string_view("_API_KEY").size());
  for (char const ch : provider_id)
  {
    auto const uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0)
    {
      key.push_back(static_cast<char>(std::toupper(uch)));
    }
    else if (ch == '-' || ch == '_')
    {
      key.push_back('_');
    }
  }
  key += "_API_KEY";
  return key;
}

std::vector<std::string> provider_env_keys(std::string_view provider_id)
{
  std::vector<std::string> keys;
  if (provider_id == "openai")
    keys.push_back("OPENAI_API_KEY");
  if (provider_id == "anthropic")
    keys.push_back("ANTHROPIC_API_KEY");
  auto const generic = env_key_from_provider_id(provider_id);
  if (!generic.empty() && std::find(keys.begin(), keys.end(), generic) == keys.end())
    keys.push_back(generic);
  return keys;
}

std::optional<ProviderCredential> provider_credential_from_env(std::string_view provider_id)
{
  for (auto const& key : provider_env_keys(provider_id))
  {
    char const* value = std::getenv(key.c_str());
    if (value == nullptr || std::string_view(value).empty())
      continue;
    return ProviderCredential{
        .provider_id = std::string(provider_id), .access_token = value, .credential_type = "api_key", .account_id = "", .source = "env:" + key};
  }
  return std::nullopt;
}

std::optional<std::string> oauth_token_from(std::string_view scope)
{
  auto access = ava::core::json::string_field(scope, "access_token");
  if (!access)
    access = ava::core::json::string_field(scope, "access");
  if (!access)
    access = ava::core::json::string_field(scope, "token");
  return access;
}

std::optional<std::string> api_key_from(std::string_view scope)
{
  auto key = ava::core::json::string_field(scope, "api_key");
  if (!key)
    key = ava::core::json::string_field(scope, "key");
  if (!key)
    key = ava::core::json::string_field(scope, "OPENAI_API_KEY");
  if (!key)
    key = ava::core::json::string_field(scope, "openai_api_key");
  return key;
}

std::optional<std::string> generic_api_key_from(std::string_view scope)
{
  auto key = ava::core::json::string_field(scope, "api_key");
  if (!key)
    key = ava::core::json::string_field(scope, "key");
  return key;
}

std::optional<ProviderCredential> parse_provider_credential(std::string_view content, std::string_view provider_id, std::filesystem::path const& source_path)
{
  auto const provider = ava::core::json::object_field(content, provider_id);
  if (!provider)
    return std::nullopt;
  std::string_view const scope(*provider);
  auto const type = ava::core::json::string_field(scope, "type");
  auto account_id = ava::core::json::string_field(scope, "account_id");

  if (type && *type != "api" && *type != "api_key")
    return std::nullopt;
  auto key = generic_api_key_from(scope);
  if (!key)
    return std::nullopt;
  return ProviderCredential{.provider_id = std::string(provider_id),
                            .access_token = *key,
                            .credential_type = "api_key",
                            .account_id = account_id.value_or(""),
                            .source = source_path.string()};
}

ava::core::Result<std::optional<ProviderCredential>> load_provider_credential_from_auth_file(XdgPaths const& paths, std::string_view provider_id)
{
  auto content = read_text_if_exists(paths.auth_file, true);
  if (!content)
    return std::unexpected(std::move(content.error()));
  if (!content->content)
    return std::optional<ProviderCredential>{};
  return parse_provider_credential(*content->content, provider_id, paths.auth_file);
}

std::optional<OpenAICredential> parse_oauth_credential(std::string_view scope, std::filesystem::path const& source_path)
{
  auto access = oauth_token_from(scope);
  if (!access)
    return std::nullopt;
  auto refresh = ava::core::json::string_field(scope, "refresh_token");
  if (!refresh)
    refresh = ava::core::json::string_field(scope, "refresh");
  auto expires = ava::core::json::integer_field(scope, "expires_at");
  if (!expires)
    expires = ava::core::json::integer_field(scope, "expires");
  auto account_id = ava::core::json::string_field(scope, "account_id");
  if (!account_id)
    account_id = ava::core::json::string_field(scope, "accountId");

  return OpenAICredential{
      .type = OpenAICredentialType::OAuth,
      .access_token = *access,
      .refresh_token = refresh.value_or(""),
      .expires_at = expires.value_or(0),
      .account_id = account_id.value_or(""),
      .source_path = source_path,
  };
}

std::optional<OpenAICredential> parse_api_key_credential(std::string_view scope, std::filesystem::path const& source_path)
{
  auto key = api_key_from(scope);
  if (!key)
    return std::nullopt;
  return OpenAICredential{
      .type = OpenAICredentialType::ApiKey,
      .access_token = *key,
      .refresh_token = "",
      .expires_at = 0,
      .account_id = "",
      .source_path = source_path,
  };
}

long long unix_time_seconds()
{
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

bool should_refresh_openai_credential(OpenAICredential const& credential, long long now_seconds)
{
  return credential.type == OpenAICredentialType::OAuth && credential.expires_at > 0 && credential.expires_at <= now_seconds + kOAuthRefreshSkewSeconds;
}

}  // namespace

std::optional<OpenAICredential> parse_openai_credential(std::string_view content, std::filesystem::path const& source_path)
{
  auto const openai = ava::core::json::object_field(content, "openai");
  std::string_view const scope = openai ? std::string_view(*openai) : content;
  if (auto const type = ava::core::json::string_field(scope, "type"))
  {
    if (*type == "oauth")
      return parse_oauth_credential(scope, source_path);
    if (*type == "api" || *type == "api_key")
      return parse_api_key_credential(scope, source_path);
    return std::nullopt;
  }

  if (auto oauth = parse_oauth_credential(scope, source_path))
    return oauth;
  if (auto api_key = parse_api_key_credential(scope, source_path))
    return api_key;

  return std::nullopt;
}

ava::core::Result<std::optional<OpenAICredential>> load_openai_credential(XdgPaths const& paths)
{
  auto explicit_content = read_text_if_exists(paths.auth_file, true);
  if (!explicit_content)
    return std::unexpected(explicit_content.error());
  if (explicit_content->content)
  {
    if (auto credential = parse_openai_credential(*explicit_content->content, paths.auth_file))
      return credential;
  }

  std::array const candidates{legacy_ava_credentials_path(), legacy_compatible_auth_path()};
  std::optional<OpenAICredential> first_api_key;
  for (auto const& path : candidates)
  {
    auto content = read_text_if_exists(path, false);
    if (!content)
      return std::unexpected(content.error());
    if (!content->content)
      continue;
    auto credential = parse_openai_credential(*content->content, path);
    if (!credential)
      continue;
    if (credential->type == OpenAICredentialType::OAuth)
      return credential;
    if (!first_api_key)
      first_api_key = std::move(credential);
  }
  if (first_api_key)
    return first_api_key;
  return std::optional<OpenAICredential>{};
}

ava::core::VoidResult store_openai_credential(XdgPaths const& paths, OpenAICredential const& credential)
{
  std::string body;
  if (credential.type == OpenAICredentialType::ApiKey)
  {
    body = "{\n    \"type\": \"api_key\",\n    \"api_key\": \"" + ava::core::json::escape(credential.access_token) + "\"\n  }";
  }
  else
  {
    body = "{\n    \"type\": \"oauth\",\n    \"access_token\": \"" + ava::core::json::escape(credential.access_token) + "\",\n    \"refresh_token\": \"" +
           ava::core::json::escape(credential.refresh_token) + "\",\n    \"expires_at\": " + std::to_string(credential.expires_at);
    if (!credential.account_id.empty())
    {
      body += ",\n    \"account_id\": \"" + ava::core::json::escape(credential.account_id) + "\"";
    }
    body += "\n  }";
  }

  return store_provider_object(paths, "openai", std::move(body));
}

bool is_openai_credential_expired(OpenAICredential const& credential, long long now_seconds)
{
  return credential.type == OpenAICredentialType::OAuth && credential.expires_at > 0 && credential.expires_at <= now_seconds;
}

ava::core::Result<std::string> openai_access_token_for_request(OpenAICredential const& credential, long long now_seconds)
{
  if (credential.access_token.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "OpenAI credential token is missing"));
  }
  if (is_openai_credential_expired(credential, now_seconds))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "OpenAI OAuth credential is expired");
    error.with_context("expires_at", std::to_string(credential.expires_at));
    if (!credential.source_path.empty())
      error.with_context("source", credential.source_path.string());
    return std::unexpected(std::move(error));
  }
  return credential.access_token;
}

ava::core::Result<std::string> openai_access_token_for_request(OpenAICredential const& credential)
{
  return openai_access_token_for_request(credential, unix_time_seconds());
}

ava::core::Result<OpenAICredential> openai_credential_for_request(XdgPaths const& paths, OpenAICredential const& credential,
                                                                  ava::provider::Transport& transport, long long now_seconds)
{
  if (credential.type == OpenAICredentialType::ApiKey)
  {
    auto token = openai_access_token_for_request(credential, now_seconds);
    if (!token)
      return std::unexpected(token.error());
    return credential;
  }

  if (!should_refresh_openai_credential(credential, now_seconds))
  {
    auto token = openai_access_token_for_request(credential, now_seconds);
    if (!token)
      return std::unexpected(token.error());
    return credential;
  }

  if (credential.refresh_token.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied,
                                  "OpenAI OAuth credential needs refresh but has no refresh token; run `ava connect "
                                  "openai` to re-authenticate");
    error.with_context("expires_at", std::to_string(credential.expires_at));
    if (!credential.source_path.empty())
      error.with_context("source", credential.source_path.string());
    return std::unexpected(std::move(error));
  }

  auto refreshed = refresh_openai_oauth_credential(credential, transport, now_seconds);
  if (!refreshed)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied,
                                  "failed to refresh OpenAI OAuth credential; run `ava connect openai` to "
                                  "re-authenticate");
    error.with_context("cause", refreshed.error().format());
    if (!credential.source_path.empty())
      error.with_context("source", credential.source_path.string());
    return std::unexpected(std::move(error));
  }

  refreshed->source_path = paths.auth_file;
  auto stored = store_openai_credential(paths, *refreshed);
  if (!stored)
    return std::unexpected(stored.error());

  auto token = openai_access_token_for_request(*refreshed, now_seconds);
  if (!token)
    return std::unexpected(token.error());
  return refreshed;
}

ava::core::Result<OpenAICredential> openai_credential_for_request(XdgPaths const& paths, OpenAICredential const& credential,
                                                                  ava::provider::Transport& transport)
{
  return openai_credential_for_request(paths, credential, transport, unix_time_seconds());
}

ava::core::Result<std::optional<ProviderCredential>> provider_credential_for_request(XdgPaths const& paths, std::string_view provider_id,
                                                                                     ava::provider::Transport& transport)
{
  if (provider_id == "openai")
  {
    auto stored = load_openai_credential(paths);
    if (!stored)
      return std::unexpected(std::move(stored.error()));
    if (*stored)
    {
      auto credential = openai_credential_for_request(paths, **stored, transport);
      if (!credential)
        return std::unexpected(std::move(credential.error()));
      auto access_token = openai_access_token_for_request(*credential);
      if (!access_token)
        return std::unexpected(std::move(access_token.error()));
      std::string account_id = credential->account_id;
      if (credential->type == OpenAICredentialType::OAuth && account_id.empty())
      {
        account_id = openai_oauth_account_id_from_token(credential->access_token).value_or("");
      }
      return ProviderCredential{.provider_id = "openai",
                                .access_token = *access_token,
                                .credential_type = credential->type == OpenAICredentialType::OAuth ? "oauth" : "api_key",
                                .account_id = std::move(account_id),
                                .source = credential->source_path.empty() ? "auth_file" : credential->source_path.string()};
    }
  }
  else
  {
    auto stored = load_provider_credential_from_auth_file(paths, provider_id);
    if (!stored)
      return std::unexpected(std::move(stored.error()));
    if (*stored)
      return *stored;
  }

  if (auto env_credential = provider_credential_from_env(provider_id))
    return env_credential;
  return std::optional<ProviderCredential>{};
}

ava::core::VoidResult store_provider_credential(XdgPaths const& paths, ProviderCredential const& credential)
{
  auto body = provider_credential_object_json(credential);
  if (!body)
    return std::unexpected(std::move(body.error()));
  return store_provider_object(paths, credential.provider_id, std::move(*body));
}

std::string authorization_header_value(OpenAICredential const& credential)
{
  return "Bearer " + credential.access_token;
}

}  // namespace ava::config
