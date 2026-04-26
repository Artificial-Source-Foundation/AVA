#include "ava/config/credentials.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include "ava/core/string_utils.hpp"
#include "ava/platform/filesystem.hpp"

namespace ava::config {

bool is_placeholder_api_key(const std::string& key) {
  if(key.empty()) {
    return false;
  }
  const std::string lowered = ava::core::lowercase_ascii(key);
  static const std::set<std::string> kPlaceholders{
      "your-api-key",
      "your_api_key",
      "replace-me",
      "replace_me",
      "replace-with-api-key",
      "replace_with_api_key",
      "sk-...",
  };
  return kPlaceholders.contains(lowered);
}

namespace {

ava::platform::LocalFileSystem g_filesystem;

[[nodiscard]] std::string to_upper_underscore(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    if(ch == '-') {
      return '_';
    }
    return static_cast<char>(std::toupper(ch));
  });
  return value;
}

[[nodiscard]] bool is_provider_configured(const std::string& provider, const ProviderCredential& credential) {
  const auto has_key = !credential.api_key.empty() && !is_placeholder_api_key(credential.api_key);
  const auto has_oauth = credential.is_oauth_configured();
  const auto has_base_url = credential.base_url.has_value();
  return has_key || has_oauth || (provider == "ollama" && has_base_url);
}

[[nodiscard]] std::optional<std::string> env_value(const std::string& name) {
  if(const char* value = std::getenv(name.c_str()); value != nullptr) {
    std::string trimmed(value);
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
      return !std::isspace(ch);
    }));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) {
      return !std::isspace(ch);
    }).base(), trimmed.end());
    if(!trimmed.empty() && !is_placeholder_api_key(trimmed)) {
      return trimmed;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<ProviderCredential> env_override_credential(
    const std::string& provider,
    const ProviderCredential* base
) {
  auto credential = base != nullptr ? *base : ProviderCredential{};
  auto apply_env_api_key = [&credential](const std::string& api_key) {
    credential.api_key = api_key;
    credential.oauth_token.reset();
    credential.oauth_refresh_token.reset();
    credential.oauth_expires_at.reset();
    credential.oauth_account_id.reset();
  };
  const auto provider_key = to_upper_underscore(provider);
  const auto ava_env = "AVA_" + provider_key + "_API_KEY";
  if(const auto override = env_value(ava_env); override.has_value()) {
    apply_env_api_key(*override);
    return credential;
  }

  if(const auto standard = standard_env_var(provider); standard.has_value()) {
    if(const auto override = env_value(*standard); override.has_value()) {
      apply_env_api_key(*override);
      return credential;
    }
  }

  return std::nullopt;
}

void enforce_owner_only_permissions(const std::filesystem::path& path) {
#if !defined(_WIN32)
  std::error_code ec;
  std::filesystem::permissions(
      path,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
      std::filesystem::perm_options::replace,
      ec
  );
  if(ec) {
    throw std::runtime_error("Failed to set secure permissions on file: " + path.string() + " (" + ec.message() + ")");
  }
#else
  (void)path;
#endif
}

}  // namespace

bool ProviderCredential::is_oauth_configured() const {
  return oauth_token.has_value();
}

bool ProviderCredential::is_oauth_expired() const {
  if(!oauth_expires_at.has_value()) {
    return false;
  }
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch()
  )
                       .count();
  return now + 30 >= static_cast<std::int64_t>(*oauth_expires_at);
}

std::optional<std::string> ProviderCredential::effective_api_key() const {
  if(oauth_token.has_value() && !is_oauth_expired()) {
    return oauth_token;
  }
  if(api_key.empty()) {
    return std::nullopt;
  }
  return api_key;
}

CredentialStore CredentialStore::load(const std::filesystem::path& path) {
  if(!g_filesystem.exists(path)) {
    return {};
  }

  CredentialStore store;
  try {
    const auto parsed = nlohmann::json::parse(g_filesystem.read_file(path));
    store = parsed.get<CredentialStore>();
  } catch(const nlohmann::json::exception&) {
    return {};
  } catch(const std::runtime_error&) {
    return {};
  }

  for(auto it = store.providers.begin(); it != store.providers.end();) {
    if(!it->second.is_oauth_configured() && is_placeholder_api_key(it->second.api_key)) {
      it = store.providers.erase(it);
    } else {
      ++it;
    }
  }

  return store;
}

