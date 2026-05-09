#pragma once

#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::provider::detail {

inline constexpr std::string_view kOpenAIResponsesReasoningFormat = "openai_responses";

[[nodiscard]] std::optional<std::string> first_string_field(std::string_view object, std::initializer_list<std::string_view> keys);
[[nodiscard]] std::string reasoning_summary_text_from_object(std::string_view object);
[[nodiscard]] std::string openai_response_stop_reason(std::string_view object);
[[nodiscard]] ava::core::Result<std::vector<StreamEvent>> parse_openai_non_stream_response(std::string_view body);

}  // namespace ava::provider::detail
