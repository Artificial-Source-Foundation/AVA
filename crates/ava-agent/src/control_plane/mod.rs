//! Compatibility + backend-helper shim over the shared control-plane contract.
//!
//! Pure command/event/session/interactive/queue/orchestration contracts live in
//! `crates/ava-control-plane/src/`. This module is intentionally limited to
//! compatibility re-exports and backend-only helpers that depend on runtime types.

pub mod commands;
pub mod events;
pub mod interactive;
pub mod orchestration;
pub mod queue;
pub mod sessions;

#[cfg(test)]
mod tests {
    use serde_json::json;

    use super::commands::canonical_command_specs;
    use super::events::{canonical_event_specs, required_backend_event_kinds};

    #[test]
    fn control_plane_contract_fixture_matches_current_wire_contract() {
        let fixture = json!({
            "commands": canonical_command_specs(),
            "events": canonical_event_specs(),
            "required_backend_events": required_backend_event_kinds(),
        });

        assert_eq!(
            fixture,
            json!({
                "commands": [
                    {
                        "command": "submit_goal",
                        "name": "submit_goal",
                        "family": "goal_submission",
                        "response_envelope": "accepted_run_handle",
                        "completion_mode": "accepted-and-streaming",
                        "terminal_signals": ["complete_event", "error_event"],
                        "correlation_ids": {
                            "accepted_response": ["session_id"],
                            "lifecycle": ["session_id"]
                        }
                    },
                    {
                        "command": "cancel_agent",
                        "name": "cancel_agent",
                        "family": "cancellation",
                        "response_envelope": "ack",
                        "completion_mode": "fire-and-forget",
                        "terminal_signals": ["none"],
                        "correlation_ids": {
                            "accepted_response": [],
                            "lifecycle": []
                        }
                    },
                    {
                        "command": "retry_last_message",
                        "name": "retry_last_message",
                        "family": "retry_and_replay",
                        "response_envelope": "accepted_run_handle",
                        "completion_mode": "accepted-and-streaming",
                        "terminal_signals": ["complete_event", "error_event"],
                        "correlation_ids": {
                            "accepted_response": ["session_id"],
                            "lifecycle": ["session_id"]
                        }
                    },
                    {
                        "command": "edit_and_resend",
                        "name": "edit_and_resend",
                        "family": "retry_and_replay",
                        "response_envelope": "accepted_run_handle",
                        "completion_mode": "accepted-and-streaming",
                        "terminal_signals": ["complete_event", "error_event"],
                        "correlation_ids": {
                            "accepted_response": ["session_id"],
                            "lifecycle": ["session_id"]
                        }
                    },
                    {
                        "command": "regenerate_response",
                        "name": "regenerate_response",
                        "family": "retry_and_replay",
                        "response_envelope": "accepted_run_handle",
                        "completion_mode": "accepted-and-streaming",
                        "terminal_signals": ["complete_event", "error_event"],
                        "correlation_ids": {
                            "accepted_response": ["session_id"],
                            "lifecycle": ["session_id"]
                        }
                    },
                    {
                        "command": "resolve_approval",
                        "name": "resolve_approval",
                        "family": "interactive_resolution",
                        "response_envelope": "ack",
                        "completion_mode": "completion-bound",
                        "terminal_signals": ["direct_result", "interactive_resolved"],
                        "correlation_ids": {
                            "accepted_response": [],
                            "lifecycle": ["interactive_request_id"]
                        }
                    },
                    {
                        "command": "resolve_question",
                        "name": "resolve_question",
                        "family": "interactive_resolution",
                        "response_envelope": "ack",
                        "completion_mode": "completion-bound",
                        "terminal_signals": ["direct_result", "interactive_resolved"],
                        "correlation_ids": {
                            "accepted_response": [],
                            "lifecycle": ["interactive_request_id"]
                        }
                    },
                    {
                        "command": "resolve_plan",
                        "name": "resolve_plan",
                        "family": "interactive_resolution",
                        "response_envelope": "ack",
                        "completion_mode": "completion-bound",
                        "terminal_signals": ["direct_result", "interactive_resolved"],
                        "correlation_ids": {
                            "accepted_response": [],
                            "lifecycle": ["interactive_request_id"]
                        }
                    },
                    {
                        "command": "steer_agent",
                        "name": "steer_agent",
                        "family": "queue_dispatch",
                        "response_envelope": "ack",
                        "completion_mode": "accepted-and-streaming",
                        "terminal_signals": ["complete_event", "error_event"],
                        "correlation_ids": {
                            "accepted_response": [],
                            "lifecycle": []
                        }
                    },
                    {
                        "command": "follow_up_agent",
                        "name": "follow_up_agent",
                        "family": "queue_dispatch",
                        "response_envelope": "ack",
                        "completion_mode": "accepted-and-streaming",
                        "terminal_signals": ["complete_event", "error_event"],
                        "correlation_ids": {
                            "accepted_response": [],
                            "lifecycle": []
                        }
                    },
                    {
                        "command": "post_complete_agent",
                        "name": "post_complete_agent",
                        "family": "queue_dispatch",
                        "response_envelope": "ack",
                        "completion_mode": "accepted-and-streaming",
                        "terminal_signals": ["complete_event", "error_event"],
                        "correlation_ids": {
                            "accepted_response": [],
                            "lifecycle": []
                        }
                    },
                    {
                        "command": "clear_message_queue",
                        "name": "clear_message_queue",
                        "family": "queue_control",
                        "response_envelope": "ack",
                        "completion_mode": "fire-and-forget",
                        "terminal_signals": ["none"],
                        "correlation_ids": {
                            "accepted_response": [],
                            "lifecycle": []
                        }
                    },
                    {
                        "command": "list_agent_tools",
                        "name": "list_agent_tools",
                        "family": "tool_introspection",
                        "response_envelope": "tool_list",
                        "completion_mode": "completion-bound",
                        "terminal_signals": ["direct_result"],
                        "correlation_ids": {
                            "accepted_response": [],
                            "lifecycle": []
                        }
                    }
                ],
                "events": [
                    {
                        "kind": "approval_request",
                        "required_fields": [
                            "run_id",
                            "id",
                            "tool_call_id",
                            "tool_name",
                            "args",
                            "risk_level",
                            "reason",
                            "warnings"
                        ]
                    },
                    {
                        "kind": "question_request",
                        "required_fields": ["run_id", "id", "question", "options"]
                    },
                    {
                        "kind": "interactive_request_cleared",
                        "required_fields": ["run_id", "request_id", "request_kind"]
                    },
                    {
                        "kind": "plan_created",
                        "required_fields": ["run_id", "id", "plan"]
                    },
                    {
                        "kind": "plan_step_complete",
                        "required_fields": ["run_id", "step_id"]
                    },
                    {
                        "kind": "complete",
                        "required_fields": ["run_id", "session"]
                    },
                    {
                        "kind": "error",
                        "required_fields": ["run_id", "message"]
                    },
                    {
                        "kind": "subagent_complete",
                        "required_fields": ["run_id", "call_id", "session_id", "description"]
                    },
                    {
                        "kind": "streaming_edit_progress",
                        "required_fields": ["run_id", "call_id", "tool_name", "bytes_received"]
                    }
                ],
                "required_backend_events": [
                    "plan_step_complete",
                    "complete",
                    "error",
                    "subagent_complete",
                    "streaming_edit_progress"
                ]
            })
        );
    }
}
