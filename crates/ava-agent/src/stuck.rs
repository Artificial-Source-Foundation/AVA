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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DoomLoopAction {
    Nudge,
    NudgeTwiceThenStop,
    StopImmediately,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum ScenarioClass {
    TextSimilarity = 0,
    ToolRepeat = 1,
    ErrorLoop = 2,
    AlternatingPattern = 3,
    HighErrorRate = 4,
    Stall = 5,
}

#[derive(Debug, Clone)]
pub struct DoomLoopPolicy {
    pub on_text_similarity: DoomLoopAction,
    pub on_tool_repeat: DoomLoopAction,
    pub on_error_loop: DoomLoopAction,
    pub on_alternating_pattern: DoomLoopAction,
    pub on_high_error_rate: DoomLoopAction,
    pub on_stall: DoomLoopAction,
}

impl DoomLoopPolicy {
    pub fn relaxed() -> Self {
        Self {
            on_text_similarity: DoomLoopAction::Nudge,
            on_tool_repeat: DoomLoopAction::Nudge,
            on_error_loop: DoomLoopAction::Nudge,
            on_alternating_pattern: DoomLoopAction::Nudge,
            on_high_error_rate: DoomLoopAction::Nudge,
            on_stall: DoomLoopAction::Nudge,
        }
    }

    pub fn aggressive() -> Self {
        Self {
            on_text_similarity: DoomLoopAction::NudgeTwiceThenStop,
            on_tool_repeat: DoomLoopAction::NudgeTwiceThenStop,
            on_error_loop: DoomLoopAction::NudgeTwiceThenStop,
            on_alternating_pattern: DoomLoopAction::NudgeTwiceThenStop,
            on_high_error_rate: DoomLoopAction::NudgeTwiceThenStop,
            on_stall: DoomLoopAction::NudgeTwiceThenStop,
        }
    }

    fn for_scenario(&self, scenario: ScenarioClass) -> DoomLoopAction {
        match scenario {
            ScenarioClass::TextSimilarity => self.on_text_similarity,
            ScenarioClass::ToolRepeat => self.on_tool_repeat,
            ScenarioClass::ErrorLoop => self.on_error_loop,
            ScenarioClass::AlternatingPattern => self.on_alternating_pattern,
            ScenarioClass::HighErrorRate => self.on_high_error_rate,
            ScenarioClass::Stall => self.on_stall,
        }
    }
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
    /// Action policy for nudge-capable loop scenarios.
    pub policy: DoomLoopPolicy,
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
            policy: DoomLoopPolicy::aggressive(),
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
            policy: DoomLoopPolicy::relaxed(),
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

        // Tier 2: Explicit Alibaba-hosted Qwen coverage.
        // Keep this scoped to provider+family so we do not blanket-match provider.
        if Self::is_alibaba_hosted_qwen(provider, model) {
            return Self::aggressive();
        }

        // Tier 2+3: Fall through to model registry + heuristics
        Self::for_model(model)
    }

    fn is_alibaba_hosted_qwen(provider: &str, model: &str) -> bool {
        let provider = provider.to_lowercase();
        let model = model.to_lowercase();

        let is_alibaba_provider = matches!(provider.as_str(), "alibaba" | "alibaba-cn");
        if !is_alibaba_provider {
            return false;
        }

        matches!(
            model.as_str(),
            "qwen3.5-plus" | "qwen3-max-2026-01-23" | "qwen3-coder-next" | "qwen3-coder-plus"
        ) || model.contains("qwen")
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
    /// Cooldown counter after an InjectMessage: skip this many checks to avoid
    /// cascading nudge → acknowledgment → nudge loops.
    inject_cooldown: usize,
    /// Tracks whether a scenario has already nudged once and should now stop.
    nudge_counts: [u8; 6],
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
            inject_cooldown: 0,
            nudge_counts: [0; 6],
        }
    }

    fn resolve_action(&mut self, scenario: ScenarioClass, msg: String) -> StuckAction {
        match self.thresholds.policy.for_scenario(scenario) {
            DoomLoopAction::Nudge => StuckAction::InjectMessage(msg),
            DoomLoopAction::StopImmediately => StuckAction::Stop(msg),
            DoomLoopAction::NudgeTwiceThenStop => {
                let slot = &mut self.nudge_counts[scenario as usize];
                *slot += 1;
                if *slot > 2 {
                    StuckAction::Stop(msg)
                } else {
                    StuckAction::InjectMessage(msg)
                }
            }
        }
    }

    fn observe_recovery(&mut self, tool_results: &[ToolResult]) {
        let had_non_error_result = tool_results.iter().any(|result| !result.is_error);
        if had_non_error_result {
            self.nudge_counts[ScenarioClass::ToolRepeat as usize] = 0;
            self.nudge_counts[ScenarioClass::ErrorLoop as usize] = 0;
            self.nudge_counts[ScenarioClass::AlternatingPattern as usize] = 0;
            self.nudge_counts[ScenarioClass::HighErrorRate as usize] = 0;
        }
    }

    fn resolve_action_with_cooldown(
        &mut self,
        scenario: ScenarioClass,
        msg: String,
        cooldown_active: bool,
    ) -> StuckAction {
        let action = self.resolve_action(scenario, msg);
        if cooldown_active {
            match action {
                StuckAction::InjectMessage(_) => StuckAction::Continue,
                other => other,
            }
        } else {
            action
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
        let responses: Vec<String> = self
            .last_responses
            .iter()
            .map(|s| s.chars().take(200).collect::<String>())
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

        // 1. Empty response detection must still work during cooldown.
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

        // 2. Cost threshold must still work during cooldown.
        if self.estimated_cost > config.max_cost_usd {
            return StuckAction::Stop(format!(
                "Stopping: estimated cost ${:.2} exceeds limit ${:.2}",
                self.estimated_cost, config.max_cost_usd
            ));
        }

        // During cooldown we still observe patterns, but suppress additional
        // nudge-style actions so state can progress toward an eventual stop.
        let cooldown_active = if self.inject_cooldown > 0 {
            self.inject_cooldown -= 1;
            true
        } else {
            false
        };

        // 3. Layer 2: Content similarity detection
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
                    return self.resolve_action_with_cooldown(
                        ScenarioClass::TextSimilarity,
                        "Your recent responses are very similar. Try a substantially different approach."
                            .to_string(),
                        cooldown_active,
                    );
                }
            }
        }

        // 4. Layer 1: Tool call loop detection
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
                return self.resolve_action_with_cooldown(
                    ScenarioClass::ToolRepeat,
                    "You're repeating the same action. Try a different approach.".to_string(),
                    cooldown_active,
                );
            }
        } else {
            self.last_tool_calls.clear();
        }

        // 5. Error loop detection
        let all_errors = !tool_results.is_empty() && tool_results.iter().all(|r| r.is_error);
        if all_errors {
            self.consecutive_errors += 1;
            if self.consecutive_errors >= self.thresholds.error_loop_count {
                self.consecutive_errors = 0;
                self.llm_judge_concern = self.llm_judge_concern.saturating_add(1);
                return self.resolve_action_with_cooldown(
                    ScenarioClass::ErrorLoop,
                    "Multiple tool errors detected. Reconsider your approach.".to_string(),
                    cooldown_active,
                );
            }
        } else if !tool_results.is_empty() {
            self.consecutive_errors = 0;
        }

        // 6. Alternating tool pattern (from ToolMonitor)
        if let Some(pattern) = self.tool_monitor.detect_repetition() {
            if !cooldown_active {
                self.llm_judge_concern = self.llm_judge_concern.saturating_add(1);
            }
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
            return self.resolve_action_with_cooldown(
                ScenarioClass::AlternatingPattern,
                msg,
                cooldown_active,
            );
        }

        // 7. High error rate (>50% of last 10 calls)
        if self.tool_monitor.len() >= 10 && self.tool_monitor.recent_error_rate(10) > 0.5 {
            return self.resolve_action_with_cooldown(
                ScenarioClass::HighErrorRate,
                "High error rate detected in recent tool calls. Step back and reconsider your approach.".to_string(),
                cooldown_active,
            );
        }

        // 8. Stalled progress
        if tool_calls.is_empty() {
            self.turns_without_tools_or_completion += 1;
            if self.turns_without_tools_or_completion >= self.thresholds.stall_turn_count {
                self.turns_without_tools_or_completion = 0;
                return self.resolve_action_with_cooldown(
                    ScenarioClass::Stall,
                    "Are you making progress? If stuck, try a different approach.".to_string(),
                    cooldown_active,
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

        self.observe_recovery(tool_results);
        StuckAction::Continue
    }

    /// Wrapper around `check` that sets a cooldown after any `InjectMessage`
    /// to prevent cascading nudge → acknowledgment → nudge loops.
    pub fn check_with_cooldown(
        &mut self,
        response_text: &str,
        tool_calls: &[ToolCall],
        tool_results: &[ToolResult],
        usage: Option<&TokenUsage>,
        config: &AgentConfig,
        llm: &dyn LLMProvider,
    ) -> StuckAction {
        let action = self.check(response_text, tool_calls, tool_results, usage, config, llm);
        if matches!(
            action,
            StuckAction::InjectMessage(_) | StuckAction::NeedsJudge(_)
        ) {
            // Skip the next 3 checks so the model can recover without re-triggering
            self.inject_cooldown = 3;
        }
        action
    }

    pub fn start_inject_cooldown(&mut self) {
        self.inject_cooldown = 3;
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
            benchmark_prompt_override: None,
            project_root: None,
            enable_dynamic_rules: false,
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: crate::agent_loop::LLM_STREAM_TIMEOUT_SECS,
            prompt_caching: true,
            headless: false,
            is_subagent: false,
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
    fn aggressive_tool_loop_nudges_twice_then_stops() {
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

        let action = detector.check(
            "r2",
            std::slice::from_ref(&call),
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::Continue));

        let action = detector.check(
            "r3",
            std::slice::from_ref(&call),
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::InjectMessage(_)));

        let action = detector.check(
            "r4",
            std::slice::from_ref(&call),
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::Continue));

        let action = detector.check(
            "r5",
            std::slice::from_ref(&call),
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn relaxed_tool_loop_keeps_nudging() {
        let mut detector = StuckDetector::with_thresholds(LoopThresholds::relaxed());
        let config = make_config(10.0, true);
        let llm = mock_llm();

        let call = ToolCall {
            id: "1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({"path": "/tmp/test"}),
        };

        for idx in 0..2 {
            let action = detector.check(
                &format!("r{idx}"),
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

        let action = detector.check(
            "r3",
            std::slice::from_ref(&call),
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::Continue));
    }

    #[test]
    fn aggressive_tool_loop_escalates_under_check_with_cooldown() {
        let mut detector = StuckDetector::with_thresholds(LoopThresholds::aggressive());
        let config = make_config(10.0, true);
        let llm = mock_llm();

        let call = ToolCall {
            id: "1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({"path": "/tmp/test"}),
        };

        let mut actions = Vec::new();
        let mut cooldowns = Vec::new();
        for idx in 0..6 {
            let action = detector.check_with_cooldown(
                &format!("r{idx}"),
                std::slice::from_ref(&call),
                &[],
                None,
                &config,
                llm.as_ref(),
            );
            actions.push(action);
            cooldowns.push(detector.inject_cooldown);
        }

        assert!(matches!(actions[0], StuckAction::Continue));
        assert!(matches!(actions[1], StuckAction::InjectMessage(_)));
        assert!(matches!(actions[2], StuckAction::Continue));
        assert!(matches!(actions[3], StuckAction::Continue));
        assert!(matches!(actions[4], StuckAction::Continue));
        assert!(matches!(actions[5], StuckAction::Stop(_)));

        assert_eq!(cooldowns, vec![0, 3, 2, 1, 0, 0]);
    }

    #[test]
    fn successful_tool_result_only_resets_tool_scenario_nudges() {
        let mut detector = StuckDetector::with_thresholds(LoopThresholds {
            text_repeat_count: 2,
            text_similarity_threshold: 0.8,
            stall_turn_count: 100,
            enable_llm_judge: false,
            ..LoopThresholds::aggressive()
        });
        let config = make_config(10.0, true);
        let llm = mock_llm();

        // First text-similarity nudge (count = 1).
        let action = detector.check(
            "Alpha task: inspect parser branch for null handling",
            &[],
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::Continue));
        let action = detector.check(
            "Alpha task: inspect parser branch for null-handling",
            &[],
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::InjectMessage(_)));

        // Productive tool turn should not reset non-tool scenario counts.
        let ok_result = ToolResult {
            call_id: "ok-1".to_string(),
            content: "done".to_string(),
            is_error: false,
        };
        let action = detector.check(
            "Executed tool successfully",
            &[],
            &[ok_result],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::Continue));
        detector.last_responses.clear();

        // Second text-similarity nudge (count = 2 if not reset).
        let action = detector.check(
            "Beta task: run migration dry-run against staging schema",
            &[],
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::Continue));
        let action = detector.check(
            "Beta task: run migration dry run against staging schema",
            &[],
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::InjectMessage(_)));
        detector.last_responses.clear();

        // Third detection should now stop (NudgeTwiceThenStop path reached).
        let action = detector.check(
            "Gamma task: re-index vector cache before final verification",
            &[],
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::Continue));
        let action = detector.check(
            "Gamma task: re index vector cache before final verification",
            &[],
            &[],
            None,
            &config,
            llm.as_ref(),
        );
        assert!(matches!(action, StuckAction::Stop(_)));
    }

    #[test]
    fn alternating_pattern_does_not_build_judge_concern_during_cooldown() {
        let mut detector = StuckDetector::with_thresholds(LoopThresholds {
            enable_llm_judge: true,
            llm_judge_min_turns: 99,
            ..LoopThresholds::aggressive()
        });
        let config = make_config(10.0, true);
        let llm = mock_llm();

        use ava_tools::monitor::{hash_arguments, ToolExecution};
        use std::time::{Duration, Instant};

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

        let action = detector.check_with_cooldown("turn 1", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::InjectMessage(_)));
        let concern_after_first = detector.llm_judge_concern;
        assert_eq!(concern_after_first, 1);
        assert_eq!(detector.inject_cooldown, 3);

        let action = detector.check_with_cooldown("turn 2", &[], &[], None, &config, llm.as_ref());
        assert!(matches!(action, StuckAction::Continue));
        assert_eq!(detector.llm_judge_concern, concern_after_first);
        assert_eq!(detector.inject_cooldown, 2);
    }

    #[test]
    fn needs_judge_sets_cooldown_in_check_with_cooldown() {
        let mut detector = StuckDetector::with_thresholds(LoopThresholds {
            enable_llm_judge: true,
            llm_judge_min_turns: 1,
            ..LoopThresholds::aggressive()
        });
        detector.llm_judge_concern = 3;
        detector.turn_count = 3;

        let config = make_config(10.0, true);
        let llm = mock_llm();
        let action = detector.check_with_cooldown("test", &[], &[], None, &config, llm.as_ref());

        assert!(matches!(action, StuckAction::NeedsJudge(_)));
        assert_eq!(detector.inject_cooldown, 3);
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

    #[test]
    fn for_provider_model_returns_aggressive_for_kimi_k2p5() {
        let t = LoopThresholds::for_provider_model("kimi", "k2p5");
        assert_eq!(t.tool_repeat_count, 2);
        assert_eq!(t.text_repeat_count, 2);
        assert!(t.enable_llm_judge);
        assert_eq!(
            t.policy.on_text_similarity,
            DoomLoopAction::NudgeTwiceThenStop
        );
    }

    #[test]
    fn for_provider_model_returns_aggressive_for_alibaba_qwen_variants() {
        let variants = [
            "qwen3.5-plus",
            "qwen3-max-2026-01-23",
            "qwen3-coder-next",
            "qwen3-coder-plus",
            "Qwen3-Coder-Plus",
        ];

        for variant in variants {
            let t = LoopThresholds::for_provider_model("alibaba", variant);
            assert_eq!(t.tool_repeat_count, 2, "{variant} should be aggressive");
            assert!(t.enable_llm_judge, "{variant} should enable judge");
        }
    }

    #[test]
    fn for_provider_model_returns_aggressive_for_alibaba_cn_qwen() {
        let t = LoopThresholds::for_provider_model("alibaba-cn", "qwen3-coder-next");
        assert_eq!(t.tool_repeat_count, 2);
        assert!(t.enable_llm_judge);
    }

    #[test]
    fn for_provider_model_keeps_existing_alibaba_glm_kimi_minimax_behavior() {
        for model in ["glm-5", "kimi-k2.5", "MiniMax-M2.5"] {
            let t = LoopThresholds::for_provider_model("alibaba", model);
            assert_eq!(t.tool_repeat_count, 2, "{model} should remain aggressive");
            assert!(t.enable_llm_judge, "{model} should remain aggressive");
        }
    }

    #[test]
    fn for_provider_model_does_not_blanket_match_alibaba_provider() {
        let t = LoopThresholds::for_provider_model("alibaba", "claude-opus-4.6");
        assert_eq!(t.tool_repeat_count, 3);
        assert!(!t.enable_llm_judge);
    }

    #[test]
    fn kimi_loop_policy_nudges_twice_then_stops_on_repeated_similarity() {
        let mut detector =
            StuckDetector::with_thresholds(LoopThresholds::for_provider_model("kimi", "k2p5"));
        let config = make_config(10.0, true);
        let llm = mock_llm();

        let response_pairs = [
            (
                "I will inspect the parser path and patch the null branch",
                "I will inspect the parser path and patch the null-branch",
            ),
            (
                "I will inspect the routing path and patch the retry branch",
                "I will inspect the routing path and patch the retry-branch",
            ),
            (
                "I will inspect the timeout path and patch the fallback branch",
                "I will inspect the timeout path and patch the fallback-branch",
            ),
        ];

        for (idx, (first, second)) in response_pairs.into_iter().enumerate() {
            let action = detector.check(first, &[], &[], None, &config, llm.as_ref());
            assert!(
                matches!(action, StuckAction::Continue),
                "pair {idx} first turn should continue"
            );

            let action = detector.check(second, &[], &[], None, &config, llm.as_ref());
            if idx < 2 {
                assert!(
                    matches!(action, StuckAction::InjectMessage(_)),
                    "pair {idx} second turn should nudge"
                );
            } else {
                assert!(
                    matches!(action, StuckAction::Stop(_)),
                    "third repeated similarity should stop loop-prone kimi run"
                );
            }
            detector.last_responses.clear();
        }
    }

    #[test]
    fn alibaba_qwen_loop_policy_nudges_twice_then_stops_on_repeated_similarity() {
        let mut detector = StuckDetector::with_thresholds(LoopThresholds::for_provider_model(
            "alibaba",
            "qwen3-coder-plus",
        ));
        let config = make_config(10.0, true);
        let llm = mock_llm();

        let response_pairs = [
            (
                "I will inspect the parser path and patch the null branch",
                "I will inspect the parser path and patch the null-branch",
            ),
            (
                "I will inspect the routing path and patch the retry branch",
                "I will inspect the routing path and patch the retry-branch",
            ),
            (
                "I will inspect the timeout path and patch the fallback branch",
                "I will inspect the timeout path and patch the fallback-branch",
            ),
        ];

        for (idx, (first, second)) in response_pairs.into_iter().enumerate() {
            let action = detector.check(first, &[], &[], None, &config, llm.as_ref());
            assert!(
                matches!(action, StuckAction::Continue),
                "pair {idx} first turn should continue"
            );

            let action = detector.check(second, &[], &[], None, &config, llm.as_ref());
            if idx < 2 {
                assert!(
                    matches!(action, StuckAction::InjectMessage(_)),
                    "pair {idx} second turn should nudge"
                );
            } else {
                assert!(
                    matches!(action, StuckAction::Stop(_)),
                    "third repeated similarity should stop loop-prone alibaba qwen run"
                );
            }
            detector.last_responses.clear();
        }
    }
}
