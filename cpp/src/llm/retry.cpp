#include "ava/llm/retry.hpp"

#include <algorithm>
#include <random>

namespace ava::llm {
namespace {

constexpr auto kPersistentMaxTotal = std::chrono::hours(6);
constexpr auto kPersistentMaxDelay = std::chrono::minutes(5);

[[nodiscard]] std::chrono::milliseconds jitter(std::chrono::milliseconds base) {
  thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_real_distribution<double> dist(0.8, 1.2);
  const auto scaled = static_cast<std::int64_t>(static_cast<double>(base.count()) * dist(rng));
  return std::chrono::milliseconds(std::max<std::int64_t>(scaled, 1));
}

}  // namespace

RetryMode retry_mode_from_config(bool headless, bool is_subagent) {
  return (headless || is_subagent) ? RetryMode::Persistent : RetryMode::Interactive;
}

bool OverloadTracker::record_overload() {
  ++consecutive_overloads;
  return consecutive_overloads >= 3;
}

void OverloadTracker::record_success() {
  consecutive_overloads = 0;
  using_fallback = false;
}

bool OverloadTracker::should_fallback() const {
  return consecutive_overloads >= 3;
}

bool OverloadTracker::is_overload_status(std::uint16_t status) {
  return status == 529 || status == 503;
}

RetryBudget::RetryBudget(std::size_t max_retries) : max_retries_(max_retries), remaining_(max_retries) {}

RetryBudget& RetryBudget::with_delays(std::chrono::milliseconds base_delay, std::chrono::milliseconds max_delay) {
  base_delay_ = base_delay;
  max_delay_ = max_delay;
  return *this;
}

RetryBudget& RetryBudget::with_mode(RetryMode mode) {
  mode_ = mode;
  if(mode_ == RetryMode::Persistent) {
    max_delay_ = std::chrono::duration_cast<std::chrono::milliseconds>(kPersistentMaxDelay);
  }
  return *this;
}

RetryMode RetryBudget::mode() const {
  return mode_;
}

std::size_t RetryBudget::remaining() const {
  return remaining_;
}

std::optional<std::chrono::milliseconds> RetryBudget::should_retry(const ProviderError& error) {
  return compute_delay(error, std::nullopt);
}

std::optional<std::chrono::milliseconds> RetryBudget::should_retry_with_hint(
    const ProviderError& error,
    std::optional<std::chrono::milliseconds> server_hint
) {
  return compute_delay(error, server_hint);
}

void RetryBudget::reset() {
  remaining_ = max_retries_;
  started_at_.reset();
}

std::optional<std::chrono::milliseconds> RetryBudget::compute_delay(
    const ProviderError& error,
    std::optional<std::chrono::milliseconds> server_hint
) {
  if(!is_retryable(error)) {
    return std::nullopt;
  }

  const auto now = std::chrono::steady_clock::now();

  if(mode_ == RetryMode::Interactive) {
    if(remaining_ == 0) {
      return std::nullopt;
    }
    --remaining_;
  } else {
    if(!started_at_.has_value()) {
      started_at_ = now;
    }
    if(now - *started_at_ >= kPersistentMaxTotal) {
      return std::nullopt;
    }
    if(remaining_ > 0) {
      --remaining_;
    }
  }

  const std::size_t attempt = max_retries_ - std::min(remaining_, max_retries_);
  const std::size_t clamped_shift = attempt == 0 ? 0 : std::min<std::size_t>(attempt - 1, 30);
  const auto exponential = base_delay_ * (1U << clamped_shift);

  const auto cap = (mode_ == RetryMode::Persistent)
                       ? std::chrono::duration_cast<std::chrono::milliseconds>(kPersistentMaxDelay)
                       : max_delay_;
  auto delayed = jitter(exponential);

  if(error.kind == ProviderErrorKind::RateLimit && error.retry_after_secs.has_value()) {
    const auto retry_after = std::chrono::seconds(*error.retry_after_secs);
    delayed = std::max(delayed, std::chrono::duration_cast<std::chrono::milliseconds>(retry_after));
  }
  if(server_hint.has_value()) {
    delayed = std::max(delayed, *server_hint);
  }

  return std::min(delayed, cap);
}

}  // namespace ava::llm
