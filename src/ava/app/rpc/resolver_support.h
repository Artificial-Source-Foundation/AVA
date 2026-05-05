#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ava/app/rpc/resolvers.h"

namespace ava::app::rpc::detail {

struct PermissionReplyDecision {
  ava::permissions::PermissionResolution resolution = ava::permissions::PermissionResolution::Deny;
  bool create_session_grant = false;
};

[[nodiscard]] ava::core::Error no_pending_request_error(std::string_view request_id);
[[nodiscard]] std::string next_resolver_request_id(std::string_view prefix);
[[nodiscard]] bool grant_matches(PermissionSessionGrant const& grant, ava::permissions::PermissionPrompt const& prompt);
[[nodiscard]] bool grant_matches(PermissionSessionGrant const& grant, PendingPermissionRequest const& request);
[[nodiscard]] PermissionSessionGrant grant_from_request(PendingPermissionRequest const& request);
[[nodiscard]] std::string permission_session_grant_json(PermissionSessionGrant const& grant);
[[nodiscard]] ava::core::Result<PermissionReplyDecision> parse_permission_reply_decision(std::string_view decision);
[[nodiscard]] ava::core::Result<ava::agent::QuestionAnswer> parse_question_reply(
    PendingQuestionRequest const& pending, std::optional<std::string> const& answer,
    std::optional<std::string> const& selected);

}  // namespace ava::app::rpc::detail
