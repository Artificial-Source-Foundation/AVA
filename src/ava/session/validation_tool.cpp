#include "ava/session/validation_tool.h"

#include <string_view>

#include "ava/core/json.h"
#include "ava/session/validation_fields.h"
#include "ava/session/validation_issue.h"

namespace ava::session {
namespace {

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

}  // namespace

void validate_tool_call_entry(SessionReplayValidation& validation, SessionReplayToolCalls& tool_calls,
                              SessionReplayValidationOptions const& options, std::size_t index,
                              SessionEntry const& entry)
{
  auto const call_id = ava::core::json::string_field(entry.data_json, "call_id").value_or("");
  auto const tool_name = ava::core::json::string_field(entry.data_json, "name").value_or("");
  if (call_id.empty()) {
    add_replay_error(validation, SessionReplayIssueKind::EmptyToolCallId, index, entry, "",
                     "tool_call entry is missing call_id");
    return;
  }
  if (options.require_tool_result_pairing && tool_calls.find(call_id) != tool_calls.end()) {
    add_replay_error(validation, SessionReplayIssueKind::DuplicateToolCallId, index, entry, call_id,
                     "tool_call id is reused in the same session");
    return;
  }
  tool_calls.emplace(call_id,
                     SessionReplayToolCallState{.entry_id = entry.id, .tool_name = tool_name, .result_seen = false});
}

void validate_tool_result_entry(SessionReplayValidation& validation, SessionReplayToolCalls& tool_calls,
                                SessionReplayValidationOptions const& options, std::size_t index,
                                SessionEntry const& entry)
{
  auto const call_id = ava::core::json::string_field(entry.data_json, "call_id").value_or("");
  auto const tool_name = ava::core::json::string_field(entry.data_json, "name").value_or("");
  if (call_id.empty()) {
    add_replay_error(validation, SessionReplayIssueKind::EmptyToolCallId, index, entry, "",
                     "tool_result entry is missing call_id");
    return;
  }
  if (!options.require_tool_result_pairing) {
    if (options.require_structured_tool_results) {
      validate_structured_tool_result(validation, index, entry, call_id, tool_name);
    }
    return;
  }

  auto tool_call = tool_calls.find(call_id);
  if (tool_call == tool_calls.end()) {
    add_replay_error(validation, SessionReplayIssueKind::ToolResultWithoutCall, index, entry, call_id,
                     "tool_result has no earlier matching tool_call");
    return;
  }
  if (tool_call->second.result_seen) {
    add_replay_error(validation, SessionReplayIssueKind::DuplicateToolResult, index, entry, call_id,
                     "tool_result duplicates an already completed tool_call");
    return;
  }
  if (!tool_call->second.tool_name.empty() && !tool_name.empty() && tool_call->second.tool_name != tool_name) {
    add_replay_error(validation, SessionReplayIssueKind::ToolResultToolMismatch, index, entry, call_id,
                     "tool_result name does not match its tool_call");
    return;
  }
  tool_call->second.result_seen = true;
  if (options.require_structured_tool_results) {
    validate_structured_tool_result(validation, index, entry, call_id, tool_name);
  }
}

void add_compaction_tool_boundary_issues(SessionReplayValidation& validation, SessionReplayToolCalls const& tool_calls,
                                         std::size_t index, SessionEntry const& entry)
{
  for (auto const& [call_id, state] : tool_calls) {
    if (state.result_seen) continue;
    add_replay_error(validation, SessionReplayIssueKind::CompactionWithUnresolvedToolCall, index, entry, call_id,
                     "compaction occurred while a tool_call had no matching tool_result");
  }
}

void add_unresolved_tool_calls(SessionReplayValidation& validation, SessionReplayToolCalls const& tool_calls,
                               std::size_t entry_index)
{
  for (auto const& [call_id, state] : tool_calls) {
    if (state.result_seen) continue;
    add_replay_issue(validation, SessionReplayIssue{.severity = SessionReplayIssueSeverity::Error,
                                                    .kind = SessionReplayIssueKind::UnresolvedToolCall,
                                                    .entry_index = entry_index,
                                                    .entry_id = state.entry_id,
                                                    .call_id = call_id,
                                                    .message = "tool_call has no matching tool_result"});
  }
}

}  // namespace ava::session
