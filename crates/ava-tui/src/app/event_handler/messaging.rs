use super::*;
use ava_control_plane::commands::{queue_command_from_tier, queue_command_label};
use ava_types::{MessageTier, QueuedMessage};
use tracing::info;

impl App {
    /// Send a mid-stream message to the running agent via the message queue.
    pub(crate) fn send_queued_message(&mut self, text: String, tier: MessageTier) {
        let label = queue_command_label(queue_command_from_tier(&tier))
            .expect("queue display label should exist for queue tier");

        if let Some(ref tx) = self.state.agent.message_tx {
            let msg = QueuedMessage {
                text: text.clone(),
                tier: tier.clone(),
            };
            if tx.send(msg).is_ok() {
                // Add to UI queue display
                self.state
                    .input
                    .queue_display
                    .push(text.clone(), tier.clone());
                // Show user message in chat with a tier badge
                let badge = match &tier {
                    MessageTier::Steering => "[S]".to_string(),
                    MessageTier::FollowUp => "[F]".to_string(),
                    MessageTier::PostComplete { group } => format!("[G{group}]"),
                };
                self.state
                    .messages
                    .push(UiMessage::new(MessageKind::User, format!("{badge} {text}")));
                self.set_status(format!("Queued {label} message"), StatusLevel::Info);
            } else {
                self.set_status(
                    format!("Failed to queue {label} message — agent may have finished"),
                    StatusLevel::Error,
                );
            }
        } else {
            self.set_status("No running agent to send messages to", StatusLevel::Warn);
        }
    }

    /// Mark in-progress messages as cancelled (UX-33).
    /// Any ToolCall without a subsequent ToolResult, and any streaming assistant/thinking
    /// message, gets marked as interrupted.
    pub(crate) fn mark_interrupted_messages(&mut self) {
        // Force flush any remaining buffered tokens so the last assistant message is complete
        self.force_flush_token_buffer();

        // BUG-41: Only walk messages from the CURRENT turn (turn_start_index) to avoid
        // marking tool groups from previous turns as interrupted.
        let start = self.state.turn_start_index;
        let msgs = &mut self.state.messages.messages;
        let mut seen_result = false;
        for msg in msgs[start..].iter_mut().rev() {
            match msg.kind {
                MessageKind::ToolResult => {
                    seen_result = true;
                }
                MessageKind::ToolCall => {
                    if !seen_result {
                        msg.cancelled = true;
                    }
                    // After encountering a ToolCall, reset: the next ToolCall
                    // going backwards needs its own ToolResult.
                    seen_result = false;
                }
                MessageKind::SubAgent => {
                    if let Some(ref mut data) = msg.sub_agent {
                        if data.is_running {
                            msg.cancelled = true;
                            msg.is_streaming = false;
                            data.is_running = false;
                            data.failed = true;
                        }
                    }
                }
                MessageKind::Assistant | MessageKind::Thinking => {
                    if msg.is_streaming {
                        msg.is_streaming = false;
                    }
                }
                MessageKind::User => break, // Stop at the last user message
                _ => {}
            }
        }
    }

    /// Cancel ALL running agents: background tasks and sub-agents (UX-34).
    pub(crate) fn cancel_all_agents(&mut self) {
        // Cancel background tasks
        let bg_cancelled = {
            let mut bg = self
                .state
                .background
                .lock()
                .unwrap_or_else(|e| e.into_inner());
            bg.cancel_all_running()
        };
        if bg_cancelled > 0 {
            info!(
                count = bg_cancelled,
                "Cancelled background tasks on interrupt"
            );
        }

        // Cancel running sub-agents
        for sa in &mut self.state.agent.sub_agents {
            if sa.is_running {
                sa.is_running = false;
                sa.elapsed = Some(sa.started_at.elapsed());
                sa.current_tool = None;
            }
        }
    }
}
