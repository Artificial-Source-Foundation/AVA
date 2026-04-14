use std::time::{Duration, Instant};

use ava_types::AvaError;
use rand::Rng;

/// Controls retry behavior based on execution context.
///
/// - `Interactive`: current behavior — limited retries, ~32s max backoff.
/// - `Persistent`: for headless/background agents — 5min max backoff, 6hr total duration,
///   unlimited attempts. Never gives up on retryable errors.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum RetryMode {
    /// Interactive/TUI mode: max ~32s backoff, give up after N attempts.
    #[default]
    Interactive,
    /// Headless/background mode: 5min max backoff, 6hr total, unlimited attempts.
    Persistent,
}

impl RetryMode {
    /// Detect retry mode from agent configuration.
    ///
    /// Returns `Persistent` when `headless` is true or the agent is a background/sub-agent.
    pub fn from_config(headless: bool, is_subagent: bool) -> Self {
        if headless || is_subagent {
            RetryMode::Persistent
        } else {
            RetryMode::Interactive
        }
    }
}

/// Tracks consecutive overload errors (529/503) per provider for fallback routing (F24).
#[derive(Debug, Clone, Default)]
pub struct OverloadTracker {
    pub consecutive_overloads: u32,
    /// Whether we have switched to the fallback model.
    pub using_fallback: bool,
}

impl OverloadTracker {
    pub fn new() -> Self {
        Self::default()
    }

    /// Record an overload error (529 or 503 status codes).
    /// Returns true if the threshold (3) has been reached and fallback should activate.
    pub fn record_overload(&mut self) -> bool {
        self.consecutive_overloads += 1;
        self.consecutive_overloads >= 3
    }

    /// Record a successful request — resets the overload counter.
    pub fn record_success(&mut self) {
        self.consecutive_overloads = 0;
        self.using_fallback = false;
    }

    /// Returns true if the overload threshold has been reached.
    pub fn should_fallback(&self) -> bool {
        self.consecutive_overloads >= 3
    }

    /// Check if a status code is an overload indicator (529 or 503).
    pub fn is_overload_status(status: u16) -> bool {
        status == 529 || status == 503
    }
}

/// Budget-aware retry with exponential backoff.
///
/// Only retries errors where `AvaError::is_retryable()` returns true.
pub struct RetryBudget {
    max_retries: usize,
    remaining: usize,
    base_delay: Duration,
    max_delay: Duration,
    mode: RetryMode,
    /// When in Persistent mode, tracks how long we've been retrying.
    started_at: Option<Instant>,
}

/// Maximum total retry duration for Persistent mode (6 hours).
const PERSISTENT_MAX_TOTAL: Duration = Duration::from_secs(6 * 60 * 60);

/// Maximum backoff delay for Persistent mode (5 minutes).
const PERSISTENT_MAX_DELAY: Duration = Duration::from_secs(300);

impl RetryBudget {
    pub fn new(max_retries: usize) -> Self {
        Self {
            max_retries,
            remaining: max_retries,
            base_delay: Duration::from_secs(1),
            max_delay: Duration::from_secs(60),
            mode: RetryMode::Interactive,
            started_at: None,
        }
    }

    pub fn with_delays(mut self, base: Duration, max: Duration) -> Self {
        self.base_delay = base;
        self.max_delay = max;
        self
    }

    /// Set the retry mode. In `Persistent` mode, the budget has unlimited attempts
    /// with a 5min max backoff and 6hr total duration.
    pub fn with_mode(mut self, mode: RetryMode) -> Self {
        self.mode = mode;
        if mode == RetryMode::Persistent {
            self.max_delay = PERSISTENT_MAX_DELAY;
        }
        self
    }

    /// Current retry mode.
    pub fn mode(&self) -> RetryMode {
        self.mode
    }

    /// Returns the delay to wait before retrying, or `None` if the error
    /// is not retryable or the budget is exhausted.
    pub fn should_retry(&mut self, error: &AvaError) -> Option<Duration> {
        if !error.is_retryable() {
            return None;
        }

        match self.mode {
            RetryMode::Interactive => {
                if self.remaining == 0 {
                    return None;
                }
                self.remaining -= 1;
                let attempt = self.max_retries - self.remaining; // 1-based
                let exponential = self
                    .base_delay
                    .saturating_mul(1u32 << (attempt - 1).min(30));
                let jitter_factor = rand::rng().random_range(0.8..=1.2);
                let delay = exponential.mul_f64(jitter_factor);
                Some(delay.min(self.max_delay))
            }
            RetryMode::Persistent => {
                let now = Instant::now();
                let started = *self.started_at.get_or_insert(now);
                if now.duration_since(started) >= PERSISTENT_MAX_TOTAL {
                    return None;
                }
                // Use attempt counter for backoff calculation but don't limit it
                if self.remaining > 0 {
                    self.remaining -= 1;
                }
                let attempt = self.max_retries - self.remaining.min(self.max_retries); // 1-based
                let exponential = self
                    .base_delay
                    .saturating_mul(1u32 << (attempt.min(30).saturating_sub(1)) as u32);
                let jitter_factor = rand::rng().random_range(0.8..=1.2);
                let delay = exponential.mul_f64(jitter_factor);
                Some(delay.min(PERSISTENT_MAX_DELAY))
            }
        }
    }

