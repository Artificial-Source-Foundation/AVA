use ava_tools::monitor::ToolMonitor;
use ava_types::{TokenUsage, ToolCall, ToolResult};

use crate::agent_loop::AgentConfig;
use crate::llm_trait::LLMProvider;

/// Action the stuck detector recommends.
pub enum StuckAction {
    Continue,
    InjectMessage(String),
    Stop(String),
}

/// Tracks stuck/loop detection state across turns.
pub struct StuckDetector {
    empty_count: usize,
    last_responses: Vec<String>,
    last_tool_calls: Vec<(String, String)>,
    consecutive_errors: usize,
    estimated_cost: f64,
    turn_count: usize,
    turns_without_tools_or_completion: usize,
    tool_monitor: ToolMonitor,
}

impl StuckDetector {
    pub fn new() -> Self {
        Self {
            empty_count: 0,
            last_responses: Vec::new(),
            last_tool_calls: Vec::new(),
            consecutive_errors: 0,
            estimated_cost: 0.0,
            turn_count: 0,
            turns_without_tools_or_completion: 0,
            tool_monitor: ToolMonitor::new(),
        }
    }

    /// Current estimated cost accumulated by this detector.
    pub fn estimated_cost(&self) -> f64 {
        self.estimated_cost
    }

    /// Access the internal tool monitor for stats.
    pub fn tool_monitor(&self) -> &ToolMonitor {
        &self.tool_monitor
    }

    /// Mutable access to the tool monitor (for recording executions externally).
    pub fn tool_monitor_mut(&mut self) -> &mut ToolMonitor {
        &mut self.tool_monitor
    }

