#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ava/config/auth.h"
#include "ava/config/oauth_crypto_support.h"
#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::config::detail {

[[nodiscard]] bool is_complete_json_object(std::string_view value);
[[nodiscard]] std::optional<std::string> oauth_account_id_from_token(std::string_view token);
[[nodiscard]] ava::core::Result<OpenAICredential> parse_oauth_token_response(std::string_view body,
                                                                             long long now_seconds,
                                                                             std::string_view refresh_fallback,
                                                                             std::string_view account_id_fallback);
[[nodiscard]] ava::core::Result<ava::provider::HttpResponse> post_oauth_token_form(std::string body,
                                                                                   ava::provider::Transport& transport,
                                                                                   std::string_view token_url,
                                                                                   std::string_view failure_message);

}  // namespace ava::config::detail
