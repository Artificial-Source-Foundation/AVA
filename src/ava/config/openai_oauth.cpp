#include "ava/config/openai_oauth.h"

#include <string_view>
#include <utility>

#include "ava/config/openai_oauth_support.h"

namespace ava::config {
namespace {

constexpr std::string_view kClientId = "app_EMoamEEZ73f0CkXaXp7hrann";
constexpr std::string_view kAuthorizeUrl = "https://auth.openai.com/oauth/authorize";
constexpr std::string_view kTokenUrl = "https://auth.openai.com/oauth/token";
constexpr std::string_view kRedirectUri = "http://localhost:1455/auth/callback";
constexpr std::string_view kScope = "openid profile email offline_access";

}  // namespace

std::string openai_oauth_code_challenge(std::string_view verifier)
{
  return detail::code_challenge(verifier);
}

std::optional<std::string> openai_oauth_account_id_from_token(std::string_view token)
{
  return detail::oauth_account_id_from_token(token);
}

ava::core::Result<OpenAIOAuthSession> make_openai_oauth_session()
{
  auto verifier = detail::random_token(32);
  if (!verifier) return std::unexpected(verifier.error());
  auto state = detail::random_token(24);
  if (!state) return std::unexpected(state.error());
  return make_openai_oauth_session(*verifier, *state);
}

ava::core::Result<OpenAIOAuthSession> make_openai_oauth_session(std::string verifier, std::string state)
{
  if (verifier.size() < 43 || verifier.size() > 128) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OAuth PKCE verifier length is invalid");
    error.with_context("length", std::to_string(verifier.size()));
    return std::unexpected(std::move(error));
  }
  if (state.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OAuth state is missing"));
  }
  auto const challenge = openai_oauth_code_challenge(verifier);
  std::string url(kAuthorizeUrl);
  url += "?response_type=code";
  url += "&client_id=" + detail::url_encode(kClientId);
  url += "&redirect_uri=" + detail::url_encode(kRedirectUri);
  url += "&scope=" + detail::url_encode(kScope);
  url += "&code_challenge=" + detail::url_encode(challenge);
  url += "&code_challenge_method=S256";
  url += "&state=" + detail::url_encode(state);
  url += "&id_token_add_organizations=true";
  url += "&codex_cli_simplified_flow=true";
  url += "&originator=ava";
  return OpenAIOAuthSession{.code_verifier = std::move(verifier), .state = std::move(state), .authorization_url = url};
}

ava::core::Result<OpenAICredential> exchange_openai_oauth_code(std::string_view code, std::string_view verifier,
                                                               ava::provider::Transport& transport,
                                                               long long now_seconds)
{
  std::string body = "grant_type=authorization_code";
  body += "&client_id=" + detail::url_encode(kClientId);
  body += "&code=" + detail::url_encode(code);
  body += "&code_verifier=" + detail::url_encode(verifier);
  body += "&redirect_uri=" + detail::url_encode(kRedirectUri);

  auto response =
      detail::post_oauth_token_form(std::move(body), transport, kTokenUrl, "OpenAI OAuth token exchange failed");
  if (!response) return std::unexpected(response.error());
  return detail::parse_oauth_token_response(response->body, now_seconds, "", "");
}

ava::core::Result<OpenAICredential> refresh_openai_oauth_credential(OpenAICredential const& credential,
                                                                    ava::provider::Transport& transport,
                                                                    long long now_seconds)
{
  if (credential.type != OpenAICredentialType::OAuth) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                            "OpenAI OAuth refresh requires an OAuth credential"));
  }
  if (credential.refresh_token.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "OpenAI OAuth refresh token is missing"));
  }

  std::string body = "grant_type=refresh_token";
  body += "&refresh_token=" + detail::url_encode(credential.refresh_token);
  body += "&client_id=" + detail::url_encode(kClientId);

  auto response = detail::post_oauth_token_form(std::move(body), transport, kTokenUrl, "OpenAI OAuth refresh failed");
  if (!response) return std::unexpected(response.error());
  auto refreshed =
      detail::parse_oauth_token_response(response->body, now_seconds, credential.refresh_token, credential.account_id);
  if (!refreshed) return std::unexpected(refreshed.error());
  refreshed->source_path = credential.source_path;
  return refreshed;
}

}  // namespace ava::config
