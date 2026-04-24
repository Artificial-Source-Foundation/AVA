#pragma once

#include <span>
#include <string_view>

namespace ava::control_plane {

enum class CanonicalEventKind {
  ApprovalRequest,
  QuestionRequest,
  PlanRequest,
  InteractiveRequestCleared,
  PlanCreated,
  PlanStepComplete,
  Complete,
  Error,
  SubagentComplete,
  StreamingEditProgress,
};

enum class CanonicalEventField {
  RunId,
  Id,
  RequestId,
  RequestKind,
  ToolCallId,
  ToolName,
  Args,
  RiskLevel,
  Reason,
  Warnings,
  Question,
  Options,
  Plan,
  StepId,
  Session,
  SessionId,
  CallId,
  Description,
  BytesReceived,
  Message,
};

struct CanonicalEventSpec {
  CanonicalEventKind kind;
  std::span<const CanonicalEventField> required_fields;
};

[[nodiscard]] std::string_view canonical_event_kind_to_type_tag(CanonicalEventKind kind);
[[nodiscard]] std::string_view canonical_event_field_to_json_key(CanonicalEventField field);

[[nodiscard]] std::span<const CanonicalEventSpec> canonical_event_specs();
[[nodiscard]] const CanonicalEventSpec* canonical_event_spec(std::string_view type_tag);
[[nodiscard]] std::span<const CanonicalEventKind> required_backend_event_kinds();

}  // namespace ava::control_plane
