use crate::state::messages::{MessageKind, UiMessage};

pub(super) fn initial_subagent_session_messages(
    description: &str,
    background: bool,
) -> Vec<UiMessage> {
    let transcript_hint = if background {
        "Sub-agent is running in the background. Live transcript updates will appear here when they arrive."
    } else {
        "Sub-agent is starting. Live transcript updates will appear here as they stream."
    };
    vec![
        UiMessage::new(
            MessageKind::System,
            format!("Delegated task: {description}"),
        ),
        UiMessage::new(MessageKind::Thinking, transcript_hint),
    ]
}

pub(super) fn normalize_subagent_description(value: &str) -> &str {
    let trimmed = value.trim();
    if let Some(rest) = trimmed
        .strip_prefix('[')
        .and_then(|rest| rest.split_once(']').map(|(_, tail)| tail.trim()))
    {
        if !rest.is_empty() {
            return rest;
        }
    }
    trimmed
}

pub(super) fn subagent_descriptions_match(a: &str, b: &str) -> bool {
    normalize_subagent_description(a) == normalize_subagent_description(b)
}

pub(super) fn subagent_matches_completion(
    subagent_call_id: &str,
    subagent_description: &str,
    event_call_id: &str,
    event_description: &str,
) -> bool {
    if !event_call_id.is_empty() {
        subagent_call_id == event_call_id
    } else {
        subagent_descriptions_match(subagent_description, event_description)
    }
}
