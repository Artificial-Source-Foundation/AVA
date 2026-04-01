//! Denial tracking with fallback suggestions.
//!
//! Tracks consecutive tool permission denials and suggests alternative approaches
//! when a tool has been denied too many times in a row.

use std::collections::HashMap;
use std::time::{Duration, Instant};

/// Timeout after which denial counters are automatically reset.
const DENIAL_TIMEOUT: Duration = Duration::from_secs(5 * 60);

/// Number of consecutive denials before suggesting a fallback.
const FALLBACK_THRESHOLD: u32 = 3;

/// State for a single tool's denial history.
#[derive(Debug, Clone)]
pub struct DenialState {
    /// Number of consecutive denials without an intervening success.
    pub consecutive_denials: u32,
    /// Timestamp of the most recent denial.
    pub last_denial: Instant,
}

/// Tracks per-tool denial counts and provides fallback suggestions.
///
/// When a tool is denied 3+ times consecutively (within a 5-minute window),
/// the tracker suggests that the agent try a different approach.
///
/// # Usage
/// ```
/// use ava_permissions::denial_tracking::DenialTracker;
///
/// let mut tracker = DenialTracker::new();
/// tracker.record_denial("bash");
/// tracker.record_denial("bash");
/// assert!(!tracker.should_fallback("bash"));
///
/// tracker.record_denial("bash");
/// assert!(tracker.should_fallback("bash"));
/// assert!(tracker.suggestion("bash").is_some());
///
/// tracker.record_success("bash");
/// assert!(!tracker.should_fallback("bash"));
/// ```
#[derive(Debug, Clone)]
pub struct DenialTracker {
    states: HashMap<String, DenialState>,
}

impl DenialTracker {
    /// Create a new empty denial tracker.
    pub fn new() -> Self {
        Self {
            states: HashMap::new(),
        }
    }

    /// Record a permission denial for the given tool.
    pub fn record_denial(&mut self, tool: &str) {
        let now = Instant::now();
        let state = self.states.entry(tool.to_string()).or_insert(DenialState {
            consecutive_denials: 0,
            last_denial: now,
        });

        // Reset if timed out
        if now.duration_since(state.last_denial) >= DENIAL_TIMEOUT {
            state.consecutive_denials = 0;
        }

        state.consecutive_denials += 1;
        state.last_denial = now;
    }

    /// Record a successful tool execution, resetting the denial counter.
    pub fn record_success(&mut self, tool: &str) {
        self.states.remove(tool);
    }

    /// Check whether the tool has been denied enough times to suggest a fallback.
    ///
    /// Returns `true` after 3+ consecutive denials within the timeout window.
    pub fn should_fallback(&self, tool: &str) -> bool {
        if let Some(state) = self.states.get(tool) {
            // Check timeout
            if Instant::now().duration_since(state.last_denial) >= DENIAL_TIMEOUT {
                return false;
            }
            state.consecutive_denials >= FALLBACK_THRESHOLD
        } else {
            false
        }
    }

    /// Get a suggestion message for a tool that has hit the fallback threshold.
    ///
    /// Returns `None` if the tool has not been denied enough times.
    pub fn suggestion(&self, tool: &str) -> Option<String> {
        if self.should_fallback(tool) {
            let state = self.states.get(tool)?;
            Some(format!(
                "The '{}' tool has been denied {} times consecutively. \
                 Consider trying a different approach or using an alternative tool.",
                tool, state.consecutive_denials
            ))
        } else {
            None
        }
    }

    /// Get the current denial state for a tool, if any.
    pub fn state(&self, tool: &str) -> Option<&DenialState> {
        self.states.get(tool)
    }
}

impl Default for DenialTracker {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn new_tracker_has_no_fallbacks() {
        let tracker = DenialTracker::new();
        assert!(!tracker.should_fallback("bash"));
        assert!(tracker.suggestion("bash").is_none());
    }

    #[test]
    fn one_denial_no_fallback() {
        let mut tracker = DenialTracker::new();
        tracker.record_denial("bash");
        assert!(!tracker.should_fallback("bash"));
    }

    #[test]
    fn two_denials_no_fallback() {
        let mut tracker = DenialTracker::new();
        tracker.record_denial("bash");
        tracker.record_denial("bash");
        assert!(!tracker.should_fallback("bash"));
    }

    #[test]
    fn three_denials_triggers_fallback() {
        let mut tracker = DenialTracker::new();
        tracker.record_denial("bash");
        tracker.record_denial("bash");
        tracker.record_denial("bash");
        assert!(tracker.should_fallback("bash"));
    }

    #[test]
    fn suggestion_after_three_denials() {
        let mut tracker = DenialTracker::new();
        tracker.record_denial("write");
        tracker.record_denial("write");
        tracker.record_denial("write");

        let suggestion = tracker.suggestion("write");
        assert!(suggestion.is_some());
        let msg = suggestion.unwrap();
        assert!(msg.contains("write"));
        assert!(msg.contains("3 times"));
        assert!(msg.contains("different approach"));
    }

    #[test]
    fn success_resets_counter() {
        let mut tracker = DenialTracker::new();
        tracker.record_denial("bash");
        tracker.record_denial("bash");
        tracker.record_denial("bash");
        assert!(tracker.should_fallback("bash"));

        tracker.record_success("bash");
        assert!(!tracker.should_fallback("bash"));
        assert!(tracker.suggestion("bash").is_none());
    }

    #[test]
    fn different_tools_tracked_independently() {
        let mut tracker = DenialTracker::new();
        tracker.record_denial("bash");
        tracker.record_denial("bash");
        tracker.record_denial("bash");
        tracker.record_denial("write");

        assert!(tracker.should_fallback("bash"));
        assert!(!tracker.should_fallback("write"));
    }

    #[test]
    fn four_denials_still_triggers() {
        let mut tracker = DenialTracker::new();
        for _ in 0..4 {
            tracker.record_denial("edit");
        }
        assert!(tracker.should_fallback("edit"));
        let msg = tracker.suggestion("edit").unwrap();
        assert!(msg.contains("4 times"));
    }

    #[test]
    fn timeout_resets_counter() {
        let mut tracker = DenialTracker::new();

        // Manually insert a state with an old timestamp
        tracker.states.insert(
            "bash".to_string(),
            DenialState {
                consecutive_denials: 5,
                last_denial: Instant::now() - Duration::from_secs(6 * 60),
            },
        );

        // Should not trigger fallback because of timeout
        assert!(!tracker.should_fallback("bash"));

        // Recording a new denial should reset and start at 1
        tracker.record_denial("bash");
        assert_eq!(tracker.states["bash"].consecutive_denials, 1);
        assert!(!tracker.should_fallback("bash"));
    }

    #[test]
    fn default_creates_empty_tracker() {
        let tracker = DenialTracker::default();
        assert!(!tracker.should_fallback("anything"));
    }

    #[test]
    fn state_returns_denial_info() {
        let mut tracker = DenialTracker::new();
        assert!(tracker.state("bash").is_none());

        tracker.record_denial("bash");
        let state = tracker.state("bash").unwrap();
        assert_eq!(state.consecutive_denials, 1);
    }
}
