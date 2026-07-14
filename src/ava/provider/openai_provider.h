#pragma once

#include "ava/config/auth.h"
#include "ava/provider/openai_response_parser.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/provider.h"

#include <memory>
#include <string>
#include <string_view>

namespace ava::provider {

class OpenAIProvider final : public Provider
{
 public:
  using Provider::build_request;

  explicit OpenAIProvider(std::string base_url = "https://api.openai.com");
  [[nodiscard]] ava::core::Result<HttpRequest> build_request(ProviderRequest const& request, std::string_view access_token) const override;
  [[nodiscard]] ava::core::VoidResult apply_auth_options(HttpRequest& request, ProviderAuthContext const& auth) const override;
  [[nodiscard]] std::unique_ptr<StreamParser> create_stream_parser() const override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_response(HttpResponse const& response, bool stream) const override;
  [[nodiscard]] ava::core::Result<HttpRequest> build_request(ProviderRequest const& request, ava::config::OpenAICredential const& credential,
                                                             long long now_seconds) const;
  [[nodiscard]] ava::core::Result<HttpRequest> build_request(ProviderRequest const& request, ava::config::OpenAICredential const& credential) const;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  std::string base_url_;
};

}  // namespace ava::provider
