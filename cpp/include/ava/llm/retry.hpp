#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "ava/llm/provider.hpp"

namespace ava::llm {

enum class RetryMode {
  Interactive,
  Persistent,
};

[[nodiscard]] RetryMode retry_mode_from_config(bool headless, bool is_subagent);

struct OverloadTracker {
  std::uint32_t consecutive_overloads{0};
  bool using_fallback{false};

  [[nodiscard]] bool record_overload();
  void record_success();
  [[nodiscard]] bool should_fallback() const;
  [[nodiscard]] static bool is_overload_status(std::uint16_t status);
};

class RetryBudget {
public:
  explicit RetryBudget(std::size_t max_retries);

  RetryBudget& with_delays(std::chrono::milliseconds base_delay, std::chrono::milliseconds max_delay);
  RetryBudget& with_mode(RetryMode mode);

  [[nodiscard]] RetryMode mode() const;
  [[nodiscard]] std::size_t remaining() const;

  [[nodiscard]] std::optional<std::chrono::milliseconds> should_retry(const ProviderError& error);
  [[nodiscard]] std::optional<std::chrono::milliseconds> should_retry_with_hint(
      const ProviderError& error,
      std::optional<std::chrono::milliseconds> server_hint
  );

  void reset();

private:
  [[nodiscard]] std::optional<std::chrono::milliseconds> compute_delay(
      const ProviderError& error,
      std::optional<std::chrono::milliseconds> server_hint
  );

  std::size_t max_retries_;
  std::size_t remaining_;
  std::chrono::milliseconds base_delay_{1000};
  std::chrono::milliseconds max_delay_{60000};
  RetryMode mode_{RetryMode::Interactive};
  std::optional<std::chrono::steady_clock::time_point> started_at_;
};

}  // namespace ava::llm
