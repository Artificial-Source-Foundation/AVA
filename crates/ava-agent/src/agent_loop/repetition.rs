//! Repetition detector for consecutive identical tool calls.
//!
//! Tracks a hash of (tool_name, tool_args_json) and detects when the same
//! pair is called consecutively more than `max_repetitions` times. Returns
//! a warning message that can be injected into the conversation to nudge
//! the agent toward a different approach.

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};

use ava_types::ToolCall;

/// Tracks consecutive identical tool calls and warns when threshold is exceeded.
pub struct RepetitionDetector {
    /// Maximum allowed consecutive repetitions before warning.
    max_repetitions: usize,
    /// Hash of the last recorded (tool_name, args) pair.
    last_hash: Option<u64>,
    /// How many times the current hash has been seen consecutively.
    consecutive_count: usize,
}

impl RepetitionDetector {
    /// Create a new detector with the given repetition threshold.
    pub fn new(max_repetitions: usize) -> Self {
        Self {
            max_repetitions,
            last_hash: None,
            consecutive_count: 0,
        }
    }

    /// Compute a hash of (tool_name, tool_args_json).
    fn call_hash(tool_call: &ToolCall) -> u64 {
        let mut hasher = DefaultHasher::new();
        tool_call.name.hash(&mut hasher);
        tool_call.arguments.to_string().hash(&mut hasher);
        hasher.finish()
    }

    /// Record a tool call and check for repetition.
    ///
    /// Returns `Some(warning_message)` if the same (name, args) pair has been
    /// called consecutively more than `max_repetitions` times. Returns `None`
    /// otherwise.
    pub fn record(&mut self, tool_call: &ToolCall) -> Option<String> {
        let hash = Self::call_hash(tool_call);

        if self.last_hash == Some(hash) {
            self.consecutive_count += 1;
        } else {
            self.last_hash = Some(hash);
            self.consecutive_count = 1;
        }

        if self.consecutive_count > self.max_repetitions {
            // Reset so we don't fire on every subsequent call
            self.consecutive_count = 0;
            self.last_hash = None;
            Some(format!(
                "You are repeating the same tool call (`{}`) with identical arguments. \
                 Try a different approach.",
                tool_call.name
            ))
        } else {
            None
        }
    }

    /// Reset the detector state.
    #[allow(dead_code)]
    pub fn reset(&mut self) {
        self.last_hash = None;
        self.consecutive_count = 0;
    }
}

impl Default for RepetitionDetector {
    fn default() -> Self {
        Self::new(3)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn make_call(name: &str, args: serde_json::Value) -> ToolCall {
        ToolCall {
            id: "test-id".to_string(),
            name: name.to_string(),
            arguments: args,
        }
    }

    #[test]
    fn no_warning_below_threshold() {
        let mut detector = RepetitionDetector::new(3);
        let call = make_call("read", json!({"path": "/tmp/foo"}));

        // 1st, 2nd, 3rd call — should all be None (threshold is 3, warn on >3)
        assert!(detector.record(&call).is_none());
        assert!(detector.record(&call).is_none());
        assert!(detector.record(&call).is_none());
    }

    #[test]
    fn warning_at_threshold() {
        let mut detector = RepetitionDetector::new(3);
        let call = make_call("read", json!({"path": "/tmp/foo"}));

        // Calls 1-3: no warning
        for _ in 0..3 {
            assert!(detector.record(&call).is_none());
        }

        // 4th consecutive call: should trigger warning
        let warning = detector.record(&call);
        assert!(warning.is_some());
        assert!(warning.unwrap().contains("repeating the same tool call"));
    }

    #[test]
    fn different_call_resets_counter() {
        let mut detector = RepetitionDetector::new(3);
        let call_a = make_call("read", json!({"path": "/tmp/foo"}));
        let call_b = make_call("write", json!({"path": "/tmp/bar", "content": "hi"}));

        // Two consecutive calls of A
        assert!(detector.record(&call_a).is_none());
        assert!(detector.record(&call_a).is_none());

        // Different call resets
        assert!(detector.record(&call_b).is_none());

        // Back to A — counter restarts from 1
        assert!(detector.record(&call_a).is_none());
        assert!(detector.record(&call_a).is_none());
        assert!(detector.record(&call_a).is_none());

        // 4th consecutive A after reset: triggers
        let warning = detector.record(&call_a);
        assert!(warning.is_some());
    }

    #[test]
    fn same_tool_different_args_resets_counter() {
        let mut detector = RepetitionDetector::new(3);
        let call_a = make_call("read", json!({"path": "/tmp/foo"}));
        let call_b = make_call("read", json!({"path": "/tmp/bar"}));

        // 3 calls of A
        for _ in 0..3 {
            assert!(detector.record(&call_a).is_none());
        }

        // Same tool name, different args — resets
        assert!(detector.record(&call_b).is_none());

        // A again — counter restarts
        assert!(detector.record(&call_a).is_none());
        assert!(detector.record(&call_a).is_none());
        assert!(detector.record(&call_a).is_none());

        // 4th triggers
        assert!(detector.record(&call_a).is_some());
    }

    #[test]
    fn warning_resets_state() {
        let mut detector = RepetitionDetector::new(2);
        let call = make_call("bash", json!({"command": "echo hello"}));

        // 1st and 2nd: no warning
        assert!(detector.record(&call).is_none());
        assert!(detector.record(&call).is_none());

        // 3rd: warning fires
        assert!(detector.record(&call).is_some());

        // After warning, state is reset — 3 more before next warning
        assert!(detector.record(&call).is_none());
        assert!(detector.record(&call).is_none());
        assert!(detector.record(&call).is_some());
    }

    #[test]
    fn threshold_of_one() {
        let mut detector = RepetitionDetector::new(1);
        let call = make_call("grep", json!({"pattern": "TODO"}));

        // 1st call: no warning
        assert!(detector.record(&call).is_none());

        // 2nd consecutive call: warning
        assert!(detector.record(&call).is_some());
    }

    #[test]
    fn default_threshold_is_three() {
        let detector = RepetitionDetector::default();
        assert_eq!(detector.max_repetitions, 3);
    }

    #[test]
    fn warning_message_contains_tool_name() {
        let mut detector = RepetitionDetector::new(2);
        let call = make_call("edit", json!({"path": "/tmp/x", "old": "a", "new": "b"}));

        detector.record(&call);
        detector.record(&call);
        let warning = detector.record(&call).unwrap();
        assert!(warning.contains("`edit`"));
    }
}
