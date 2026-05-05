#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/config/auth.h"
#include "ava/core/result.h"

namespace ava::config::detail {

[[nodiscard]] std::string provider_env_key_from_id(std::string_view provider_id);
[[nodiscard]] std::vector<std::string> provider_env_keys(std::string_view provider_id);
[[nodiscard]] std::optional<ProviderCredential> provider_credential_from_env(std::string_view provider_id);

[[nodiscard]] std::optional<std::string> oauth_token_from(std::string_view scope);
[[nodiscard]] std::optional<std::string> openai_api_key_from(std::string_view scope);
[[nodiscard]] std::optional<std::string> generic_api_key_from(std::string_view scope);

[[nodiscard]] std::optional<ProviderCredential> parse_provider_credential(std::string_view content,
                                                                          std::string_view provider_id,
                                                                          std::filesystem::path const& source_path);
[[nodiscard]] ava::core::Result<std::optional<ProviderCredential>> load_provider_credential_from_auth_file(
    XdgPaths const& paths, std::string_view provider_id);

}  // namespace ava::config::detail
