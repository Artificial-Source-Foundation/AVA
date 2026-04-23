#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ava::config {

struct ProviderCredential {
  std::string api_key;
  std::optional<std::string> base_url;
  std::optional<std::string> org_id;
  std::optional<std::string> oauth_token;
  std::optional<std::string> oauth_refresh_token;
  std::optional<std::uint64_t> oauth_expires_at;
  std::optional<std::string> oauth_account_id;
  std::optional<bool> litellm_compatible;
  std::optional<bool> loop_prone;

  [[nodiscard]] bool is_oauth_configured() const;
  [[nodiscard]] bool is_oauth_expired() const;
  [[nodiscard]] std::optional<std::string> effective_api_key() const;
};

struct CredentialStore {
  std::map<std::string, ProviderCredential> providers;

  [[nodiscard]] static CredentialStore load(const std::filesystem::path& path);
  void save(const std::filesystem::path& path) const;

  [[nodiscard]] std::optional<ProviderCredential> get(const std::string& provider) const;
  void set(const std::string& provider, const ProviderCredential& credential);
  bool remove(const std::string& provider);

  [[nodiscard]] std::vector<std::string> provider_names() const;
  [[nodiscard]] std::vector<std::string> configured_providers() const;
};

[[nodiscard]] const std::vector<std::string>& known_providers();
[[nodiscard]] std::string normalize_provider_name(const std::string& provider);
[[nodiscard]] std::optional<std::string> standard_env_var(const std::string& provider);
[[nodiscard]] std::optional<std::string> default_base_url_for_provider(const std::string& provider);

void to_json(nlohmann::json& j, const ProviderCredential& value);
void from_json(const nlohmann::json& j, ProviderCredential& value);
void to_json(nlohmann::json& j, const CredentialStore& value);
void from_json(const nlohmann::json& j, CredentialStore& value);

}  // namespace ava::config
