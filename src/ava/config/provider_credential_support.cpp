#include "ava/config/provider_credential_support.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "ava/config/auth_storage.h"
#include "ava/core/json.h"

namespace ava::config::detail {

std::string provider_env_key_from_id(std::string_view provider_id)
{
  std::string key;
  key.reserve(provider_id.size() + std::string_view("_API_KEY").size());
  for (char const ch : provider_id) {
    auto const uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0) {
      key.push_back(static_cast<char>(std::toupper(uch)));
    } else if (ch == '-' || ch == '_') {
      key.push_back('_');
    }
  }
  key += "_API_KEY";
  return key;
}

std::vector<std::string> provider_env_keys(std::string_view provider_id)
{
  std::vector<std::string> keys;
  if (provider_id == "openai") keys.push_back("OPENAI_API_KEY");
  if (provider_id == "anthropic") {
    keys.push_back("ANTHROPIC_OAUTH_TOKEN");
    keys.push_back("ANTHROPIC_API_KEY");
  }
  auto const generic = provider_env_key_from_id(provider_id);
  if (!generic.empty() && std::find(keys.begin(), keys.end(), generic) == keys.end()) keys.push_back(generic);
  return keys;
}

std::optional<ProviderCredential> provider_credential_from_env(std::string_view provider_id)
{
  for (auto const& key : provider_env_keys(provider_id)) {
    char const* value = std::getenv(key.c_str());
    if (value == nullptr || std::string_view(value).empty()) continue;
    return ProviderCredential{.provider_id = std::string(provider_id),
                              .access_token = value,
                              .credential_type = key.ends_with("_OAUTH_TOKEN") ? "oauth" : "api_key",
                              .account_id = "",
                              .source = "env:" + key};
  }
  return std::nullopt;
}

std::optional<std::string> oauth_token_from(std::string_view scope)
{
  auto access = ava::core::json::string_field(scope, "access_token");
  if (!access) access = ava::core::json::string_field(scope, "access");
  if (!access) access = ava::core::json::string_field(scope, "token");
  return access;
}

std::optional<std::string> openai_api_key_from(std::string_view scope)
{
  auto key = ava::core::json::string_field(scope, "api_key");
  if (!key) key = ava::core::json::string_field(scope, "key");
  if (!key) key = ava::core::json::string_field(scope, "OPENAI_API_KEY");
  if (!key) key = ava::core::json::string_field(scope, "openai_api_key");
  return key;
}

std::optional<std::string> generic_api_key_from(std::string_view scope)
{
  auto key = ava::core::json::string_field(scope, "api_key");
  if (!key) key = ava::core::json::string_field(scope, "key");
  return key;
}

std::optional<ProviderCredential> parse_provider_credential(std::string_view content, std::string_view provider_id,
                                                            std::filesystem::path const& source_path)
{
  auto const provider = ava::core::json::object_field(content, provider_id);
  if (!provider) return std::nullopt;
  std::string_view const scope(*provider);
  auto const type = ava::core::json::string_field(scope, "type");
  auto account_id = ava::core::json::string_field(scope, "account_id");

  if (type && *type == "oauth") {
    auto token = oauth_token_from(scope);
    if (!token) return std::nullopt;
    return ProviderCredential{.provider_id = std::string(provider_id),
                              .access_token = *token,
                              .credential_type = "oauth",
                              .account_id = account_id.value_or(""),
                              .source = source_path.string()};
  }

  if (type && *type != "api" && *type != "api_key") return std::nullopt;
  auto key = generic_api_key_from(scope);
  if (!key) return std::nullopt;
  return ProviderCredential{.provider_id = std::string(provider_id),
                            .access_token = *key,
                            .credential_type = "api_key",
                            .account_id = account_id.value_or(""),
                            .source = source_path.string()};
}

ava::core::Result<std::optional<ProviderCredential>> load_provider_credential_from_auth_file(
    XdgPaths const& paths, std::string_view provider_id)
{
  auto content = read_auth_text_if_exists(paths.auth_file, true);
  if (!content) return std::unexpected(std::move(content.error()));
  if (!content->content) return std::optional<ProviderCredential>{};
  return parse_provider_credential(*content->content, provider_id, paths.auth_file);
}

}  // namespace ava::config::detail
