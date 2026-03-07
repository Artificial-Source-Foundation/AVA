use std::time::Duration;

use ava_types::AvaError;

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
        let delay = self
            .base_delay
            .saturating_mul(1u32 << (attempt - 1).min(30));
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
    fn exponential_backoff() {
        let mut budget = RetryBudget::new(5)
            .with_delays(Duration::from_secs(1), Duration::from_secs(60));

        let err = AvaError::TimeoutError("test".to_string());

        let d1 = budget.should_retry(&err).unwrap();
        assert_eq!(d1, Duration::from_secs(1)); // 1 * 2^0

        let d2 = budget.should_retry(&err).unwrap();
        assert_eq!(d2, Duration::from_secs(2)); // 1 * 2^1

        let d3 = budget.should_retry(&err).unwrap();
        assert_eq!(d3, Duration::from_secs(4)); // 1 * 2^2

        let d4 = budget.should_retry(&err).unwrap();
        assert_eq!(d4, Duration::from_secs(8)); // 1 * 2^3
    }

    #[test]
    fn caps_at_max_delay() {
        let mut budget = RetryBudget::new(10)
            .with_delays(Duration::from_secs(1), Duration::from_secs(10));

        let err = AvaError::TimeoutError("test".to_string());

        // Exhaust through several retries
        for _ in 0..8 {
            let delay = budget.should_retry(&err).unwrap();
            assert!(delay <= Duration::from_secs(10));
        }
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
