#include "ava/config/openai_oauth_support.h"

#include <cctype>
#include <utility>

#include "ava/core/json.h"

namespace ava::config::detail {
namespace {

std::string_view trim_ascii(std::string_view value)
{
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  return value;
}

long long token_response_expiry(std::string_view body, long long now_seconds)
{
  if (auto const expires_at = ava::core::json::integer_field(body, "expires_at")) return *expires_at;
  return now_seconds + ava::core::json::integer_field(body, "expires_in").value_or(3600);
}

std::optional<std::string> jwt_payload(std::string_view token)
{
  auto const first = token.find('.');
  if (first == std::string_view::npos) return std::nullopt;
  auto const second = token.find('.', first + 1);
  if (second == std::string_view::npos || token.find('.', second + 1) != std::string_view::npos) return std::nullopt;
  auto decoded = base64_url_decode(token.substr(first + 1, second - first - 1));
  if (!decoded) return std::nullopt;
  return std::string(decoded->begin(), decoded->end());
}

std::optional<std::string> account_id_from_payload(std::string_view payload)
{
  auto account = ava::core::json::string_field(payload, "chatgpt_account_id");
  if (account && !account->empty()) return account;
  auto auth = ava::core::json::object_field(payload, "https://api.openai.com/auth");
  if (auth) {
    account = ava::core::json::string_field(*auth, "chatgpt_account_id");
    if (account && !account->empty()) return account;
  }
  auto const organizations = ava::core::json::objects_in_array_field(payload, "organizations");
  if (!organizations.empty()) {
    account = ava::core::json::string_field(organizations.front(), "id");
    if (account && !account->empty()) return account;
  }
  return std::nullopt;
}

std::optional<std::string> token_response_account_id(std::string_view body, std::string_view access_token)
{
  auto account_id = ava::core::json::string_field(body, "account_id");
  if (account_id && !account_id->empty()) return account_id;
  auto id_token = ava::core::json::string_field(body, "id_token");
  account_id = id_token ? oauth_account_id_from_token(*id_token) : std::optional<std::string>{};
  if (!account_id) account_id = oauth_account_id_from_token(access_token);
  return account_id;
}

}  // namespace

bool is_complete_json_object(std::string_view value)
{
  value = trim_ascii(value);
  if (value.empty() || value.front() != '{') return false;

  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (std::size_t index = 0; index < value.size(); ++index) {
    char const ch = value[index];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\' && in_string) {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) continue;
    if (ch == '{') {
      ++depth;
      continue;
    }
    if (ch == '}') {
      --depth;
      if (depth < 0) return false;
      if (depth == 0) return trim_ascii(value.substr(index + 1)).empty();
    }
  }
  return false;
}

std::optional<std::string> oauth_account_id_from_token(std::string_view token)
{
  auto const payload = jwt_payload(token);
  if (!payload) return std::nullopt;
  return account_id_from_payload(*payload);
}

ava::core::Result<OpenAICredential> parse_oauth_token_response(std::string_view body, long long now_seconds,
                                                               std::string_view refresh_fallback,
                                                               std::string_view account_id_fallback)
{
  if (!is_complete_json_object(body)) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI OAuth token response was malformed JSON"));
  }

  auto access = ava::core::json::string_field(body, "access_token");
  if (!access || access->empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider,
                                            "OpenAI OAuth token response did not include an access token"));
  }
  auto refresh = ava::core::json::string_field(body, "refresh_token");
  auto account_id = token_response_account_id(body, *access);
  if (!account_id && !account_id_fallback.empty()) account_id = std::string(account_id_fallback);

  return OpenAICredential{.type = OpenAICredentialType::OAuth,
                          .access_token = *access,
                          .refresh_token = refresh && !refresh->empty() ? *refresh : std::string(refresh_fallback),
                          .expires_at = token_response_expiry(body, now_seconds),
                          .account_id = account_id.value_or(""),
                          .source_path = {}};
}

ava::core::Result<ava::provider::HttpResponse> post_oauth_token_form(std::string body,
                                                                     ava::provider::Transport& transport,
                                                                     std::string_view token_url,
                                                                     std::string_view failure_message)
{
  ava::provider::HttpRequest request;
  request.method = "POST";
  request.url = std::string(token_url);
  request.headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
  request.body = std::move(body);
  request.timeout_ms = 60000;
  auto response = transport.send(request);
  if (!response) return std::unexpected(response.error());
  if (response->status_code < 200 || response->status_code >= 300) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, std::string(failure_message));
    error.with_context("status", std::to_string(response->status_code));
    return std::unexpected(std::move(error));
  }
  return response;
}

}  // namespace ava::config::detail
