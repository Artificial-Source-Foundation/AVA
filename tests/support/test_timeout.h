#pragma once

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <system_error>

namespace ava::tests {
namespace detail {

// Return the runtime debug timeout when timeout stretching is enabled, otherwise return authored_timeout.
//
// AVA_DEBUG_NO_TIMEOUT stretches deadlines to one hour. A positive integral AVA_DEBUG_NO_TIMEOUT_SECONDS overrides that default; invalid overrides
// deliberately fall back to one hour, matching the CTest configuration behavior.
template <typename Rep, typename Period>
std::chrono::steady_clock::duration effective_test_timeout(std::chrono::duration<Rep, Period> authored_timeout) noexcept
{
  if (std::getenv("AVA_DEBUG_NO_TIMEOUT") == nullptr)
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(authored_timeout);

  std::chrono::seconds::rep debug_seconds = 3600;
  if (char const* value = std::getenv("AVA_DEBUG_NO_TIMEOUT_SECONDS"); value != nullptr)
  {
    std::string_view const text(value);
    std::chrono::seconds::rep parsed = 0;
    auto const result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec == std::errc{} && result.ptr == text.data() + text.size() && parsed > 0)
      debug_seconds = parsed;
  }
  return std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds(debug_seconds));
}

}  // namespace detail

// Return a steady-clock deadline using authored_seconds unless runtime debug timeout stretching is enabled.
//
// The environment is read on every call so individual tests can override it with ScopedEnvVar. The returned deadline is suitable for polling loops;
// this helper does not sleep or otherwise block.
inline std::chrono::steady_clock::time_point now_plus_seconds(std::chrono::seconds::rep authored_seconds) noexcept
{
  return std::chrono::steady_clock::now() + detail::effective_test_timeout(std::chrono::seconds(authored_seconds));
}

// Return a steady-clock deadline using authored_timeout unless runtime debug timeout stretching is enabled.
//
// This overload preserves existing duration-valued timeouts while giving them the same AVA_DEBUG_NO_TIMEOUT behavior as integer-second deadlines.
template <typename Rep, typename Period>
std::chrono::steady_clock::time_point now_plus_seconds(std::chrono::duration<Rep, Period> authored_timeout) noexcept
{
  return std::chrono::steady_clock::now() + detail::effective_test_timeout(authored_timeout);
}

}  // namespace ava::tests
