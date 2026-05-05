#include "ava/session/validation.h"

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "ava/core/json.h"
#include "ava/session/validation_fields.h"
#include "ava/session/validation_issue.h"
#include "ava/session/validation_model.h"

namespace ava::session {
namespace {

struct ToolCallState {
  std::string entry_id;
  std::string tool_name;
  bool result_seen = false;
};

struct PendingPermissionPrompt {
  std::string entry_id;
  std::size_t entry_index = 0;
};

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

bool supported_entry_version(long long version)
{
  return version == 0 || (version >= 1 && version <= kCurrentSessionEntryVersion);
}

void validate_structured_tool_result(SessionReplayValidation& validation, std::size_t index, SessionEntry const& entry,
                                     std::string_view call_id, std::string_view tool_name)
{
  auto const structured = ava::core::json::object_field(entry.data_json, "structured_result");
  if (!structured) {
    add_replay_error(validation, SessionReplayIssueKind::MissingStructuredToolResult, index, entry,
                     std::string(call_id), "tool_result entry is missing structured_result");
    return;
  }
  if (!ava::core::json::is_valid_object(*structured)) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidStructuredToolResult, index, entry,
                     std::string(call_id), "tool_result structured_result is not valid JSON");
    return;
  }

  auto const structured_call_id = ava::core::json::string_field(*structured, "call_id").value_or("");
  auto const structured_tool = ava::core::json::string_field(*structured, "tool").value_or("");
  auto const status = ava::core::json::string_field(*structured, "status").value_or("");
  auto const content_type = ava::core::json::string_field(*structured, "content_type").value_or("");
  if (structured_call_id.empty() || structured_tool.empty() || status.empty() || content_type.empty() ||
      !valid_status(status)) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidStructuredToolResult, index, entry,
                     std::string(call_id), "tool_result structured_result is missing required semantic fields");
    return;
  }
  if (structured_call_id != call_id || structured_tool != tool_name) {
    add_replay_error(validation, SessionReplayIssueKind::StructuredToolResultMismatch, index, entry,
                     std::string(call_id), "tool_result structured_result does not match top-level call_id/name");
    return;
  }

  auto const top_status = ava::core::json::string_field(entry.data_json, "status").value_or("");
  if (!top_status.empty() && top_status != status) {
    add_replay_error(validation, SessionReplayIssueKind::StructuredToolResultMismatch, index, entry,
                     std::string(call_id), "tool_result structured_result status does not match top-level status");
    return;
  }
  if (bool_field_is_true(entry.data_json, "success") && status != "success") {
    add_replay_error(validation, SessionReplayIssueKind::StructuredToolResultMismatch, index, entry,
                     std::string(call_id), "successful tool_result has non-success structured_result status");
    return;
  }
  if (bool_field_is_false(entry.data_json, "success") && status == "success") {
    add_replay_error(validation, SessionReplayIssueKind::StructuredToolResultMismatch, index, entry,
                     std::string(call_id), "failed tool_result has success structured_result status");
  }
}

void validate_permission_decision(SessionReplayValidation& validation,
                                  std::unordered_map<std::string, std::vector<PendingPermissionPrompt>>& pending,
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
    pending[permission_key(entry)].push_back(PendingPermissionPrompt{.entry_id = entry.id, .entry_index = index});
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

void validate_compaction_entry(SessionReplayValidation& validation, std::size_t index, SessionEntry const& entry)
{
  if (!ava::core::json::is_valid_object(entry.data_json)) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidCompactionEntry, index, entry, "",
                     "compaction entry data is not valid JSON");
    return;
  }

  auto const summary = ava::core::json::string_field(entry.data_json, "summary");
  if (!summary || summary->empty()) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidCompactionEntry, index, entry, "",
                     "compaction entry is missing a non-empty summary");
    return;
  }

  auto const unavailable_present = ava::core::json::field_value_start(entry.data_json, "summary_unavailable");
  if (unavailable_present && !bool_field_is_true(entry.data_json, "summary_unavailable") &&
      !bool_field_is_false(entry.data_json, "summary_unavailable")) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidCompactionEntry, index, entry, "",
                     "compaction entry summary_unavailable must be a boolean");
    return;
  }

  auto const status_present = ava::core::json::field_value_start(entry.data_json, "status");
  auto const status = ava::core::json::string_field(entry.data_json, "status");
  if (status_present && (!status || *status != "recorded")) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidCompactionEntry, index, entry, "",
                     "compaction entry status must be recorded when present");
    return;
  }

  if (!present_non_empty_string(entry.data_json, "trigger") || !present_non_empty_string(entry.data_json, "model") ||
      !present_integer_matching(entry.data_json, "threshold_tokens", false) ||
      !present_integer_matching(entry.data_json, "estimated_tokens", false) ||
      !present_integer_matching(entry.data_json, "keep_recent_tokens", false) ||
      !present_integer_matching(entry.data_json, "keep_recent_messages", false) ||
      !present_integer_matching(entry.data_json, "max_summary_bytes", true)) {
    add_replay_error(validation, SessionReplayIssueKind::InvalidCompactionEntry, index, entry, "",
                     "compaction entry has malformed semantic metadata");
  }
}

