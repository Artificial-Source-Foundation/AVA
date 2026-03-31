use ava_types::Message;

#[derive(Debug, Clone)]
pub struct ContextChunk {
    pub messages: Vec<Message>,
    pub estimated_tokens: usize,
}

#[derive(Debug, Clone)]
pub struct CondensationResult {
    pub messages: Vec<Message>,
    pub estimated_tokens: usize,
    pub strategy: String,
    /// Messages that were compacted (removed from the agent context).
    /// These have `agent_visible = false` and `original_content` set so
    /// the UI can still display them dimmed/collapsed.
    pub compacted_messages: Vec<Message>,
}

#[derive(Debug, Clone)]
pub struct CompactionReport {
    pub tokens_before: usize,
    pub tokens_after: usize,
    pub tokens_saved: usize,
    pub messages_before: usize,
    pub messages_after: usize,
    pub strategy: String,
    pub summary: Option<String>,
}

#[derive(Debug, Clone)]
pub struct CondenserConfig {
    pub max_tokens: usize,
    pub target_tokens: usize,
    pub max_tool_content_chars: usize,
    /// Keep the last N messages intact during summarization (default 4).
    pub preserve_recent_messages: usize,
    /// Keep the last N user turns intact during summarization (default 2).
    pub preserve_recent_turns: usize,
    /// Use LLM-based summarization when available (default true).
    pub enable_summarization: bool,
    /// Number of oldest messages to summarize per batch (default 20).
    pub summarization_batch_size: usize,
    /// Trigger compaction at this fraction of max_tokens (default 0.8).
    pub compaction_threshold_pct: f32,
    /// Optional focus hint for manual compaction.
    pub focus: Option<String>,
}

/// F15 — Compaction Circuit Breaker.
///
/// Prevents wasting tokens on compaction attempts that keep failing.
/// After `MAX_FAILURES` consecutive failures, the breaker opens and
/// compaction falls back to the cheap `SlidingWindowStrategy`.
/// After `COOLDOWN` elapses, a single test attempt is allowed (HalfOpen).
#[derive(Debug, Clone)]
pub struct CompactionCircuitBreaker {
    state: CircuitBreakerState,
    consecutive_failures: u32,
    last_failure_time: Option<std::time::Instant>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum CircuitBreakerState {
    Closed,
    Open,
    HalfOpen,
}

impl CompactionCircuitBreaker {
    const MAX_FAILURES: u32 = 3;
    const COOLDOWN: std::time::Duration = std::time::Duration::from_secs(60);

    pub fn new() -> Self {
        Self {
            state: CircuitBreakerState::Closed,
            consecutive_failures: 0,
            last_failure_time: None,
        }
    }

    /// Returns `true` if a full (LLM-based) compaction attempt is allowed.
    /// When the breaker is Open, the caller should use a cheap fallback instead.
    pub fn allow_compaction(&mut self) -> bool {
        match self.state {
            CircuitBreakerState::Closed => true,
            CircuitBreakerState::Open => {
                // Check if cooldown has elapsed → transition to HalfOpen.
                if let Some(last) = self.last_failure_time {
                    if last.elapsed() >= Self::COOLDOWN {
                        self.state = CircuitBreakerState::HalfOpen;
                        return true;
                    }
                }
                false
            }
            CircuitBreakerState::HalfOpen => true,
        }
    }

    /// Record a successful compaction — resets the breaker to Closed.
    pub fn record_success(&mut self) {
        self.state = CircuitBreakerState::Closed;
        self.consecutive_failures = 0;
        self.last_failure_time = None;
    }

    /// Record a failed compaction — may trip the breaker to Open.
    pub fn record_failure(&mut self) {
        self.consecutive_failures += 1;
        self.last_failure_time = Some(std::time::Instant::now());
        if self.consecutive_failures >= Self::MAX_FAILURES {
            self.state = CircuitBreakerState::Open;
            tracing::warn!(
                failures = self.consecutive_failures,
                "compaction circuit breaker tripped — falling back to sliding window"
            );
        }
    }

    pub fn is_open(&self) -> bool {
        self.state == CircuitBreakerState::Open
    }
}

impl Default for CompactionCircuitBreaker {
    fn default() -> Self {
        Self::new()
    }
}

impl Default for CondenserConfig {
    fn default() -> Self {
        Self {
            max_tokens: 16_000,
            target_tokens: 12_000,
            max_tool_content_chars: 2_000,
            preserve_recent_messages: 4,
            preserve_recent_turns: 2,
            enable_summarization: true,
            summarization_batch_size: 20,
            compaction_threshold_pct: 0.8,
            focus: None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn circuit_breaker_allows_initially() {
        let mut cb = CompactionCircuitBreaker::new();
        assert!(cb.allow_compaction());
        assert!(!cb.is_open());
    }

    #[test]
    fn circuit_breaker_trips_after_three_failures() {
        let mut cb = CompactionCircuitBreaker::new();
        cb.record_failure();
        assert!(cb.allow_compaction(), "should still allow after 1 failure");
        cb.record_failure();
        assert!(cb.allow_compaction(), "should still allow after 2 failures");
        cb.record_failure();
        assert!(cb.is_open(), "should be open after 3 failures");
        assert!(!cb.allow_compaction(), "should deny when open");
    }

    #[test]
    fn circuit_breaker_resets_on_success() {
        let mut cb = CompactionCircuitBreaker::new();
        cb.record_failure();
        cb.record_failure();
        cb.record_success();
        assert!(!cb.is_open());
        assert!(cb.allow_compaction());
        // Should need 3 more failures to trip again
        cb.record_failure();
        cb.record_failure();
        assert!(cb.allow_compaction());
    }
}
