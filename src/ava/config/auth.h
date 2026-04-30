#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"

namespace ava::provider {
class Transport;
}

namespace ava::config {

enum class OpenAICredentialType {
  OAuth,
  ApiKey,
};

struct OpenAICredential {
  OpenAICredentialType type = OpenAICredentialType::OAuth;
  std::string access_token;
  std::string refresh_token;
  long long expires_at = 0;
  std::string account_id = "";
  std::filesystem::path source_path;
};

[[nodiscard]] std::optional<OpenAICredential> parse_openai_credential(std::string_view content,
                                                                      const std::filesystem::path& source_path = {});
[[nodiscard]] ava::core::Result<std::optional<OpenAICredential>> load_openai_credential(const XdgPaths& paths);
[[nodiscard]] ava::core::VoidResult store_openai_credential(const XdgPaths& paths, const OpenAICredential& credential);
[[nodiscard]] bool is_openai_credential_expired(const OpenAICredential& credential, long long now_seconds);
[[nodiscard]] ava::core::Result<std::string> openai_access_token_for_request(const OpenAICredential& credential,
                                                                              long long now_seconds);
[[nodiscard]] ava::core::Result<std::string> openai_access_token_for_request(const OpenAICredential& credential);
[[nodiscard]] ava::core::Result<OpenAICredential> openai_credential_for_request(const XdgPaths& paths,
                                                                                const OpenAICredential& credential,
                                                                                ava::provider::Transport& transport,
                                                                                long long now_seconds);
[[nodiscard]] ava::core::Result<OpenAICredential> openai_credential_for_request(const XdgPaths& paths,
                                                                                const OpenAICredential& credential,
                                                                                ava::provider::Transport& transport);
[[nodiscard]] std::string authorization_header_value(const OpenAICredential& credential);

}  // namespace ava::config
