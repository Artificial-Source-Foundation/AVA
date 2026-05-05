#include "ava/provider/retry_policy.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>
#include <thread>

namespace ava::provider::detail {

bool is_retryable_kind(ProviderErrorKind kind) noexcept
{
  return kind == ProviderErrorKind::RateLimited || kind == ProviderErrorKind::Transient;
}

bool is_retryable_transport_error(ava::core::Error const& error) noexcept
{
  return error.category() == ava::core::ErrorCategory::Io;
}

int exponential_delay_ms(RetryOptions const& options, int attempt) noexcept
{
  if (options.base_delay_ms <= 0) return 0;
  long long delay = options.base_delay_ms;
  for (int index = 1; index < attempt; ++index) {
    if (delay > std::numeric_limits<int>::max() / 2) return std::numeric_limits<int>::max();
    delay *= 2;
  }
  return static_cast<int>(delay);
}

bool retry_cancel_requested(RetryOptions const& options, Transport::CancelCallback const& cancel_requested)
{
  return (options.cancel_requested && options.cancel_requested()) || (cancel_requested && cancel_requested());
}

ava::core::Error retry_canceled_error()
{
  return ava::core::Error(ava::core::ErrorCategory::Unknown, "transport retry canceled");
}

std::optional<int> retry_after_ms(HttpResponse const& response, int max_retry_after_ms)
{
  auto const value = retry_after_header(response);
  if (!value) return std::nullopt;
  std::size_t index = 0;
  while (index < value->size() && std::isspace(static_cast<unsigned char>((*value)[index])) != 0) ++index;
  auto const start = index;
  while (index < value->size() && std::isdigit(static_cast<unsigned char>((*value)[index])) != 0) ++index;
  if (index == start) return std::nullopt;
  try {
    auto const seconds = std::stoi(value->substr(start, index - start));
    if (seconds < 0) return std::nullopt;
    long long const delay_ms = static_cast<long long>(seconds) * 1000LL;
    return static_cast<int>(std::min(delay_ms, static_cast<long long>(max_retry_after_ms)));
  } catch (...) {
    return std::nullopt;
  }
}

ava::core::VoidResult publish_retry_event(RetryOptions const& options, std::size_t attempt, std::size_t max_attempts,
                                          int delay_ms, std::size_t remaining_ms, std::string_view reason,
                                          int status_code, bool streaming, bool countdown_tick)
{
  if (!options.on_retry) return {};
  return options.on_retry(RetryOptions::Event{.attempt = attempt,
                                              .max_attempts = max_attempts,
                                              .delay_ms = static_cast<std::size_t>(std::max(0, delay_ms)),
                                              .remaining_ms = remaining_ms,
                                              .reason = std::string(reason),
                                              .status_code = status_code,
                                              .streaming = streaming,
                                              .countdown_tick = countdown_tick});
}

ava::core::VoidResult sleep_before_retry(RetryOptions const& options, std::size_t attempt, std::size_t max_attempts,
                                         int delay_ms, std::string_view reason, int status_code, bool streaming,
                                         Transport::CancelCallback const& cancel_requested)
{
  if (delay_ms <= 0) return {};
  auto const tick_ms = std::max(0, options.countdown_tick_ms);
  auto remaining_ms = delay_ms;
  while (remaining_ms > 0) {
    if (retry_cancel_requested(options, cancel_requested)) return std::unexpected(retry_canceled_error());
    auto const chunk_ms = tick_ms > 0 ? std::min(tick_ms, remaining_ms) : remaining_ms;
    std::this_thread::sleep_for(std::chrono::milliseconds(chunk_ms));
    remaining_ms -= chunk_ms;
    if (retry_cancel_requested(options, cancel_requested)) return std::unexpected(retry_canceled_error());
    if (tick_ms > 0) {
      if (auto published =
              publish_retry_event(options, attempt, max_attempts, delay_ms, static_cast<std::size_t>(remaining_ms),
                                  reason, status_code, streaming, true);
          !published) {
        return std::unexpected(std::move(published.error()));
      }
    }
  }
  return {};
}

}  // namespace ava::provider::detail
