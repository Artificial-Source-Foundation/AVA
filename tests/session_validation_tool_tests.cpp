#include <algorithm>
#include <string>
#include <utility>

#include "ava/session/validation_tool.h"
#include "tests/support/test_harness.h"

namespace {

ava::session::SessionEntry tool_entry(std::string id, ava::session::EntryType type, std::string data_json)
{
  return ava::session::SessionEntry{.id = std::move(id),
                                    .parent_id = "",
                                    .type = type,
                                    .timestamp = "2026-05-05T00:00:00Z",
                                    .data_json = std::move(data_json)};
}

ava::session::SessionEntry tool_call_entry(std::string id, std::string call_id, std::string name)
{
  return tool_entry(std::move(id), ava::session::EntryType::ToolCall,
                    "{\"call_id\":\"" + std::move(call_id) + "\",\"name\":\"" + std::move(name) + "\"}");
}

ava::session::SessionEntry tool_result_entry(std::string id, std::string call_id, std::string name,
                                             std::string suffix = "")
{
  return tool_entry(
      std::move(id), ava::session::EntryType::ToolResult,
      "{\"call_id\":\"" + std::move(call_id) + "\",\"name\":\"" + std::move(name) + "\"" + std::move(suffix) + "}");
}

ava::session::SessionEntry compaction_entry()
{
  return tool_entry("entry_compaction", ava::session::EntryType::Compaction, "{\"summary\":\"compact\"}");
}

bool has_issue(ava::session::SessionReplayValidation const& validation, ava::session::SessionReplayIssueKind kind)
{
  return std::ranges::any_of(validation.issues,
                             [kind](ava::session::SessionReplayIssue const& issue) { return issue.kind == kind; });
}

std::string structured_success_suffix()
{
  return ",\"success\":true,\"status\":\"success\","
         "\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_read\","
         "\"tool\":\"read_file\",\"status\":\"success\",\"ok\":true,"
         "\"summary\":\"read 2 bytes\",\"content_type\":\"text/plain\","
         "\"content\":\"ok\"}";
}

void test_tool_call_and_result_pair_with_structured_payload()
{
  ava::session::SessionReplayValidation validation;
  ava::session::SessionReplayToolCalls calls;
  ava::session::SessionReplayValidationOptions options{.require_structured_tool_results = true};

  validate_tool_call_entry(validation, calls, options, 0, tool_call_entry("entry_tool_call", "call_read", "read_file"));
  validate_tool_result_entry(
      validation, calls, options, 1,
      tool_result_entry("entry_tool_result", "call_read", "read_file", structured_success_suffix()));
  expect(validation.ok(), "tool call/result pair accepts matching structured payload");
  expect(calls.contains("call_read") && calls.at("call_read").result_seen, "tool result marks call as completed");
}

void test_tool_call_duplicate_and_missing_ids_are_reported()
{
  ava::session::SessionReplayValidation validation;
  ava::session::SessionReplayToolCalls calls;
  ava::session::SessionReplayValidationOptions options;

  validate_tool_call_entry(validation, calls, options, 0, tool_call_entry("entry_tool_call", "call_read", "read_file"));
  validate_tool_call_entry(validation, calls, options, 1,
                           tool_call_entry("entry_duplicate_tool_call", "call_read", "read_file"));
  expect(!validation.ok(), "duplicate tool call id is rejected");
  expect(has_issue(validation, ava::session::SessionReplayIssueKind::DuplicateToolCallId),
         "duplicate tool call id records duplicate issue");

  ava::session::SessionReplayValidation missing_id_validation;
  validate_tool_result_entry(
      missing_id_validation, calls, options, 2,
      tool_entry("entry_missing_call_id", ava::session::EntryType::ToolResult, "{\"name\":\"read_file\"}"));
  expect(!missing_id_validation.ok(), "tool result without call_id is rejected");
  expect(has_issue(missing_id_validation, ava::session::SessionReplayIssueKind::EmptyToolCallId),
         "tool result without call_id records empty id issue");
}

void test_tool_result_pairing_errors_are_reported()
{
  ava::session::SessionReplayValidation missing_call_validation;
  ava::session::SessionReplayToolCalls missing_call_map;
  ava::session::SessionReplayValidationOptions options;
  validate_tool_result_entry(missing_call_validation, missing_call_map, options, 0,
                             tool_result_entry("entry_orphan_result", "call_read", "read_file"));
  expect(!missing_call_validation.ok(), "tool result without earlier call is rejected");
  expect(has_issue(missing_call_validation, ava::session::SessionReplayIssueKind::ToolResultWithoutCall),
         "tool result without earlier call records orphan result issue");

  ava::session::SessionReplayValidation mismatch_validation;
  ava::session::SessionReplayToolCalls calls;
  validate_tool_call_entry(mismatch_validation, calls, options, 1,
                           tool_call_entry("entry_tool_call", "call_read", "read_file"));
  validate_tool_result_entry(mismatch_validation, calls, options, 2,
                             tool_result_entry("entry_tool_result", "call_read", "write_file"));
  expect(!mismatch_validation.ok(), "tool result with mismatched tool name is rejected");
  expect(has_issue(mismatch_validation, ava::session::SessionReplayIssueKind::ToolResultToolMismatch),
         "tool result with mismatched tool name records mismatch issue");
}

void test_structured_tool_result_validation()
{
  ava::session::SessionReplayValidation missing_structured;
  ava::session::SessionReplayToolCalls calls;
  ava::session::SessionReplayValidationOptions options{.require_tool_result_pairing = false,
                                                       .require_structured_tool_results = true};
  validate_tool_result_entry(missing_structured, calls, options, 0,
                             tool_result_entry("entry_result", "call_read", "read_file"));
  expect(!missing_structured.ok(), "tool result rejects missing structured payload when required");
  expect(has_issue(missing_structured, ava::session::SessionReplayIssueKind::MissingStructuredToolResult),
         "missing structured payload records missing structured issue");

  ava::session::SessionReplayValidation mismatched_structured;
  validate_tool_result_entry(mismatched_structured, calls, options, 1,
                             tool_result_entry("entry_result_mismatch", "call_read", "read_file",
                                               ",\"structured_result\":{\"call_id\":\"other\",\"tool\":\"read_file\","
                                               "\"status\":\"success\",\"content_type\":\"text/plain\"}"));
  expect(!mismatched_structured.ok(), "tool result rejects structured payload with mismatched call id");
  expect(has_issue(mismatched_structured, ava::session::SessionReplayIssueKind::StructuredToolResultMismatch),
         "mismatched structured payload records mismatch issue");
}

void test_unresolved_tool_calls_are_reported()
{
  ava::session::SessionReplayValidation validation;
  ava::session::SessionReplayToolCalls calls;
  ava::session::SessionReplayValidationOptions options;

  validate_tool_call_entry(validation, calls, options, 0, tool_call_entry("entry_tool_call", "call_read", "read_file"));
  add_unresolved_tool_calls(validation, calls, 1);
  expect(!validation.ok(), "unresolved tool call is reported at replay completion");
  expect(has_issue(validation, ava::session::SessionReplayIssueKind::UnresolvedToolCall),
         "unresolved tool call records unresolved issue");

  ava::session::SessionReplayValidation compaction_validation;
  add_compaction_tool_boundary_issues(compaction_validation, calls, 1, compaction_entry());
  expect(!compaction_validation.ok(), "compaction boundary reports unresolved tool call");
  expect(has_issue(compaction_validation, ava::session::SessionReplayIssueKind::CompactionWithUnresolvedToolCall),
         "compaction boundary records unresolved tool call issue");
}

}  // namespace

void run_session_validation_tool_tests()
{
  test_tool_call_and_result_pair_with_structured_payload();
  test_tool_call_duplicate_and_missing_ids_are_reported();
  test_tool_result_pairing_errors_are_reported();
  test_structured_tool_result_validation();
  test_unresolved_tool_calls_are_reported();
}
