use std::time::Duration;

use ava_types::AvaError;
use rand::Rng;

/// Budget-aware retry with exponential backoff.
///
/// Only retries errors where `AvaError::is_retryable()` returns true.
pub struct RetryBudget {
    max_retries: usize,
    remaining: usize,
    base_delay: Duration,
    max_delay: Duration,
}

impl RetryBudget {
    pub fn new(max_retries: usize) -> Self {
        Self {
            max_retries,
            remaining: max_retries,
            base_delay: Duration::from_secs(1),
            max_delay: Duration::from_secs(60),
        }
    }

    pub fn with_delays(mut self, base: Duration, max: Duration) -> Self {
        self.base_delay = base;
        self.max_delay = max;
        self
    }

    /// Returns the delay to wait before retrying, or `None` if the error
    /// is not retryable or the budget is exhausted.
    pub fn should_retry(&mut self, error: &AvaError) -> Option<Duration> {
        if !error.is_retryable() || self.remaining == 0 {
            return None;
        }

        self.remaining -= 1;
        let attempt = self.max_retries - self.remaining; // 1-based
        let exponential = self
            .base_delay
            .saturating_mul(1u32 << (attempt - 1).min(30));
        let jitter_factor = rand::thread_rng().gen_range(0.8..=1.2);
        let delay = exponential.mul_f64(jitter_factor);
        Some(delay.min(self.max_delay))
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
        if !error.is_retryable() || self.remaining == 0 {
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
        let jitter_factor = rand::thread_rng().gen_range(0.8..=1.2);
        let delay = base.mul_f64(jitter_factor);
        Some(delay.min(self.max_delay))
    }

    /// Reset the budget for a new operation.
    pub fn reset(&mut self) {
        self.remaining = self.max_retries;
    }

    pub fn remaining(&self) -> usize {
        self.remaining
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
}
