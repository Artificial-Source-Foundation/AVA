#include "ava/session/validation_compaction.h"

#include "ava/core/json.h"
#include "ava/session/validation_fields.h"
#include "ava/session/validation_issue.h"

namespace ava::session {

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

void validate_compaction_boundaries(SessionReplayValidation& validation, std::size_t index, SessionEntry const& entry,
                                    SessionReplayToolCalls const& tool_calls,
                                    SessionReplayPendingPermissions const& pending_permissions,
                                    SessionReplayValidationOptions const& options)
{
  if (options.require_tool_result_pairing) {
    add_compaction_tool_boundary_issues(validation, tool_calls, index, entry);
  }

  if (options.require_permission_decision_integrity) {
    add_compaction_permission_boundary_issues(validation, pending_permissions, index, entry);
  }
}

}  // namespace ava::session
