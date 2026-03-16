//! Budget telemetry for tracking agent run costs against a spending limit.
//!
//! Extracted from `stack.rs` to isolate cost tracking, threshold alerts,
//! and session metadata attachment from the main agent orchestration.

use ava_types::Session;

use crate::agent_loop::AgentEvent;

/// Tracks cumulative token usage and cost during an agent run,
/// emitting warnings when configurable spend thresholds are crossed.
#[derive(Debug, Default)]
pub(crate) struct BudgetTelemetry {
    pub input_tokens: usize,
    pub output_tokens: usize,
    pub total_cost_usd: f64,
    pub max_budget_usd: f64,
    last_alert_threshold_percent: Option<u8>,
    emitted_thresholds: Vec<u8>,
    skipped_follow_up_messages: usize,
    skipped_post_complete_groups: usize,
    skipped_post_complete_messages: usize,
}

impl BudgetTelemetry {
    const ALERT_THRESHOLDS: [u8; 3] = [50, 75, 90];

    pub fn new(max_budget_usd: f64) -> Self {
        Self {
            max_budget_usd,
            ..Self::default()
        }
    }

    /// Observe an agent event and update internal counters.
    /// Returns any budget warning events that should be forwarded.
    pub fn observe(&mut self, event: &AgentEvent) -> Vec<AgentEvent> {
        match event {
            AgentEvent::TokenUsage {
                input_tokens,
                output_tokens,
                cost_usd,
            } => {
                self.input_tokens += input_tokens;
                self.output_tokens += output_tokens;
                self.total_cost_usd += cost_usd;
            }
            AgentEvent::SubAgentComplete {
                input_tokens,
                output_tokens,
                cost_usd,
                ..
            } => {
                self.input_tokens += input_tokens;
                self.output_tokens += output_tokens;
                self.total_cost_usd += cost_usd;
            }
            _ => return Vec::new(),
        }

        if self.max_budget_usd <= 0.0 {
            return Vec::new();
        }

        let percent_used = self.total_cost_usd / self.max_budget_usd * 100.0;
        let mut warnings = Vec::new();
        for threshold in Self::ALERT_THRESHOLDS {
            if percent_used >= f64::from(threshold) && !self.emitted_thresholds.contains(&threshold)
            {
                self.emitted_thresholds.push(threshold);
                self.last_alert_threshold_percent = Some(threshold);
                warnings.push(AgentEvent::BudgetWarning {
                    threshold_percent: threshold,
                    current_cost_usd: self.total_cost_usd,
                    max_budget_usd: self.max_budget_usd,
                });
            }
        }
        warnings
    }

    /// Attach cost summary metadata to a session.
    pub fn attach_to_session(&self, session: &mut Session) {
        session.metadata["costSummary"] = serde_json::json!({
            "totalUsd": self.total_cost_usd,
            "budgetUsd": (self.max_budget_usd > 0.0).then_some(self.max_budget_usd),
            "inputTokens": self.input_tokens,
            "outputTokens": self.output_tokens,
            "lastAlertThresholdPercent": self.last_alert_threshold_percent,
            "skippedQueuedFollowUps": self.skipped_follow_up_messages,
            "skippedQueuedPostCompleteGroups": self.skipped_post_complete_groups,
            "skippedQueuedPostCompleteMessages": self.skipped_post_complete_messages,
        });
    }

    /// Returns remaining budget in USD, or `None` if no budget is set.
    pub fn remaining_budget_usd(&self) -> Option<f64> {
        if self.max_budget_usd <= 0.0 {
            None
        } else {
            Some((self.max_budget_usd - self.total_cost_usd).max(0.0))
        }
    }

    /// Returns `true` if a budget was set and has been fully consumed.
    pub fn budget_exhausted(&self) -> bool {
        self.remaining_budget_usd()
            .is_some_and(|remaining| remaining <= 0.0)
    }

    /// Human-readable budget status like "$1.23/$5.00" or "$1.23".
    pub fn budget_status_label(&self) -> String {
        if self.max_budget_usd > 0.0 {
            format!("${:.2}/${:.2}", self.total_cost_usd, self.max_budget_usd)
        } else {
            format!("${:.2}", self.total_cost_usd)
        }
    }

    pub fn record_skipped_follow_up_messages(&mut self, count: usize) {
        self.skipped_follow_up_messages += count;
    }

    pub fn record_skipped_post_complete_group(&mut self, message_count: usize) {
        self.skipped_post_complete_groups += 1;
        self.skipped_post_complete_messages += message_count;
    }
}
