mod agent_events;
mod goal;
mod hooks;
mod interactive;
mod messaging;
mod voice;

use super::*;

impl App {
    /// Finalize the assistant stream after the agent loop completes or errors.
    ///
    /// This flushes any buffered tokens, marks the last assistant message as done
    /// streaming, attaches the model name, and records the total loop duration.
    /// Call this from Complete, Error, and any other terminal agent event path.
    fn finalize_assistant_stream(&mut self) {
        // Force flush any remaining buffered tokens
        self.force_flush_token_buffer();

        // Mark last assistant message as done streaming and attach model info.
        // Use loop_started_at for the TOTAL elapsed time on the final message.
        if let Some(last) = self.state.messages.messages.last_mut() {
            last.is_streaming = false;
            if matches!(last.kind, MessageKind::Assistant) {
                if last.model_name.is_none() {
                    last.model_name = Some(self.state.agent.model_name.clone());
                }
                // Set total loop duration from loop_started_at
                if let Some(started) = self.state.agent.loop_started_at {
                    last.response_time = Some(started.elapsed().as_secs_f64());
                }
            }
        }
        self.state.agent.loop_started_at = None;
    }
}
