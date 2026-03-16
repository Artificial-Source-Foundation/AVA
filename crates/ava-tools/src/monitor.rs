use std::collections::HashMap;
use std::hash::{DefaultHasher, Hash, Hasher};
use std::time::{Duration, Instant};

use serde::{Deserialize, Serialize};

/// Records a single tool execution.
#[derive(Debug, Clone)]
pub struct ToolExecution {
    pub tool_name: String,
    pub arguments_hash: u64,
    pub success: bool,
    pub duration: Duration,
    pub timestamp: Instant,
}

/// Detected repetition pattern.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RepetitionPattern {
    pub tool_name: String,
    pub count: usize,
    pub pattern_type: RepetitionType,
}

/// Type of repetition detected.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RepetitionType {
    /// Same tool + same arguments repeated.
    ExactRepeat,
    /// Same tool called many times regardless of arguments.
    ToolLoop,
    /// Two tools alternating (A → B → A → B).
    AlternatingLoop,
}

/// Aggregate statistics for tool usage.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct ToolStats {
    pub total_calls: usize,
    pub unique_tools: usize,
    pub total_errors: usize,
    pub total_duration_ms: u64,
    /// (name, calls, errors)
    pub tool_breakdown: Vec<(String, usize, usize)>,
}

/// Tracks tool usage patterns and detects repetition loops.
#[derive(Debug)]
pub struct ToolMonitor {
    history: Vec<ToolExecution>,
    tool_counts: HashMap<String, usize>,
    error_counts: HashMap<String, usize>,
    total_duration: Duration,
}

impl Default for ToolMonitor {
    fn default() -> Self {
        Self::new()
    }
}

impl ToolMonitor {
    pub fn new() -> Self {
        Self {
            history: Vec::new(),
            tool_counts: HashMap::new(),
            error_counts: HashMap::new(),
            total_duration: Duration::ZERO,
        }
    }

    /// Record a tool execution.
    pub fn record(&mut self, execution: ToolExecution) {
        *self
            .tool_counts
            .entry(execution.tool_name.clone())
            .or_insert(0) += 1;
        if !execution.success {
            *self
                .error_counts
                .entry(execution.tool_name.clone())
                .or_insert(0) += 1;
        }
        self.total_duration += execution.duration;
        self.history.push(execution);

        // Cap history to prevent unbounded growth
        const MAX_HISTORY: usize = 1000;
        if self.history.len() > MAX_HISTORY {
            self.history.drain(..self.history.len() - MAX_HISTORY);
        }
    }

    /// Detect repetition patterns in recent tool history.
    pub fn detect_repetition(&self) -> Option<RepetitionPattern> {
        if self.history.len() < 3 {
            return None;
        }

        // Check exact repeat: same tool + same args 3 times in a row
        let last3 = &self.history[self.history.len() - 3..];
        if last3[0].tool_name == last3[1].tool_name
            && last3[1].tool_name == last3[2].tool_name
            && last3[0].arguments_hash == last3[1].arguments_hash
            && last3[1].arguments_hash == last3[2].arguments_hash
        {
            return Some(RepetitionPattern {
                tool_name: last3[0].tool_name.clone(),
                count: 3,
                pattern_type: RepetitionType::ExactRepeat,
            });
        }

        // Check alternating loop: A-B-A-B-A-B (3 cycles = 6 entries)
        if self.history.len() >= 6 {
            let tail = &self.history[self.history.len() - 6..];
            let a = &tail[0].tool_name;
            let b = &tail[1].tool_name;
            if a != b
                && tail[2].tool_name == *a
                && tail[3].tool_name == *b
                && tail[4].tool_name == *a
                && tail[5].tool_name == *b
            {
                return Some(RepetitionPattern {
                    tool_name: a.clone(),
                    count: 6,
                    pattern_type: RepetitionType::AlternatingLoop,
                });
            }
        }

        // Check tool loop: same tool 5 times regardless of args
        if self.history.len() >= 5 {
            let last5 = &self.history[self.history.len() - 5..];
            let name = &last5[0].tool_name;
            if last5.iter().all(|e| e.tool_name == *name) {
                return Some(RepetitionPattern {
                    tool_name: name.clone(),
                    count: 5,
                    pattern_type: RepetitionType::ToolLoop,
                });
            }
        }

        None
    }

    /// Get aggregate statistics.
    pub fn stats(&self) -> ToolStats {
        let mut breakdown: Vec<(String, usize, usize)> = self
            .tool_counts
            .iter()
            .map(|(name, &count)| {
                let errors = self.error_counts.get(name).copied().unwrap_or(0);
                (name.clone(), count, errors)
            })
            .collect();
        breakdown.sort_by(|a, b| b.1.cmp(&a.1));

        ToolStats {
            total_calls: self.history.len(),
            unique_tools: self.tool_counts.len(),
            total_errors: self.error_counts.values().sum(),
            total_duration_ms: self.total_duration.as_millis() as u64,
            tool_breakdown: breakdown,
        }
    }

    /// Return tools sorted by usage count (descending).
    pub fn most_used(&self) -> Vec<(&str, usize)> {
        let mut entries: Vec<(&str, usize)> = self
            .tool_counts
            .iter()
            .map(|(name, &count)| (name.as_str(), count))
            .collect();
        entries.sort_by(|a, b| b.1.cmp(&a.1));
        entries
    }

    /// Error rate for a specific tool (0.0–1.0).
    pub fn error_rate(&self, tool_name: &str) -> f64 {
        let total = self.tool_counts.get(tool_name).copied().unwrap_or(0);
        if total == 0 {
            return 0.0;
        }
        let errors = self.error_counts.get(tool_name).copied().unwrap_or(0);
        errors as f64 / total as f64
    }

