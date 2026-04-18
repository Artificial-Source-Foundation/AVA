//! Shared queue target parsing and clear semantics.
//!
//! Ownership: canonical queue target aliases, clear behavior contract, and active
//! session ownership validation for deferred queue operations.

use serde::{Deserialize, Serialize};
use std::fmt;
use uuid::Uuid;

pub const UNSUPPORTED_QUEUE_CLEAR_ERROR: &str =
    "Clearing follow-up or post-complete queues is not supported yet.";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum ClearQueueTarget {
    All,
    Steering,
    FollowUp,
    PostComplete,
}

impl ClearQueueTarget {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::All => "all",
            Self::Steering => "steering",
            Self::FollowUp => "followUp",
            Self::PostComplete => "postComplete",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum QueueClearSemantics {
    CancelRunAndClearSteering,
    Unsupported,
}

pub const fn clear_queue_semantics(target: ClearQueueTarget) -> QueueClearSemantics {
    match target {
        ClearQueueTarget::All | ClearQueueTarget::Steering => {
            QueueClearSemantics::CancelRunAndClearSteering
        }
        ClearQueueTarget::FollowUp | ClearQueueTarget::PostComplete => {
            QueueClearSemantics::Unsupported
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DeferredQueueSessionError {
    MissingActiveSession,
    SessionMismatch {
        requested_session_id: Uuid,
        active_session_id: Uuid,
    },
}

impl fmt::Display for DeferredQueueSessionError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::MissingActiveSession => {
                write!(f, "Agent queue has no active session owner.")
            }
            Self::SessionMismatch {
                requested_session_id,
                active_session_id,
            } => write!(
                f,
                "Requested session {requested_session_id} does not match active queued-run session {active_session_id}."
            ),
        }
    }
}

impl std::error::Error for DeferredQueueSessionError {}

pub fn resolve_deferred_queue_session(
    requested_session_id: Option<Uuid>,
    active_session_id: Option<Uuid>,
) -> Result<Uuid, DeferredQueueSessionError> {
    let active_session_id =
        active_session_id.ok_or(DeferredQueueSessionError::MissingActiveSession)?;

    match requested_session_id {
        Some(requested_session_id) if requested_session_id != active_session_id => {
            Err(DeferredQueueSessionError::SessionMismatch {
                requested_session_id,
                active_session_id,
            })
        }
        _ => Ok(active_session_id),
    }
}

pub fn parse_clear_queue_target(target: &str) -> Option<ClearQueueTarget> {
    match target {
        "all" | "All" => Some(ClearQueueTarget::All),
        "steering" | "Steering" => Some(ClearQueueTarget::Steering),
        "followUp" | "follow_up" | "follow-up" | "FollowUp" => Some(ClearQueueTarget::FollowUp),
        "postComplete" | "post_complete" | "post-complete" | "PostComplete" => {
            Some(ClearQueueTarget::PostComplete)
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use uuid::Uuid;

    #[test]
    fn clear_queue_targets_accept_shared_aliases() {
        assert_eq!(parse_clear_queue_target("all"), Some(ClearQueueTarget::All));
        assert_eq!(
            parse_clear_queue_target("follow-up"),
            Some(ClearQueueTarget::FollowUp)
        );
        assert_eq!(
            parse_clear_queue_target("post_complete"),
            Some(ClearQueueTarget::PostComplete)
        );
        assert_eq!(parse_clear_queue_target("unknown"), None);
    }

    #[test]
    fn clear_queue_semantics_follow_contract() {
        assert_eq!(
            clear_queue_semantics(ClearQueueTarget::All),
            QueueClearSemantics::CancelRunAndClearSteering
        );
        assert_eq!(
            clear_queue_semantics(ClearQueueTarget::Steering),
            QueueClearSemantics::CancelRunAndClearSteering
        );
        assert_eq!(
            clear_queue_semantics(ClearQueueTarget::FollowUp),
            QueueClearSemantics::Unsupported
        );
        assert_eq!(
            clear_queue_semantics(ClearQueueTarget::PostComplete),
            QueueClearSemantics::Unsupported
        );
        assert!(UNSUPPORTED_QUEUE_CLEAR_ERROR.contains("not supported yet"));
    }

    #[test]
    fn deferred_queue_session_resolution_uses_active_owner_when_requested_matches() {
        let session_id = Uuid::new_v4();

        assert_eq!(
            resolve_deferred_queue_session(Some(session_id), Some(session_id)),
            Ok(session_id)
        );
        assert_eq!(
            resolve_deferred_queue_session(None, Some(session_id)),
            Ok(session_id)
        );
    }

    #[test]
    fn deferred_queue_session_resolution_rejects_cross_session_append() {
        let requested_session_id = Uuid::new_v4();
        let active_session_id = Uuid::new_v4();

        assert_eq!(
            resolve_deferred_queue_session(Some(requested_session_id), Some(active_session_id)),
            Err(DeferredQueueSessionError::SessionMismatch {
                requested_session_id,
                active_session_id,
            })
        );
    }

    #[test]
    fn deferred_queue_session_resolution_requires_active_owner() {
        assert_eq!(
            resolve_deferred_queue_session(Some(Uuid::new_v4()), None),
            Err(DeferredQueueSessionError::MissingActiveSession)
        );
    }
}
