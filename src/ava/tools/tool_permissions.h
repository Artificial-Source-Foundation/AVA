#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "ava/agent/mode.h"
#include "ava/core/result.h"
#include "ava/permissions/permission.h"

namespace ava::tools {

struct ToolContext;

struct PermissionAuditEvent {
  std::string permission_request_id = {};
  ava::permissions::Operation operation;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  std::string tool_name;
  ava::permissions::PermissionAction action = ava::permissions::PermissionAction::Deny;
  std::string reason;
  ava::permissions::PermissionRisk risk = ava::permissions::PermissionRisk::Low;
  std::filesystem::path target_path;
  std::string command;
  std::string resolution;
  std::string resolution_source;
};

using PermissionAuditSink = std::function<ava::core::VoidResult(PermissionAuditEvent const&)>;

[[nodiscard]] ava::core::VoidResult ensure_permission(ToolContext const& context, ava::permissions::Operation operation,
                                                      std::filesystem::path const& target_path,
                                                      std::string_view command, std::string_view tool_name,
                                                      std::string_view error_message,
                                                      std::string_view diff_preview = {}, bool diff_truncated = false);
[[nodiscard]] std::string permission_audit_data_json(PermissionAuditEvent const& event);

}  // namespace ava::tools
