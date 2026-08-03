#pragma once
#include "ava/http/transport.h"
#include "ava/config/auth.h"
#include "ava/provider/openai_response_parser.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/provider.h"

#include <memory>
#include <string>
#include <string_view>

namespace ava::provider {

// Explicit adapter options so generic Responses providers do not inherit Codex
// OAuth URL/header/body mutations from the built-in OpenAI path.
struct OpenAIProviderOptions
{
  std::string base_url = "https://api.openai.com";
  // When non-empty, used as the exact request URL (no /v1/responses join).
  std::string endpoint = {};
  bool follow_redirects = true;
  bool require_credential = true;
  bool send_authorization_bearer = true;
  // Built-in OpenAI only: rewrite URL/headers/body for ChatGPT Codex OAuth.
  bool enable_codex_oauth_mutations = true;
  // Built-in OAuth omits max_output_tokens via credential_type; generic always includes.
  bool force_include_max_output_tokens = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

class OpenAIProvider final : public Provider
{
 public:
  using Provider::build_request;

  explicit OpenAIProvider(OpenAIProviderOptions options = {});
  explicit OpenAIProvider(std::string base_url);
  [[nodiscard]] ava::core::Result<ava::http::HttpRequest> build_request(ProviderRequest const& request, std::string_view access_token) const override;
  [[nodiscard]] ava::core::Result<ava::http::HttpRequest> build_request(ProviderRequest const& request, ProviderAuthContext const& auth) const override;
  [[nodiscard]] ava::core::VoidResult apply_auth_options(ava::http::HttpRequest& request, ProviderAuthContext const& auth) const override;
  [[nodiscard]] std::unique_ptr<StreamParser> create_stream_parser() const override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_response(ava::http::HttpResponse const& response, bool stream) const override;
  [[nodiscard]] ava::core::Result<ava::http::HttpRequest> build_request(ProviderRequest const& request, ava::config::OpenAICredential const& credential,
                                                                        long long now_seconds) const;
  [[nodiscard]] ava::core::Result<ava::http::HttpRequest> build_request(ProviderRequest const& request, ava::config::OpenAICredential const& credential) const;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  OpenAIProviderOptions options_;
};

}  // namespace ava::provider
