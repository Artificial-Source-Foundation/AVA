#pragma once

#include <chrono>
#include <optional>
#include <string_view>

namespace ava::tools::retry {

inline constexpr std::size_t MAX_RETRIES = 2;

[[nodiscard]] bool is_retryable_tool(std::string_view tool_name);
[[nodiscard]] bool is_transient_error(std::string_view error_message);
[[nodiscard]] std::optional<std::chrono::milliseconds> backoff_for_attempt(std::size_t attempt);

}  // namespace ava::tools::retry
