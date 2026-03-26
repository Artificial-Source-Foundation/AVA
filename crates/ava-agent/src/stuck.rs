use ava_tools::monitor::ToolMonitor;
use ava_types::{TokenUsage, ToolCall, ToolResult};

use crate::agent_loop::AgentConfig;
use crate::llm_trait::LLMProvider;

/// Action the stuck detector recommends.
pub enum StuckAction {
    Continue,
    InjectMessage(String),
    Stop(String),
    /// Layer 3: request an async LLM-as-judge evaluation.
    /// Contains a summary of recent responses for the judge prompt.
    NeedsJudge(String),
}

/// Thresholds for loop detection, adjusted per model tier.
#[derive(Debug, Clone)]
pub struct LoopThresholds {
    /// Layer 1: consecutive identical tool calls before injection.
    pub tool_repeat_count: usize,
    /// Layer 2: consecutive identical/similar text responses before stop.
    pub text_repeat_count: usize,
    /// Layer 2: similarity ratio threshold (0.0-1.0) for "similar enough".
    pub text_similarity_threshold: f64,
    /// Consecutive empty responses before stop.
    pub empty_response_count: usize,
    /// Consecutive error turns before injection.
    pub error_loop_count: usize,
    /// Turns without tools before injection.
    pub stall_turn_count: usize,
    /// Whether to enable Layer 3 (LLM-as-judge).
    pub enable_llm_judge: bool,
    /// Minimum turns before LLM-as-judge can fire.
    pub llm_judge_min_turns: usize,
}

impl LoopThresholds {
    /// Aggressive thresholds for loop-prone models (Chinese, fast inference).
    pub fn aggressive() -> Self {
        Self {
            tool_repeat_count: 2,
            text_repeat_count: 2,
            text_similarity_threshold: 0.85,
            empty_response_count: 1,
            error_loop_count: 2,
            stall_turn_count: 3,
            enable_llm_judge: true,
            llm_judge_min_turns: 4,
        }
    }

    /// Relaxed thresholds for SOTA models (Opus, GPT-5.4, etc).
    pub fn relaxed() -> Self {
        Self {
            tool_repeat_count: 3,
            text_repeat_count: 3,
            text_similarity_threshold: 0.95,
            empty_response_count: 2,
            error_loop_count: 3,
            stall_turn_count: 5,
            enable_llm_judge: false,
            llm_judge_min_turns: 8,
        }
    }

    /// Choose thresholds based on model name using the registry.
    pub fn for_model(model: &str) -> Self {
        let registry = ava_config::model_catalog::registry::registry();
        if registry.is_loop_prone(model) {
            Self::aggressive()
        } else {
            Self::relaxed()
        }
    }

    /// Choose thresholds with 3-tier priority:
    /// 1. Provider credential `loop_prone` override (user config — highest priority)
    /// 2. Model registry `loop_prone` flag (compiled-in default)
    /// 3. Name-based heuristic fallback
    pub fn for_provider_model(provider: &str, model: &str) -> Self {
        // Tier 1: Check provider credential config for user override
        if !provider.is_empty() {
            if let Some(override_val) = Self::provider_loop_prone_override(provider) {
                return if override_val {
                    Self::aggressive()
                } else {
                    Self::relaxed()
                };
            }
        }

        // Tier 2+3: Fall through to model registry + heuristics
        Self::for_model(model)
    }

    /// Check `~/.ava/credentials.json` for a provider-level `loop_prone` override.
    fn provider_loop_prone_override(provider: &str) -> Option<bool> {
        let home = std::env::var("HOME").ok()?;
        let cred_path = std::path::PathBuf::from(home)
            .join(".ava")
            .join("credentials.json");
        let content = std::fs::read_to_string(cred_path).ok()?;
        let store: ava_config::CredentialStore = serde_json::from_str(&content).ok()?;
        store.get(provider)?.loop_prone
    }
}

impl Default for LoopThresholds {
    fn default() -> Self {
        Self::relaxed()
    }
}

