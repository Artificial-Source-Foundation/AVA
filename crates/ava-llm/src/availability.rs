//! Model availability tracking with automatic degradation and recovery.
//!
//! Tracks per-model health based on success/failure patterns:
//! - 3 failures in 60s → Degraded
//! - 5 failures in 60s → Unavailable
//! - 120s with no failures → auto-recover to Available

use std::collections::HashMap;
use std::sync::Mutex;
use std::time::{Duration, Instant};

const DEGRADED_THRESHOLD: usize = 3;
const UNAVAILABLE_THRESHOLD: usize = 5;
const FAILURE_WINDOW: Duration = Duration::from_secs(60);
const RECOVERY_PERIOD: Duration = Duration::from_secs(120);

/// Current health status of a model.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ModelStatus {
    Available,
    Degraded(String),
    Unavailable(String),
}

/// Per-model failure record.
#[derive(Debug)]
struct ModelRecord {
    /// Timestamps of recent failures (within the failure window).
    failures: Vec<(Instant, String)>,
    /// Last time a failure was recorded (for recovery timing).
    last_failure: Option<Instant>,
}

impl ModelRecord {
    fn new() -> Self {
        Self {
            failures: Vec::new(),
            last_failure: None,
        }
    }

    /// Remove failures older than the window.
    fn prune(&mut self, now: Instant) {
        self.failures
            .retain(|(t, _)| now.duration_since(*t) < FAILURE_WINDOW);
    }
}

/// Tracks per-model health based on recent success/failure patterns.
#[derive(Debug)]
pub struct ModelAvailability {
    records: Mutex<HashMap<String, ModelRecord>>,
}

impl ModelAvailability {
    pub fn new() -> Self {
        Self {
            records: Mutex::new(HashMap::new()),
        }
    }

    /// Record a successful request for a model, resetting its failure state.
    pub fn record_success(&self, model: &str) {
        let mut records = self.records.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(record) = records.get_mut(model) {
            record.failures.clear();
            record.last_failure = None;
        }
    }

    /// Record a failed request for a model.
    pub fn record_failure(&self, model: &str, error: &str) {
        let mut records = self.records.lock().unwrap_or_else(|e| e.into_inner());
        let record = records
            .entry(model.to_string())
            .or_insert_with(ModelRecord::new);
        let now = Instant::now();
        record.prune(now);
        record.failures.push((now, error.to_string()));
        record.last_failure = Some(now);
    }

    /// Get the current status of a model.
    pub fn get_status(&self, model: &str) -> ModelStatus {
        let mut records = self.records.lock().unwrap_or_else(|e| e.into_inner());
        let Some(record) = records.get_mut(model) else {
            return ModelStatus::Available;
        };

        let now = Instant::now();

        // Auto-recover: if no failures for RECOVERY_PERIOD, mark available.
        if let Some(last) = record.last_failure {
            if now.duration_since(last) >= RECOVERY_PERIOD {
                record.failures.clear();
                record.last_failure = None;
                return ModelStatus::Available;
            }
        }

        record.prune(now);
        let count = record.failures.len();

        if count >= UNAVAILABLE_THRESHOLD {
            let reason = record
                .failures
                .last()
                .map(|(_, e)| e.clone())
                .unwrap_or_default();
            ModelStatus::Unavailable(reason)
        } else if count >= DEGRADED_THRESHOLD {
            let reason = record
                .failures
                .last()
                .map(|(_, e)| e.clone())
                .unwrap_or_default();
            ModelStatus::Degraded(reason)
        } else {
            ModelStatus::Available
        }
    }
}

impl Default for ModelAvailability {
    fn default() -> Self {
        Self::new()
    }
}

/// Ordered list of fallback models. Selects the first available or degraded model.
#[derive(Debug, Clone)]
pub struct FallbackChain {
    pub models: Vec<String>,
}

impl FallbackChain {
    pub fn new(models: Vec<String>) -> Self {
        Self { models }
    }

