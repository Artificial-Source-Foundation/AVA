#pragma once

#include <optional>
#include <string_view>

#include "ava/provider/provider.h"

namespace ava::provider {

[[nodiscard]] std::optional<TokenUsage> parse_openai_usage(std::string_view body);

}  // namespace ava::provider