void validate_compaction_boundaries(
    SessionReplayValidation& validation, std::size_t index, SessionEntry const& entry,
    std::unordered_map<std::string, ToolCallState> const& tool_calls,
    std::unordered_map<std::string, std::vector<PendingPermissionPrompt>> const& pending_permissions,
    SessionReplayValidationOptions const& options)
{
  if (options.require_tool_result_pairing) {
    for (auto const& [call_id, state] : tool_calls) {
      if (state.result_seen) continue;
      add_replay_error(validation, SessionReplayIssueKind::CompactionWithUnresolvedToolCall, index, entry, call_id,
                       "compaction occurred while a tool_call had no matching tool_result");
    }
  }

  if (options.require_permission_decision_integrity) {
    for (auto const& [unused_key, prompts] : pending_permissions) {
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
}

}  // namespace

std::string_view to_string(SessionReplayIssueSeverity severity) noexcept
{
  switch (severity) {
    case SessionReplayIssueSeverity::Warning:
      return "warning";
    case SessionReplayIssueSeverity::Error:
      return "error";
  }
  return "error";
}

std::string_view to_string(SessionReplayIssueKind kind) noexcept
{
  switch (kind) {
    case SessionReplayIssueKind::UnsupportedEntryVersion:
      return "unsupported_entry_version";
    case SessionReplayIssueKind::DuplicateEntryId:
      return "duplicate_entry_id";
    case SessionReplayIssueKind::UnknownParentId:
      return "unknown_parent_id";
    case SessionReplayIssueKind::EmptyToolCallId:
      return "empty_tool_call_id";
    case SessionReplayIssueKind::DuplicateToolCallId:
      return "duplicate_tool_call_id";
    case SessionReplayIssueKind::ToolResultWithoutCall:
      return "tool_result_without_call";
    case SessionReplayIssueKind::ToolResultToolMismatch:
      return "tool_result_tool_mismatch";
    case SessionReplayIssueKind::DuplicateToolResult:
      return "duplicate_tool_result";
    case SessionReplayIssueKind::UnresolvedToolCall:
      return "unresolved_tool_call";
    case SessionReplayIssueKind::MissingStructuredToolResult:
      return "missing_structured_tool_result";
    case SessionReplayIssueKind::InvalidStructuredToolResult:
      return "invalid_structured_tool_result";
    case SessionReplayIssueKind::StructuredToolResultMismatch:
      return "structured_tool_result_mismatch";
    case SessionReplayIssueKind::InvalidPermissionDecision:
      return "invalid_permission_decision";
    case SessionReplayIssueKind::PermissionResolutionWithoutAsk:
      return "permission_resolution_without_ask";
    case SessionReplayIssueKind::UnresolvedPermissionPrompt:
      return "unresolved_permission_prompt";
    case SessionReplayIssueKind::InvalidCompactionEntry:
      return "invalid_compaction_entry";
    case SessionReplayIssueKind::CompactionWithUnresolvedToolCall:
      return "compaction_with_unresolved_tool_call";
    case SessionReplayIssueKind::CompactionWithUnresolvedPermissionPrompt:
      return "compaction_with_unresolved_permission_prompt";
    case SessionReplayIssueKind::InvalidModelEntry:
      return "invalid_model_entry";
    case SessionReplayIssueKind::InvalidReasoningEntry:
      return "invalid_reasoning_entry";
  }
  return "invalid_structured_tool_result";
}

SessionReplayValidation validate_session_replay(std::vector<SessionEntry> const& entries,
                                                SessionReplayValidationOptions options)
{
  SessionReplayValidation validation;
  std::unordered_set<std::string> seen_entry_ids;
  std::unordered_map<std::string, ToolCallState> tool_calls;
  std::unordered_map<std::string, std::vector<PendingPermissionPrompt>> pending_permissions;
  SessionReplayModelState active_model;

  for (std::size_t index = 0; index < entries.size(); ++index) {
    auto const& entry = entries[index];
    if (options.require_entry_versions && !supported_entry_version(entry.version)) {
      add_replay_error(validation, SessionReplayIssueKind::UnsupportedEntryVersion, index, entry, "",
                       "session entry version is outside the supported range");
    }
    if (options.require_known_parent_ids && !entry.parent_id.empty() &&
        seen_entry_ids.find(entry.parent_id) == seen_entry_ids.end()) {
      add_replay_error(validation, SessionReplayIssueKind::UnknownParentId, index, entry, "",
                       "session entry parent_id does not reference an earlier entry");
    }
    if (!seen_entry_ids.insert(entry.id).second) {
      add_replay_error(validation, SessionReplayIssueKind::DuplicateEntryId, index, entry, "",
                       "duplicate session entry id");
    }

    if (options.require_model_reasoning_integrity) {
      if (entry.type == EntryType::SessionStart) {
        validate_session_start_entry(validation, active_model, index, entry);
        continue;
      }
      if (entry.type == EntryType::ModelChange) {
        validate_model_change_entry(validation, active_model, index, entry);
        continue;
      }
      if (entry.type == EntryType::ReasoningChange) {
        validate_reasoning_change_entry(validation, active_model, index, entry);
        continue;
      }
      if (entry.type == EntryType::ReasoningBlock) {
        validate_reasoning_block_entry(validation, index, entry);
        continue;
      }
    }

    if (entry.type == EntryType::PermissionDecision && options.require_permission_decision_integrity) {
      validate_permission_decision(validation, pending_permissions, index, entry);
      continue;
    }

    if (entry.type == EntryType::Compaction && options.require_compaction_integrity) {
      validate_compaction_entry(validation, index, entry);
      validate_compaction_boundaries(validation, index, entry, tool_calls, pending_permissions, options);
      continue;
    }

    if (entry.type == EntryType::ToolCall) {
      auto const call_id = ava::core::json::string_field(entry.data_json, "call_id").value_or("");
      auto const tool_name = ava::core::json::string_field(entry.data_json, "name").value_or("");
      if (call_id.empty()) {
        add_replay_error(validation, SessionReplayIssueKind::EmptyToolCallId, index, entry, "",
                         "tool_call entry is missing call_id");
        continue;
      }
      if (options.require_tool_result_pairing && tool_calls.find(call_id) != tool_calls.end()) {
        add_replay_error(validation, SessionReplayIssueKind::DuplicateToolCallId, index, entry, call_id,
                         "tool_call id is reused in the same session");
        continue;
      }
      tool_calls.emplace(call_id, ToolCallState{.entry_id = entry.id, .tool_name = tool_name, .result_seen = false});
      continue;
    }

    if (entry.type != EntryType::ToolResult) continue;

    auto const call_id = ava::core::json::string_field(entry.data_json, "call_id").value_or("");
    auto const tool_name = ava::core::json::string_field(entry.data_json, "name").value_or("");
    if (call_id.empty()) {
      add_replay_error(validation, SessionReplayIssueKind::EmptyToolCallId, index, entry, "",
                       "tool_result entry is missing call_id");
      continue;
    }
    if (!options.require_tool_result_pairing) {
      if (options.require_structured_tool_results) {
        validate_structured_tool_result(validation, index, entry, call_id, tool_name);
      }
      continue;
    }

    auto tool_call = tool_calls.find(call_id);
    if (tool_call == tool_calls.end()) {
      add_replay_error(validation, SessionReplayIssueKind::ToolResultWithoutCall, index, entry, call_id,
                       "tool_result has no earlier matching tool_call");
      continue;
    }
    if (tool_call->second.result_seen) {
      add_replay_error(validation, SessionReplayIssueKind::DuplicateToolResult, index, entry, call_id,
                       "tool_result duplicates an already completed tool_call");
      continue;
    }
    if (!tool_call->second.tool_name.empty() && !tool_name.empty() && tool_call->second.tool_name != tool_name) {
      add_replay_error(validation, SessionReplayIssueKind::ToolResultToolMismatch, index, entry, call_id,
                       "tool_result name does not match its tool_call");
      continue;
    }
    tool_call->second.result_seen = true;
    if (options.require_structured_tool_results) {
      validate_structured_tool_result(validation, index, entry, call_id, tool_name);
    }
  }

  if (options.require_tool_result_pairing) {
    for (auto const& [call_id, state] : tool_calls) {
      if (state.result_seen) continue;
      add_replay_issue(validation, SessionReplayIssue{.severity = SessionReplayIssueSeverity::Error,
                                                      .kind = SessionReplayIssueKind::UnresolvedToolCall,
                                                      .entry_index = entries.size(),
                                                      .entry_id = state.entry_id,
                                                      .call_id = call_id,
                                                      .message = "tool_call has no matching tool_result"});
    }
  }

  if (options.require_permission_decision_integrity) {
    for (auto const& [unused_key, prompts] : pending_permissions) {
      (void)unused_key;
      for (auto const& prompt : prompts) {
        add_replay_issue(validation,
                         SessionReplayIssue{.severity = SessionReplayIssueSeverity::Error,
                                            .kind = SessionReplayIssueKind::UnresolvedPermissionPrompt,
                                            .entry_index = entries.size(),
                                            .entry_id = prompt.entry_id,
                                            .call_id = "",
                                            .message = "ask permission_decision has no matching resolution"});
      }
    }
  }

  return validation;
}

}  // namespace ava::session