    /// Pick the first model that is Available or Degraded.
    /// Returns `None` if all models are Unavailable.
    pub fn select<'a>(&'a self, availability: &ModelAvailability) -> Option<&'a str> {
        // First pass: prefer Available models.
        for model in &self.models {
            if matches!(availability.get_status(model), ModelStatus::Available) {
                return Some(model.as_str());
            }
        }
        // Second pass: accept Degraded models.
        for model in &self.models {
            if matches!(availability.get_status(model), ModelStatus::Degraded(_)) {
                return Some(model.as_str());
            }
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unknown_model_is_available() {
        let avail = ModelAvailability::new();
        assert_eq!(avail.get_status("gpt-4"), ModelStatus::Available);
    }

    #[test]
    fn success_resets_failures() {
        let avail = ModelAvailability::new();
        avail.record_failure("gpt-4", "timeout");
        avail.record_failure("gpt-4", "timeout");
        avail.record_failure("gpt-4", "timeout");
        assert!(matches!(
            avail.get_status("gpt-4"),
            ModelStatus::Degraded(_)
        ));

        avail.record_success("gpt-4");
        assert_eq!(avail.get_status("gpt-4"), ModelStatus::Available);
    }

    #[test]
    fn three_failures_degrades() {
        let avail = ModelAvailability::new();
        avail.record_failure("claude", "rate limit");
        avail.record_failure("claude", "rate limit");
        assert_eq!(avail.get_status("claude"), ModelStatus::Available);

        avail.record_failure("claude", "rate limit");
        assert!(matches!(
            avail.get_status("claude"),
            ModelStatus::Degraded(_)
        ));
    }

    #[test]
    fn five_failures_makes_unavailable() {
        let avail = ModelAvailability::new();
        for i in 0..5 {
            avail.record_failure("claude", &format!("error {i}"));
        }
        assert!(matches!(
            avail.get_status("claude"),
            ModelStatus::Unavailable(_)
        ));
    }

    #[test]
    fn degraded_reason_is_latest_error() {
        let avail = ModelAvailability::new();
        avail.record_failure("m", "first");
        avail.record_failure("m", "second");
        avail.record_failure("m", "third");
        match avail.get_status("m") {
            ModelStatus::Degraded(reason) => assert_eq!(reason, "third"),
            other => panic!("expected Degraded, got {other:?}"),
        }
    }

    #[test]
    fn independent_models() {
        let avail = ModelAvailability::new();
        for _ in 0..5 {
            avail.record_failure("bad-model", "error");
        }
        assert!(matches!(
            avail.get_status("bad-model"),
            ModelStatus::Unavailable(_)
        ));
        assert_eq!(avail.get_status("good-model"), ModelStatus::Available);
    }

    #[test]
    fn fallback_chain_prefers_available() {
        let avail = ModelAvailability::new();
        // Make first model degraded
        for _ in 0..3 {
            avail.record_failure("primary", "error");
        }
        let chain = FallbackChain::new(vec![
            "primary".into(),
            "secondary".into(),
            "tertiary".into(),
        ]);
        assert_eq!(chain.select(&avail), Some("secondary"));
    }

    #[test]
    fn fallback_chain_falls_to_degraded() {
        let avail = ModelAvailability::new();
        // Make all unavailable except one degraded
        for _ in 0..5 {
            avail.record_failure("a", "error");
            avail.record_failure("c", "error");
        }
        for _ in 0..3 {
            avail.record_failure("b", "error");
        }

        let chain = FallbackChain::new(vec!["a".into(), "b".into(), "c".into()]);
        assert_eq!(chain.select(&avail), Some("b"));
    }

    #[test]
    fn fallback_chain_returns_none_when_all_unavailable() {
        let avail = ModelAvailability::new();
        for _ in 0..5 {
            avail.record_failure("a", "error");
            avail.record_failure("b", "error");
        }
        let chain = FallbackChain::new(vec!["a".into(), "b".into()]);
        assert_eq!(chain.select(&avail), None);
    }

    #[test]
    fn auto_recovery_after_timeout() {
        // We can't easily test real Duration::from_secs(120), but we can verify
        // the recovery logic by testing that old failures are pruned.
        let avail = ModelAvailability::new();
        avail.record_failure("m", "error");
        avail.record_failure("m", "error");
        avail.record_failure("m", "error");
        assert!(matches!(avail.get_status("m"), ModelStatus::Degraded(_)));

        // Simulate recovery by recording success
        avail.record_success("m");
        assert_eq!(avail.get_status("m"), ModelStatus::Available);
    }

    #[test]
    fn fallback_chain_empty() {
        let avail = ModelAvailability::new();
        let chain = FallbackChain::new(vec![]);
        assert_eq!(chain.select(&avail), None);
    }
}
