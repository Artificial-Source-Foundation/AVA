#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ava/config/auth.h"
#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::config {

struct OpenAIOAuthSession {
  std::string code_verifier;
  std::string state;
  std::string authorization_url;
};

[[nodiscard]] std::string openai_oauth_code_challenge(std::string_view verifier);
[[nodiscard]] std::optional<std::string> openai_oauth_account_id_from_token(std::string_view token);
[[nodiscard]] ava::core::Result<OpenAIOAuthSession> make_openai_oauth_session();
[[nodiscard]] ava::core::Result<OpenAIOAuthSession> make_openai_oauth_session(std::string verifier, std::string state);
[[nodiscard]] ava::core::Result<OpenAICredential> exchange_openai_oauth_code(std::string_view code,
                                                                             std::string_view verifier,
                                                                             ava::provider::Transport& transport,
                                                                             long long now_seconds);
[[nodiscard]] ava::core::Result<OpenAICredential> refresh_openai_oauth_credential(OpenAICredential const& credential,
                                                                                  ava::provider::Transport& transport,
                                                                                  long long now_seconds);

}  // namespace ava::config
