#include "interactive_action_adapter.hpp"

#include <utility>

namespace ava::tui {

InteractiveActionAdapter::InteractiveActionAdapter(std::shared_ptr<ava::orchestration::InteractiveBridge> bridge)
    : bridge_(std::move(bridge)) {}

InteractiveAdapterActionResult InteractiveActionAdapter::apply(const InteractiveAdapterAction& action) const {
  if(!bridge_) {
    return failure("interactive bridge is not available");
  }
  if(action.request_id.empty()) {
    return failure("interactive request_id is required");
  }

  std::optional<ava::control_plane::InteractiveRequestHandle> terminal;
  switch(action.kind) {
    case InteractiveAdapterActionKind::Approve:
      terminal = bridge_->approve_from_adapter(action.request_id);
      break;
    case InteractiveAdapterActionKind::Reject:
      terminal = bridge_->reject_from_adapter(action.request_id, action.value.value_or("rejected from TUI"));
      break;
    case InteractiveAdapterActionKind::Answer:
      terminal = bridge_->answer_from_adapter(action.request_id, action.value.value_or(""));
      break;
    case InteractiveAdapterActionKind::CancelQuestion:
      terminal = bridge_->cancel_question_from_adapter(action.request_id);
      break;
    case InteractiveAdapterActionKind::AcceptPlan:
      terminal = bridge_->accept_plan_from_adapter(action.request_id);
      break;
    case InteractiveAdapterActionKind::RejectPlan:
      terminal = bridge_->reject_plan_from_adapter(action.request_id);
      break;
    default:
      return failure("unknown interactive adapter action kind");
  }

  if(!terminal.has_value()) {
    return failure("interactive request is missing, stale, or already terminal: " + action.request_id);
  }
  return success(std::move(*terminal));
}

InteractiveAdapterActionResult InteractiveActionAdapter::success(
    ava::control_plane::InteractiveRequestHandle request
) const {
  return InteractiveAdapterActionResult{.accepted = true, .terminal_request = std::move(request), .error = {}};
}

InteractiveAdapterActionResult InteractiveActionAdapter::failure(std::string message) const {
  return InteractiveAdapterActionResult{.accepted = false, .terminal_request = std::nullopt, .error = std::move(message)};
}

}  // namespace ava::tui