    /// Check all stuck detection scenarios. Returns the recommended action.
    ///
    /// 8 scenarios:
    /// 1. Empty response (2 consecutive)
    /// 2. Identical response (3 consecutive)
    /// 3. Tool call loop (same tool + args 3 times)
    /// 4. Error loop (3 consecutive all-error turns)
    /// 5. Cost threshold exceeded
    /// 6. Alternating tool pattern (from ToolMonitor)
    /// 7. High error rate (>50% of last 10 calls)
    /// 8. Stalled progress (5 turns with no tools and no completion)
    pub fn check(
        &mut self,
        response_text: &str,
        tool_calls: &[ToolCall],
        tool_results: &[ToolResult],
        usage: Option<&TokenUsage>,
        config: &AgentConfig,
        llm: &dyn LLMProvider,
    ) -> StuckAction {
        if !config.loop_detection {
            return StuckAction::Continue;
        }

        self.turn_count += 1;

        // Update cost estimate
        let turn_cost = if let Some(usage) = usage {
            let conservative_input_tokens = usage
                .input_tokens
                .saturating_add(usage.cache_creation_tokens);
            llm.estimate_cost(conservative_input_tokens, usage.output_tokens)
        } else {
            let output_tokens = llm.estimate_tokens(response_text);
            llm.estimate_cost(0, output_tokens)
        };
        self.estimated_cost += turn_cost;

        // 1. Empty response detection (2 consecutive)
        if response_text.trim().is_empty() && tool_calls.is_empty() {
            self.empty_count += 1;
            if self.empty_count >= 2 {
                return StuckAction::Stop("Stopping: 2 consecutive empty responses".to_string());
            }
            return StuckAction::Continue;
        }
        self.empty_count = 0;

        // 2. Identical response detection (3 consecutive)
        let trimmed = response_text.trim().to_string();
        if !trimmed.is_empty() {
            self.last_responses.push(trimmed);
            if self.last_responses.len() > 3 {
                self.last_responses.remove(0);
            }
            if self.last_responses.len() == 3
                && self.last_responses[0] == self.last_responses[1]
                && self.last_responses[1] == self.last_responses[2]
            {
                return StuckAction::Stop(
                    "Stopping: model returned identical response 3 times in a row".to_string(),
                );
            }
        }

        // 3. Tool call loop detection (same tool + args 3 times)
        if tool_calls.len() == 1 {
            let call = &tool_calls[0];
            let sig = (call.name.clone(), call.arguments.to_string());
            if self.last_tool_calls.last().is_some_and(|last| *last == sig) {
                self.last_tool_calls.push(sig);
            } else {
                self.last_tool_calls.clear();
                self.last_tool_calls.push(sig);
            }
            if self.last_tool_calls.len() >= 3 {
                self.last_tool_calls.clear();
                return StuckAction::InjectMessage(
                    "You're repeating the same action. Try a different approach.".to_string(),
                );
            }
        } else {
            self.last_tool_calls.clear();
        }

        // 4. Error loop detection (3 consecutive tool errors)
        let all_errors = !tool_results.is_empty() && tool_results.iter().all(|r| r.is_error);
        if all_errors {
            self.consecutive_errors += 1;
            if self.consecutive_errors >= 3 {
                self.consecutive_errors = 0;
                return StuckAction::InjectMessage(
                    "Multiple tool errors detected. Reconsider your approach.".to_string(),
                );
            }
        } else if !tool_results.is_empty() {
            self.consecutive_errors = 0;
        }

        // 5. Cost threshold
        if self.estimated_cost > config.max_cost_usd {
            return StuckAction::Stop(format!(
                "Stopping: estimated cost ${:.2} exceeds limit ${:.2}",
                self.estimated_cost, config.max_cost_usd
            ));
        }

        // 6. Alternating tool pattern (from ToolMonitor)
        if let Some(pattern) = self.tool_monitor.detect_repetition() {
            let msg = match pattern.pattern_type {
                ava_tools::monitor::RepetitionType::ExactRepeat => {
                    format!(
                        "You've called '{}' with identical arguments {} times. Try a different approach.",
                        pattern.tool_name, pattern.count
                    )
                }
                ava_tools::monitor::RepetitionType::AlternatingLoop => {
                    format!(
                        "Detected alternating tool pattern involving '{}'. Break the cycle and try something new.",
                        pattern.tool_name
                    )
                }
                ava_tools::monitor::RepetitionType::ToolLoop => {
                    format!(
                        "You've called '{}' {} times in a row. Consider a different tool or approach.",
                        pattern.tool_name, pattern.count
                    )
                }
            };
            return StuckAction::InjectMessage(msg);
        }

        // 7. High error rate (>50% of last 10 calls)
        if self.tool_monitor.len() >= 10 && self.tool_monitor.recent_error_rate(10) > 0.5 {
            return StuckAction::InjectMessage(
                "High error rate detected in recent tool calls. Step back and reconsider your approach.".to_string(),
            );
        }

        // 8. Stalled progress (5 turns with no tool calls and no completion)
        if tool_calls.is_empty() {
            self.turns_without_tools_or_completion += 1;
            if self.turns_without_tools_or_completion >= 5 {
                self.turns_without_tools_or_completion = 0;
                return StuckAction::InjectMessage(
                    "Are you making progress? If stuck, try a different approach.".to_string(),
                );
            }
        } else {
            self.turns_without_tools_or_completion = 0;
        }

        StuckAction::Continue
    }
}

impl Default for StuckDetector {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_config(max_cost_usd: f64, loop_detection: bool) -> AgentConfig {
        AgentConfig {
            max_turns: 10,
            max_budget_usd: 0.0,
            token_limit: 128_000,
            model: "mock".to_string(),
            max_cost_usd,
            loop_detection,
            custom_system_prompt: None,
            thinking_level: ava_types::ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
        }
    }

    fn mock_llm() -> std::sync::Arc<dyn LLMProvider> {
        crate::tests::mock_llm()
    }

    #[test]
    fn empty_responses() {
        let mut detector = StuckDetector::new();
        let config = make_config(1.0, true);
        let llm = mock_llm();

        let action = detector.check("", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Continue));