/// Tracks stuck/loop detection state across turns.
///
/// 3-layer architecture:
/// - Layer 1: Tool call hash (same tool + same args repeated N times)
/// - Layer 2: Content similarity (identical or highly similar text responses)
/// - Layer 3: LLM-as-judge (ask same model "is this stuck?" — only for loop-prone models)
pub struct StuckDetector {
    empty_count: usize,
    last_responses: Vec<String>,
    last_tool_calls: Vec<(String, String)>,
    consecutive_errors: usize,
    estimated_cost: f64,
    turn_count: usize,
    turns_without_tools_or_completion: usize,
    tool_monitor: ToolMonitor,
    thresholds: LoopThresholds,
    /// Accumulated concern score from Layer 1+2 detections.
    /// When this reaches 3, Layer 3 fires (if enabled).
    llm_judge_concern: u8,
    /// Prevent repeated judge calls — fire at most once.
    llm_judge_fired: bool,
}

impl StuckDetector {
    pub fn new() -> Self {
        Self::with_thresholds(LoopThresholds::relaxed())
    }

    pub fn with_thresholds(thresholds: LoopThresholds) -> Self {
        Self {
            empty_count: 0,
            last_responses: Vec::new(),
            last_tool_calls: Vec::new(),
            consecutive_errors: 0,
            estimated_cost: 0.0,
            turn_count: 0,
            turns_without_tools_or_completion: 0,
            tool_monitor: ToolMonitor::new(),
            thresholds,
            llm_judge_concern: 0,
            llm_judge_fired: false,
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

    /// Build a summary of recent responses for the LLM judge.
    fn build_judge_context(&self) -> String {
        let responses: Vec<&str> = self
            .last_responses
            .iter()
            .map(|s| if s.len() > 200 { &s[..200] } else { s.as_str() })
            .collect();
        format!(
            "Recent {} responses (truncated):\n{}",
            responses.len(),
            responses
                .iter()
                .enumerate()
                .map(|(i, r)| format!("  [{}]: {}", i + 1, r))
                .collect::<Vec<_>>()
                .join("\n")
        )
    }

    /// Check all stuck detection scenarios. Returns the recommended action.
    ///
    /// Layer 1: Tool call hash (scenarios 3, 6)
    /// Layer 2: Content similarity (scenarios 1, 2)
    /// Layer 3: LLM-as-judge trigger (accumulated concern)
    /// Plus: cost threshold (5), error loop (4), high error rate (7), stalled progress (8)
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

        // 1. Empty response detection
        if response_text.trim().is_empty() && tool_calls.is_empty() {
            self.empty_count += 1;
            if self.empty_count >= self.thresholds.empty_response_count {
                return StuckAction::Stop(format!(
                    "Stopping: {} consecutive empty responses",
                    self.empty_count
                ));
            }
            return StuckAction::Continue;
        }
        self.empty_count = 0;

        // 2. Layer 2: Content similarity detection
        let trimmed = response_text.trim().to_string();
        if !trimmed.is_empty() {
            self.last_responses.push(trimmed);
            let window = self.thresholds.text_repeat_count;
            if self.last_responses.len() > window {
                self.last_responses.remove(0);
            }
            if self.last_responses.len() == window {
                // Check exact match first
                let all_identical = self.last_responses.windows(2).all(|w| w[0] == w[1]);
                if all_identical {
                    return StuckAction::Stop(format!(
                        "Stopping: model returned identical response {} times in a row",
                        window
                    ));
                }
                // Check similarity (Layer 2 enhancement)
                let all_similar = self.last_responses.windows(2).all(|w| {
                    let ratio = similar::TextDiff::from_chars(&w[0], &w[1]).ratio() as f64;
                    ratio >= self.thresholds.text_similarity_threshold
                });
                if all_similar {
                    self.llm_judge_concern = self.llm_judge_concern.saturating_add(2);
                    return StuckAction::InjectMessage(
                        "Your recent responses are very similar. Try a substantially different approach."
                            .to_string(),
                    );
                }
            }
        }

        // 3. Layer 1: Tool call loop detection
        if tool_calls.len() == 1 {
            let call = &tool_calls[0];
            let sig = (call.name.clone(), call.arguments.to_string());
            if self.last_tool_calls.last().is_some_and(|last| *last == sig) {
                self.last_tool_calls.push(sig);
            } else {
                self.last_tool_calls.clear();
                self.last_tool_calls.push(sig);
            }
            if self.last_tool_calls.len() >= self.thresholds.tool_repeat_count {
                self.last_tool_calls.clear();
                self.llm_judge_concern = self.llm_judge_concern.saturating_add(1);
                return StuckAction::InjectMessage(
                    "You're repeating the same action. Try a different approach.".to_string(),
                );
            }
        } else {
            self.last_tool_calls.clear();
        }

        // 4. Error loop detection
        let all_errors = !tool_results.is_empty() && tool_results.iter().all(|r| r.is_error);
        if all_errors {
            self.consecutive_errors += 1;
            if self.consecutive_errors >= self.thresholds.error_loop_count {
                self.consecutive_errors = 0;
                self.llm_judge_concern = self.llm_judge_concern.saturating_add(1);
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
            self.llm_judge_concern = self.llm_judge_concern.saturating_add(1);
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

        // 8. Stalled progress
        if tool_calls.is_empty() {
            self.turns_without_tools_or_completion += 1;
            if self.turns_without_tools_or_completion >= self.thresholds.stall_turn_count {
                self.turns_without_tools_or_completion = 0;
                return StuckAction::InjectMessage(
                    "Are you making progress? If stuck, try a different approach.".to_string(),
                );
            }
        } else {
            self.turns_without_tools_or_completion = 0;
        }

        // Layer 3: LLM-as-judge trigger
        if self.thresholds.enable_llm_judge
            && !self.llm_judge_fired
            && self.turn_count >= self.thresholds.llm_judge_min_turns
            && self.llm_judge_concern >= 3
        {
            self.llm_judge_fired = true;
            let summary = self.build_judge_context();
            return StuckAction::NeedsJudge(summary);
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
            provider: "mock".to_string(),
            model: "mock".to_string(),
            max_cost_usd,
            loop_detection,
            custom_system_prompt: None,
            thinking_level: ava_types::ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: None,
            project_root: None,
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: crate::agent_loop::LLM_STREAM_TIMEOUT_SECS,
            prompt_caching: true,
        }
    }

    fn mock_llm() -> std::sync::Arc<dyn LLMProvider> {
        crate::tests::mock_llm()
    }

    // ── Existing scenarios (relaxed thresholds) ────────────────────────────

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

    // ── Aggressive thresholds (loop-prone models) ──────────────────────────

    #[test]
    fn aggressive_empty_response_fires_on_first() {
        let mut detector = StuckDetector::with_thresholds(LoopThresholds::aggressive());
        let config = make_config(1.0, true);
        let llm = mock_llm();

        // Aggressive: empty_response_count = 1, so first empty fires
        let action = detector.check("", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn aggressive_identical_response_fires_on_second() {
        let mut detector = StuckDetector::with_thresholds(LoopThresholds::aggressive());
        let config = make_config(10.0, true);
        let llm = mock_llm();

        // Aggressive: text_repeat_count = 2
        let action = detector.check("same", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Continue));

        let action = detector.check("same", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn aggressive_tool_loop_fires_on_second() {
        let mut detector = StuckDetector::with_thresholds(LoopThresholds::aggressive());
        let config = make_config(10.0, true);
        let llm = mock_llm();

        let call = ToolCall {
            id: "1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({"path": "/tmp/test"}),
        };

        // Aggressive: tool_repeat_count = 2
        let action = detector.check(
            "r0",
            std::slice::from_ref(&call),
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::Continue));

        let action = detector.check(
            "r1",
            std::slice::from_ref(&call),
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::InjectMessage(_)));
    }

    // ── Layer 2: Similarity detection ──────────────────────────────────────

    #[test]
    fn similar_responses_trigger_injection() {
        let mut detector = StuckDetector::with_thresholds(LoopThresholds {
            text_repeat_count: 3,
            text_similarity_threshold: 0.8,
            ..LoopThresholds::relaxed()
        });
        let config = make_config(10.0, true);
        let llm = mock_llm();

        // Three very similar but not identical responses
        detector.check(
            "I'll try to fix the bug in parser.rs by editing line 42",
            &[],
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        detector.check(
            "I'll try to fix the bug in parser.rs by editing line 43",
            &[],
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        let action = detector.check(
            "I'll try to fix the bug in parser.rs by editing line 44",
            &[],
            &[],
            None,
            &config,
            llm.as_ref(),
        );

        assert!(
            matches!(action, StuckAction::InjectMessage(ref msg) if msg.contains("similar")),
            "similar responses should trigger injection"
        );
    }

    #[test]
    fn dissimilar_responses_do_not_trigger() {
        let mut detector = StuckDetector::with_thresholds(LoopThresholds {
            text_repeat_count: 3,
            text_similarity_threshold: 0.95,
            ..LoopThresholds::relaxed()
        });
        let config = make_config(10.0, true);
        let llm = mock_llm();

        // Three completely different responses
        detector.check(
            "First I need to read the config file",
            &[],
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        detector.check(
            "Now let me update the database schema",
            &[],
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        let action = detector.check(
            "Finally I'll run the test suite to verify",
            &[],
            &[],
            None,
            &config,
            llm.as_ref(),
        );

        assert!(matches!(action, StuckAction::Continue));
    }

    // ── Layer 3: LLM-as-judge trigger ──────────────────────────────────────

    #[test]
    fn llm_judge_fires_after_accumulated_concern() {
        // Directly set concern and turn count to test the trigger condition
        let mut detector = StuckDetector::with_thresholds(LoopThresholds {
            enable_llm_judge: true,
            llm_judge_min_turns: 1,
            ..LoopThresholds::aggressive()
        });
        let config = make_config(10.0, true);
        let llm = mock_llm();

        // Simulate accumulated concern from earlier Layer 1/2 detections
        detector.llm_judge_concern = 3;
        detector.turn_count = 3;

        // This check should see concern >= 3 and turn_count >= min_turns → NeedsJudge
        let action = detector.check("test", &[], &[], None, &config, llm.as_ref());
        assert!(
            matches!(action, StuckAction::NeedsJudge(_)),
            "should have triggered NeedsJudge after accumulated concern"
        );
    }

    #[test]
    fn llm_judge_does_not_fire_when_disabled() {
        let mut detector = StuckDetector::with_thresholds(LoopThresholds {
            enable_llm_judge: false,
            ..LoopThresholds::relaxed()
        });
        // Manually set concern high
        detector.llm_judge_concern = 10;
        detector.turn_count = 20;

        let config = make_config(10.0, true);
        let llm = mock_llm();

        let action = detector.check("test", &[], &[], None, &config, llm.as_ref());
        assert!(!matches!(action, StuckAction::NeedsJudge(_)));
    }

    #[test]
    fn llm_judge_fires_at_most_once() {
        let mut detector = StuckDetector::with_thresholds(LoopThresholds {
            enable_llm_judge: true,
            llm_judge_min_turns: 1,
            ..LoopThresholds::aggressive()
        });
        detector.llm_judge_concern = 5;
        detector.turn_count = 5;

        let config = make_config(10.0, true);
        let llm = mock_llm();

        // First check should trigger NeedsJudge
        let action = detector.check("test1", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::NeedsJudge(_)));

        // Second check should NOT trigger again
        detector.llm_judge_concern = 10; // Even with high concern
        let action = detector.check("test2", &[], &[], None, &config, llm.as_ref());
        assert!(!matches!(action, StuckAction::NeedsJudge(_)));
    }

    // ── LoopThresholds::for_model ──────────────────────────────────────────

    #[test]
    fn for_model_returns_aggressive_for_loop_prone() {
        let t = LoopThresholds::for_model("glm-4.7");
        assert_eq!(t.tool_repeat_count, 2);
        assert!(t.enable_llm_judge);
    }

    #[test]
    fn for_model_returns_relaxed_for_sota() {
        let t = LoopThresholds::for_model("claude-opus-4.6");
        assert_eq!(t.tool_repeat_count, 3);
        assert!(!t.enable_llm_judge);
    }
}
