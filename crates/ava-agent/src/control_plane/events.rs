use serde::Serialize;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum CanonicalEventKind {
    ApprovalRequest,
    QuestionRequest,
    InteractiveRequestCleared,
    PlanCreated,
    PlanStepComplete,
    Complete,
    Error,
    SubagentComplete,
    StreamingEditProgress,
}

impl CanonicalEventKind {
    pub const ALL: [Self; 9] = [
        Self::ApprovalRequest,
        Self::QuestionRequest,
        Self::InteractiveRequestCleared,
        Self::PlanCreated,
        Self::PlanStepComplete,
        Self::Complete,
        Self::Error,
        Self::SubagentComplete,
        Self::StreamingEditProgress,
    ];

    pub const fn type_tag(self) -> &'static str {
        match self {
            Self::ApprovalRequest => "approval_request",
            Self::QuestionRequest => "question_request",
            Self::InteractiveRequestCleared => "interactive_request_cleared",
            Self::PlanCreated => "plan_created",
            Self::PlanStepComplete => "plan_step_complete",
            Self::Complete => "complete",
            Self::Error => "error",
            Self::SubagentComplete => "subagent_complete",
            Self::StreamingEditProgress => "streaming_edit_progress",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum CanonicalEventField {
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
}

impl CanonicalEventField {
    pub const fn json_key(self) -> &'static str {
        match self {
            Self::RunId => "run_id",
            Self::Id => "id",
            Self::RequestId => "request_id",
            Self::RequestKind => "request_kind",
            Self::ToolCallId => "tool_call_id",
            Self::ToolName => "tool_name",
            Self::Args => "args",
            Self::RiskLevel => "risk_level",
            Self::Reason => "reason",
            Self::Warnings => "warnings",
            Self::Question => "question",
            Self::Options => "options",
            Self::Plan => "plan",
            Self::StepId => "step_id",
            Self::Session => "session",
            Self::SessionId => "session_id",
            Self::CallId => "call_id",
            Self::Description => "description",
            Self::BytesReceived => "bytes_received",
            Self::Message => "message",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub struct CanonicalEventSpec {
    pub kind: CanonicalEventKind,
    pub required_fields: &'static [CanonicalEventField],
}

const APPROVAL_REQUEST_FIELDS: &[CanonicalEventField] = &[
    CanonicalEventField::RunId,
    CanonicalEventField::Id,
    CanonicalEventField::ToolCallId,
    CanonicalEventField::ToolName,
    CanonicalEventField::Args,
    CanonicalEventField::RiskLevel,
    CanonicalEventField::Reason,
    CanonicalEventField::Warnings,
];
const QUESTION_REQUEST_FIELDS: &[CanonicalEventField] = &[
    CanonicalEventField::RunId,
    CanonicalEventField::Id,
    CanonicalEventField::Question,
    CanonicalEventField::Options,
];
const INTERACTIVE_REQUEST_CLEARED_FIELDS: &[CanonicalEventField] = &[
    CanonicalEventField::RunId,
    CanonicalEventField::RequestId,
    CanonicalEventField::RequestKind,
];
const PLAN_CREATED_FIELDS: &[CanonicalEventField] = &[
    CanonicalEventField::RunId,
    CanonicalEventField::Id,
    CanonicalEventField::Plan,
];
const PLAN_STEP_COMPLETE_FIELDS: &[CanonicalEventField] =
    &[CanonicalEventField::RunId, CanonicalEventField::StepId];
const COMPLETE_FIELDS: &[CanonicalEventField] =
    &[CanonicalEventField::RunId, CanonicalEventField::Session];
const ERROR_FIELDS: &[CanonicalEventField] =
    &[CanonicalEventField::RunId, CanonicalEventField::Message];
const SUBAGENT_COMPLETE_FIELDS: &[CanonicalEventField] = &[
    CanonicalEventField::RunId,
    CanonicalEventField::CallId,
    CanonicalEventField::SessionId,
    CanonicalEventField::Description,
];
const STREAMING_EDIT_PROGRESS_FIELDS: &[CanonicalEventField] = &[
    CanonicalEventField::RunId,
    CanonicalEventField::CallId,
    CanonicalEventField::ToolName,
    CanonicalEventField::BytesReceived,
];

const CANONICAL_EVENT_SPECS: &[CanonicalEventSpec] = &[
    CanonicalEventSpec {
        kind: CanonicalEventKind::ApprovalRequest,
        required_fields: APPROVAL_REQUEST_FIELDS,
    },
    CanonicalEventSpec {
        kind: CanonicalEventKind::QuestionRequest,
        required_fields: QUESTION_REQUEST_FIELDS,
    },
    CanonicalEventSpec {
        kind: CanonicalEventKind::InteractiveRequestCleared,
        required_fields: INTERACTIVE_REQUEST_CLEARED_FIELDS,
    },
    CanonicalEventSpec {
        kind: CanonicalEventKind::PlanCreated,
        required_fields: PLAN_CREATED_FIELDS,
    },
    CanonicalEventSpec {
        kind: CanonicalEventKind::PlanStepComplete,
        required_fields: PLAN_STEP_COMPLETE_FIELDS,
    },
    CanonicalEventSpec {
        kind: CanonicalEventKind::Complete,
        required_fields: COMPLETE_FIELDS,
    },
    CanonicalEventSpec {
        kind: CanonicalEventKind::Error,
        required_fields: ERROR_FIELDS,
    },
    CanonicalEventSpec {
        kind: CanonicalEventKind::SubagentComplete,
        required_fields: SUBAGENT_COMPLETE_FIELDS,
    },
    CanonicalEventSpec {
        kind: CanonicalEventKind::StreamingEditProgress,
        required_fields: STREAMING_EDIT_PROGRESS_FIELDS,
    },
];

const REQUIRED_BACKEND_EVENT_KINDS: &[CanonicalEventKind] = &[
    CanonicalEventKind::PlanStepComplete,
    CanonicalEventKind::Complete,
    CanonicalEventKind::Error,
    CanonicalEventKind::SubagentComplete,
    CanonicalEventKind::StreamingEditProgress,
];

pub const fn canonical_event_specs() -> &'static [CanonicalEventSpec] {
    CANONICAL_EVENT_SPECS
}

pub fn canonical_event_spec(type_tag: &str) -> Option<&'static CanonicalEventSpec> {
    CANONICAL_EVENT_SPECS
        .iter()
        .find(|spec| spec.kind.type_tag() == type_tag)
}

pub fn required_backend_event_kinds() -> &'static [CanonicalEventKind] {
    REQUIRED_BACKEND_EVENT_KINDS
}

pub fn required_backend_event_kind(
    event: &crate::agent_loop::AgentEvent,
) -> Option<CanonicalEventKind> {
    use crate::agent_loop::AgentEvent as BackendEvent;

    match event {
        BackendEvent::PlanStepComplete { .. } => Some(CanonicalEventKind::PlanStepComplete),
        BackendEvent::Complete(_) => Some(CanonicalEventKind::Complete),
        BackendEvent::Error(_) => Some(CanonicalEventKind::Error),
        BackendEvent::SubAgentComplete { .. } => Some(CanonicalEventKind::SubagentComplete),
        BackendEvent::StreamingEditProgress { .. } => {
            Some(CanonicalEventKind::StreamingEditProgress)
        }
        _ => None,
    }
}

pub fn backend_event_requires_interactive_projection(
    event: &crate::agent_loop::AgentEvent,
) -> bool {
    required_backend_event_kind(event).is_some()
}

#[cfg(test)]
mod tests {
    use std::collections::HashSet;