    /// Returns the delay to wait before retrying, incorporating an optional
    /// server-supplied hint (e.g. from a `Retry-After` header). The hint is
    /// used as the minimum delay — `max(hint, exponential_backoff)` — with
    /// ±20% jitter applied on top.
    pub fn should_retry_with_hint(
        &mut self,
        error: &AvaError,
        server_hint: Option<Duration>,
    ) -> Option<Duration> {
        if !error.is_retryable() {
            return None;
        }

        match self.mode {
            RetryMode::Interactive => {
                if self.remaining == 0 {
                    return None;
                }
                self.remaining -= 1;
                let attempt = self.max_retries - self.remaining; // 1-based
                let exponential = self
                    .base_delay
                    .saturating_mul(1u32 << (attempt - 1).min(30));
                let base = match server_hint {
                    Some(hint) => hint.max(exponential),
                    None => exponential,
                };
                let jitter_factor = rand::rng().random_range(0.8..=1.2);
                let delay = base.mul_f64(jitter_factor);
                Some(delay.min(self.max_delay))
            }
            RetryMode::Persistent => {
                let now = Instant::now();
                let started = *self.started_at.get_or_insert(now);
                if now.duration_since(started) >= PERSISTENT_MAX_TOTAL {
                    return None;
                }
                if self.remaining > 0 {
                    self.remaining -= 1;
                }
                let attempt = self.max_retries - self.remaining.min(self.max_retries);
                let exponential = self
                    .base_delay
                    .saturating_mul(1u32 << (attempt.min(30).saturating_sub(1)) as u32);
                let base = match server_hint {
                    Some(hint) => hint.max(exponential),
                    None => exponential,
                };
                let jitter_factor = rand::rng().random_range(0.8..=1.2);
                let delay = base.mul_f64(jitter_factor);
                Some(delay.min(PERSISTENT_MAX_DELAY))
            }
        }
    }

    /// Reset the budget for a new operation.
    pub fn reset(&mut self) {
        self.remaining = self.max_retries;
        self.started_at = None;
    }

    pub fn remaining(&self) -> usize {
        self.remaining
    }

