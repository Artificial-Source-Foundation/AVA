#include "ava/tools/tool_permissions.h"

#include <utility>

#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/tools/file_tools.h"

namespace ava::tools {

namespace {

std::string effective_tool_name(ToolContext const& context, ava::permissions::Operation operation,
                                std::string_view tool_name)
{
  if (!tool_name.empty()) return std::string(tool_name);
  if (!context.permission_tool_name.empty()) return context.permission_tool_name;
  return ava::permissions::to_string(operation);
}

ava::core::VoidResult record_permission_audit(ToolContext const& context, PermissionAuditEvent const& event)
{
  if (!context.permission_audit_sink) return {};
  return context.permission_audit_sink(event);
}

PermissionAuditEvent audit_event(ToolContext const& context, std::string permission_request_id,
                                 ava::permissions::Operation operation, std::string tool_name,
                                 ava::permissions::PermissionDecision const& decision,
                                 std::filesystem::path const& target_path, std::string_view command)
{
  return PermissionAuditEvent{.permission_request_id = std::move(permission_request_id),
                              .operation = operation,
                              .mode = context.mode,
                              .tool_name = std::move(tool_name),
                              .action = decision.action,
                              .reason = decision.reason,
                              .risk = decision.risk,
                              .target_path = target_path,
                              .command = std::string(command),
                              .resolution = "",
                              .resolution_source = "policy"};
}

ava::core::Error permission_denied_error(std::string_view error_message,
                                         ava::permissions::PermissionDecision const& decision,
                                         std::filesystem::path const& target_path, std::string_view command,
                                         std::string_view resolution_context)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, std::string(error_message));
  error.with_context("action", ava::permissions::to_string(decision.action));
  error.with_context("reason", decision.reason);
  error.with_context("risk", ava::permissions::to_string(decision.risk));
  if (!command.empty()) {
    error.with_context("command", std::string(command));
  } else {
    error.with_context("path", target_path.string());
  }
  if (decision.action == ava::permissions::PermissionAction::Ask) {
    error.with_context("resolution", std::string(resolution_context));
  }
  return error;
}

}  // namespace

ava::core::VoidResult ensure_permission(ToolContext const& context, ava::permissions::Operation operation,
                                        std::filesystem::path const& target_path, std::string_view command,
                                        std::string_view tool_name, std::string_view error_message,
                                        std::string_view diff_preview, bool diff_truncated)
{
  auto const request_tool_name = effective_tool_name(context, operation, tool_name);
  auto const permission_request_id = ava::core::make_id("permreq");
  auto const decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = operation,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = target_path,
      .command = std::string(command),
  });

  auto policy_event =
      audit_event(context, permission_request_id, operation, request_tool_name, decision, target_path, command);
  if (decision.action == ava::permissions::PermissionAction::Allow ||
      decision.action == ava::permissions::PermissionAction::Deny) {
    policy_event.resolution = ava::permissions::to_string(decision.action);
  }
  if (auto audited = record_permission_audit(context, policy_event); !audited) {
    return std::unexpected(std::move(audited.error()));
  }
  if (decision.action == ava::permissions::PermissionAction::Allow) {
    return {};
  }
  if (decision.action == ava::permissions::PermissionAction::Deny) {
    return std::unexpected(permission_denied_error(error_message, decision, target_path, command, "policy"));
  }

  if (!context.permission_resolver) {
    auto outcome_event = policy_event;
    outcome_event.resolution = "deny";
    outcome_event.resolution_source = "no_resolver";
    if (auto audited = record_permission_audit(context, outcome_event); !audited) {
      return std::unexpected(std::move(audited.error()));
    }
    return std::unexpected(permission_denied_error(error_message, decision, target_path, command, "no_resolver"));
  }

  auto resolution = context.permission_resolver(ava::permissions::PermissionPrompt{
      .permission_request_id = permission_request_id,
      .operation = operation,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = target_path,
      .command = std::string(command),
      .tool_name = request_tool_name,
      .reason = decision.reason,
      .risk = decision.risk,
      .diff_preview = std::string(diff_preview),
      .diff_truncated = diff_truncated,
  });

  auto outcome_event = policy_event;
  outcome_event.resolution_source =
      resolution && *resolution == ava::permissions::PermissionResolution::AllowSessionGrant ? "session_grant"
                                                                                             : "resolver";
  if (!resolution) outcome_event.resolution_source = "resolver_failed";
  outcome_event.resolution = resolution ? ava::permissions::to_string(*resolution) : "deny";
  if (auto audited = record_permission_audit(context, outcome_event); !audited) {
    return std::unexpected(std::move(audited.error()));
  }
  if (resolution && (*resolution == ava::permissions::PermissionResolution::Allow ||
                     *resolution == ava::permissions::PermissionResolution::AllowSessionGrant)) {
    return {};
  }

  auto const resolution_context =
      resolution ? ava::permissions::to_string(*resolution) : std::string("resolver_failed");
  return std::unexpected(permission_denied_error(error_message, decision, target_path, command, resolution_context));
}

std::string permission_audit_data_json(PermissionAuditEvent const& event)
{
  std::string data = "{";
  if (!event.permission_request_id.empty()) {
    data += "\"permission_request_id\":\"" + ava::core::json::escape(event.permission_request_id) + "\",";
  }
  data += "\"operation\":\"" + ava::core::json::escape(ava::permissions::to_string(event.operation)) +
          "\",\"mode\":\"" + ava::core::json::escape(ava::agent::to_string(event.mode)) + "\",\"tool_name\":\"" +
          ava::core::json::escape(event.tool_name) + "\",\"action\":\"" +
          ava::core::json::escape(ava::permissions::to_string(event.action)) + "\",\"reason\":\"" +
          ava::core::json::escape(event.reason) + "\",\"risk\":\"" +
          ava::core::json::escape(ava::permissions::to_string(event.risk)) + "\"";
  if (event.operation != ava::permissions::Operation::RunCommand &&
      event.operation != ava::permissions::Operation::NetworkFetch && !event.target_path.empty()) {
    data += ",\"target_path\":\"" + ava::core::json::escape(event.target_path.string()) + "\"";
  }
  if (!event.command.empty()) {
    data += ",\"command\":\"" + ava::core::json::escape(event.command) + "\"";
  }
  if (!event.resolution.empty()) {
    data += ",\"resolution\":\"" + ava::core::json::escape(event.resolution) + "\"";
  }
  if (!event.resolution_source.empty()) {
    data += ",\"resolution_source\":\"" + ava::core::json::escape(event.resolution_source) + "\"";
  }
  data += '}';
  return data;
}

}  // namespace ava::tools