    use serde_json::json;

    use super::*;

    #[test]
    fn canonical_event_type_tags_are_unique() {
        let mut seen = HashSet::new();
        for spec in canonical_event_specs() {
            assert!(seen.insert(spec.kind.type_tag()));
        }
    }

    #[test]
    fn ws2_event_inventory_matches_control_plane_contract() {
        let names: Vec<_> = canonical_event_specs()
            .iter()
            .map(|spec| spec.kind.type_tag())
            .collect();
        let expected: Vec<_> = CanonicalEventKind::ALL
            .into_iter()
            .map(CanonicalEventKind::type_tag)
            .collect();

        assert_eq!(names, expected);
    }

    #[test]
    fn required_events_have_expected_required_fields() {
        let approval = canonical_event_spec("approval_request").expect("approval request spec");
        assert_eq!(
            approval.required_fields,
            &[
                CanonicalEventField::RunId,
                CanonicalEventField::Id,
                CanonicalEventField::ToolCallId,
                CanonicalEventField::ToolName,
                CanonicalEventField::Args,
                CanonicalEventField::RiskLevel,
                CanonicalEventField::Reason,
                CanonicalEventField::Warnings,
            ]
        );

        let subagent = canonical_event_spec("subagent_complete").expect("subagent spec");
        assert_eq!(
            subagent.required_fields,
            &[
                CanonicalEventField::RunId,
                CanonicalEventField::CallId,
                CanonicalEventField::SessionId,
                CanonicalEventField::Description,
            ]
        );

        let edit_progress =
            canonical_event_spec("streaming_edit_progress").expect("edit progress spec");
        assert_eq!(
            edit_progress.required_fields,
            &[
                CanonicalEventField::RunId,
                CanonicalEventField::CallId,
                CanonicalEventField::ToolName,
                CanonicalEventField::BytesReceived,
            ]
        );
    }

    #[test]
    fn backend_projection_helper_flags_terminal_and_progress_events() {
        assert_eq!(
            required_backend_event_kind(&crate::agent_loop::AgentEvent::Complete(
                ava_types::Session::new()
            )),
            Some(CanonicalEventKind::Complete)
        );
        assert_eq!(
            required_backend_event_kind(&crate::agent_loop::AgentEvent::SubAgentComplete {
                call_id: "call-1".to_string(),
                session_id: "session-1".to_string(),
                messages: Vec::new(),
                description: "delegate".to_string(),
                input_tokens: 1,
                output_tokens: 2,
                cost_usd: 0.1,
                agent_type: None,
                provider: None,
                resumed: false,
            }),
            Some(CanonicalEventKind::SubagentComplete)
        );
        assert!(backend_event_requires_interactive_projection(
            &crate::agent_loop::AgentEvent::StreamingEditProgress {
                call_id: "call-2".to_string(),
                tool_name: "edit".to_string(),
                file_path: Some("src/lib.rs".to_string()),
                bytes_received: 256,
            }
        ));
        assert!(!backend_event_requires_interactive_projection(
            &crate::agent_loop::AgentEvent::Token("hello".to_string())
        ));
    }

    #[test]
    fn event_fixture_serializes_required_fields() {
        let fixture = serde_json::to_value(canonical_event_specs()).expect("fixture");

        assert!(fixture.as_array().expect("array").iter().any(|entry| {
            entry["kind"] == json!("approval_request")
                && entry["required_fields"]
                    == json!([
                        "run_id",
                        "id",
                        "tool_call_id",
                        "tool_name",
                        "args",
                        "risk_level",
                        "reason",
                        "warnings"
                    ])
        }));

        assert!(fixture.as_array().expect("array").iter().any(|entry| {
            entry["kind"] == json!("subagent_complete")
                && entry["required_fields"]
                    == json!(["run_id", "call_id", "session_id", "description"])
        }));
    }
}
