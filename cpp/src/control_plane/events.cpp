#include "ava/control_plane/events.hpp"

#include <array>

namespace ava::control_plane {
namespace {

constexpr std::array<CanonicalEventField, 8> kApprovalRequestFields{
    CanonicalEventField::RunId,
    CanonicalEventField::Id,
    CanonicalEventField::ToolCallId,
    CanonicalEventField::ToolName,
    CanonicalEventField::Args,
    CanonicalEventField::RiskLevel,
    CanonicalEventField::Reason,
    CanonicalEventField::Warnings,
};
constexpr std::array<CanonicalEventField, 4> kQuestionRequestFields{
    CanonicalEventField::RunId,
    CanonicalEventField::Id,
    CanonicalEventField::Question,
    CanonicalEventField::Options,
};
constexpr std::array<CanonicalEventField, 3> kInteractiveRequestClearedFields{
    CanonicalEventField::RunId,
    CanonicalEventField::RequestId,
    CanonicalEventField::RequestKind,
};
constexpr std::array<CanonicalEventField, 3> kPlanCreatedFields{
    CanonicalEventField::RunId,
    CanonicalEventField::Id,
    CanonicalEventField::Plan,
};
constexpr std::array<CanonicalEventField, 2> kPlanStepCompleteFields{
    CanonicalEventField::RunId,
    CanonicalEventField::StepId,
};
constexpr std::array<CanonicalEventField, 2> kCompleteFields{
    CanonicalEventField::RunId,
    CanonicalEventField::Session,
};
constexpr std::array<CanonicalEventField, 2> kErrorFields{
    CanonicalEventField::RunId,
    CanonicalEventField::Message,
};
constexpr std::array<CanonicalEventField, 4> kSubagentCompleteFields{
    CanonicalEventField::RunId,
    CanonicalEventField::CallId,
    CanonicalEventField::SessionId,
    CanonicalEventField::Description,
};
constexpr std::array<CanonicalEventField, 4> kStreamingEditProgressFields{
    CanonicalEventField::RunId,
    CanonicalEventField::CallId,
    CanonicalEventField::ToolName,
    CanonicalEventField::BytesReceived,
};

constexpr std::array<CanonicalEventSpec, 9> kCanonicalEventSpecs{ {
    {CanonicalEventKind::ApprovalRequest, kApprovalRequestFields},
    {CanonicalEventKind::QuestionRequest, kQuestionRequestFields},
    {CanonicalEventKind::InteractiveRequestCleared, kInteractiveRequestClearedFields},
    {CanonicalEventKind::PlanCreated, kPlanCreatedFields},
    {CanonicalEventKind::PlanStepComplete, kPlanStepCompleteFields},
    {CanonicalEventKind::Complete, kCompleteFields},
    {CanonicalEventKind::Error, kErrorFields},
    {CanonicalEventKind::SubagentComplete, kSubagentCompleteFields},
    {CanonicalEventKind::StreamingEditProgress, kStreamingEditProgressFields},
} };

constexpr std::array<CanonicalEventKind, 5> kRequiredBackendKinds{
    CanonicalEventKind::PlanStepComplete,
    CanonicalEventKind::Complete,
    CanonicalEventKind::Error,
    CanonicalEventKind::SubagentComplete,
    CanonicalEventKind::StreamingEditProgress,
};

}  // namespace

std::string_view canonical_event_kind_to_type_tag(CanonicalEventKind kind) {
  switch(kind) {
    case CanonicalEventKind::ApprovalRequest:
      return "approval_request";
    case CanonicalEventKind::QuestionRequest:
      return "question_request";
    case CanonicalEventKind::InteractiveRequestCleared:
      return "interactive_request_cleared";
    case CanonicalEventKind::PlanCreated:
      return "plan_created";
    case CanonicalEventKind::PlanStepComplete:
      return "plan_step_complete";
    case CanonicalEventKind::Complete:
      return "complete";
    case CanonicalEventKind::Error:
      return "error";
    case CanonicalEventKind::SubagentComplete:
      return "subagent_complete";
    case CanonicalEventKind::StreamingEditProgress:
      return "streaming_edit_progress";
  }
  return "error";
}

std::string_view canonical_event_field_to_json_key(CanonicalEventField field) {
  switch(field) {
    case CanonicalEventField::RunId:
      return "run_id";
    case CanonicalEventField::Id:
      return "id";
    case CanonicalEventField::RequestId:
      return "request_id";
    case CanonicalEventField::RequestKind:
      return "request_kind";
    case CanonicalEventField::ToolCallId:
      return "tool_call_id";
    case CanonicalEventField::ToolName:
      return "tool_name";
    case CanonicalEventField::Args:
      return "args";
    case CanonicalEventField::RiskLevel:
      return "risk_level";
    case CanonicalEventField::Reason:
      return "reason";
    case CanonicalEventField::Warnings:
      return "warnings";
    case CanonicalEventField::Question:
      return "question";
    case CanonicalEventField::Options:
      return "options";
    case CanonicalEventField::Plan:
      return "plan";
    case CanonicalEventField::StepId:
      return "step_id";
    case CanonicalEventField::Session:
      return "session";
    case CanonicalEventField::SessionId:
      return "session_id";
    case CanonicalEventField::CallId:
      return "call_id";
    case CanonicalEventField::Description:
      return "description";
    case CanonicalEventField::BytesReceived:
      return "bytes_received";
    case CanonicalEventField::Message:
      return "message";
  }
  return "message";
}

std::span<const CanonicalEventSpec> canonical_event_specs() {
  return kCanonicalEventSpecs;
}

const CanonicalEventSpec* canonical_event_spec(std::string_view type_tag) {
  for(const auto& spec : kCanonicalEventSpecs) {
    if(canonical_event_kind_to_type_tag(spec.kind) == type_tag) {
      return &spec;
    }
  }
  return nullptr;
}

std::span<const CanonicalEventKind> required_backend_event_kinds() {
  return kRequiredBackendKinds;
}

}  // namespace ava::control_plane
