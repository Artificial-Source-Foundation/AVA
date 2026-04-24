#include "ava/orchestration/interactive.hpp"

#include <stdexcept>

namespace ava::orchestration {
InteractiveBridge::InteractiveBridge(
    std::optional<std::string> run_id,
    InteractiveApprovalResolver approval_resolver,
    InteractiveQuestionResolver question_resolver,
    InteractivePlanResolver plan_resolver
)
    : run_id_(std::move(run_id)),
      approval_resolver_(std::move(approval_resolver)),
      question_resolver_(std::move(question_resolver)),
      plan_resolver_(std::move(plan_resolver)),
      approval_requests_(
          std::make_shared<ava::control_plane::InteractiveRequestStore>(
              ava::control_plane::InteractiveRequestKind::Approval
          )
      ),
      question_requests_(
          std::make_shared<ava::control_plane::InteractiveRequestStore>(
              ava::control_plane::InteractiveRequestKind::Question
          )
      ),
      plan_requests_(
          std::make_shared<ava::control_plane::InteractiveRequestStore>(ava::control_plane::InteractiveRequestKind::Plan)
      ) {}

ava::tools::ToolApproval InteractiveBridge::request_approval(
    const ava::types::ToolCall& call,
    const ava::tools::PermissionInspection& inspection
) const {
  const auto run_id = [&] {
    const std::lock_guard<std::mutex> lock(run_id_mutex_);
    return run_id_;
  }();
  const auto handle = approval_requests_->register_request(run_id);
  if(!approval_resolver_) {
    settle_request(*approval_requests_, handle, ava::control_plane::InteractiveRequestState::Cancelled);
    return ava::tools::ToolApproval::rejected(
        "tool '" + call.name + "' requires approval and no interactive resolver is configured"
    );
  }

  ApprovalResolution resolution;
  try {
    resolution = approval_resolver_(
        handle,
        ApprovalRequestPayload{
            .call = call,
            .inspection = inspection,
        }
    );
  } catch(...) {
    (void) approval_requests_->cancel(handle.request_id);
    throw;
  }
  settle_request(*approval_requests_, handle, resolution.state);
  if(resolution.state == ava::control_plane::InteractiveRequestState::Cancelled) {
    return ava::tools::ToolApproval::rejected("tool approval request cancelled");
  }
  if(resolution.state == ava::control_plane::InteractiveRequestState::TimedOut) {
    return ava::tools::ToolApproval::rejected("tool approval request timed out");
  }
  return resolution.approval;
}

std::optional<std::string> InteractiveBridge::request_question(
    std::string question,
    std::vector<std::string> options
) const {
  const auto run_id = [&] {
    const std::lock_guard<std::mutex> lock(run_id_mutex_);
    return run_id_;
  }();
  const auto handle = question_requests_->register_request(run_id);
  if(!question_resolver_) {
    settle_request(*question_requests_, handle, ava::control_plane::InteractiveRequestState::Cancelled);
    return std::nullopt;
  }

  QuestionResolution resolution;
  try {
    resolution = question_resolver_(
        handle,
        QuestionRequestPayload{
            .question = std::move(question),
            .options = std::move(options),
        }
    );
  } catch(...) {
    (void) question_requests_->cancel(handle.request_id);
    throw;
  }
  settle_request(*question_requests_, handle, resolution.state);
  if(resolution.state != ava::control_plane::InteractiveRequestState::Resolved) {
    return std::nullopt;
  }
  return resolution.answer;
}

bool InteractiveBridge::request_plan(nlohmann::json plan) const {
  const auto run_id = [&] {
    const std::lock_guard<std::mutex> lock(run_id_mutex_);
    return run_id_;
  }();
  const auto handle = plan_requests_->register_request(run_id);
  if(!plan_resolver_) {
    settle_request(*plan_requests_, handle, ava::control_plane::InteractiveRequestState::Cancelled);
    return false;
  }

  PlanResolution resolution;
  try {
    resolution = plan_resolver_(handle, PlanRequestPayload{.plan = std::move(plan)});
  } catch(...) {
    (void) plan_requests_->cancel(handle.request_id);
    throw;
  }
  settle_request(*plan_requests_, handle, resolution.state);
  if(resolution.state != ava::control_plane::InteractiveRequestState::Resolved) {
    return false;
  }
  return resolution.accepted;
}

void InteractiveBridge::settle_request_for_testing(
    ava::control_plane::InteractiveRequestKind kind,
    const std::string& request_id,
    ava::control_plane::InteractiveRequestState state
) const {
  auto handle = ava::control_plane::InteractiveRequestHandle{
      .request_id = request_id,
      .kind = kind,
      .state = ava::control_plane::InteractiveRequestState::Pending,
      .run_id = [&] {
        const std::lock_guard<std::mutex> lock(run_id_mutex_);
        return run_id_;
      }(),
  };

  switch(kind) {
    case ava::control_plane::InteractiveRequestKind::Approval:
      settle_request(*approval_requests_, handle, state);
      return;
    case ava::control_plane::InteractiveRequestKind::Question:
      settle_request(*question_requests_, handle, state);
      return;
    case ava::control_plane::InteractiveRequestKind::Plan:
      settle_request(*plan_requests_, handle, state);
      return;
  }
}

void InteractiveBridge::set_run_id(std::optional<std::string> run_id) {
  const std::lock_guard<std::mutex> lock(run_id_mutex_);
  run_id_ = std::move(run_id);
}

void InteractiveBridge::settle_request(
    ava::control_plane::InteractiveRequestStore& store,
    const ava::control_plane::InteractiveRequestHandle& handle,
    ava::control_plane::InteractiveRequestState state
) const {
  auto ensure_idempotent_or_throw = [&](ava::control_plane::InteractiveRequestState expected, const char* message) {
    const auto existing = store.request_by_id(handle.request_id);
    if(existing.has_value() && existing->state == expected) {
      return;
    }
    throw std::logic_error(message);
  };

  switch(state) {
    case ava::control_plane::InteractiveRequestState::Resolved:
      if(!store.resolve(handle.request_id).has_value()) {
        ensure_idempotent_or_throw(
            ava::control_plane::InteractiveRequestState::Resolved,
            "interactive request resolution transition failed"
        );
      }
      return;
    case ava::control_plane::InteractiveRequestState::Cancelled:
      if(!store.cancel(handle.request_id).has_value()) {
        ensure_idempotent_or_throw(
            ava::control_plane::InteractiveRequestState::Cancelled,
            "interactive request cancel transition failed"
        );
      }
      return;
    case ava::control_plane::InteractiveRequestState::TimedOut:
      if(!store.timeout(handle.request_id).has_value()) {
        ensure_idempotent_or_throw(
            ava::control_plane::InteractiveRequestState::TimedOut,
            "interactive request timeout transition failed"
        );
      }
      return;
    case ava::control_plane::InteractiveRequestState::Pending:
      if(!store.cancel(handle.request_id).has_value()) {
        ensure_idempotent_or_throw(
            ava::control_plane::InteractiveRequestState::Cancelled,
            "interactive request cancel transition failed for invalid pending state"
        );
      }
      throw std::invalid_argument("resolver returned invalid terminal state: pending");
  }
}

}  // namespace ava::orchestration
