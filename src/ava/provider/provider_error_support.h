#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "ava/provider/provider.h"

namespace ava::provider::detail {

[[nodiscard]] std::string lower_copy(std::string_view value);
[[nodiscard]] bool has_any(std::string_view haystack, std::initializer_list<std::string_view> needles);
[[nodiscard]] bool looks_like_context_overflow(std::string_view text);
[[nodiscard]] bool looks_like_quota(std::string_view text);
[[nodiscard]] bool looks_like_content_filter(std::string_view text);
[[nodiscard]] bool looks_like_refusal(std::string_view text);

}  // namespace ava::provider::detail
