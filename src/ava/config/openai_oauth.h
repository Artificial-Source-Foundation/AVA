#pragma once

#include "ava/http/transport.h"
#include "ava/config/auth.h"
#include "ava/core/result.h"

#include <optional>
#include <string>
#include <string_view>

namespace ava::config {

struct OpenAIOAuthSession
{
  std::string code_verifier;
  std::string state;
  std::string authorization_url;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct OpenAIOAuthDeviceAuthorization
{
  std::string device_auth_id;
  std::string user_code;
  std::string verification_url;
  int interval_seconds = 5;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] std::string openai_oauth_code_challenge(std::string_view verifier);
[[nodiscard]] std::optional<std::string> openai_oauth_account_id_from_token(std::string_view token);
[[nodiscard]] ava::core::Result<OpenAIOAuthSession> make_openai_oauth_session();
[[nodiscard]] ava::core::Result<OpenAIOAuthSession> make_openai_oauth_session(std::string verifier, std::string state);
[[nodiscard]] ava::core::Result<OpenAICredential> exchange_openai_oauth_code(std::string_view code, std::string_view verifier, ava::http::Transport& transport,
                                                                             long long now_seconds);
[[nodiscard]] ava::core::Result<OpenAICredential> exchange_openai_oauth_code(std::string_view code, std::string_view verifier, std::string_view redirect_uri,
                                                                             ava::http::Transport& transport, long long now_seconds);
[[nodiscard]] ava::core::Result<OpenAIOAuthDeviceAuthorization> start_openai_oauth_device_authorization(ava::http::Transport& transport);
[[nodiscard]] ava::core::Result<std::optional<OpenAICredential>> poll_openai_oauth_device_authorization(OpenAIOAuthDeviceAuthorization const& authorization,
                                                                                                        ava::http::Transport& transport, long long now_seconds);
[[nodiscard]] ava::core::Result<OpenAICredential> refresh_openai_oauth_credential(OpenAICredential const& credential, ava::http::Transport& transport,
                                                                                  long long now_seconds);

}  // namespace ava::config
