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

#[cfg(test)]
mod tests {
    use super::*;
    use crate::agent_loop::AgentEvent;

    fn token_usage_event(cost_usd: f64) -> AgentEvent {
        AgentEvent::TokenUsage {
            input_tokens: 100,
            output_tokens: 50,
            cost_usd,
        }
    }

    #[test]
    fn observe_accumulates_cost() {
        let mut budget = BudgetTelemetry::new(5.0);
        budget.observe(&token_usage_event(1.0));
        budget.observe(&token_usage_event(0.5));
        assert!((budget.total_cost_usd - 1.5).abs() < 1e-9);
        assert_eq!(budget.input_tokens, 200);
        assert_eq!(budget.output_tokens, 100);
    }

    #[test]
    fn observe_emits_threshold_warnings() {
        let mut budget = BudgetTelemetry::new(1.0);
        // No budget warning below 50%
        let warnings = budget.observe(&token_usage_event(0.4));
        assert!(warnings.is_empty(), "below 50% should produce no warnings");

        // Cross the 50% threshold
        let warnings = budget.observe(&token_usage_event(0.15));
        assert_eq!(warnings.len(), 1);
        match &warnings[0] {
            AgentEvent::BudgetWarning {
                threshold_percent, ..
            } => assert_eq!(*threshold_percent, 50),
            _ => panic!("expected BudgetWarning"),
        }
    }

    #[test]
    fn threshold_warnings_are_emitted_only_once() {
        let mut budget = BudgetTelemetry::new(1.0);
        // Push past 50% in a single step
        budget.observe(&token_usage_event(0.55));
        // A second event at the same level must not re-emit the 50% warning
        let warnings = budget.observe(&token_usage_event(0.01));
        assert!(
            warnings.is_empty(),
            "threshold at 50% should not fire twice"
        );
    }

    #[test]
    fn no_warnings_when_no_budget_set() {
        let mut budget = BudgetTelemetry::new(0.0);
        let warnings = budget.observe(&token_usage_event(100.0));
        assert!(
            warnings.is_empty(),
            "no budget limit → no warnings regardless of cost"
        );
    }

    #[test]
    fn budget_exhausted_when_over_limit() {
        let mut budget = BudgetTelemetry::new(1.0);
        budget.observe(&token_usage_event(1.5));
        assert!(budget.budget_exhausted());
        assert_eq!(budget.remaining_budget_usd(), Some(0.0));
    }

    #[test]
    fn remaining_budget_none_when_no_limit() {
        let budget = BudgetTelemetry::new(0.0);
        assert_eq!(budget.remaining_budget_usd(), None);
    }

    #[test]
    fn budget_status_label_with_limit() {
        let mut budget = BudgetTelemetry::new(5.0);
        budget.observe(&token_usage_event(1.25));
        let label = budget.budget_status_label();
        assert!(label.contains("$1.25"), "label: {label}");
        assert!(label.contains("$5.00"), "label: {label}");
    }

    #[test]
    fn budget_status_label_without_limit() {
        let mut budget = BudgetTelemetry::new(0.0);
        budget.observe(&token_usage_event(0.5));
        let label = budget.budget_status_label();
        assert!(label.contains("$0.50"), "label: {label}");
        assert!(!label.contains('/'), "no slash when no budget set: {label}");
    }

    #[test]
    fn non_usage_events_do_not_accumulate_cost() {
        let mut budget = BudgetTelemetry::new(1.0);
        let warnings = budget.observe(&AgentEvent::Token("hello".to_string()));
        assert!(warnings.is_empty());
        assert_eq!(budget.total_cost_usd, 0.0);
    }
}
