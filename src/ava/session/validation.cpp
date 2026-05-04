#include "ava/session/validation.h"

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "ava/core/json.h"

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

std::string permission_key(const SessionEntry& entry) {
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

bool bool_field_is_true(std::string_view object, std::string_view key) {
  const auto start = ava::core::json::field_value_start(object, key);
  if (!start) return false;
  const auto end = *start + std::string_view("true").size();
  if (end > object.size() || object.substr(*start, std::string_view("true").size()) != "true") return false;
  return end == object.size() || object[end] == ',' || object[end] == '}' || object[end] == ']' || object[end] == ' ' ||
         object[end] == '\t' || object[end] == '\n' || object[end] == '\r';
}

bool bool_field_is_false(std::string_view object, std::string_view key) {
  const auto start = ava::core::json::field_value_start(object, key);
  if (!start) return false;
  const auto end = *start + std::string_view("false").size();
  if (end > object.size() || object.substr(*start, std::string_view("false").size()) != "false") return false;
  return end == object.size() || object[end] == ',' || object[end] == '}' || object[end] == ']' || object[end] == ' ' ||
         object[end] == '\t' || object[end] == '\n' || object[end] == '\r';
}

bool valid_status(std::string_view status) { return status == "success" || status == "error" || status == "canceled"; }

bool valid_operation(std::string_view operation) {
  return operation == "read" || operation == "search" || operation == "edit" || operation == "bash" ||
         operation == "network.fetch" || operation == "lsp.query" || operation == "plugin.execute" ||
         operation == "plugin.tool.call" || operation == "plugin.command.run" || operation == "plugin.event.observe" ||
         operation == "mcp.server.launch" || operation == "mcp.server.connect" || operation == "mcp.tool.call";
}

bool valid_mode(std::string_view mode) { return mode == "build" || mode == "plan"; }

bool valid_action(std::string_view action) { return action == "allow" || action == "ask" || action == "deny"; }

bool valid_resolution(std::string_view resolution) { return resolution == "allow" || resolution == "deny"; }

bool valid_resolution_source(std::string_view source) {
  return source == "policy" || source == "resolver" || source == "no_resolver" || source == "resolver_failed";
}

void add_issue(SessionReplayValidation& validation, SessionReplayIssue issue) {
  if (issue.severity == SessionReplayIssueSeverity::Warning) {
    ++validation.warning_count;
  } else {
    ++validation.error_count;
  }
  validation.issues.push_back(std::move(issue));
}

void add_error(SessionReplayValidation& validation, SessionReplayIssueKind kind, std::size_t index,
               const SessionEntry& entry, std::string call_id, std::string message) {
  add_issue(validation, SessionReplayIssue{.severity = SessionReplayIssueSeverity::Error,
                                           .kind = kind,
                                           .entry_index = index,
                                           .entry_id = entry.id,
                                           .call_id = std::move(call_id),
                                           .message = std::move(message)});
}

void validate_structured_tool_result(SessionReplayValidation& validation, std::size_t index, const SessionEntry& entry,
                                     std::string_view call_id, std::string_view tool_name) {
  const auto structured = ava::core::json::object_field(entry.data_json, "structured_result");
  if (!structured) {
    add_error(validation, SessionReplayIssueKind::MissingStructuredToolResult, index, entry, std::string(call_id),
              "tool_result entry is missing structured_result");
    return;
  }
  if (!ava::core::json::is_valid_object(*structured)) {
    add_error(validation, SessionReplayIssueKind::InvalidStructuredToolResult, index, entry, std::string(call_id),
              "tool_result structured_result is not valid JSON");
    return;
  }

  const auto structured_call_id = ava::core::json::string_field(*structured, "call_id").value_or("");
  const auto structured_tool = ava::core::json::string_field(*structured, "tool").value_or("");
  const auto status = ava::core::json::string_field(*structured, "status").value_or("");
  const auto content_type = ava::core::json::string_field(*structured, "content_type").value_or("");
  if (structured_call_id.empty() || structured_tool.empty() || status.empty() || content_type.empty() ||
      !valid_status(status)) {
    add_error(validation, SessionReplayIssueKind::InvalidStructuredToolResult, index, entry, std::string(call_id),
              "tool_result structured_result is missing required semantic fields");
    return;
  }
  if (structured_call_id != call_id || structured_tool != tool_name) {
    add_error(validation, SessionReplayIssueKind::StructuredToolResultMismatch, index, entry, std::string(call_id),
              "tool_result structured_result does not match top-level call_id/name");
    return;
  }

  const auto top_status = ava::core::json::string_field(entry.data_json, "status").value_or("");
  if (!top_status.empty() && top_status != status) {
    add_error(validation, SessionReplayIssueKind::StructuredToolResultMismatch, index, entry, std::string(call_id),
              "tool_result structured_result status does not match top-level status");
    return;
  }
  if (bool_field_is_true(entry.data_json, "success") && status != "success") {
    add_error(validation, SessionReplayIssueKind::StructuredToolResultMismatch, index, entry, std::string(call_id),
              "successful tool_result has non-success structured_result status");
    return;
  }
  if (bool_field_is_false(entry.data_json, "success") && status == "success") {
    add_error(validation, SessionReplayIssueKind::StructuredToolResultMismatch, index, entry, std::string(call_id),
              "failed tool_result has success structured_result status");
  }
}

void validate_permission_decision(SessionReplayValidation& validation,
                                  std::unordered_map<std::string, std::vector<PendingPermissionPrompt>>& pending,
                                  std::size_t index, const SessionEntry& entry) {
  const auto operation = ava::core::json::string_field(entry.data_json, "operation").value_or("");
  const auto mode = ava::core::json::string_field(entry.data_json, "mode").value_or("");
  const auto tool_name = ava::core::json::string_field(entry.data_json, "tool_name").value_or("");
  const auto action = ava::core::json::string_field(entry.data_json, "action").value_or("");
  const auto reason = ava::core::json::string_field(entry.data_json, "reason").value_or("");
  const auto resolution = ava::core::json::string_field(entry.data_json, "resolution").value_or("");
  const auto resolution_source = ava::core::json::string_field(entry.data_json, "resolution_source").value_or("");

  if (!valid_operation(operation) || !valid_mode(mode) || tool_name.empty() || !valid_action(action) ||
      reason.empty()) {
    add_error(validation, SessionReplayIssueKind::InvalidPermissionDecision, index, entry, "",
              "permission_decision entry is missing required semantic fields");
    return;
  }
  if (!resolution.empty() && !valid_resolution(resolution)) {
    add_error(validation, SessionReplayIssueKind::InvalidPermissionDecision, index, entry, "",
              "permission_decision entry has an invalid resolution");
    return;
  }
  if (!resolution_source.empty() && !valid_resolution_source(resolution_source)) {
    add_error(validation, SessionReplayIssueKind::InvalidPermissionDecision, index, entry, "",
              "permission_decision entry has an invalid resolution_source");
    return;
  }

  if (action == "allow" || action == "deny") {
    if (resolution != action || resolution_source != "policy") {
      add_error(validation, SessionReplayIssueKind::InvalidPermissionDecision, index, entry, "",
                "policy allow/deny permission_decision must resolve to its action from policy");
    }
    return;
  }

  if (resolution.empty()) {
    if (resolution_source != "policy") {
      add_error(validation, SessionReplayIssueKind::InvalidPermissionDecision, index, entry, "",
                "ask permission_decision without resolution must come from policy");
      return;
    }
    pending[permission_key(entry)].push_back(PendingPermissionPrompt{.entry_id = entry.id, .entry_index = index});
    return;
  }

  if (resolution_source == "policy" || resolution_source.empty()) {
    add_error(validation, SessionReplayIssueKind::InvalidPermissionDecision, index, entry, "",
              "resolved ask permission_decision must include a resolver outcome source");
    return;
  }

  auto pending_for_key = pending.find(permission_key(entry));
  if (pending_for_key == pending.end() || pending_for_key->second.empty()) {
    add_error(validation, SessionReplayIssueKind::PermissionResolutionWithoutAsk, index, entry, "",
              "resolved ask permission_decision has no earlier matching ask prompt");
    return;
  }
  pending_for_key->second.pop_back();
  if (pending_for_key->second.empty()) pending.erase(pending_for_key);
}

}  // namespace

