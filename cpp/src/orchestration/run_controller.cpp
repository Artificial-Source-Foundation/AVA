#include "ava/orchestration/run_controller.hpp"

#include <utility>

namespace ava::orchestration {

bool RunCancellationToken::is_cancelled() const {
  return (cancelled_flag_ != nullptr && cancelled_flag_->load(std::memory_order_acquire)) || is_deadline_expired();
}

bool RunCancellationToken::is_deadline_expired() const {
  return deadline_ != nullptr && deadline_->has_value() && std::chrono::steady_clock::now() >= **deadline_;
}

void RunCancellationHandle::cancel() const {
  if(cancelled_flag_ == nullptr) {
    return;
  }
  cancelled_flag_->store(true, std::memory_order_release);
}

bool RunCancellationHandle::is_cancelled() const {
  return (cancelled_flag_ != nullptr && cancelled_flag_->load(std::memory_order_acquire)) || is_deadline_expired();
}

bool RunCancellationHandle::is_deadline_expired() const {
  return deadline_ != nullptr && deadline_->has_value() && std::chrono::steady_clock::now() >= **deadline_;
}

RunController::RunController(std::string session_id)
    : session_id_(std::move(session_id)) {}

RunLease RunController::begin_run(std::optional<std::chrono::steady_clock::time_point> deadline) {
  const auto counter = next_run_counter_.fetch_add(1, std::memory_order_acq_rel);
  auto cancelled_flag = std::make_shared<std::atomic<bool>>(false);
  auto shared_deadline = std::make_shared<const std::optional<std::chrono::steady_clock::time_point>>(std::move(deadline));

  return RunLease{
      .run_id = session_id_ + ":run:" + std::to_string(counter),
      .token = RunCancellationToken{cancelled_flag, shared_deadline},
      .handle = RunCancellationHandle{std::move(cancelled_flag), std::move(shared_deadline)},
  };
}

}  // namespace ava::orchestration
