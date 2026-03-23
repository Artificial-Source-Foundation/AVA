//! Completion detection: natural completion checks, budget/turn limit checks,
//! and final session emission.
#![allow(dead_code)] // Methods extracted for future use by refactored run_unified

use ava_types::{Session, TokenUsage, ToolCall};
use tokio::sync::mpsc;
use tracing::info;

use super::{AgentEvent, AgentLoop};
use crate::stuck::StuckDetector;

impl AgentLoop {
    /// Check if the turn limit has been reached. If so, force a summary and return `true`.
    pub(super) async fn check_turn_limit(
        &mut self,
        turn: usize,
        session: &mut Session,
        total_usage: &mut TokenUsage,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> bool {
        if self.config.max_turns > 0 && turn >= self.effective_max_turns() {
            self.force_summary(
                session,
                &format!(
                    "You have reached the maximum number of turns ({}). Please summarize what you've accomplished and list any remaining work.",
                    self.config.max_turns,
                ),
                total_usage,
                event_tx,
            ).await;
            true
        } else {
            false
        }
    }

    /// Check if the budget limit has been reached. If so, force a summary and return `true`.
    pub(super) async fn check_budget_limit(
        &mut self,
        total_cost_usd: f64,
        session: &mut Session,
        total_usage: &mut TokenUsage,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> bool {
        let max_budget = self.config.max_budget_usd;
        if max_budget > 0.0 && total_cost_usd >= max_budget {
            self.force_summary(
                session,
                &format!(
                    "You have reached the budget limit (${:.2}). Please summarize what you've accomplished and list any remaining work.",
                    max_budget,
                ),
                total_usage,
                event_tx,
            ).await;
            true
        } else {
            false
        }
    }

    /// Effective max turns (0 means unlimited, represented as `usize::MAX`).
    pub(super) fn effective_max_turns(&self) -> usize {
        if self.config.max_turns == 0 {
            usize::MAX
        } else {
            self.config.max_turns
        }
    }

    /// Handle natural completion (non-empty text, no tool calls).
    ///
    /// Checks for pending steering messages first. If steering is pending,
    /// returns `false` (do not complete, continue looping). Otherwise, emits
    /// completion events and returns `true` (complete).
    pub(super) async fn handle_natural_completion(
        &mut self,
        response_text: &str,
        session: &mut Session,
        total_usage: TokenUsage,
        detector: &StuckDetector,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> bool {
        // Check for steering messages that arrived during LLM call
        if self.check_steering_before_complete(session, event_tx) {
            return false; // Continue looping
        }

        info!(
            text_len = response_text.len(),
            "natural completion — no tool calls"
        );
        session.token_usage = total_usage;
        Self::emit(
            event_tx,
            AgentEvent::ToolStats(detector.tool_monitor().stats()),
        );
        let complete_event = AgentEvent::Complete(session.clone());
        Self::emit(event_tx, complete_event.clone());
        self.broadcast_event_to_plugins(&complete_event).await;
        true // Complete
    }

    /// Emit final completion events when the loop ends (turn/budget limit, stuck, etc.).
    pub(super) async fn emit_final_completion(
        &self,
        session: &mut Session,
        total_usage: TokenUsage,
        detector: &StuckDetector,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) {
        info!("agent loop ended");
        session.token_usage = total_usage;
        Self::emit(
            event_tx,
            AgentEvent::ToolStats(detector.tool_monitor().stats()),
        );
        let complete_event = AgentEvent::Complete(session.clone());
        Self::emit(event_tx, complete_event.clone());
        self.broadcast_event_to_plugins(&complete_event).await;
    }

    /// Emit completion events for attempt_completion tool call.
    pub(super) async fn handle_attempt_completion(
        &self,
        tool_calls: &[ToolCall],
        session: &mut Session,
        total_usage: TokenUsage,
        detector: &StuckDetector,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> bool {
        let completion_requested = tool_calls
            .iter()
            .any(|call| call.name == "attempt_completion");
        if !completion_requested {
            return false;
        }
        session.token_usage = total_usage;
        Self::emit(
            event_tx,
            AgentEvent::ToolStats(detector.tool_monitor().stats()),
        );
        let complete_event = AgentEvent::Complete(session.clone());
        Self::emit(event_tx, complete_event.clone());
        self.broadcast_event_to_plugins(&complete_event).await;
        true
    }
}
