//! Shared session selection and replay payload contract helpers.
//!
//! Ownership: requested/last/new session precedence and retry/edit/regenerate prompt
//! context derivation rules reused by multiple runtime adapters.

use std::fmt;

use ava_types::{ImageContent, Message, Role, Session};
use serde::Serialize;
use uuid::Uuid;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum SessionSelectionSource {
    Requested,
    LastActive,
    New,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionSelection {
    pub session_id: Uuid,
    pub source: SessionSelectionSource,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct SessionPromptContext {
    pub goal: String,
    pub history: Vec<Message>,
    pub images: Vec<ImageContent>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SessionReplayAction {
    Retry,
    Regenerate,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SessionReplayPayloadError {
    MissingUserMessage { action: SessionReplayAction },
    InvalidEditTarget,
    MessageNotFound { message_id: Uuid },
    NonUserEditTarget { message_id: Uuid },
}

impl fmt::Display for SessionReplayPayloadError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::MissingUserMessage { action } => match action {
                SessionReplayAction::Retry => {
                    write!(f, "No user message found in session to retry")
                }
                SessionReplayAction::Regenerate => {
                    write!(f, "No user message found in session to regenerate from")
                }
            },
            Self::InvalidEditTarget => write!(f, "Invalid message ID for edit-resend"),
            Self::MessageNotFound { message_id } => {
                write!(f, "Message {message_id} not found in session")
            }
            Self::NonUserEditTarget { .. } => write!(f, "Only user messages can be edited"),
        }
    }
}

impl std::error::Error for SessionReplayPayloadError {}

pub fn collect_history_before_last_user(messages: &[Message]) -> Vec<Message> {
    messages
        .iter()
        .rposition(|message| message.role == Role::User)
        .map(|pos| messages[..pos].to_vec())
        .unwrap_or_default()
}

pub fn load_prompt_context(session: &Session) -> SessionPromptContext {
    last_user_message(session)
        .map(|message| SessionPromptContext {
            goal: message.content.clone(),
            history: collect_history_before_last_user(&session.messages),
            images: message.images.clone(),
        })
        .unwrap_or_default()
}

pub fn build_retry_replay_payload(
    session: &Session,
) -> Result<SessionPromptContext, SessionReplayPayloadError> {
    build_last_user_replay_payload(session, SessionReplayAction::Retry)
}

pub fn build_edit_replay_payload(
    session: &Session,
    message_id: Option<Uuid>,
    new_content: String,
) -> Result<SessionPromptContext, SessionReplayPayloadError> {
    let target_id = message_id.ok_or(SessionReplayPayloadError::InvalidEditTarget)?;
    let pos = session
        .messages
        .iter()
        .position(|message| message.id == target_id)
        .ok_or(SessionReplayPayloadError::MessageNotFound {
            message_id: target_id,
        })?;
    let target = &session.messages[pos];

    if target.role != Role::User {
        return Err(SessionReplayPayloadError::NonUserEditTarget {
            message_id: target_id,
        });
    }

    Ok(SessionPromptContext {
        goal: new_content,
        history: session.messages[..pos].to_vec(),
        images: target.images.clone(),
    })
}

pub fn build_regenerate_replay_payload(
    session: &Session,
) -> Result<SessionPromptContext, SessionReplayPayloadError> {
    build_last_user_replay_payload(session, SessionReplayAction::Regenerate)
}

fn build_last_user_replay_payload(
    session: &Session,
    action: SessionReplayAction,
) -> Result<SessionPromptContext, SessionReplayPayloadError> {
    last_user_message(session)
        .map(|_| load_prompt_context(session))
        .ok_or(SessionReplayPayloadError::MissingUserMessage { action })
}

fn last_user_message(session: &Session) -> Option<&Message> {
    session
        .messages
        .iter()
        .rev()
        .find(|message| message.role == Role::User)
}

pub fn resolve_existing_session(
    requested_session_id: Option<Uuid>,
    last_active_session_id: Option<Uuid>,
) -> Option<SessionSelection> {
    requested_session_id
        .map(|session_id| SessionSelection {
            session_id,
            source: SessionSelectionSource::Requested,
        })
        .or_else(|| {
            last_active_session_id.map(|session_id| SessionSelection {
                session_id,
                source: SessionSelectionSource::LastActive,
            })
        })
}

pub fn resolve_session_precedence<F>(
    requested_session_id: Option<Uuid>,
    last_active_session_id: Option<Uuid>,
    new_session_id: F,
) -> SessionSelection
where
    F: FnOnce() -> Uuid,
{
    resolve_existing_session(requested_session_id, last_active_session_id).unwrap_or_else(|| {
        SessionSelection {
            session_id: new_session_id(),
            source: SessionSelectionSource::New,
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_image(label: &str) -> ImageContent {
        ImageContent::new(label, ava_types::ImageMediaType::Png)
    }

    fn sample_user_message(content: &str, images: Vec<ImageContent>) -> Message {
        let mut message = Message::new(Role::User, content);
        message.images = images;
        message
    }

    #[test]
    fn existing_session_precedence_prefers_requested_over_last_active() {
        let requested = Uuid::new_v4();
        let last_active = Uuid::new_v4();

        assert_eq!(
            resolve_existing_session(Some(requested), Some(last_active)),
            Some(SessionSelection {
                session_id: requested,
                source: SessionSelectionSource::Requested,
            })
        );
    }

    #[test]
    fn existing_session_precedence_falls_back_to_last_active() {
        let last_active = Uuid::new_v4();

        assert_eq!(
            resolve_existing_session(None, Some(last_active)),
            Some(SessionSelection {
                session_id: last_active,
                source: SessionSelectionSource::LastActive,
            })
        );
        assert_eq!(resolve_existing_session(None, None), None);
    }

    #[test]
    fn session_precedence_generates_new_when_needed() {
        let requested = Uuid::new_v4();
        let last_active = Uuid::new_v4();
        let generated = Uuid::new_v4();

        assert_eq!(
            resolve_session_precedence(Some(requested), Some(last_active), Uuid::new_v4),
            SessionSelection {
                session_id: requested,
                source: SessionSelectionSource::Requested,
            }
        );
        assert_eq!(
            resolve_session_precedence(None, Some(last_active), Uuid::new_v4),
            SessionSelection {
                session_id: last_active,
                source: SessionSelectionSource::LastActive,
            }
        );
        assert_eq!(
            resolve_session_precedence(None, None, || generated),
            SessionSelection {
                session_id: generated,
                source: SessionSelectionSource::New,
            }
        );
    }

    #[test]
    fn load_prompt_context_uses_latest_user_turn() {
        let mut session = Session::new();
        session.add_message(Message::new(Role::System, "system"));
        session.add_message(sample_user_message("first", vec![]));
        session.add_message(Message::new(Role::Assistant, "reply"));
        session.add_message(sample_user_message("second", vec![sample_image("latest")]));

        assert_eq!(
            load_prompt_context(&session),
            SessionPromptContext {
                goal: "second".to_string(),
                history: session.messages[..3].to_vec(),
                images: vec![sample_image("latest")],
            }
        );
    }

    #[test]
    fn load_prompt_context_defaults_when_session_has_no_user_messages() {
        let mut session = Session::new();
        session.add_message(Message::new(Role::Assistant, "reply"));

        assert_eq!(
            load_prompt_context(&session),
            SessionPromptContext::default()
        );
        assert!(collect_history_before_last_user(&session.messages).is_empty());
    }

    #[test]
    fn retry_and_regenerate_payloads_share_latest_user_context() {
        let mut session = Session::new();
        session.add_message(Message::new(Role::System, "system"));
        session.add_message(sample_user_message("describe", vec![sample_image("retry")]));
        session.add_message(Message::new(Role::Assistant, "done"));

        let expected = SessionPromptContext {
            goal: "describe".to_string(),
            history: vec![session.messages[0].clone()],
            images: vec![sample_image("retry")],
        };

        assert_eq!(
            build_retry_replay_payload(&session).expect("retry payload"),
            expected
        );
        assert_eq!(
            build_regenerate_replay_payload(&session).expect("regenerate payload"),
            expected
        );
    }

    #[test]
    fn edit_replay_payload_rejects_missing_or_non_user_targets() {
        let mut session = Session::new();
        let assistant = Message::new(Role::Assistant, "done");
        let assistant_id = assistant.id;
        session.add_message(assistant);

        assert_eq!(
            build_edit_replay_payload(&session, None, "after".to_string())
                .expect_err("missing target should fail")
                .to_string(),
            "Invalid message ID for edit-resend"
        );
        assert_eq!(
            build_edit_replay_payload(&session, Some(assistant_id), "after".to_string())
                .expect_err("assistant target should fail")
                .to_string(),
            "Only user messages can be edited"
        );
    }
}
