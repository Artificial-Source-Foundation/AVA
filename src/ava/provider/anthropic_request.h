#pragma once
#include "ava/http/transport.h"
#include "ava/provider/provider.h"

#include <string>
#include <string_view>

namespace ava::provider {

struct AnthropicProviderOptions;

[[nodiscard]] std::string normalize_anthropic_base_url(std::string base_url);
[[nodiscard]] ava::core::Result<ava::http::HttpRequest> build_anthropic_http_request(AnthropicProviderOptions const& options, ProviderRequest const& request,
                                                                                     std::string_view access_token);
// Compatibility wrapper for tests and callers that still pass a bare base URL.
[[nodiscard]] ava::core::Result<ava::http::HttpRequest> build_anthropic_http_request(std::string const& base_url, ProviderRequest const& request,
                                                                                     std::string_view access_token);

}  // namespace ava::provider
