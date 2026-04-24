#include "ava/control_plane/interactive.hpp"

#include <algorithm>

namespace ava::control_plane {

std::string_view interactive_request_kind_to_string(InteractiveRequestKind kind) {
  switch(kind) {
    case InteractiveRequestKind::Approval:
      return "approval";
    case InteractiveRequestKind::Question:
      return "question";
    case InteractiveRequestKind::Plan:
      return "plan";
  }
  return "approval";
}

std::string_view interactive_request_state_to_string(InteractiveRequestState state) {
  switch(state) {
    case InteractiveRequestState::Pending:
      return "pending";
    case InteractiveRequestState::Resolved:
      return "resolved";
    case InteractiveRequestState::Cancelled:
      return "cancelled";
    case InteractiveRequestState::TimedOut:
      return "timeout";
  }
  return "pending";
}

InteractiveRequestStore::InteractiveRequestStore(InteractiveRequestKind kind) : kind_(kind) {}

InteractiveRequestHandle InteractiveRequestStore::register_request(std::optional<std::string> run_id) {
  const std::lock_guard<std::mutex> lock(mutex_);
  auto handle = InteractiveRequestHandle{
      .request_id = next_request_id(),
      .kind = kind_,
      .state = InteractiveRequestState::Pending,
      .run_id = std::move(run_id),
  };
  pending_order_.push_back(handle.request_id);
  pending_by_id_[handle.request_id] = handle;
  return handle;
}

std::optional<InteractiveRequestHandle> InteractiveRequestStore::resolve(const std::string& request_id) {
  return transition(request_id, InteractiveRequestState::Resolved);
}

std::optional<InteractiveRequestHandle> InteractiveRequestStore::cancel(const std::string& request_id) {
  return transition(request_id, InteractiveRequestState::Cancelled);
}

std::optional<InteractiveRequestHandle> InteractiveRequestStore::timeout(const std::string& request_id) {
  return transition(request_id, InteractiveRequestState::TimedOut);
}

std::optional<InteractiveRequestHandle> InteractiveRequestStore::current_pending() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  if(pending_order_.empty()) {
    return std::nullopt;
  }
  const auto& id = pending_order_.front();
  const auto it = pending_by_id_.find(id);
  if(it == pending_by_id_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::vector<InteractiveRequestHandle> InteractiveRequestStore::pending_requests() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  std::vector<InteractiveRequestHandle> snapshot;
  snapshot.reserve(pending_order_.size());
  for(const auto& id : pending_order_) {
    const auto it = pending_by_id_.find(id);
    if(it != pending_by_id_.end()) {
      snapshot.push_back(it->second);
    }
  }
  return snapshot;
}

std::optional<InteractiveRequestHandle> InteractiveRequestStore::request_by_id(const std::string& request_id) const {
  const std::lock_guard<std::mutex> lock(mutex_);
  if(const auto pending_it = pending_by_id_.find(request_id); pending_it != pending_by_id_.end()) {
    return pending_it->second;
  }
  if(const auto terminal_it = terminal_by_id_.find(request_id); terminal_it != terminal_by_id_.end()) {
    return terminal_it->second;
  }
  return std::nullopt;
}

std::string InteractiveRequestStore::next_request_id() {
  const auto id = next_request_counter_;
  next_request_counter_ += 1;
  return std::string(interactive_request_kind_to_string(kind_)) + "-" + std::to_string(id);
}

std::optional<InteractiveRequestHandle> InteractiveRequestStore::transition(
    const std::string& request_id,
    InteractiveRequestState terminal_state
) {
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto pending_it = pending_by_id_.find(request_id);
  if(pending_it == pending_by_id_.end()) {
    return std::nullopt;
  }

  auto handle = pending_it->second;
  handle.state = terminal_state;

  pending_by_id_.erase(pending_it);
  pending_order_.erase(std::remove(pending_order_.begin(), pending_order_.end(), request_id), pending_order_.end());
  terminal_order_.push_back(request_id);
  terminal_by_id_[request_id] = handle;
  while(terminal_order_.size() > kMaxTerminalRetention) {
    const auto evicted_id = terminal_order_.front();
    terminal_order_.pop_front();
    terminal_by_id_.erase(evicted_id);
  }
  return handle;
}

}  // namespace ava::control_plane