void CredentialStore::save(const std::filesystem::path& path) const {
  if(path.has_parent_path()) {
    g_filesystem.create_dir_all(path.parent_path());
  }

  const auto temp_path = path.string() + ".tmp";
  g_filesystem.write_file(temp_path, nlohmann::json(*this).dump(2));
#if !defined(_WIN32)
  enforce_owner_only_permissions(temp_path);
#endif
  std::filesystem::rename(temp_path, path);
  enforce_owner_only_permissions(path);
}

std::optional<ProviderCredential> CredentialStore::get(const std::string& provider) const {
  const auto it = providers.find(provider);
  if(const auto override = env_override_credential(provider, it == providers.end() ? nullptr : &it->second);
     override.has_value()) {
    return override;
  }

  if(it == providers.end()) {
    return std::nullopt;
  }
  return it->second;
}

void CredentialStore::set(const std::string& provider, const ProviderCredential& credential) {
  providers[provider] = credential;
}

void CredentialStore::set_oauth(
    const std::string& provider,
    const std::string& access_token,
    std::optional<std::string> refresh_token,
    std::optional<std::uint64_t> expires_at,
    std::optional<std::string> account_id
) {
  auto credential = providers.contains(provider) ? providers.at(provider) : ProviderCredential{};
  credential.oauth_token = access_token;
  credential.oauth_refresh_token = std::move(refresh_token);
  credential.oauth_expires_at = expires_at;
  credential.oauth_account_id = std::move(account_id);
  providers[provider] = std::move(credential);
}

bool CredentialStore::remove(const std::string& provider) {
  return providers.erase(provider) > 0;
}

