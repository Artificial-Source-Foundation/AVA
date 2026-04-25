#include "ava/tools/permission_middleware.hpp"

#include <stdexcept>
#include <utility>

namespace ava::tools {

PermissionInspection AllowAllPermissionInspector::inspect(
    const std::string& tool_name,
    const nlohmann::json& arguments
) const {
  (void)tool_name;
  (void)arguments;
  return PermissionInspection{};
}

PermissionInspection PermissionInspector::inspect(
    const std::string& tool_name,
    const nlohmann::json& arguments,
    const ToolSource& source
) const {
  (void)source;
  return inspect(tool_name, arguments);
}

PermissionInspection DefaultHeadlessPermissionInspector::inspect(
    const std::string& tool_name,
    const nlohmann::json& arguments
) const {
  return inspect(tool_name, arguments, ToolSource::built_in());
}

PermissionInspection DefaultHeadlessPermissionInspector::inspect(
    const std::string& tool_name,
    const nlohmann::json& arguments,
    const ToolSource& source
) const {
  if(source.kind == ToolSourceKind::MCP) {
    return PermissionInspection{
        .action = PermissionAction::Ask,
        .reason = "headless mode requires explicit approval for MCP tools",
        .risk_level = risk_level_to_string(RiskLevel::High),
    };
  }

  if(source.kind == ToolSourceKind::Custom) {
    return PermissionInspection{
        .action = PermissionAction::Ask,
        .reason = "headless mode requires explicit approval for custom tools",
        .risk_level = risk_level_to_string(RiskLevel::High),
    };
  }

  if(tool_name.rfind("mcp_", 0) == 0) {
    return PermissionInspection{
        .action = PermissionAction::Ask,
        .reason = "headless mode requires explicit approval for MCP tools",
        .risk_level = risk_level_to_string(RiskLevel::High),
    };
  }

  if(tool_name == "bash") {
    const auto command = arguments.value("command", std::string{});
    const auto classification = classify_bash_command(command);
    if(classification.risk_level == RiskLevel::Critical) {
      return PermissionInspection{
          .action = PermissionAction::Deny,
          .reason = classification.reason,
          .risk_level = risk_level_to_string(classification.risk_level),
      };
    }
    return PermissionInspection{
        .action = PermissionAction::Ask,
        .reason = classification.reason,
        .risk_level = risk_level_to_string(classification.risk_level),
    };
  }

  if(tool_name == "write" || tool_name == "edit") {
    return PermissionInspection{
        .action = PermissionAction::Ask,
        .reason = "headless mode requires explicit approval bridge for mutating tools",
        .risk_level = risk_level_to_string(RiskLevel::High),
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
  before_with_source(tool_call, ToolSource::built_in());
}

std::string PermissionMiddleware::approval_subject(
    const ava::types::ToolCall& tool_call,
    const ToolSource& source,
    const PermissionInspection& inspection
) {
  return source.to_string() + "\n" + tool_call.name + "\n" + inspection.risk_level + "\n" + tool_call.arguments.dump();
}

void PermissionMiddleware::before_with_source(const ava::types::ToolCall& tool_call, const ToolSource& source) const {
  const auto inspection = inspector_->inspect(tool_call.name, tool_call.arguments, source);

  if(inspection.action == PermissionAction::Deny) {
    throw std::runtime_error("Permission denied: " + inspection.reason);
  }

  if(inspection.action == PermissionAction::Allow) {
    return;
  }

  const auto subject = approval_subject(tool_call, source, inspection);

  {
    const std::lock_guard<std::mutex> lock(session_approved_mutex_);
    if(session_approved_.contains(subject)) {
      return;
    }
  }

  if(!approval_bridge_) {
    throw std::runtime_error(
        "Permission denied: tool '" + tool_call.name + "' requires approval but no approval bridge is attached"
    );
  }

  {
    std::unique_lock<std::mutex> lock(session_approved_mutex_);
    if(session_approved_.contains(subject)) {
      return;
    }

    while(session_approval_inflight_.contains(subject)) {
      session_approved_cv_.wait(lock, [&] {
        return !session_approval_inflight_.contains(subject);
      });
      if(session_approved_.contains(subject)) {
        return;
      }
    }

    session_approval_inflight_.insert(subject);
  }

  const auto finish_approval = [&](bool approve_for_session) {
    {
      const std::lock_guard<std::mutex> lock(session_approved_mutex_);
      if(approve_for_session) {
        session_approved_.insert(subject);
      }
      session_approval_inflight_.erase(subject);
    }
    session_approved_cv_.notify_all();
  };

  ToolApproval approval;
  try {
    approval = approval_bridge_->request_approval(tool_call, inspection);
  } catch(...) {
    finish_approval(false);
    throw;
  }

  switch(approval.kind) {
    case ToolApprovalKind::Allowed:
      finish_approval(false);
      return;
    case ToolApprovalKind::AllowedForSession:
      finish_approval(true);
      return;
    case ToolApprovalKind::AllowAlways:
      finish_approval(false);
      throw std::runtime_error(
          "Permission denied: persistent AllowAlways rules are not implemented in the C++ backend yet"
      );
    case ToolApprovalKind::Rejected: {
      finish_approval(false);
      const auto reason = approval.reason.value_or("Tool approval rejected by user");
      throw std::runtime_error("Permission denied: " + reason);
    }
  }
}

}  // namespace ava::tools