        let action = detector.check("", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn identical_responses() {
        let mut detector = StuckDetector::new();
        let config = make_config(10.0, true);
        let llm = mock_llm();

        for i in 0..2 {
            let action = detector.check("same", &[], &[], None, &config, llm.as_ref());
            assert!(matches!(action, StuckAction::Continue), "iteration {i}");
        }

        let action = detector.check("same", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn tool_call_loop() {
        let mut detector = StuckDetector::new();
        let config = make_config(10.0, true);
        let llm = mock_llm();

        let call = ToolCall {
            id: "1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({"path": "/tmp/test"}),
        };

        for i in 0..2 {
            let action = detector.check(
                &format!("r{i}"),
                std::slice::from_ref(&call),
                &[],
                None,
                &config,
                llm.as_ref(),
            );
            assert!(matches!(action, StuckAction::Continue));
        }

        let action = detector.check(
            "r2",
            std::slice::from_ref(&call),
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::InjectMessage(_)));
    }

    #[test]
    fn error_loop() {
        let mut detector = StuckDetector::new();
        let config = make_config(10.0, true);
        let llm = mock_llm();

        let err = ToolResult {
            call_id: "1".to_string(),
            content: "fail".to_string(),
            is_error: true,
        };

        for i in 0..2 {
            let action = detector.check(
                &format!("e{i}"),
                &[],
                std::slice::from_ref(&err),
                None,
                &config,
                llm.as_ref(),
            );
            assert!(matches!(action, StuckAction::Continue));
        }

        let action = detector.check(
            "e2",
            &[],
            std::slice::from_ref(&err),
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::InjectMessage(_)));
    }

    #[test]
    fn cost_threshold() {
        let mut detector = StuckDetector::new();
        let config = make_config(0.0, true);
        let llm = mock_llm();

        let action = detector.check("hello", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn disabled() {
        let mut detector = StuckDetector::new();
        let config = make_config(0.0, false);
        let llm = mock_llm();

        let action = detector.check("hello", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Continue));
    }

    #[test]
    fn alternating_tool_pattern() {
        let mut detector = StuckDetector::new();
        let config = make_config(10.0, true);
        let llm = mock_llm();

        use ava_tools::monitor::{hash_arguments, ToolExecution};
        use std::time::{Duration, Instant};

        // Record A-B-A-B-A-B in the tool monitor
        for i in 0..3 {
            detector.tool_monitor_mut().record(ToolExecution {
                tool_name: "read".to_string(),
                arguments_hash: hash_arguments(&serde_json::json!({"i": i})),
                success: true,
                duration: Duration::from_millis(1),
                timestamp: Instant::now(),
            });
            detector.tool_monitor_mut().record(ToolExecution {
                tool_name: "write".to_string(),
                arguments_hash: hash_arguments(&serde_json::json!({"i": i})),
                success: true,
                duration: Duration::from_millis(1),
                timestamp: Instant::now(),
            });
        }

        let action = detector.check("checking", &[], &[], None, &config, llm.as_ref());
        assert!(
            matches!(action, StuckAction::InjectMessage(ref msg) if msg.contains("alternating"))
        );
    }

    #[test]
    fn high_error_rate() {
        let mut detector = StuckDetector::new();
        let config = make_config(10.0, true);
        let llm = mock_llm();

        use ava_tools::monitor::ToolExecution;
        use std::time::{Duration, Instant};

        // Record 10 calls, 6 of them errors (>50%)
        for i in 0..10 {
            detector.tool_monitor_mut().record(ToolExecution {
                tool_name: format!("tool_{i}"),
                arguments_hash: i as u64,
                success: i < 4, // first 4 succeed, last 6 fail
                duration: Duration::from_millis(1),
                timestamp: Instant::now(),
            });
        }

        let action = detector.check("checking", &[], &[], None, &config, llm.as_ref());
        assert!(
            matches!(action, StuckAction::InjectMessage(ref msg) if msg.contains("error rate"))
        );
    }

    #[test]
    fn stalled_progress() {
        let mut detector = StuckDetector::new();
        let config = make_config(10.0, true);
        let llm = mock_llm();

        // 5 turns with no tool calls — use different responses to avoid identical response detection
        for i in 0..4 {
            let action = detector.check(
                &format!("thinking {i}"),
                &[],
                &[],
                None,
                &config,
                llm.as_ref(),
            );
            assert!(matches!(action, StuckAction::Continue), "turn {i}");
        }

        let action = detector.check("thinking 4", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::InjectMessage(ref msg) if msg.contains("progress")));
    }

    #[test]
    fn cost_threshold_prefers_usage_over_response_text_estimate() {
        let mut detector = StuckDetector::new();
        let config = make_config(0.01, true);
        let llm = mock_llm();
        let response_text = "x".repeat(10_000);
        let usage = TokenUsage {
            input_tokens: 10,
            output_tokens: 20,
            cache_read_tokens: 0,
            cache_creation_tokens: 0,
        };

        let action = detector.check(
            &response_text,
            &[],
            &[],
            Some(&usage),
            &config,
            llm.as_ref(),
        );

        assert!(matches!(action, StuckAction::Continue));
        assert!(detector.estimated_cost() < 0.01);
    }
}