    /// Total elapsed time since first retry (Persistent mode tracking).
    pub fn elapsed(&self) -> Option<Duration> {
        self.started_at.map(|s| s.elapsed())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn retries_retryable_errors() {
        let mut budget = RetryBudget::new(3);
        let err = AvaError::RateLimited {
            provider: "openai".to_string(),
            retry_after_secs: 5,
        };

        assert!(budget.should_retry(&err).is_some());
        assert_eq!(budget.remaining(), 2);
        assert!(budget.should_retry(&err).is_some());
        assert_eq!(budget.remaining(), 1);
        assert!(budget.should_retry(&err).is_some());
        assert_eq!(budget.remaining(), 0);
        assert!(budget.should_retry(&err).is_none()); // exhausted
    }

    #[test]
    fn does_not_retry_non_retryable() {
        let mut budget = RetryBudget::new(3);
        let err = AvaError::MissingApiKey {
            provider: "openai".to_string(),
        };

        assert!(budget.should_retry(&err).is_none());
        assert_eq!(budget.remaining(), 3); // not consumed
    }

    #[test]
    fn exponential_backoff_within_jitter_bounds() {
        let mut budget =
            RetryBudget::new(5).with_delays(Duration::from_secs(1), Duration::from_secs(60));

        let err = AvaError::TimeoutError("test".to_string());

        // With ±20% jitter, base 1s → [0.8, 1.2]
        let d1 = budget.should_retry(&err).unwrap();
        assert!(d1 >= Duration::from_millis(800) && d1 <= Duration::from_millis(1200));

        // base 2s → [1.6, 2.4]
        let d2 = budget.should_retry(&err).unwrap();
        assert!(d2 >= Duration::from_millis(1600) && d2 <= Duration::from_millis(2400));

        // base 4s → [3.2, 4.8]
        let d3 = budget.should_retry(&err).unwrap();
        assert!(d3 >= Duration::from_millis(3200) && d3 <= Duration::from_millis(4800));

        // base 8s → [6.4, 9.6]
        let d4 = budget.should_retry(&err).unwrap();
        assert!(d4 >= Duration::from_millis(6400) && d4 <= Duration::from_millis(9600));
    }

    #[test]
    fn caps_at_max_delay() {
        let mut budget =
            RetryBudget::new(10).with_delays(Duration::from_secs(1), Duration::from_secs(10));

        let err = AvaError::TimeoutError("test".to_string());

        // Exhaust through several retries
        for _ in 0..8 {
            let delay = budget.should_retry(&err).unwrap();
            assert!(delay <= Duration::from_secs(10));
        }
    }

    #[test]
    fn retry_delays_have_jitter() {
        let delays: Vec<Duration> = (0..20)
            .map(|_| {
                let mut budget = RetryBudget::new(5)
                    .with_delays(Duration::from_millis(100), Duration::from_secs(30));
                let error = AvaError::RateLimited {
                    provider: "test".into(),
                    retry_after_secs: 5,
                };
                budget.should_retry(&error).unwrap()
            })
            .collect();
        let first = delays[0];
        assert!(
            delays.iter().any(|d| *d != first),
            "Expected jitter but all delays were identical"
        );
    }

    #[test]
    fn retry_jitter_stays_within_bounds() {
        for _ in 0..100 {
            let mut budget = RetryBudget::new(5)
                .with_delays(Duration::from_millis(1000), Duration::from_secs(60));
            let error = AvaError::RateLimited {
                provider: "test".into(),
                retry_after_secs: 5,
            };
            let delay = budget.should_retry(&error).unwrap();
            assert!(
                delay >= Duration::from_millis(800),
                "Delay too short: {delay:?}"
            );
            assert!(
                delay <= Duration::from_millis(1200),
                "Delay too long: {delay:?}"
            );
        }
    }

    #[test]
    fn should_retry_with_hint_uses_hint_as_floor() {
        let mut budget =
            RetryBudget::new(5).with_delays(Duration::from_secs(1), Duration::from_secs(120));
        let err = AvaError::RateLimited {
            provider: "openai".to_string(),
            retry_after_secs: 30,
        };
        let hint = Some(Duration::from_secs(30));

        // Attempt 1: exp=1s, hint=30s -> base=30s, with ±20% jitter: [24s, 36s]
        let d = budget.should_retry_with_hint(&err, hint).unwrap();
        assert!(
            d >= Duration::from_secs(24) && d <= Duration::from_secs(36),
            "Expected ~30s delay, got {d:?}"
        );
    }

    #[test]
    fn should_retry_with_hint_none_falls_back_to_exponential() {
        let mut budget =
            RetryBudget::new(5).with_delays(Duration::from_secs(1), Duration::from_secs(120));
        let err = AvaError::RateLimited {
            provider: "openai".to_string(),
            retry_after_secs: 5,
        };

        // No hint: same as regular should_retry
        let d = budget.should_retry_with_hint(&err, None).unwrap();
        // Attempt 1: base_delay * 2^0 = 1s, with ±20% jitter: [0.8s, 1.2s]
        assert!(
            d >= Duration::from_millis(800) && d <= Duration::from_millis(1200),
            "Expected ~1s delay, got {d:?}"
        );
    }

    #[test]
    fn should_retry_with_hint_prefers_backoff_when_larger() {
        let mut budget =
            RetryBudget::new(5).with_delays(Duration::from_secs(1), Duration::from_secs(120));
        let err = AvaError::TimeoutError("test".to_string());
        let hint = Some(Duration::from_millis(100)); // very small hint

        // Consume first 3 retries to get to attempt 4
        budget.should_retry_with_hint(&err, hint);
        budget.should_retry_with_hint(&err, hint);
        budget.should_retry_with_hint(&err, hint);

        // Attempt 4: base_delay * 2^3 = 8s, hint=100ms -> base=8s
        let d = budget.should_retry_with_hint(&err, hint).unwrap();
        assert!(
            d >= Duration::from_millis(6400) && d <= Duration::from_millis(9600),
            "Expected ~8s delay (backoff > hint), got {d:?}"
        );
    }

    #[test]
    fn reset_restores_budget() {
        let mut budget = RetryBudget::new(2);
        let err = AvaError::TimeoutError("test".to_string());

        budget.should_retry(&err);
        budget.should_retry(&err);
        assert!(budget.should_retry(&err).is_none());

        budget.reset();
        assert_eq!(budget.remaining(), 2);
        assert!(budget.should_retry(&err).is_some());
    }

    // ── F23: Persistent Retry Mode tests ──────────────────────────────────

    #[test]
    fn interactive_mode_caps_backoff_at_max_delay() {
        let mut budget = RetryBudget::new(10)
            .with_delays(Duration::from_secs(1), Duration::from_secs(32))
            .with_mode(RetryMode::Interactive);

        let err = AvaError::TimeoutError("test".to_string());

        for _ in 0..8 {
            let delay = budget.should_retry(&err).unwrap();
            assert!(
                delay <= Duration::from_millis(38400), // 32s * 1.2 jitter
                "Interactive mode exceeded max delay: {delay:?}"
            );
        }
    }

    #[test]
    fn interactive_mode_exhausts_budget() {
        let mut budget = RetryBudget::new(3)
            .with_delays(Duration::from_secs(1), Duration::from_secs(60))
            .with_mode(RetryMode::Interactive);

        let err = AvaError::TimeoutError("test".to_string());

        assert!(budget.should_retry(&err).is_some());
        assert!(budget.should_retry(&err).is_some());
        assert!(budget.should_retry(&err).is_some());
        assert!(budget.should_retry(&err).is_none()); // exhausted
    }

    #[test]
    fn persistent_mode_allows_longer_backoff() {
        let mut budget = RetryBudget::new(3)
            .with_delays(Duration::from_secs(1), Duration::from_secs(60))
            .with_mode(RetryMode::Persistent);

        let err = AvaError::TimeoutError("test".to_string());

        // Persistent mode should continue retrying even after budget "exhausted"
        for _ in 0..10 {
            let delay = budget.should_retry(&err);
            assert!(delay.is_some(), "Persistent mode should not exhaust");
        }
    }

    #[test]
    fn persistent_mode_caps_at_5min() {
        let mut budget = RetryBudget::new(20)
            .with_delays(Duration::from_secs(1), Duration::from_secs(60))
            .with_mode(RetryMode::Persistent);

        let err = AvaError::TimeoutError("test".to_string());

        // After many retries, delay should be capped at 5min (300s * 1.2 = 360s)
        for _ in 0..18 {
            budget.should_retry(&err);
        }
        let delay = budget.should_retry(&err).unwrap();
        assert!(
            delay <= Duration::from_secs(360),
            "Persistent mode exceeded 5min cap: {delay:?}"
        );
    }

    #[test]
    fn retry_mode_from_config_headless() {
        assert_eq!(RetryMode::from_config(true, false), RetryMode::Persistent);
    }

    #[test]
    fn retry_mode_from_config_subagent() {
        assert_eq!(RetryMode::from_config(false, true), RetryMode::Persistent);
    }

    #[test]
    fn retry_mode_from_config_interactive() {
        assert_eq!(RetryMode::from_config(false, false), RetryMode::Interactive);
    }

    // ── F24: Overload Tracker tests ───────────────────────────────────────

    #[test]
    fn overload_tracker_triggers_after_3() {
        let mut tracker = OverloadTracker::new();
        assert!(!tracker.record_overload()); // 1
        assert!(!tracker.record_overload()); // 2
        assert!(tracker.record_overload()); // 3 — should trigger fallback
        assert!(tracker.should_fallback());
    }

    #[test]
    fn overload_tracker_success_resets() {
        let mut tracker = OverloadTracker::new();
        tracker.record_overload();
        tracker.record_overload();
        tracker.record_success(); // reset
        assert!(!tracker.should_fallback());
        assert_eq!(tracker.consecutive_overloads, 0);
    }

    #[test]
    fn overload_tracker_no_config_no_switch() {
        // Without FallbackConfig, the tracker still counts but the caller
        // decides whether to act. This test verifies the counter works independently.
        let mut tracker = OverloadTracker::new();
        for _ in 0..5 {
            tracker.record_overload();
        }
        assert!(tracker.should_fallback());
        // Caller would check for FallbackConfig before switching
    }

    #[test]
    fn overload_status_detection() {
        assert!(OverloadTracker::is_overload_status(529));
        assert!(OverloadTracker::is_overload_status(503));
        assert!(!OverloadTracker::is_overload_status(500));
        assert!(!OverloadTracker::is_overload_status(429));
        assert!(!OverloadTracker::is_overload_status(200));
    }
}
