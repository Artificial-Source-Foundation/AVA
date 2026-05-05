#pragma once

#include <string>

#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::provider {

[[nodiscard]] ava::core::Result<std::string> anthropic_request_body_json(ProviderRequest const& request);

}  // namespace ava::provider
