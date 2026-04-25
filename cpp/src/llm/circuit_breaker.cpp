#include "ava/llm/circuit_breaker.hpp"

namespace ava::llm {

CircuitBreaker::CircuitBreaker(std::uint32_t failure_threshold, std::chrono::milliseconds cooldown)
    : failure_count_(0), failure_threshold_(failure_threshold), cooldown_(cooldown), state_(State::Closed) {}

CircuitBreaker CircuitBreaker::default_provider() {
  return CircuitBreaker(kDefaultFailureThreshold, std::chrono::seconds(kDefaultCooldownSecs));
}

bool CircuitBreaker::allow_request() {
  const auto state = state_.load(std::memory_order_acquire);
  switch(state) {
    case State::Closed:
      return true;
    case State::Open: {
      std::scoped_lock lock(last_failure_mutex_);
      if(!last_failure_.has_value()) {
        State expected = State::Open;
        return state_.compare_exchange_strong(expected, State::HalfOpen, std::memory_order_acq_rel);
      }
      if(std::chrono::steady_clock::now() - *last_failure_ >= cooldown_) {
        State expected = State::Open;
        return state_.compare_exchange_strong(expected, State::HalfOpen, std::memory_order_acq_rel);
      }
      return false;
    }
    case State::HalfOpen:
      return false;
  }
  return false;
}

void CircuitBreaker::record_success() {
  const auto state = state_.load(std::memory_order_acquire);
  if(state == State::HalfOpen || state == State::Closed) {
    failure_count_.store(0, std::memory_order_release);
    state_.store(State::Closed, std::memory_order_release);
  }
}

void CircuitBreaker::record_failure() {
  const auto previous = failure_count_.fetch_add(1, std::memory_order_acq_rel);
  {
    std::scoped_lock lock(last_failure_mutex_);
    last_failure_ = std::chrono::steady_clock::now();
  }

  const auto state = state_.load(std::memory_order_acquire);
  if(state == State::HalfOpen || previous + 1 >= failure_threshold_) {
    state_.store(State::Open, std::memory_order_release);
  }
}

bool CircuitBreaker::is_open() const {
  return state_.load(std::memory_order_acquire) == State::Open;
}

std::string_view CircuitBreaker::state_name() const {
  switch(state_.load(std::memory_order_acquire)) {
    case State::Closed:
      return "closed";
    case State::Open:
      return "open";
    case State::HalfOpen:
      return "half-open";
  }
  return "unknown";
}

}  // namespace ava::llm