    /// Error rate over the last N calls (across all tools).
    pub fn recent_error_rate(&self, last_n: usize) -> f64 {
        if self.history.is_empty() {
            return 0.0;
        }
        let start = self.history.len().saturating_sub(last_n);
        let recent = &self.history[start..];
        if recent.is_empty() {
            return 0.0;
        }
        let errors = recent.iter().filter(|e| !e.success).count();
        errors as f64 / recent.len() as f64
    }

    /// Number of executions recorded.
    pub fn len(&self) -> usize {
        self.history.len()
    }

    /// Whether any executions have been recorded.
    pub fn is_empty(&self) -> bool {
        self.history.is_empty()
    }
}

/// Hash tool arguments to a u64 for dedup detection.
pub fn hash_arguments(args: &serde_json::Value) -> u64 {
    let mut hasher = DefaultHasher::new();
    args.to_string().hash(&mut hasher);
    hasher.finish()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn exec(name: &str, args_hash: u64, success: bool) -> ToolExecution {
        ToolExecution {
            tool_name: name.to_string(),
            arguments_hash: args_hash,
            success,
            duration: Duration::from_millis(10),
            timestamp: Instant::now(),
        }
    }

    #[test]
    fn records_and_counts() {
        let mut monitor = ToolMonitor::new();
        monitor.record(exec("read", 1, true));
        monitor.record(exec("read", 2, true));
        monitor.record(exec("write", 3, false));

        assert_eq!(monitor.len(), 3);
        assert_eq!(monitor.stats().total_calls, 3);
        assert_eq!(monitor.stats().unique_tools, 2);
        assert_eq!(monitor.stats().total_errors, 1);
    }

    #[test]
    fn detects_exact_repeat() {
        let mut monitor = ToolMonitor::new();
        for _ in 0..3 {
            monitor.record(exec("read", 42, true));
        }
        let pattern = monitor.detect_repetition().expect("should detect");
        assert_eq!(pattern.pattern_type, RepetitionType::ExactRepeat);
        assert_eq!(pattern.tool_name, "read");
    }

    #[test]
    fn detects_tool_loop() {
        let mut monitor = ToolMonitor::new();
        for i in 0..5 {
            monitor.record(exec("read", i, true)); // different args each time
        }
        let pattern = monitor.detect_repetition().expect("should detect");
        assert_eq!(pattern.pattern_type, RepetitionType::ToolLoop);
    }

    #[test]
    fn detects_alternating_loop() {
        let mut monitor = ToolMonitor::new();
        for i in 0..3 {
            monitor.record(exec("read", i, true));
            monitor.record(exec("write", i + 100, true));
        }
        let pattern = monitor.detect_repetition().expect("should detect");
        assert_eq!(pattern.pattern_type, RepetitionType::AlternatingLoop);
    }

    #[test]
    fn no_pattern_with_varied_tools() {
        let mut monitor = ToolMonitor::new();
        monitor.record(exec("read", 1, true));
        monitor.record(exec("write", 2, true));
        monitor.record(exec("bash", 3, true));
        monitor.record(exec("glob", 4, true));
        monitor.record(exec("grep", 5, true));
        assert!(monitor.detect_repetition().is_none());
    }

    #[test]
    fn most_used_sorted() {
        let mut monitor = ToolMonitor::new();
        monitor.record(exec("read", 1, true));
        monitor.record(exec("write", 2, true));
        monitor.record(exec("read", 3, true));
        monitor.record(exec("read", 4, true));

        let most = monitor.most_used();
        assert_eq!(most[0], ("read", 3));
        assert_eq!(most[1], ("write", 1));
    }

    #[test]
    fn error_rate_calculation() {
        let mut monitor = ToolMonitor::new();
        monitor.record(exec("bash", 1, true));
        monitor.record(exec("bash", 2, false));
        monitor.record(exec("bash", 3, true));
        monitor.record(exec("bash", 4, false));

        assert!((monitor.error_rate("bash") - 0.5).abs() < f64::EPSILON);
        assert!((monitor.error_rate("unknown") - 0.0).abs() < f64::EPSILON);
    }

    #[test]
    fn recent_error_rate() {
        let mut monitor = ToolMonitor::new();
        // 5 successes then 5 errors
        for _ in 0..5 {
            monitor.record(exec("bash", 1, true));
        }
        for _ in 0..5 {
            monitor.record(exec("bash", 2, false));
        }

        // Last 5 should be 100% errors
        assert!((monitor.recent_error_rate(5) - 1.0).abs() < f64::EPSILON);
        // Last 10 should be 50% errors
        assert!((monitor.recent_error_rate(10) - 0.5).abs() < f64::EPSILON);
    }

    #[test]
    fn hash_arguments_deterministic() {
        let args = serde_json::json!({"path": "/tmp/test"});
        let h1 = hash_arguments(&args);
        let h2 = hash_arguments(&args);
        assert_eq!(h1, h2);
    }

    #[test]
    fn exact_repeat_takes_priority_over_tool_loop() {
        // 5 calls with same tool AND same args — should detect ExactRepeat (checked first)
        let mut monitor = ToolMonitor::new();
        for _ in 0..5 {
            monitor.record(exec("read", 42, true));
        }
        let pattern = monitor.detect_repetition().expect("should detect");
        assert_eq!(pattern.pattern_type, RepetitionType::ExactRepeat);
    }
}
