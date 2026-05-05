#pragma once

#include "ava/provider/provider.h"

#include <string>
#include <string_view>

namespace ava::provider {

[[nodiscard]] std::string normalize_anthropic_base_url(std::string base_url);
[[nodiscard]] ava::core::Result<HttpRequest> build_anthropic_http_request(std::string const& base_url,
                                                                          ProviderRequest const& request,
                                                                          std::string_view access_token);

}  // namespace ava::provider
