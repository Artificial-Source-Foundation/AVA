#include <algorithm>
#include <string>
#include <utility>

#include "ava/session/validation_permission.h"
#include "tests/support/test_harness.h"

namespace {

ava::session::SessionEntry permission_entry(std::string id, std::string data_json)
{
  return ava::session::SessionEntry{.id = std::move(id),
                                    .parent_id = "",
                                    .type = ava::session::EntryType::PermissionDecision,
                                    .timestamp = "2026-05-05T00:00:00Z",
                                    .data_json = std::move(data_json)};
}

ava::session::SessionEntry compaction_entry()
{
  return ava::session::SessionEntry{.id = "entry_compaction",
                                    .parent_id = "",
                                    .type = ava::session::EntryType::Compaction,
                                    .timestamp = "2026-05-05T00:00:01Z",
                                    .data_json = "{\"summary\":\"compact\"}"};
}

bool has_issue(ava::session::SessionReplayValidation const& validation, ava::session::SessionReplayIssueKind kind)
{
  return std::ranges::any_of(validation.issues,
                             [kind](ava::session::SessionReplayIssue const& issue) { return issue.kind == kind; });
}

void test_permission_ask_and_resolution_pair_by_request_id()
{
  ava::session::SessionReplayValidation validation;
  ava::session::SessionReplayPendingPermissions pending;

  validate_permission_decision(
      validation, pending, 0,
      permission_entry("entry_permission_ask",
                       "{\"permission_request_id\":\"permreq_read\",\"operation\":\"read\",\"mode\":\"build\","
                       "\"tool_name\":\"read_file\",\"action\":\"ask\",\"reason\":\"outside workspace\","
                       "\"target_path\":\"/tmp/file.txt\",\"resolution_source\":\"policy\"}"));
  expect(validation.ok(), "permission ask with semantic metadata is valid");
  expect(!pending.empty(), "permission ask records a pending prompt");

  validate_permission_decision(
      validation, pending, 1,
      permission_entry("entry_permission_resolution",
                       "{\"permission_request_id\":\"permreq_read\",\"operation\":\"read\",\"mode\":\"build\","
                       "\"tool_name\":\"read_file\",\"action\":\"ask\",\"reason\":\"outside workspace\","
                       "\"target_path\":\"/tmp/file.txt\",\"resolution\":\"allow\","
                       "\"resolution_source\":\"resolver\"}"));
  expect(validation.ok(), "permission resolver outcome is valid after matching ask");
  expect(pending.empty(), "permission resolver outcome clears the matching pending prompt");
}

void test_permission_validation_rejects_malformed_fields()
{
  ava::session::SessionReplayValidation invalid_operation;
  ava::session::SessionReplayPendingPermissions pending;
  validate_permission_decision(invalid_operation, pending, 0,
                               permission_entry("entry_invalid_operation",
                                                "{\"operation\":\"unknown\",\"mode\":\"build\","
                                                "\"tool_name\":\"read_file\",\"action\":\"allow\","
                                                "\"reason\":\"bad\",\"resolution\":\"allow\","
                                                "\"resolution_source\":\"policy\"}"));
  expect(!invalid_operation.ok(), "permission decision rejects invalid operation");
  expect(has_issue(invalid_operation, ava::session::SessionReplayIssueKind::InvalidPermissionDecision),
         "invalid permission operation records an invalid permission issue");

  ava::session::SessionReplayValidation invalid_risk;
  validate_permission_decision(invalid_risk, pending, 1,
                               permission_entry("entry_invalid_risk",
                                                "{\"operation\":\"read\",\"mode\":\"build\","
                                                "\"tool_name\":\"read_file\",\"action\":\"allow\","
                                                "\"reason\":\"bad\",\"risk\":\"extreme\","
                                                "\"resolution\":\"allow\",\"resolution_source\":\"policy\"}"));
  expect(!invalid_risk.ok(), "permission decision rejects invalid risk");
  expect(has_issue(invalid_risk, ava::session::SessionReplayIssueKind::InvalidPermissionDecision),
         "invalid permission risk records an invalid permission issue");
}

void test_permission_resolution_without_ask_is_reported()
{
  ava::session::SessionReplayValidation validation;
  ava::session::SessionReplayPendingPermissions pending;

  validate_permission_decision(validation, pending, 0,
                               permission_entry("entry_resolution_without_ask",
                                                "{\"operation\":\"edit\",\"mode\":\"build\","
                                                "\"tool_name\":\"write_file\",\"action\":\"ask\","
                                                "\"reason\":\"outside workspace\","
                                                "\"target_path\":\"/tmp/file.txt\","
                                                "\"resolution\":\"deny\",\"resolution_source\":\"resolver\"}"));
  expect(!validation.ok(), "permission resolver outcome without ask is rejected");
  expect(has_issue(validation, ava::session::SessionReplayIssueKind::PermissionResolutionWithoutAsk),
         "permission resolver outcome without ask records pairing issue");
}

void test_unresolved_permission_prompts_are_reported()
{
  ava::session::SessionReplayValidation validation;
  ava::session::SessionReplayPendingPermissions pending;

  validate_permission_decision(
      validation, pending, 0,
      permission_entry("entry_unresolved_ask",
                       "{\"operation\":\"network.fetch\",\"mode\":\"build\","
                       "\"tool_name\":\"webfetch\",\"action\":\"ask\","
                       "\"reason\":\"network access requires approval\","
                       "\"command\":\"https://example.com\",\"resolution_source\":\"policy\"}"));
  add_unresolved_permission_prompts(validation, pending, 1);
  expect(!validation.ok(), "unresolved permission ask is reported at replay completion");
  expect(has_issue(validation, ava::session::SessionReplayIssueKind::UnresolvedPermissionPrompt),
         "unresolved permission ask records unresolved prompt issue");
}

void test_compaction_boundary_reports_pending_permission_prompt()
{
  ava::session::SessionReplayValidation validation;
  ava::session::SessionReplayPendingPermissions pending;

  validate_permission_decision(validation, pending, 0,
                               permission_entry("entry_pending_before_compaction",
                                                "{\"operation\":\"edit\",\"mode\":\"build\","
                                                "\"tool_name\":\"write_file\",\"action\":\"ask\","
                                                "\"reason\":\"outside workspace\",\"target_path\":\"/tmp/file.txt\","
                                                "\"resolution_source\":\"policy\"}"));
  add_compaction_permission_boundary_issues(validation, pending, 1, compaction_entry());
  expect(!validation.ok(), "compaction boundary reports pending permission ask");
  expect(has_issue(validation, ava::session::SessionReplayIssueKind::CompactionWithUnresolvedPermissionPrompt),
         "compaction boundary records unresolved permission issue");
}

}  // namespace

void run_session_validation_permission_tests()
{
  test_permission_ask_and_resolution_pair_by_request_id();
  test_permission_validation_rejects_malformed_fields();
  test_permission_resolution_without_ask_is_reported();
  test_unresolved_permission_prompts_are_reported();
  test_compaction_boundary_reports_pending_permission_prompt();
}
