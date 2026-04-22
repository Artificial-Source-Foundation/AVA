//! Canonical control-plane command inventory and completion semantics.
//!
//! Ownership: command names, command families, response envelopes, lifecycle closure
//! signals, and correlation requirements shared across runtime adapters.

use ava_types::MessageTier;
use serde::Serialize;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ControlPlaneCommand {
    SubmitGoal,
    CancelAgent,
    RetryLastMessage,
    EditAndResend,
    RegenerateResponse,
    ResolveApproval,
    ResolveQuestion,
    ResolvePlan,
    SteerAgent,
    FollowUpAgent,
    PostCompleteAgent,
    ClearMessageQueue,
    ListAgentTools,
}

impl ControlPlaneCommand {
    pub const ALL: [Self; 13] = [
        Self::SubmitGoal,
        Self::CancelAgent,
        Self::RetryLastMessage,
        Self::EditAndResend,
        Self::RegenerateResponse,
        Self::ResolveApproval,
        Self::ResolveQuestion,
        Self::ResolvePlan,
        Self::SteerAgent,
        Self::FollowUpAgent,
        Self::PostCompleteAgent,
        Self::ClearMessageQueue,
        Self::ListAgentTools,
    ];

    pub const fn as_str(self) -> &'static str {
        match self {
            Self::SubmitGoal => "submit_goal",
            Self::CancelAgent => "cancel_agent",
            Self::RetryLastMessage => "retry_last_message",
            Self::EditAndResend => "edit_and_resend",
            Self::RegenerateResponse => "regenerate_response",
            Self::ResolveApproval => "resolve_approval",
            Self::ResolveQuestion => "resolve_question",
            Self::ResolvePlan => "resolve_plan",
            Self::SteerAgent => "steer_agent",
            Self::FollowUpAgent => "follow_up_agent",
            Self::PostCompleteAgent => "post_complete_agent",
            Self::ClearMessageQueue => "clear_message_queue",
            Self::ListAgentTools => "list_agent_tools",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum CommandFamily {
    GoalSubmission,
    Cancellation,
    RetryAndReplay,
    InteractiveResolution,
    QueueDispatch,
    QueueControl,
    ToolIntrospection,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ResponseEnvelope {
    AcceptedRunHandle,
    Ack,
    DirectResult,
    ToolList,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum CompletionMode {
    CompletionBound,
    AcceptedAndStreaming,
    FireAndForget,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum TerminalClosureSignal {
    CompleteEvent,
    ErrorEvent,
    RunInterrupted,
    InteractiveResolved,
    InteractiveError,
    DirectResult,
    None,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum CorrelationIdKey {
    SessionId,
    InteractiveRequestId,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub struct CorrelationIdRequirements {
    pub accepted_response: &'static [CorrelationIdKey],
    pub lifecycle: &'static [CorrelationIdKey],
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub struct CommandSpec {
    pub command: ControlPlaneCommand,
    pub name: &'static str,
    pub family: CommandFamily,
    pub response_envelope: ResponseEnvelope,
    pub completion_mode: CompletionMode,
    pub terminal_signals: &'static [TerminalClosureSignal],
    pub correlation_ids: CorrelationIdRequirements,
}

const COMPLETE_OR_ERROR: &[TerminalClosureSignal] = &[
    TerminalClosureSignal::CompleteEvent,
    TerminalClosureSignal::ErrorEvent,
];
const DIRECT_RESULT_ONLY: &[TerminalClosureSignal] = &[TerminalClosureSignal::DirectResult];
const DIRECT_RESULT_AND_INTERACTIVE_RESOLVED: &[TerminalClosureSignal] = &[
    TerminalClosureSignal::DirectResult,
    TerminalClosureSignal::InteractiveResolved,
];
const NO_TERMINAL_SIGNAL: &[TerminalClosureSignal] = &[TerminalClosureSignal::None];

const NO_CORRELATION_IDS: &[CorrelationIdKey] = &[];
const SESSION_ID_ONLY: &[CorrelationIdKey] = &[CorrelationIdKey::SessionId];
const INTERACTIVE_REQUEST_ID_ONLY: &[CorrelationIdKey] = &[CorrelationIdKey::InteractiveRequestId];
pub const CANONICAL_COMMAND_SPECS: &[CommandSpec] = &[
    CommandSpec {
        command: ControlPlaneCommand::SubmitGoal,
        name: "submit_goal",
        family: CommandFamily::GoalSubmission,
        response_envelope: ResponseEnvelope::AcceptedRunHandle,
        completion_mode: CompletionMode::AcceptedAndStreaming,
        terminal_signals: COMPLETE_OR_ERROR,
        correlation_ids: CorrelationIdRequirements {
            accepted_response: SESSION_ID_ONLY,
            lifecycle: SESSION_ID_ONLY,
        },
    },
    CommandSpec {
        command: ControlPlaneCommand::CancelAgent,
        name: "cancel_agent",
        family: CommandFamily::Cancellation,
        response_envelope: ResponseEnvelope::Ack,
        completion_mode: CompletionMode::FireAndForget,
        terminal_signals: NO_TERMINAL_SIGNAL,
        correlation_ids: CorrelationIdRequirements {
            accepted_response: NO_CORRELATION_IDS,
            lifecycle: NO_CORRELATION_IDS,
        },
    },
    CommandSpec {
        command: ControlPlaneCommand::RetryLastMessage,
        name: "retry_last_message",
        family: CommandFamily::RetryAndReplay,
        response_envelope: ResponseEnvelope::AcceptedRunHandle,
        completion_mode: CompletionMode::AcceptedAndStreaming,
        terminal_signals: COMPLETE_OR_ERROR,
        correlation_ids: CorrelationIdRequirements {
            accepted_response: SESSION_ID_ONLY,
            lifecycle: SESSION_ID_ONLY,
        },
    },
    CommandSpec {
        command: ControlPlaneCommand::EditAndResend,
        name: "edit_and_resend",
        family: CommandFamily::RetryAndReplay,
        response_envelope: ResponseEnvelope::AcceptedRunHandle,
        completion_mode: CompletionMode::AcceptedAndStreaming,
        terminal_signals: COMPLETE_OR_ERROR,
        correlation_ids: CorrelationIdRequirements {
            accepted_response: SESSION_ID_ONLY,
            lifecycle: SESSION_ID_ONLY,
        },
    },
    CommandSpec {
        command: ControlPlaneCommand::RegenerateResponse,
        name: "regenerate_response",
        family: CommandFamily::RetryAndReplay,
        response_envelope: ResponseEnvelope::AcceptedRunHandle,
        completion_mode: CompletionMode::AcceptedAndStreaming,
        terminal_signals: COMPLETE_OR_ERROR,
        correlation_ids: CorrelationIdRequirements {
            accepted_response: SESSION_ID_ONLY,
            lifecycle: SESSION_ID_ONLY,
        },
    },
    CommandSpec {
        command: ControlPlaneCommand::ResolveApproval,
        name: "resolve_approval",
        family: CommandFamily::InteractiveResolution,
        response_envelope: ResponseEnvelope::Ack,
        completion_mode: CompletionMode::CompletionBound,
        terminal_signals: DIRECT_RESULT_AND_INTERACTIVE_RESOLVED,
        correlation_ids: CorrelationIdRequirements {
            accepted_response: NO_CORRELATION_IDS,
            lifecycle: INTERACTIVE_REQUEST_ID_ONLY,
        },
    },
    CommandSpec {
        command: ControlPlaneCommand::ResolveQuestion,
        name: "resolve_question",
        family: CommandFamily::InteractiveResolution,
        response_envelope: ResponseEnvelope::Ack,
        completion_mode: CompletionMode::CompletionBound,
        terminal_signals: DIRECT_RESULT_AND_INTERACTIVE_RESOLVED,
        correlation_ids: CorrelationIdRequirements {
            accepted_response: NO_CORRELATION_IDS,
            lifecycle: INTERACTIVE_REQUEST_ID_ONLY,
        },
    },
    CommandSpec {
        command: ControlPlaneCommand::ResolvePlan,
        name: "resolve_plan",
        family: CommandFamily::InteractiveResolution,
        response_envelope: ResponseEnvelope::Ack,
        completion_mode: CompletionMode::CompletionBound,
        terminal_signals: DIRECT_RESULT_AND_INTERACTIVE_RESOLVED,
        correlation_ids: CorrelationIdRequirements {
            accepted_response: NO_CORRELATION_IDS,
            lifecycle: INTERACTIVE_REQUEST_ID_ONLY,
        },
    },
    CommandSpec {
        command: ControlPlaneCommand::SteerAgent,
        name: "steer_agent",
        family: CommandFamily::QueueDispatch,
        response_envelope: ResponseEnvelope::Ack,
        completion_mode: CompletionMode::AcceptedAndStreaming,
        terminal_signals: COMPLETE_OR_ERROR,
        correlation_ids: CorrelationIdRequirements {
            accepted_response: NO_CORRELATION_IDS,
            lifecycle: NO_CORRELATION_IDS,
        },
    },
    CommandSpec {
        command: ControlPlaneCommand::FollowUpAgent,
        name: "follow_up_agent",
        family: CommandFamily::QueueDispatch,
        response_envelope: ResponseEnvelope::Ack,
        completion_mode: CompletionMode::AcceptedAndStreaming,
        terminal_signals: COMPLETE_OR_ERROR,
        correlation_ids: CorrelationIdRequirements {
            accepted_response: NO_CORRELATION_IDS,
            lifecycle: NO_CORRELATION_IDS,
        },
    },
    CommandSpec {
        command: ControlPlaneCommand::PostCompleteAgent,
        name: "post_complete_agent",
        family: CommandFamily::QueueDispatch,
        response_envelope: ResponseEnvelope::Ack,
        completion_mode: CompletionMode::AcceptedAndStreaming,
        terminal_signals: COMPLETE_OR_ERROR,
        correlation_ids: CorrelationIdRequirements {
            accepted_response: NO_CORRELATION_IDS,
            lifecycle: NO_CORRELATION_IDS,
        },
    },
    CommandSpec {
        command: ControlPlaneCommand::ClearMessageQueue,
        name: "clear_message_queue",
        family: CommandFamily::QueueControl,
        response_envelope: ResponseEnvelope::Ack,
        completion_mode: CompletionMode::FireAndForget,
        terminal_signals: NO_TERMINAL_SIGNAL,
        correlation_ids: CorrelationIdRequirements {
            accepted_response: NO_CORRELATION_IDS,
            lifecycle: NO_CORRELATION_IDS,
        },
    },
    CommandSpec {
        command: ControlPlaneCommand::ListAgentTools,
        name: "list_agent_tools",
        family: CommandFamily::ToolIntrospection,
        response_envelope: ResponseEnvelope::ToolList,
        completion_mode: CompletionMode::CompletionBound,
        terminal_signals: DIRECT_RESULT_ONLY,
        correlation_ids: CorrelationIdRequirements {
            accepted_response: NO_CORRELATION_IDS,
            lifecycle: NO_CORRELATION_IDS,
        },
    },
];

pub fn command_spec(command: ControlPlaneCommand) -> &'static CommandSpec {
    CANONICAL_COMMAND_SPECS
        .iter()
        .find(|spec| spec.command == command)
        .expect("canonical command spec missing")
}

pub fn command_spec_by_name(name: &str) -> Option<&'static CommandSpec> {
    CANONICAL_COMMAND_SPECS
        .iter()
        .find(|spec| spec.name == name)
}

pub fn canonical_command_specs() -> &'static [CommandSpec] {
    CANONICAL_COMMAND_SPECS
}

pub fn queue_command_from_tier(tier: &MessageTier) -> ControlPlaneCommand {
    match tier {
        MessageTier::Steering => ControlPlaneCommand::SteerAgent,
        MessageTier::FollowUp => ControlPlaneCommand::FollowUpAgent,
        MessageTier::PostComplete { .. } => ControlPlaneCommand::PostCompleteAgent,
    }
}

pub fn queue_message_tier(
    command: ControlPlaneCommand,
    post_complete_group: Option<u32>,
) -> Option<MessageTier> {
    match command {
        ControlPlaneCommand::SteerAgent => Some(MessageTier::Steering),
        ControlPlaneCommand::FollowUpAgent => Some(MessageTier::FollowUp),
        ControlPlaneCommand::PostCompleteAgent => Some(MessageTier::PostComplete {
            group: post_complete_group.unwrap_or(1),
        }),
        _ => None,
    }
}

pub fn queue_command_label(command: ControlPlaneCommand) -> Option<&'static str> {
    match command {
        ControlPlaneCommand::SteerAgent => Some("steering"),
        ControlPlaneCommand::FollowUpAgent => Some("follow-up"),
        ControlPlaneCommand::PostCompleteAgent => Some("post-complete"),
        _ => None,
    }
}

pub fn queue_command_from_alias(alias: &str) -> Option<ControlPlaneCommand> {
    match alias {
        "steering" | "steer_agent" => Some(ControlPlaneCommand::SteerAgent),
        "follow-up" | "followup" | "follow_up" | "follow_up_agent" => {
            Some(ControlPlaneCommand::FollowUpAgent)
        }
        "post-complete" | "postcomplete" | "post_complete" | "post_complete_agent" => {
            Some(ControlPlaneCommand::PostCompleteAgent)
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use std::collections::HashSet;

    #[test]
    fn ws1_command_fixture_covers_expected_inventory() {
        let names: Vec<_> = canonical_command_specs()
            .iter()
            .map(|spec| spec.name)
            .collect();
        let expected: Vec<_> = ControlPlaneCommand::ALL
            .into_iter()
            .map(ControlPlaneCommand::as_str)
            .collect();

        assert_eq!(names, expected);
    }

    #[test]
    fn command_names_are_unique() {
        let names: HashSet<_> = canonical_command_specs()
            .iter()
            .map(|spec| spec.name)
            .collect();

        assert_eq!(names.len(), canonical_command_specs().len());
    }

    #[test]
    fn command_completion_modes_match_ws1_contract() {
        assert_eq!(
            command_spec(ControlPlaneCommand::SubmitGoal).completion_mode,
            CompletionMode::AcceptedAndStreaming
        );
        assert_eq!(
            command_spec(ControlPlaneCommand::RetryLastMessage).completion_mode,
            CompletionMode::AcceptedAndStreaming
        );
        assert_eq!(
            command_spec(ControlPlaneCommand::ResolvePlan).completion_mode,
            CompletionMode::CompletionBound
        );
        assert_eq!(
            command_spec(ControlPlaneCommand::ClearMessageQueue).completion_mode,
            CompletionMode::FireAndForget
        );
    }

    #[test]
    fn command_specs_match_ws1_contract_behavior_end_to_end() {
        let expected = [
            (
                ControlPlaneCommand::SubmitGoal,
                CommandFamily::GoalSubmission,
                ResponseEnvelope::AcceptedRunHandle,
                CompletionMode::AcceptedAndStreaming,
                COMPLETE_OR_ERROR,
                SESSION_ID_ONLY,
                SESSION_ID_ONLY,
            ),
            (
                ControlPlaneCommand::CancelAgent,
                CommandFamily::Cancellation,
                ResponseEnvelope::Ack,
                CompletionMode::FireAndForget,
                NO_TERMINAL_SIGNAL,
                NO_CORRELATION_IDS,
                NO_CORRELATION_IDS,
            ),
            (
                ControlPlaneCommand::RetryLastMessage,
                CommandFamily::RetryAndReplay,
                ResponseEnvelope::AcceptedRunHandle,
                CompletionMode::AcceptedAndStreaming,
                COMPLETE_OR_ERROR,
                SESSION_ID_ONLY,
                SESSION_ID_ONLY,
            ),
            (
                ControlPlaneCommand::EditAndResend,
                CommandFamily::RetryAndReplay,
                ResponseEnvelope::AcceptedRunHandle,
                CompletionMode::AcceptedAndStreaming,
                COMPLETE_OR_ERROR,
                SESSION_ID_ONLY,
                SESSION_ID_ONLY,
            ),
            (
                ControlPlaneCommand::RegenerateResponse,
                CommandFamily::RetryAndReplay,
                ResponseEnvelope::AcceptedRunHandle,
                CompletionMode::AcceptedAndStreaming,
                COMPLETE_OR_ERROR,
                SESSION_ID_ONLY,
                SESSION_ID_ONLY,
            ),
            (
                ControlPlaneCommand::ResolveApproval,
                CommandFamily::InteractiveResolution,
                ResponseEnvelope::Ack,
                CompletionMode::CompletionBound,
                DIRECT_RESULT_AND_INTERACTIVE_RESOLVED,
                NO_CORRELATION_IDS,
                INTERACTIVE_REQUEST_ID_ONLY,
            ),
            (
                ControlPlaneCommand::ResolveQuestion,
                CommandFamily::InteractiveResolution,
                ResponseEnvelope::Ack,
                CompletionMode::CompletionBound,
                DIRECT_RESULT_AND_INTERACTIVE_RESOLVED,
                NO_CORRELATION_IDS,
                INTERACTIVE_REQUEST_ID_ONLY,
            ),
            (
                ControlPlaneCommand::ResolvePlan,
                CommandFamily::InteractiveResolution,
                ResponseEnvelope::Ack,
                CompletionMode::CompletionBound,
                DIRECT_RESULT_AND_INTERACTIVE_RESOLVED,
                NO_CORRELATION_IDS,
                INTERACTIVE_REQUEST_ID_ONLY,
            ),
            (
                ControlPlaneCommand::SteerAgent,
                CommandFamily::QueueDispatch,
                ResponseEnvelope::Ack,
                CompletionMode::AcceptedAndStreaming,
                COMPLETE_OR_ERROR,
                NO_CORRELATION_IDS,
                NO_CORRELATION_IDS,
            ),
            (
                ControlPlaneCommand::FollowUpAgent,
                CommandFamily::QueueDispatch,
                ResponseEnvelope::Ack,
                CompletionMode::AcceptedAndStreaming,
                COMPLETE_OR_ERROR,
                NO_CORRELATION_IDS,
                NO_CORRELATION_IDS,
            ),
            (
                ControlPlaneCommand::PostCompleteAgent,
                CommandFamily::QueueDispatch,
                ResponseEnvelope::Ack,
                CompletionMode::AcceptedAndStreaming,
                COMPLETE_OR_ERROR,
                NO_CORRELATION_IDS,
                NO_CORRELATION_IDS,
            ),
            (
                ControlPlaneCommand::ClearMessageQueue,
                CommandFamily::QueueControl,
                ResponseEnvelope::Ack,
                CompletionMode::FireAndForget,
                NO_TERMINAL_SIGNAL,
                NO_CORRELATION_IDS,
                NO_CORRELATION_IDS,
            ),
            (
                ControlPlaneCommand::ListAgentTools,
                CommandFamily::ToolIntrospection,
                ResponseEnvelope::ToolList,
                CompletionMode::CompletionBound,
                DIRECT_RESULT_ONLY,
                NO_CORRELATION_IDS,
                NO_CORRELATION_IDS,
            ),
        ];

        for (command, family, envelope, completion, terminals, accepted_ids, lifecycle_ids) in
            expected
        {
            let spec = command_spec(command);
            assert_eq!(
                spec.name,
                command.as_str(),
                "name mismatch for {:?}",
                command
            );
            assert_eq!(spec.family, family, "family mismatch for {:?}", command);
            assert_eq!(
                spec.response_envelope, envelope,
                "envelope mismatch for {:?}",
                command
            );
            assert_eq!(
                spec.completion_mode, completion,
                "completion mismatch for {:?}",
                command
            );
            assert_eq!(
                spec.terminal_signals, terminals,
                "terminal signal mismatch for {:?}",
                command
            );
            assert_eq!(
                spec.correlation_ids.accepted_response, accepted_ids,
                "accepted correlation IDs mismatch for {:?}",
                command
            );
            assert_eq!(
                spec.correlation_ids.lifecycle, lifecycle_ids,
                "lifecycle correlation IDs mismatch for {:?}",
                command
            );
        }
    }

    #[test]
    fn command_fixture_serializes_correlation_requirements() {
        let fixture = serde_json::to_value(canonical_command_specs()).expect("serialize fixture");

        assert!(fixture.as_array().expect("array").iter().any(|entry| {
            entry["name"] == json!("resolve_plan")
                && entry["correlation_ids"]["accepted_response"] == json!([])
                && entry["correlation_ids"]["lifecycle"] == json!(["interactive_request_id"])
                && entry["completion_mode"] == json!("completion-bound")
                && entry["terminal_signals"] == json!(["direct_result", "interactive_resolved"])
        }));

        assert!(fixture.as_array().expect("array").iter().any(|entry| {
            entry["name"] == json!("submit_goal")
                && entry["correlation_ids"]["accepted_response"] == json!(["session_id"])
                && entry["terminal_signals"] == json!(["complete_event", "error_event"])
        }));
    }

    #[test]
    fn queue_commands_round_trip_through_message_tiers() {
        for (command, tier) in [
            (ControlPlaneCommand::SteerAgent, MessageTier::Steering),
            (ControlPlaneCommand::FollowUpAgent, MessageTier::FollowUp),
            (
                ControlPlaneCommand::PostCompleteAgent,
                MessageTier::PostComplete { group: 3 },
            ),
        ] {
            let rebuilt = queue_message_tier(
                command,
                match tier {
                    MessageTier::PostComplete { group } => Some(group),
                    _ => None,
                },
            )
            .expect("queue tier");

            assert_eq!(queue_command_from_tier(&tier), command);
            assert_eq!(rebuilt, tier);
        }
    }

    #[test]
    fn queue_aliases_follow_shared_command_map() {
        assert_eq!(
            queue_command_from_alias("steer_agent"),
            Some(ControlPlaneCommand::SteerAgent)
        );
        assert_eq!(
            queue_command_from_alias("follow_up"),
            Some(ControlPlaneCommand::FollowUpAgent)
        );
        assert_eq!(
            queue_command_from_alias("follow_up_agent"),
            Some(ControlPlaneCommand::FollowUpAgent)
        );
        assert_eq!(
            queue_command_from_alias("post_complete_agent"),
            Some(ControlPlaneCommand::PostCompleteAgent)
        );
        assert_eq!(
            queue_command_label(ControlPlaneCommand::PostCompleteAgent),
            Some("post-complete")
        );
    }
}
