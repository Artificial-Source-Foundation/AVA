#pragma once

#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <string_view>

namespace ava::provider::detail {

[[nodiscard]] ava::core::Result<HttpRequest> build_openai_responses_request(ProviderRequest const& request, std::string_view access_token,
                                                                            std::string_view base_url);
[[nodiscard]] ava::core::VoidResult apply_openai_auth_options(HttpRequest& request, ProviderAuthContext const& auth);

}  // namespace ava::provider::detail
