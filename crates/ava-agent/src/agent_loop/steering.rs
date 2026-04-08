//! Mid-stream messaging: steering queue polling, steering message injection,
//! and forced summary generation.

use ava_types::{Message, Role, Session, TokenUsage};
use tokio::sync::mpsc;
use tracing::info;

use super::{AgentEvent, AgentLoop};

fn format_steering_message(combined: &str) -> String {
    format!(
        "[The user has interrupted with a new instruction. \
         Stop what you were doing and address this instead. \
         Do NOT retry any interrupted tools.]\n\n{combined}"
    )
}

impl AgentLoop {
    /// Inject a summary prompt and do one final LLM call so the agent can wrap up.
    pub(super) async fn force_summary(
        &mut self,
        session: &mut Session,
        prompt: &str,
        total_usage: &mut TokenUsage,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) {
        let summary_msg = Message::new(Role::User, prompt.to_string());
        self.context.add_message(summary_msg.clone());
        session.add_message(summary_msg);
        Self::emit(
            event_tx,
            AgentEvent::Progress(if prompt.contains("budget") {
                "budget limit reached — requesting summary".to_string()
            } else {
                "turn limit reached — requesting summary".to_string()
            }),
        );
        if let Ok((text, _, usage)) = self.generate_response_with_thinking().await {
            Self::merge_usage(total_usage, &usage);
            if !text.trim().is_empty() {
                Self::emit(event_tx, AgentEvent::Token(text.clone()));
                let msg = Message::new(Role::Assistant, text);
                self.context.add_message(msg.clone());
                session.add_message(msg);
            }
        }
    }

    /// Drain pending steering messages from the queue and inject them as a single
    /// user turn with a framing prefix instructing the LLM to address the new
    /// instruction.
    ///
    /// Returns `true` if steering messages were injected.
    pub(super) fn inject_steering_messages(
        &mut self,
        session: &mut Session,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> bool {
        let Some(ref mut queue) = self.message_queue else {
            return false;
        };
        let steering_msgs = queue.drain_steering();
        if steering_msgs.is_empty() {
            return false;
        }
        let combined = steering_msgs.join("\n");
        Self::emit(
            event_tx,
            AgentEvent::Progress(format!("steering: {combined}")),
        );
        let prefixed = format_steering_message(&combined);
        let msg = Message::new(Role::User, prefixed);
        self.context.add_message(msg.clone());
        session.add_message(msg);
        true
    }

    /// Check for steering messages that arrived during an LLM call (before natural
    /// completion). If found, drain them and inject as a user turn.
    ///
    /// Returns `true` if the agent should continue looping (steering was injected),
    /// `false` if natural completion should proceed.
    pub(super) fn check_steering_before_complete(
        &mut self,
        session: &mut Session,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> bool {
        let Some(ref mut queue) = self.message_queue else {
            return false;
        };

        queue.poll();
        let steering_msgs = queue.drain_steering();
        if steering_msgs.is_empty() {
            return false;
        }

        info!("Natural completion deferred — steering messages pending");
        let combined = steering_msgs.join("\n");
        Self::emit(
            event_tx,
            AgentEvent::Progress(format!("steering: {combined}")),
        );
        let msg = Message::new(Role::User, format_steering_message(&combined));
        self.context.add_message(msg.clone());
        session.add_message(msg);
        true
    }
}

#[cfg(test)]
mod tests {
    use super::format_steering_message;

    #[test]
    fn steering_message_includes_interrupt_prefix() {
        let formatted = format_steering_message("please switch tasks");
        assert!(formatted.contains("interrupted with a new instruction"));
        assert!(formatted.contains("Do NOT retry any interrupted tools"));
        assert!(formatted.ends_with("please switch tasks"));
    }
}
