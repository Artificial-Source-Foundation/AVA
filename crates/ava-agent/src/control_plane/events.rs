//! Backend projection helpers on top of shared event contracts.

use ava_control_plane::events::CanonicalEventKind;

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
    use super::*;

    #[test]
    fn backend_projection_helper_flags_terminal_and_progress_events() {
        assert_eq!(
            required_backend_event_kind(&crate::agent_loop::AgentEvent::PlanStepComplete {
                step_id: "step-1".to_string(),
            }),
            Some(CanonicalEventKind::PlanStepComplete)
        );
        assert_eq!(
            required_backend_event_kind(&crate::agent_loop::AgentEvent::Complete(
                ava_types::Session::new()
            )),
            Some(CanonicalEventKind::Complete)
        );
        assert_eq!(
            required_backend_event_kind(&crate::agent_loop::AgentEvent::Error("boom".to_string())),
            Some(CanonicalEventKind::Error)
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
}
