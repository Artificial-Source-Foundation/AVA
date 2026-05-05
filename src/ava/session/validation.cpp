#include "ava/session/validation.h"

#include <algorithm>
#include <unordered_set>

#include "ava/session/validation_compaction.h"
#include "ava/session/validation_issue.h"
#include "ava/session/validation_model.h"
#include "ava/session/validation_permission.h"
#include "ava/session/validation_tool.h"

namespace ava::session {
namespace {

bool supported_entry_version(long long version)
{
  return version == 0 || (version >= 1 && version <= kCurrentSessionEntryVersion);
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
  SessionReplayToolCalls tool_calls;
  SessionReplayPendingPermissions pending_permissions;
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
      validate_tool_call_entry(validation, tool_calls, options, index, entry);
      continue;
    }

    if (entry.type != EntryType::ToolResult) continue;
    validate_tool_result_entry(validation, tool_calls, options, index, entry);
  }

  if (options.require_tool_result_pairing) {
    add_unresolved_tool_calls(validation, tool_calls, entries.size());
  }

  if (options.require_permission_decision_integrity) {
    add_unresolved_permission_prompts(validation, pending_permissions, entries.size());
  }

  return validation;
}

}  // namespace ava::session
