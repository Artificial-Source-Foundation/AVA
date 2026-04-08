//! Shared helper functions for command handlers.

/// Collect conversation history up to (but not including) the last user message.
pub fn collect_history_before_last_user(
    messages: &[ava_types::Message],
) -> Vec<ava_types::Message> {
    let last_user_pos = messages
        .iter()
        .rposition(|m| m.role == ava_types::Role::User);
    match last_user_pos {
        Some(pos) => messages[..pos].to_vec(),
        None => vec![],
    }
}