std::vector<std::string> CredentialStore::provider_names() const {
  std::vector<std::string> names;
  names.reserve(providers.size());
  for(const auto& [provider, _] : providers) {
    names.push_back(provider);
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::vector<std::string> CredentialStore::configured_providers() const {
  std::set<std::string> configured;

  for(const auto& provider : provider_names()) {
    const auto credential = get(provider);
    if(!credential.has_value()) {
      continue;
    }
    if(is_provider_configured(provider, *credential)) {
      configured.insert(provider);
    }
  }

  for(const auto& provider : known_providers()) {
    const auto credential = get(provider);
    if(!credential.has_value()) {
      continue;
    }
    if(is_provider_configured(provider, *credential)) {
      configured.insert(provider);
    }
  }

  return {configured.begin(), configured.end()};
}

const std::vector<std::string>& known_providers() {
  static const std::vector<std::string> kProviders{
      "anthropic",
      "openai",
      "openrouter",
      "copilot",
      "gemini",
      "inception",
      "alibaba",
      "zai",
      "kimi",
      "minimax",
      "ollama",
  };
  return kProviders;
}

std::string normalize_provider_name(const std::string& provider) {
  const auto lower = ava::core::lowercase_ascii(provider);
  if(lower == "chatgpt") {
    return "openai";
  }
  if(lower == "google") {
    return "gemini";
  }
  if(lower == "alibaba-cn") {
    return "alibaba";
  }
  if(lower == "kimi-for-coding") {
    return "kimi";
  }
  if(lower == "minimax-coding-plan" || lower == "minimax-cn-coding-plan") {
    return "minimax";
  }
  if(lower == "zhipuai-coding-plan" || lower == "zai-coding-plan") {
    return "zai";
  }
  return lower;
}

std::optional<std::string> standard_env_var(const std::string& provider) {
  const auto normalized = normalize_provider_name(provider);
  static const std::map<std::string, std::string> kEnvMap{
      {"anthropic", "ANTHROPIC_API_KEY"},
      {"openai", "OPENAI_API_KEY"},
      {"openrouter", "OPENROUTER_API_KEY"},
      {"copilot", "GITHUB_TOKEN"},
      {"gemini", "GEMINI_API_KEY"},
      {"inception", "INCEPTION_API_KEY"},
      {"alibaba", "DASHSCOPE_API_KEY"},
      {"zai", "ZAI_API_KEY"},
      {"kimi", "MOONSHOT_API_KEY"},
      {"minimax", "MINIMAX_API_KEY"},
  };

  if(const auto it = kEnvMap.find(normalized); it != kEnvMap.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<std::string> default_base_url_for_provider(const std::string& provider) {
  const auto lower = ava::core::lowercase_ascii(provider);
  if(lower == "alibaba-cn") {
    return "https://coding.dashscope.aliyuncs.com/apps/anthropic/v1";
  }

  const auto normalized = normalize_provider_name(provider);
  static const std::map<std::string, std::string> kBaseUrls{
      {"openai", "https://api.openai.com"},
      {"anthropic", "https://api.anthropic.com"},
      {"openrouter", "https://openrouter.ai/api"},
      {"gemini", "https://generativelanguage.googleapis.com"},
      {"copilot", "https://api.individual.githubcopilot.com"},
      {"inception", "https://api.inceptionlabs.ai"},
      {"ollama", "http://localhost:11434"},
      {"alibaba", "https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1"},
      {"zai", "https://api.z.ai/api/coding/paas/v4"},
      {"kimi", "https://api.kimi.com/coding/v1"},
      {"minimax", "https://api.minimax.io/anthropic/v1"},
  };

  if(const auto it = kBaseUrls.find(normalized); it != kBaseUrls.end()) {
    return it->second;
  }
  return std::nullopt;
}

void to_json(nlohmann::json& j, const ProviderCredential& value) {
  j = nlohmann::json{
      {"api_key", value.api_key},
  };
  if(value.base_url.has_value()) {
    j["base_url"] = *value.base_url;
  }
  if(value.org_id.has_value()) {
    j["org_id"] = *value.org_id;
  }
  if(value.oauth_token.has_value()) {
    j["oauth_token"] = *value.oauth_token;
  }
  if(value.oauth_refresh_token.has_value()) {
    j["oauth_refresh_token"] = *value.oauth_refresh_token;
  }
  if(value.oauth_expires_at.has_value()) {
    j["oauth_expires_at"] = *value.oauth_expires_at;
  }
  if(value.oauth_account_id.has_value()) {
    j["oauth_account_id"] = *value.oauth_account_id;
  }
  if(value.litellm_compatible.has_value()) {
    j["litellm_compatible"] = *value.litellm_compatible;
  }
  if(value.loop_prone.has_value()) {
    j["loop_prone"] = *value.loop_prone;
  }
}

void from_json(const nlohmann::json& j, ProviderCredential& value) {
  if(j.contains("api_key") && !j.at("api_key").is_null()) {
    value.api_key = j.at("api_key").get<std::string>();
  }
  if(j.contains("base_url") && !j.at("base_url").is_null()) {
    value.base_url = j.at("base_url").get<std::string>();
  }
  if(j.contains("org_id") && !j.at("org_id").is_null()) {
    value.org_id = j.at("org_id").get<std::string>();
  }
  if(j.contains("oauth_token") && !j.at("oauth_token").is_null()) {
    value.oauth_token = j.at("oauth_token").get<std::string>();
  }
  if(j.contains("oauth_refresh_token") && !j.at("oauth_refresh_token").is_null()) {
    value.oauth_refresh_token = j.at("oauth_refresh_token").get<std::string>();
  }
  if(j.contains("oauth_expires_at") && !j.at("oauth_expires_at").is_null()) {
    value.oauth_expires_at = j.at("oauth_expires_at").get<std::uint64_t>();
  }
  if(j.contains("oauth_account_id") && !j.at("oauth_account_id").is_null()) {
    value.oauth_account_id = j.at("oauth_account_id").get<std::string>();
  }
  if(j.contains("litellm_compatible") && !j.at("litellm_compatible").is_null()) {
    value.litellm_compatible = j.at("litellm_compatible").get<bool>();
  }
  if(j.contains("loop_prone") && !j.at("loop_prone").is_null()) {
    value.loop_prone = j.at("loop_prone").get<bool>();
  }
}

void to_json(nlohmann::json& j, const CredentialStore& value) {
  j = nlohmann::json{{"providers", value.providers}};
}

void from_json(const nlohmann::json& j, CredentialStore& value) {
  value.providers = j.value("providers", std::map<std::string, ProviderCredential>{});
}

}  // namespace ava::config
