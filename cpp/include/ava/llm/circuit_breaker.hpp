#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>

namespace ava::llm {

inline constexpr std::uint32_t kDefaultFailureThreshold = 5;
inline constexpr std::uint64_t kDefaultCooldownSecs = 30;

class CircuitBreaker {
public:
  CircuitBreaker(std::uint32_t failure_threshold, std::chrono::milliseconds cooldown);

  [[nodiscard]] static CircuitBreaker default_provider();

  [[nodiscard]] bool allow_request();
  void record_success();
  void record_failure();

  [[nodiscard]] bool is_open() const;
  [[nodiscard]] std::string_view state_name() const;

private:
  enum class State : std::uint8_t {
    Closed = 0,
    Open = 1,
    HalfOpen = 2,
  };

  std::atomic<std::uint32_t> failure_count_;
  std::uint32_t failure_threshold_;
  std::chrono::milliseconds cooldown_;
  mutable std::mutex last_failure_mutex_;
  std::optional<std::chrono::steady_clock::time_point> last_failure_;
  std::atomic<State> state_;
};

}  // namespace ava::llm
