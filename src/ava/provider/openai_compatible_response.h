#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::provider::detail {

[[nodiscard]] std::string normalized_openai_compatible_finish_reason(std::string_view reason);
[[nodiscard]] std::string sanitized_openai_compatible_snippet(std::string_view body);
[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_openai_compatible_chat_response(
    std::string_view body, std::string_view reasoning_format);

}  // namespace ava::provider::detail
