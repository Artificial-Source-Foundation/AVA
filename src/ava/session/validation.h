#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ava/session/session_store.h"

namespace ava::session {

enum class SessionReplayIssueSeverity {
  Warning,
  Error,
};

enum class SessionReplayIssueKind {
  DuplicateEntryId,
  UnknownParentId,
  EmptyToolCallId,
  DuplicateToolCallId,
  ToolResultWithoutCall,
  ToolResultToolMismatch,
  DuplicateToolResult,
  UnresolvedToolCall,
  MissingStructuredToolResult,
  InvalidStructuredToolResult,
  StructuredToolResultMismatch,
  InvalidPermissionDecision,
  PermissionResolutionWithoutAsk,
  UnresolvedPermissionPrompt,
};

struct SessionReplayIssue {
  SessionReplayIssueSeverity severity = SessionReplayIssueSeverity::Error;
  SessionReplayIssueKind kind = SessionReplayIssueKind::DuplicateEntryId;
  std::size_t entry_index = 0;
  std::string entry_id;
  std::string call_id;
  std::string message;
};

struct SessionReplayValidationOptions {
  bool require_known_parent_ids = true;
  bool require_tool_result_pairing = true;
  bool require_permission_decision_integrity = true;
  bool require_structured_tool_results = false;
};

struct SessionReplayValidation {
  std::vector<SessionReplayIssue> issues;
  std::size_t error_count = 0;
  std::size_t warning_count = 0;

  [[nodiscard]] bool ok() const noexcept { return error_count == 0; }
};

[[nodiscard]] std::string_view to_string(SessionReplayIssueSeverity severity) noexcept;
[[nodiscard]] std::string_view to_string(SessionReplayIssueKind kind) noexcept;
[[nodiscard]] SessionReplayValidation validate_session_replay(const std::vector<SessionEntry>& entries,
                                                              SessionReplayValidationOptions options = {});

}  // namespace ava::session
