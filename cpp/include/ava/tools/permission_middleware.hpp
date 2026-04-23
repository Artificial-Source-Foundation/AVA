#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "ava/tools/tool.hpp"

namespace ava::tools {

enum class ToolApprovalKind {
  Allowed,
  AllowedForSession,
  AllowAlways,
  Rejected,
};

struct ToolApproval {
  ToolApprovalKind kind{ToolApprovalKind::Allowed};
  std::optional<std::string> reason;

  [[nodiscard]] static ToolApproval allowed() { return ToolApproval{}; }
  [[nodiscard]] static ToolApproval rejected(std::string reason) {
    return ToolApproval{.kind = ToolApprovalKind::Rejected, .reason = std::move(reason)};
  }
};

enum class PermissionAction {
  Allow,
  Deny,
  Ask,
};

struct PermissionInspection {
  PermissionAction action{PermissionAction::Allow};
  std::string reason;
  std::string risk_level{"low"};
};

class PermissionInspector {
 public:
  virtual ~PermissionInspector() = default;
  [[nodiscard]] virtual PermissionInspection inspect(
      const std::string& tool_name,
      const nlohmann::json& arguments
  ) const = 0;
};

class ApprovalBridge {
 public:
  virtual ~ApprovalBridge() = default;
  [[nodiscard]] virtual ToolApproval request_approval(
      const ava::types::ToolCall& call,
      const PermissionInspection& inspection
  ) const = 0;
};

class AllowAllPermissionInspector final : public PermissionInspector {
 public:
  [[nodiscard]] PermissionInspection inspect(
      const std::string& tool_name,
      const nlohmann::json& arguments
  ) const override;
};

class DefaultHeadlessPermissionInspector final : public PermissionInspector {
 public:
  [[nodiscard]] PermissionInspection inspect(
      const std::string& tool_name,
      const nlohmann::json& arguments
  ) const override;
};

class PermissionMiddleware final : public Middleware {
 public:
  PermissionMiddleware(
      std::shared_ptr<PermissionInspector> inspector,
      std::shared_ptr<ApprovalBridge> approval_bridge = nullptr
  );

  void before(const ava::types::ToolCall& tool_call) const override;

 private:
  std::shared_ptr<PermissionInspector> inspector_;
  std::shared_ptr<ApprovalBridge> approval_bridge_;
  mutable std::mutex session_approved_mutex_;
  mutable std::set<std::string> session_approved_;
};

}  // namespace ava::tools
