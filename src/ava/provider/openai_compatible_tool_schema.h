#pragma once

#include <string>
#include <string_view>

#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::provider {

[[nodiscard]] ava::core::VoidResult validate_openai_compatible_tools_json(ProviderRequest const& request);
[[nodiscard]] ava::core::Result<std::string> chat_completion_tool_json(std::string_view schema);

}  // namespace ava::provider
