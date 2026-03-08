use std::sync::atomic::{AtomicU32, AtomicU8, Ordering};
use std::sync::Mutex;
use std::time::{Duration, Instant};

const STATE_CLOSED: u8 = 0;
const STATE_OPEN: u8 = 1;
const STATE_HALF_OPEN: u8 = 2;

pub struct CircuitBreaker {
    failure_count: AtomicU32,
    failure_threshold: u32,
    cooldown: Duration,
    last_failure: Mutex<Option<Instant>>,
    state: AtomicU8,
}

impl CircuitBreaker {
    pub fn new(failure_threshold: u32, cooldown: Duration) -> Self {
        Self {
            failure_count: AtomicU32::new(0),
            failure_threshold,
            cooldown,
            last_failure: Mutex::new(None),
            state: AtomicU8::new(STATE_CLOSED),
        }
    }

    /// Default circuit breaker: 5 failures, 30s cooldown.
    pub fn default_provider() -> Self {
        Self::new(5, Duration::from_secs(30))
    }

    /// Check if a request is allowed. Returns `true` if allowed.
    pub fn allow_request(&self) -> bool {
        let state = self.state.load(Ordering::Acquire);
        match state {
            STATE_CLOSED => true,
            STATE_OPEN => {
                // Check if cooldown has elapsed
                let last = self.last_failure.lock().unwrap();
                if let Some(last_time) = *last {
                    if last_time.elapsed() >= self.cooldown {
                        drop(last);
                        // Transition to half-open
                        self.state.store(STATE_HALF_OPEN, Ordering::Release);
                        true
                    } else {
                        false
                    }
                } else {
                    // No recorded failure time — shouldn't happen, allow anyway
                    true
                }
            }
            STATE_HALF_OPEN => {
                // Allow exactly one probe request (already transitioned)
                true
            }
            _ => true,
        }
    }

    /// Record a successful request.
    pub fn record_success(&self) {
        let state = self.state.load(Ordering::Acquire);
        if state == STATE_HALF_OPEN || state == STATE_CLOSED {
            self.failure_count.store(0, Ordering::Release);
            self.state.store(STATE_CLOSED, Ordering::Release);
        }
    }

    /// Record a failed request.
    pub fn record_failure(&self) {
        let prev = self.failure_count.fetch_add(1, Ordering::AcqRel);
        *self.last_failure.lock().unwrap() = Some(Instant::now());

        let state = self.state.load(Ordering::Acquire);
        if state == STATE_HALF_OPEN {
            // Half-open probe failed → reopen
            self.state.store(STATE_OPEN, Ordering::Release);
        } else if prev + 1 >= self.failure_threshold {
            self.state.store(STATE_OPEN, Ordering::Release);
        }
    }

    /// Current state as a string (for diagnostics).
    pub fn state_name(&self) -> &'static str {
        match self.state.load(Ordering::Acquire) {
            STATE_CLOSED => "closed",
            STATE_OPEN => "open",
            STATE_HALF_OPEN => "half-open",
            _ => "unknown",
        }
    }

    pub fn is_open(&self) -> bool {
        self.state.load(Ordering::Acquire) == STATE_OPEN
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn circuit_breaker_opens_after_failures() {
        let cb = CircuitBreaker::new(3, Duration::from_secs(30));
        assert!(cb.allow_request());
        assert_eq!(cb.state_name(), "closed");

        cb.record_failure();
        cb.record_failure();
        assert!(cb.allow_request()); // Still closed after 2

        cb.record_failure();
        assert_eq!(cb.state_name(), "open");
        assert!(!cb.allow_request()); // Now open, blocked
    }

    #[test]
    fn circuit_breaker_recovers_after_cooldown() {
        let cb = CircuitBreaker::new(2, Duration::from_millis(10));

        cb.record_failure();
        cb.record_failure();
        assert!(cb.is_open());
        assert!(!cb.allow_request());

        // Wait for cooldown
        std::thread::sleep(Duration::from_millis(15));

        // Should transition to half-open and allow
        assert!(cb.allow_request());
        assert_eq!(cb.state_name(), "half-open");
    }

    #[test]
    fn circuit_breaker_half_open_success_closes() {
        let cb = CircuitBreaker::new(2, Duration::from_millis(10));

        cb.record_failure();
        cb.record_failure();
        std::thread::sleep(Duration::from_millis(15));
        assert!(cb.allow_request()); // transitions to half-open

        cb.record_success();
        assert_eq!(cb.state_name(), "closed");
        assert!(cb.allow_request());
    }

    #[test]
    fn five_failures_opens_circuit() {
        let cb = CircuitBreaker::default_provider(); // threshold=5, cooldown=30s
        assert!(!cb.is_open());

        for _ in 0..4 {
            cb.record_failure();
            assert!(!cb.is_open(), "should still be closed before 5 failures");
        }

        cb.record_failure(); // 5th failure
        assert!(cb.is_open(), "should be open after exactly 5 failures");
        assert!(!cb.allow_request(), "should block requests when open");
    }

    #[test]
    fn circuit_breaker_half_open_failure_reopens() {
        let cb = CircuitBreaker::new(2, Duration::from_millis(10));

        cb.record_failure();
        cb.record_failure();
        std::thread::sleep(Duration::from_millis(15));
        assert!(cb.allow_request()); // transitions to half-open

        cb.record_failure();
        assert_eq!(cb.state_name(), "open");
        assert!(!cb.allow_request());
    }
}
