#include "ava/session/validation_permission.h"

#include <utility>

#include "ava/core/json.h"
#include "ava/session/validation_fields.h"
#include "ava/session/validation_issue.h"

namespace ava::session {
namespace {

std::string permission_key(SessionEntry const& entry)
{
  auto const permission_request_id = ava::core::json::string_field(entry.data_json, "permission_request_id");
  if (permission_request_id && !permission_request_id->empty()) {
    return "id:" + *permission_request_id;
  }

  std::string key = ava::core::json::string_field(entry.data_json, "operation").value_or("");
  key += '\x1F';
  key += ava::core::json::string_field(entry.data_json, "tool_name").value_or("");
  key += '\x1F';
  key += ava::core::json::string_field(entry.data_json, "target_path").value_or("");
  key += '\x1F';
  key += ava::core::json::string_field(entry.data_json, "command").value_or("");
  key += '\x1F';
  key += ava::core::json::string_field(entry.data_json, "reason").value_or("");
  return key;
}

}  // namespace

void validate_permission_decision(SessionReplayValidation& validation, SessionReplayPendingPermissions& pending,
                                  std::size_t index, SessionEntry const& entry)
{
  auto const operation = ava::core::json::string_field(entry.data_json, "operation").value_or("");
  auto const mode = ava::core::json::string_field(entry.data_json, "mode").value_or("");
  auto const tool_name = ava::core::json::string_field(entry.data_json, "tool_name").value_or("");
  auto const action = ava::core::json::string_field(entry.data_json, "action").value_or("");
  auto const reason = ava::core::json::string_field(entry.data_json, "reason").value_or("");
  auto const risk = ava::core::json::string_field(entry.data_json, "risk");
  auto const resolution = ava::core::json::string_field(entry.data_json, "resolution").value_or("");
  auto const resolution_source = ava::core::json::string_field(entry.data_json, "resolution_source").value_or("");

  if (!valid_operation(operation) || !valid_mode(mode) || tool_name.empty() || !valid_action(action) ||
      reason.empty() || !present_non_empty_string(entry.data_json, "permission_request_id")) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidPermissionDecision, index, entry, "",
                     "permission_decision entry is missing required semantic fields");
    return;
  }
  if (risk && !valid_risk(*risk)) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidPermissionDecision, index, entry, "",
                     "permission_decision entry has an invalid risk");
    return;
  }
  if (!resolution.empty() && !valid_resolution(resolution)) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidPermissionDecision, index, entry, "",
                     "permission_decision entry has an invalid resolution");
    return;
  }
  if (!resolution_source.empty() && !valid_resolution_source(resolution_source)) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidPermissionDecision, index, entry, "",
                     "permission_decision entry has an invalid resolution_source");
    return;
  }

  if (action == "allow" || action == "deny") {
    if (resolution != action || resolution_source != "policy") {
      add_replay_error(validation, SessionReplayIssueKind::InvalidPermissionDecision, index, entry, "",
                       "policy allow/deny permission_decision must resolve to its action from policy");
    }
    return;
  }

  if (resolution.empty()) {
    if (resolution_source != "policy") {
      add_replay_error(validation, SessionReplayIssueKind::InvalidPermissionDecision, index, entry, "",
                       "ask permission_decision without resolution must come from policy");
      return;
    }
    pending[permission_key(entry)].push_back(
        SessionReplayPendingPermissionPrompt{.entry_id = entry.id, .entry_index = index});
    return;
  }

  if (resolution_source == "policy" || resolution_source.empty()) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidPermissionDecision, index, entry, "",
                     "resolved ask permission_decision must include a resolver outcome source");
    return;
  }

  auto pending_for_key = pending.find(permission_key(entry));
  if (pending_for_key == pending.end() || pending_for_key->second.empty()) {
    add_replay_error(validation, SessionReplayIssueKind::PermissionResolutionWithoutAsk, index, entry, "",
                     "resolved ask permission_decision has no earlier matching ask prompt");
    return;
  }
  pending_for_key->second.pop_back();
  if (pending_for_key->second.empty()) pending.erase(pending_for_key);
}

void add_compaction_permission_boundary_issues(SessionReplayValidation& validation,
                                               SessionReplayPendingPermissions const& pending, std::size_t index,
                                               SessionEntry const& entry)
{
  for (auto const& [unused_key, prompts] : pending) {
    (void)unused_key;
    for (auto const& prompt : prompts) {
      add_replay_issue(validation,
                       SessionReplayIssue{.severity = SessionReplayIssueSeverity::Error,
                                          .kind = SessionReplayIssueKind::CompactionWithUnresolvedPermissionPrompt,
                                          .entry_index = index,
                                          .entry_id = entry.id,
                                          .call_id = "",
                                          .message = "compaction occurred while a permission prompt was unresolved"});
      (void)prompt;
    }
  }
}

void add_unresolved_permission_prompts(SessionReplayValidation& validation,
                                       SessionReplayPendingPermissions const& pending, std::size_t entry_index)
{
  for (auto const& [unused_key, prompts] : pending) {
    (void)unused_key;
    for (auto const& prompt : prompts) {
      add_replay_issue(validation, SessionReplayIssue{.severity = SessionReplayIssueSeverity::Error,
                                                      .kind = SessionReplayIssueKind::UnresolvedPermissionPrompt,
                                                      .entry_index = entry_index,
                                                      .entry_id = prompt.entry_id,
                                                      .call_id = "",
                                                      .message = "ask permission_decision has no matching resolution"});
    }
  }
}

}  // namespace ava::session
