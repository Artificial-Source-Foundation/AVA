#include <algorithm>
#include <string>
#include <utility>

#include "ava/session/validation_compaction.h"
#include "tests/support/test_harness.h"

namespace {

ava::session::SessionEntry compaction_entry(std::string id, std::string data_json)
{
  return ava::session::SessionEntry{.id = std::move(id),
                                    .parent_id = "",
                                    .type = ava::session::EntryType::Compaction,
                                    .timestamp = "2026-05-05T00:00:00Z",
                                    .data_json = std::move(data_json)};
}

bool has_issue(ava::session::SessionReplayValidation const& validation, ava::session::SessionReplayIssueKind kind)
{
  return std::ranges::any_of(validation.issues,
                             [kind](ava::session::SessionReplayIssue const& issue) { return issue.kind == kind; });
}

void test_compaction_accepts_valid_semantic_metadata()
{
  ava::session::SessionReplayValidation validation;
  validate_compaction_entry(validation, 0,
                            compaction_entry("entry_compaction",
                                             "{\"trigger\":\"manual\",\"status\":\"recorded\","
                                             "\"summary\":\"durable summary\",\"model\":\"gpt-5.5\","
                                             "\"summary_unavailable\":false,\"threshold_tokens\":100,"
                                             "\"estimated_tokens\":120,\"keep_recent_tokens\":64,"
                                             "\"keep_recent_messages\":4,\"max_summary_bytes\":65536}"));
  expect(validation.ok(), "compaction accepts complete semantic metadata");

  ava::session::SessionReplayValidation minimal_validation;
  validate_compaction_entry(minimal_validation, 1,
                            compaction_entry("entry_compaction_minimal", "{\"summary\":\"durable summary\"}"));
  expect(minimal_validation.ok(), "compaction accepts a durable summary when optional metadata is absent");
}

void test_compaction_rejects_malformed_entries()
{
  ava::session::SessionReplayValidation invalid_json;
  validate_compaction_entry(invalid_json, 0, compaction_entry("entry_invalid_json", "not-json"));
  expect(!invalid_json.ok(), "compaction rejects invalid JSON");
  expect(has_issue(invalid_json, ava::session::SessionReplayIssueKind::InvalidCompactionEntry),
         "invalid JSON records invalid compaction issue");

  ava::session::SessionReplayValidation empty_summary;
  validate_compaction_entry(empty_summary, 1, compaction_entry("entry_empty_summary", "{\"summary\":\"\"}"));
  expect(!empty_summary.ok(), "compaction rejects empty summary");
  expect(has_issue(empty_summary, ava::session::SessionReplayIssueKind::InvalidCompactionEntry),
         "empty summary records invalid compaction issue");

  ava::session::SessionReplayValidation invalid_flag;
  validate_compaction_entry(
      invalid_flag, 2,
      compaction_entry("entry_invalid_flag", "{\"summary\":\"durable\",\"summary_unavailable\":\"false\"}"));
  expect(!invalid_flag.ok(), "compaction rejects non-boolean summary_unavailable");
  expect(has_issue(invalid_flag, ava::session::SessionReplayIssueKind::InvalidCompactionEntry),
         "invalid summary_unavailable records invalid compaction issue");

  ava::session::SessionReplayValidation invalid_status;
  validate_compaction_entry(invalid_status, 3,
                            compaction_entry("entry_invalid_status", "{\"summary\":\"durable\",\"status\":\"done\"}"));
  expect(!invalid_status.ok(), "compaction rejects invalid status values");
  expect(has_issue(invalid_status, ava::session::SessionReplayIssueKind::InvalidCompactionEntry),
         "invalid status records invalid compaction issue");

  ava::session::SessionReplayValidation invalid_metadata;
  validate_compaction_entry(
      invalid_metadata, 4,
      compaction_entry("entry_invalid_metadata", "{\"summary\":\"durable\",\"threshold_tokens\":1.5}"));
  expect(!invalid_metadata.ok(), "compaction rejects malformed token metadata");
  expect(has_issue(invalid_metadata, ava::session::SessionReplayIssueKind::InvalidCompactionEntry),
         "malformed token metadata records invalid compaction issue");
}

void test_compaction_boundary_reports_unresolved_tool_and_permission_state()
{
  ava::session::SessionReplayValidation validation;
  ava::session::SessionReplayToolCalls tool_calls;
  tool_calls.emplace("call_pending",
                     ava::session::SessionReplayToolCallState{
                         .entry_id = "entry_tool_call", .tool_name = "read_file", .result_seen = false});
  ava::session::SessionReplayPendingPermissions pending_permissions;
  pending_permissions["id:permreq_pending"].push_back(
      ava::session::SessionReplayPendingPermissionPrompt{.entry_id = "entry_permission_ask", .entry_index = 0});

  validate_compaction_boundaries(validation, 2, compaction_entry("entry_compaction", "{\"summary\":\"durable\"}"),
                                 tool_calls, pending_permissions, ava::session::SessionReplayValidationOptions{});
  expect(!validation.ok(), "compaction boundary rejects unresolved replay state");
  expect(has_issue(validation, ava::session::SessionReplayIssueKind::CompactionWithUnresolvedToolCall),
         "compaction boundary records unresolved tool call issue");
  expect(has_issue(validation, ava::session::SessionReplayIssueKind::CompactionWithUnresolvedPermissionPrompt),
         "compaction boundary records unresolved permission prompt issue");
}

void test_compaction_boundary_honors_disabled_integrity_options()
{
  ava::session::SessionReplayValidation validation;
  ava::session::SessionReplayToolCalls tool_calls;
  tool_calls.emplace("call_pending",
                     ava::session::SessionReplayToolCallState{
                         .entry_id = "entry_tool_call", .tool_name = "read_file", .result_seen = false});
  ava::session::SessionReplayPendingPermissions pending_permissions;
  pending_permissions["id:permreq_pending"].push_back(
      ava::session::SessionReplayPendingPermissionPrompt{.entry_id = "entry_permission_ask", .entry_index = 0});

  validate_compaction_boundaries(
      validation, 2, compaction_entry("entry_compaction", "{\"summary\":\"durable\"}"), tool_calls, pending_permissions,
      ava::session::SessionReplayValidationOptions{.require_tool_result_pairing = false,
                                                   .require_permission_decision_integrity = false});
  expect(validation.ok(), "compaction boundary honors disabled integrity options");
}

}  // namespace

void run_session_validation_compaction_tests()
{
  test_compaction_accepts_valid_semantic_metadata();
  test_compaction_rejects_malformed_entries();
  test_compaction_boundary_reports_unresolved_tool_and_permission_state();
  test_compaction_boundary_honors_disabled_integrity_options();
}