std::string_view to_string(SessionReplayIssueSeverity severity) noexcept {
  switch (severity) {
    case SessionReplayIssueSeverity::Warning:
      return "warning";
    case SessionReplayIssueSeverity::Error:
      return "error";
  }
  return "error";
}

std::string_view to_string(SessionReplayIssueKind kind) noexcept {
  switch (kind) {
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
  }
  return "invalid_structured_tool_result";
}

SessionReplayValidation validate_session_replay(const std::vector<SessionEntry>& entries,
                                                SessionReplayValidationOptions options) {
  SessionReplayValidation validation;
  std::unordered_set<std::string> seen_entry_ids;
  std::unordered_map<std::string, ToolCallState> tool_calls;
  std::unordered_map<std::string, std::vector<PendingPermissionPrompt>> pending_permissions;

  for (std::size_t index = 0; index < entries.size(); ++index) {
    const auto& entry = entries[index];
    if (options.require_known_parent_ids && !entry.parent_id.empty() &&
        seen_entry_ids.find(entry.parent_id) == seen_entry_ids.end()) {
      add_error(validation, SessionReplayIssueKind::UnknownParentId, index, entry, "",
                "session entry parent_id does not reference an earlier entry");
    }
    if (!seen_entry_ids.insert(entry.id).second) {
      add_error(validation, SessionReplayIssueKind::DuplicateEntryId, index, entry, "", "duplicate session entry id");
    }

    if (entry.type == EntryType::PermissionDecision && options.require_permission_decision_integrity) {
      validate_permission_decision(validation, pending_permissions, index, entry);
      continue;
    }

    if (entry.type == EntryType::ToolCall) {
      const auto call_id = ava::core::json::string_field(entry.data_json, "call_id").value_or("");
      const auto tool_name = ava::core::json::string_field(entry.data_json, "name").value_or("");
      if (call_id.empty()) {
        add_error(validation, SessionReplayIssueKind::EmptyToolCallId, index, entry, "",
                  "tool_call entry is missing call_id");
        continue;
      }
      if (options.require_tool_result_pairing && tool_calls.find(call_id) != tool_calls.end()) {
        add_error(validation, SessionReplayIssueKind::DuplicateToolCallId, index, entry, call_id,
                  "tool_call id is reused in the same session");
        continue;
      }
      tool_calls.emplace(call_id, ToolCallState{.entry_id = entry.id, .tool_name = tool_name, .result_seen = false});
      continue;
    }

    if (entry.type != EntryType::ToolResult) continue;

    const auto call_id = ava::core::json::string_field(entry.data_json, "call_id").value_or("");
    const auto tool_name = ava::core::json::string_field(entry.data_json, "name").value_or("");
    if (call_id.empty()) {
      add_error(validation, SessionReplayIssueKind::EmptyToolCallId, index, entry, "",
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
      add_error(validation, SessionReplayIssueKind::ToolResultWithoutCall, index, entry, call_id,
                "tool_result has no earlier matching tool_call");
      continue;
    }
    if (tool_call->second.result_seen) {
      add_error(validation, SessionReplayIssueKind::DuplicateToolResult, index, entry, call_id,
                "tool_result duplicates an already completed tool_call");
      continue;
    }
    if (!tool_call->second.tool_name.empty() && !tool_name.empty() && tool_call->second.tool_name != tool_name) {
      add_error(validation, SessionReplayIssueKind::ToolResultToolMismatch, index, entry, call_id,
                "tool_result name does not match its tool_call");
      continue;
    }
    tool_call->second.result_seen = true;
    if (options.require_structured_tool_results) {
      validate_structured_tool_result(validation, index, entry, call_id, tool_name);
    }
  }

  if (options.require_tool_result_pairing) {
    for (const auto& [call_id, state] : tool_calls) {
      if (state.result_seen) continue;
      add_issue(validation, SessionReplayIssue{.severity = SessionReplayIssueSeverity::Error,
                                               .kind = SessionReplayIssueKind::UnresolvedToolCall,
                                               .entry_index = entries.size(),
                                               .entry_id = state.entry_id,
                                               .call_id = call_id,
                                               .message = "tool_call has no matching tool_result"});
    }
  }

  if (options.require_permission_decision_integrity) {
    for (const auto& [unused_key, prompts] : pending_permissions) {
      (void)unused_key;
      for (const auto& prompt : prompts) {
        add_issue(validation, SessionReplayIssue{.severity = SessionReplayIssueSeverity::Error,
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
