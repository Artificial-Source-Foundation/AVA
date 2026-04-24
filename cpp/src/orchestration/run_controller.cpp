#include "ava/orchestration/run_controller.hpp"

#include <utility>

namespace ava::orchestration {

bool RunCancellationToken::is_cancelled() const {
  return cancelled_flag_ != nullptr && cancelled_flag_->load(std::memory_order_acquire);
}

void RunCancellationHandle::cancel() const {
  if(cancelled_flag_ == nullptr) {
    return;
  }
  cancelled_flag_->store(true, std::memory_order_release);
}

bool RunCancellationHandle::is_cancelled() const {
  return cancelled_flag_ != nullptr && cancelled_flag_->load(std::memory_order_acquire);
}

RunController::RunController(std::string session_id)
    : session_id_(std::move(session_id)) {}

RunLease RunController::begin_run() {
  const auto counter = next_run_counter_.fetch_add(1, std::memory_order_acq_rel);
  auto cancelled_flag = std::make_shared<std::atomic<bool>>(false);

  return RunLease{
      .run_id = session_id_ + ":run:" + std::to_string(counter),
      .token = RunCancellationToken{cancelled_flag},
      .handle = RunCancellationHandle{std::move(cancelled_flag)},
  };
}

}  // namespace ava::orchestration
