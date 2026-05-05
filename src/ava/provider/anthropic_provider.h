#pragma once

#include <memory>
#include <string>

#include "ava/provider/anthropic_response.h"
#include "ava/provider/provider.h"

namespace ava::provider {

class AnthropicProvider final : public Provider {
 public:
  using Provider::build_request;

  explicit AnthropicProvider(std::string base_url = "");
  [[nodiscard]] ava::core::Result<HttpRequest> build_request(ProviderRequest const& request,
                                                             std::string_view access_token) const override;
  [[nodiscard]] ava::core::VoidResult apply_auth_options(HttpRequest& request,
                                                         ProviderAuthContext const& auth) const override;
  [[nodiscard]] std::unique_ptr<StreamParser> create_stream_parser() const override;
  [[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_response(HttpResponse const& response,
                                                                           bool stream) const override;

 private:
  std::string base_url_;
};

}  // namespace ava::provider
