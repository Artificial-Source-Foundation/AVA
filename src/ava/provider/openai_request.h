#pragma once

#include <string_view>

#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::provider::detail {

[[nodiscard]] ava::core::Result<HttpRequest> build_openai_responses_request(const ProviderRequest& request,
                                                                            std::string_view access_token,
                                                                            std::string_view base_url);
[[nodiscard]] ava::core::VoidResult apply_openai_auth_options(HttpRequest& request, const ProviderAuthContext& auth);

}  // namespace ava::provider::detail
