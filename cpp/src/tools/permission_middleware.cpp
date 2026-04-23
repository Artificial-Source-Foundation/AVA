#include "ava/tools/permission_middleware.hpp"

#include <stdexcept>

namespace ava::tools {

PermissionInspection AllowAllPermissionInspector::inspect(
    const std::string& tool_name,
    const nlohmann::json& arguments
) const {
  (void)tool_name;
  (void)arguments;
  return PermissionInspection{};
}

PermissionInspection DefaultHeadlessPermissionInspector::inspect(
    const std::string& tool_name,
    const nlohmann::json& arguments
) const {
  (void)arguments;
  if(tool_name == "write" || tool_name == "edit" || tool_name == "bash") {
    return PermissionInspection{
        .action = PermissionAction::Ask,
        .reason = "headless mode requires explicit approval bridge for mutating tools",
        .risk_level = "high",
    };
  }
  return PermissionInspection{};
}

PermissionMiddleware::PermissionMiddleware(
    std::shared_ptr<PermissionInspector> inspector,
    std::shared_ptr<ApprovalBridge> approval_bridge
)
    : inspector_(std::move(inspector)),
      approval_bridge_(std::move(approval_bridge)) {
  if(!inspector_) {
    inspector_ = std::make_shared<AllowAllPermissionInspector>();
  }
}

void PermissionMiddleware::before(const ava::types::ToolCall& tool_call) const {
  {
    const std::lock_guard<std::mutex> lock(session_approved_mutex_);
    if(session_approved_.contains(tool_call.name)) {
      return;
    }
  }

  const auto inspection = inspector_->inspect(tool_call.name, tool_call.arguments);

  if(inspection.action == PermissionAction::Allow) {
    return;
  }

  if(inspection.action == PermissionAction::Deny) {
    throw std::runtime_error("Permission denied: " + inspection.reason);
  }

  if(!approval_bridge_) {
    throw std::runtime_error(
        "Permission denied: tool '" + tool_call.name + "' requires approval but no approval bridge is attached"
    );
  }

  const auto approval = approval_bridge_->request_approval(tool_call, inspection);
  switch(approval.kind) {
    case ToolApprovalKind::Allowed:
      return;
    case ToolApprovalKind::AllowedForSession:
    case ToolApprovalKind::AllowAlways:
      {
        const std::lock_guard<std::mutex> lock(session_approved_mutex_);
        session_approved_.insert(tool_call.name);
      }
      return;
    case ToolApprovalKind::Rejected: {
      const auto reason = approval.reason.value_or("Tool approval rejected by user");
      throw std::runtime_error("Permission denied: " + reason);
    }
  }
}

}  // namespace ava::tools
