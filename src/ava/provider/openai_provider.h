#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "ava/config/auth.h"
#include "ava/provider/openai_response_parser.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/provider.h"

namespace ava::provider {

class OpenAIProvider final : public Provider {
 public:
  using Provider::build_request;

  explicit OpenAIProvider(std::string base_url = "https://api.openai.com");
  [[nodiscard]] ava::core::Result<HttpRequest> build_request(const ProviderRequest& request,
                                                             std::string_view access_token) const override;
  [[nodiscard]] ava::core::VoidResult apply_auth_options(HttpRequest& request,
                                                         const ProviderAuthContext& auth) const override;
  [[nodiscard]] std::unique_ptr<StreamParser> create_stream_parser() const override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_response(const HttpResponse& response,
                                                                           bool stream) const override;
  [[nodiscard]] ava::core::Result<HttpRequest> build_request(const ProviderRequest& request,
                                                             const ava::config::OpenAICredential& credential,
                                                             long long now_seconds) const;
  [[nodiscard]] ava::core::Result<HttpRequest> build_request(const ProviderRequest& request,
                                                             const ava::config::OpenAICredential& credential) const;

 private:
  std::string base_url_;
};

}  // namespace ava::provider
