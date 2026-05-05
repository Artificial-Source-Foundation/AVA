#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"
#include "ava/provider/openai_usage_parser.h"
#include "ava/provider/provider.h"

namespace ava::provider {

[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_openai_sse(std::string_view sse);
[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_openai_sse_response(HttpResponse const& response);
[[nodiscard]] ava::core::Result<std::string> parse_openai_response_text(std::string_view body);
[[nodiscard]] bool is_retryable_status(int status_code) noexcept;
[[nodiscard]] bool is_auth_status(int status_code) noexcept;

}  // namespace ava::provider
