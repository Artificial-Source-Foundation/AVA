#include "ava/app/rpc/resolver_support.h"

#include <utility>

#include "ava/app/rpc/protocol.h"
#include "ava/app/rpc/serialization.h"
#include "ava/core/ids.h"

namespace ava::app::rpc::detail {

ava::core::Error no_pending_request_error(std::string_view request_id)
{
  auto error = invalid_rpc("RPC resolver reply has no matching pending request");
  error.with_context("request_id", std::string(request_id));
  return error;
}

std::string next_resolver_request_id(std::string_view prefix)
{
  return ava::core::make_id(prefix);
}

bool grant_matches(PermissionSessionGrant const& grant, ava::permissions::PermissionPrompt const& prompt)
{
  return grant.operation == prompt.operation && grant.mode == prompt.mode && grant.tool_name == prompt.tool_name &&
         grant.target_path == prompt.target_path && grant.command == prompt.command;
}

bool grant_matches(PermissionSessionGrant const& grant, PendingPermissionRequest const& request)
{
  return grant.operation == request.operation && grant.mode == request.mode && grant.tool_name == request.tool_name &&
         grant.target_path == request.target_path && grant.command == request.command;
}

PermissionSessionGrant grant_from_request(PendingPermissionRequest const& request)
{
  return PermissionSessionGrant{.grant_id = ava::core::make_id("permgrant"),
                                .permission_request_id = request.permission_request_id,
                                .operation = request.operation,
                                .mode = request.mode,
                                .tool_name = request.tool_name,
                                .target_path = request.target_path,
                                .command = request.command,
                                .reason = request.reason,
                                .risk = request.risk};
}

std::string permission_session_grant_json(PermissionSessionGrant const& grant)
{
  std::string json = "{";
  json += string_field_json("grant_id", grant.grant_id);
  json += ',';
  json += string_field_json("permission_request_id", grant.permission_request_id);
  json += ',';
  json += string_field_json("operation", ava::permissions::to_string(grant.operation));
  json += ',';
  json += string_field_json("mode", ava::agent::to_string(grant.mode));
  json += ',';
  json += string_field_json("tool_name", grant.tool_name);
  json += ',';
  json += string_field_json("target_path", grant.target_path.string());
  json += ',';
  json += string_field_json("command", grant.command);
  json += ',';
  json += string_field_json("reason", grant.reason);
  json += ',';
  json += string_field_json("risk", ava::permissions::to_string(grant.risk));
  json += '}';
  return json;
}

ava::core::Result<PermissionReplyDecision> parse_permission_reply_decision(std::string_view decision)
{
  if (decision == "allow") {
    return PermissionReplyDecision{.resolution = ava::permissions::PermissionResolution::Allow,
                                   .create_session_grant = false};
  }
  if (decision == "allow_session") {
    return PermissionReplyDecision{.resolution = ava::permissions::PermissionResolution::Allow,
                                   .create_session_grant = true};
  }
  if (decision == "deny") {
    return PermissionReplyDecision{.resolution = ava::permissions::PermissionResolution::Deny,
                                   .create_session_grant = false};
  }

  auto error = invalid_rpc("permission_reply decision must be allow, allow_session, or deny");
  error.with_context("decision", std::string(decision));
  return std::unexpected(std::move(error));
}

ava::core::Result<ava::agent::QuestionAnswer> parse_question_reply(PendingQuestionRequest const& pending,
                                                                   std::optional<std::string> const& answer,
                                                                   std::optional<std::string> const& selected)
{
  if (answer && selected) return std::unexpected(invalid_rpc("question_reply requires answer or selected, not both"));

  ava::agent::QuestionAnswer parsed;
  if (answer) {
    if (!pending.allow_custom) {
      return std::unexpected(invalid_rpc("question_reply answer is not allowed for this request"));
    }
    parsed.custom_text = *answer;
  } else if (selected) {
    bool valid_option = false;
    for (auto const& option : pending.options) valid_option = valid_option || option.value == *selected;
    if (!valid_option) {
      return std::unexpected(invalid_rpc("question_reply selected option is not valid for this request"));
    }
    parsed.selected_options.push_back(*selected);
  } else {
    return std::unexpected(invalid_rpc("question_reply requires answer or selected"));
  }

  return parsed;
}

}  // namespace ava::app::rpc::detail
