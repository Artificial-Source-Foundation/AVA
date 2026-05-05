#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ava/config/auth.h"
#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::config::detail {

[[nodiscard]] std::string base64_url_encode(std::span<std::uint8_t const> bytes);
[[nodiscard]] std::optional<std::vector<std::uint8_t>> base64_url_decode(std::string_view value);
[[nodiscard]] std::string code_challenge(std::string_view verifier);
[[nodiscard]] std::string url_encode(std::string_view value);
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
[[nodiscard]] ava::core::Result<std::string> random_token(std::size_t bytes);

}  // namespace ava::config::detail
