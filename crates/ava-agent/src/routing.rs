use ava_config::RoutingProfile;
use ava_llm::RouteRequirements;
use ava_types::{ImageContent, Message, Role, ThinkingLevel};

pub const EXPLICIT_DELEGATION_REASON: &str =
    "prompt explicitly asks for delegation or specialist help";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ToolVisibilityProfile {
    Full,
    ReadOnly,
    AnswerOnly,
}

/// F17 — Effort level for a task, used to scale thinking budgets and sub-agent resources.
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub enum EffortLevel {
    /// Simple tasks: greetings, one-liners, quick lookups. Minimal thinking.
    Low,
    /// Standard tasks: single-file edits, focused debugging. Normal thinking.
    Medium,
    /// Complex tasks: multi-file refactors, architecture, deep analysis. Maximum thinking.
    High,
}

impl EffortLevel {
    /// Return the fraction of the configured thinking budget to use.
    pub fn thinking_budget_fraction(self) -> f64 {
        match self {
            EffortLevel::Low => 0.25,
            EffortLevel::Medium => 0.6,
            EffortLevel::High => 1.0,
        }
    }

    /// Scale a thinking budget by this effort level.
    pub fn scale_budget(self, budget: Option<u32>) -> Option<u32> {
        budget.map(|b| (b as f64 * self.thinking_budget_fraction()) as u32)
    }
}

#[derive(Debug, Clone)]
pub struct TaskRoutingIntent {
    pub profile: RoutingProfile,
    pub requirements: RouteRequirements,
    pub reasons: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SubagentDelegationPolicy {
    pub enable_subagent_tool: bool,
    pub max_subagents: usize,
    pub reason: String,
}

#[derive(Debug, Clone)]
pub struct TaskAnalysis {
    pub routing: TaskRoutingIntent,
    pub tool_visibility: ToolVisibilityProfile,
    pub delegation: SubagentDelegationPolicy,
    /// F17 — Estimated effort level for thinking budget scaling.
    pub effort: EffortLevel,
}

#[derive(Debug, Clone)]
struct TaskSignals {
    trimmed: String,
    lower: String,
    line_count: usize,
    has_images: bool,
    mentions_repo_read: bool,
    mentions_write: bool,
    explicit_delegate: bool,
    broad_task: bool,
    small_task: bool,
    file_reference_count: usize,
    capable_reasons: Vec<String>,
}

pub fn analyze_task_full(
    goal: &str,
    images: &[ImageContent],
    thinking: ThinkingLevel,
    plan_mode: bool,
) -> TaskAnalysis {
    analyze_task_full_with_history(goal, &[], images, thinking, plan_mode)
}

pub fn analyze_task_full_with_history(
    goal: &str,
    history: &[Message],
    images: &[ImageContent],
    thinking: ThinkingLevel,
    plan_mode: bool,
) -> TaskAnalysis {
    let signals = analyze_signals(goal, history, images, thinking, plan_mode);
    let routing = classify_routing(&signals, thinking, plan_mode);
    let tool_visibility = classify_tool_visibility(&signals, thinking, plan_mode);
    let delegation = classify_delegation(&signals, &tool_visibility);
    let effort = classify_effort(&signals);

    TaskAnalysis {
        routing,
        tool_visibility,
        delegation,
        effort,
    }
}

pub fn analyze_task(
    goal: &str,
    images: &[ImageContent],
    thinking: ThinkingLevel,
    plan_mode: bool,
) -> TaskRoutingIntent {
    analyze_task_full(goal, images, thinking, plan_mode).routing
}

pub fn infer_tool_visibility(
    goal: &str,
    images: &[ImageContent],
    thinking: ThinkingLevel,
    plan_mode: bool,
) -> ToolVisibilityProfile {
    analyze_task_full(goal, images, thinking, plan_mode).tool_visibility
}

pub fn infer_subagent_delegation(
    goal: &str,
    images: &[ImageContent],
    thinking: ThinkingLevel,
    plan_mode: bool,
) -> SubagentDelegationPolicy {
    analyze_task_full(goal, images, thinking, plan_mode).delegation
}

fn analyze_signals(
    goal: &str,
    history: &[Message],
    images: &[ImageContent],
    thinking: ThinkingLevel,
    plan_mode: bool,
) -> TaskSignals {
    let trimmed = goal.trim().to_string();
    let lower = trimmed.to_lowercase();
    let line_count = trimmed.lines().count();
    let has_images = !images.is_empty();

    let capable_keywords = [
        "debug",
        "investigate",
        "root cause",
        "review",
        "refactor",
        "architecture",
        "migrate",
        "performance",
        "security",
        "bug",
        "failing test",
        "design",
        "implement",
        "integration",
        "compare",
        "audit",
    ];
    let read_only_keywords = [
        "read",
        "find",
        "list",
        "show",
        "search",
        "grep",
        "glob",
        "package.json",
        "repo",
        "repository",
        "file",
        "directory",
    ];
    let write_keywords = [
        "edit",
        "write",
        "rename",
        "fix",
        "change",
        "update",
        "move",
        "delete",
        "remove",
        "create",
        "implement",
        "refactor",
        "migrate",
        "add",
    ];
    let broad_task_keywords = [
        "across files",
        "multiple files",
        "multi-file",
        "research",
        "investigate",
        "review",
        "architecture",
        "plan",
        "compare",
        "audit",
        "survey",
        "root cause",
        "wrapper",
        "integration",
    ];
    let small_task_keywords = [
        "rename",
        "typo",
        "comment",
        "doc comment",
        "single file",
        "one file",
    ];

    let mut capable_reasons = Vec::new();
    if has_images {
        capable_reasons.push("images attached; keep vision-capable route".to_string());
    }
    if plan_mode {
        capable_reasons.push("plan mode prefers a more capable model".to_string());
    }
    if thinking != ThinkingLevel::Off {
        capable_reasons.push("thinking mode is enabled".to_string());
    }
    if trimmed.len() > 700 || line_count > 8 {
        capable_reasons.push("prompt is long or multi-step".to_string());
    }
    if capable_keywords
        .iter()
        .any(|keyword| contains_keyword(&lower, keyword))
    {
        capable_reasons.push("task wording suggests deeper reasoning/coding work".to_string());
    }

    TaskSignals {
        mentions_repo_read: read_only_keywords
            .iter()
            .any(|keyword| contains_keyword(&lower, keyword)),
        mentions_write: write_keywords
            .iter()
            .any(|keyword| contains_keyword(&lower, keyword)),
        explicit_delegate: contains_explicit_delegate_keyword(&lower)
            || inherits_explicit_delegation_from_history(&lower, line_count, history),
        broad_task: broad_task_keywords
            .iter()
            .any(|keyword| lower.contains(keyword)),
        small_task: small_task_keywords
            .iter()
            .any(|keyword| lower.contains(keyword)),
        file_reference_count: [".rs", ".ts", ".tsx", ".js", ".jsx", ".py", ".go"]
            .iter()
            .map(|needle| lower.matches(needle).count())
            .sum::<usize>(),
        trimmed,
        lower,
        line_count,
        has_images,
        capable_reasons,
    }
}

fn contains_explicit_delegate_keyword(text: &str) -> bool {
    [
        "subagent",
        "sub-agent",
        "delegate",
        "parallel",
        "background agent",
        "scout",
        "reviewer",
        "planner",
        "worker",
    ]
    .iter()
    .any(|keyword| text.contains(keyword))
}

fn inherits_explicit_delegation_from_history(
    goal_lower: &str,
    line_count: usize,
    history: &[Message],
) -> bool {
    if !looks_like_short_follow_up(goal_lower, line_count) {
        return false;
    }

    history
        .iter()
        .rev()
        .find(|message| message.agent_visible && message.role == Role::User)
        .is_some_and(|message| contains_explicit_delegate_keyword(&message.content.to_lowercase()))
}

fn looks_like_short_follow_up(goal_lower: &str, line_count: usize) -> bool {
    if goal_lower.len() > 160 || line_count > 3 {
        return false;
    }

    [
        "go ahead",
        "continue",
        "do it",
        "do that",
        "implement it",
        "implement the fix",
        "fix it",
        "apply it",
        "use that plan",
        "next step",
        "use one",
        "invoke one",
        "test it",
    ]
    .iter()
    .any(|marker| goal_lower.contains(marker))
        || (goal_lower.starts_with("now ") || goal_lower.starts_with("now,"))
            && [
                " it", " that", " this", " one", " fix", " plan", " scout", " review",
            ]
            .iter()
            .any(|needle| goal_lower.contains(needle))
}

fn classify_routing(
    signals: &TaskSignals,
    thinking: ThinkingLevel,
    _plan_mode: bool,
) -> TaskRoutingIntent {
    let requirements = RouteRequirements {
        needs_vision: signals.has_images,
        prefer_reasoning: thinking != ThinkingLevel::Off,
    };

    if !signals.capable_reasons.is_empty() {
        return TaskRoutingIntent {
            profile: RoutingProfile::Capable,
            requirements,
            reasons: signals.capable_reasons.clone(),
        };
    }

    let cheap_keywords = [
        "summarize",
        "rewrite",
        "rephrase",
        "explain",
        "list",
        "draft",
        "quick",
        "short",
        "fix",
        "add",
        "rename",
        "change",
        "update",
        "move",
        "delete",
        "remove",
        "create",
        "comment",
        "docstring",
        "typo",
    ];

    if signals.trimmed.len() <= 400
        && signals.line_count <= 4
        && cheap_keywords
            .iter()
            .any(|keyword| contains_keyword(&signals.lower, keyword))
    {
        return TaskRoutingIntent {
            profile: RoutingProfile::Cheap,
            requirements,
            reasons: vec!["short low-risk request; prefer cheaper route".to_string()],
        };
    }

    TaskRoutingIntent {
        profile: RoutingProfile::Capable,
        requirements,
        reasons: vec![
            "defaulting to capable route until work looks obviously lightweight".to_string(),
        ],
    }
}

fn classify_tool_visibility(
    signals: &TaskSignals,
    thinking: ThinkingLevel,
    plan_mode: bool,
) -> ToolVisibilityProfile {
    if signals.has_images || thinking != ThinkingLevel::Off || plan_mode {
        return ToolVisibilityProfile::Full;
    }

    let answer_only_phrases = [
        "reply exactly",
        "and nothing else",
        "answer with only",
        "respond with only",
    ];

    if signals.trimmed.len() <= 160
        && signals.line_count <= 3
        && !signals.mentions_repo_read
        && !signals.mentions_write
        && answer_only_phrases
            .iter()
            .any(|phrase| signals.lower.contains(phrase))
    {
        return ToolVisibilityProfile::AnswerOnly;
    }

    if signals.trimmed.len() <= 260
        && signals.line_count <= 4
        && signals.mentions_repo_read
        && !signals.mentions_write
    {
        return ToolVisibilityProfile::ReadOnly;
    }

    ToolVisibilityProfile::Full
}

fn classify_delegation(
    signals: &TaskSignals,
    tool_visibility: &ToolVisibilityProfile,
) -> SubagentDelegationPolicy {
    if signals.explicit_delegate {
        return SubagentDelegationPolicy {
            enable_subagent_tool: true,
            max_subagents: 3,
            reason: EXPLICIT_DELEGATION_REASON.to_string(),
        };
    }

    if *tool_visibility != ToolVisibilityProfile::Full {
        return SubagentDelegationPolicy {
            enable_subagent_tool: false,
            max_subagents: 0,
            reason: "tool access is limited for this task, so hidden delegation stays off"
                .to_string(),
        };
    }

    if signals.small_task
        && signals.trimmed.len() <= 220
        && signals.line_count <= 3
        && signals.file_reference_count <= 1
    {
        return SubagentDelegationPolicy {
            enable_subagent_tool: false,
            max_subagents: 0,
            reason: "request looks like a small in-thread edit".to_string(),
        };
    }

    if signals.trimmed.len() > 700
        || signals.line_count > 8
        || signals.broad_task
        || signals.file_reference_count >= 3
    {
        return SubagentDelegationPolicy {
            enable_subagent_tool: true,
            max_subagents: 2,
            reason: "task looks broad enough to justify one scout or reviewer".to_string(),
        };
    }

    if signals.trimmed.len() > 320 || signals.line_count > 4 {
        return SubagentDelegationPolicy {
            enable_subagent_tool: true,
            max_subagents: 1,
            reason: "task is moderately multi-step, so one focused helper may help".to_string(),
        };
    }

    SubagentDelegationPolicy {
        enable_subagent_tool: false,
        max_subagents: 0,
        reason: "request is simple enough to keep in the main thread".to_string(),
    }
}

fn contains_keyword(text: &str, keyword: &str) -> bool {
    if keyword.contains(' ') {
        return text.contains(keyword);
    }

    text.split(|ch: char| !ch.is_ascii_alphanumeric())
        .any(|token| !token.is_empty() && token == keyword)
}

/// F17 — Classify effort based on task signals.
///
/// Low: short greetings, exact-reply tasks, trivial lookups.
/// Medium: standard edits, focused debugging, single-file work.
/// High: multi-file refactors, architecture, broad investigation.
fn classify_effort(signals: &TaskSignals) -> EffortLevel {
    // Trivial tasks: very short, no write intent, no complexity signals
    if signals.trimmed.len() <= 80
        && signals.line_count <= 2
        && !signals.mentions_write
        && !signals.broad_task
        && signals.file_reference_count == 0
    {
        tracing::debug!(
            effort = "Low",
            prompt_len = signals.trimmed.len(),
            lines = signals.line_count,
            "F17: classified effort level (short, no write, no complexity)"
        );
        return EffortLevel::Low;
    }

    // Complex tasks: broad scope, many files, long prompts
    if signals.broad_task
        || signals.file_reference_count >= 3
        || signals.trimmed.len() > 500
        || signals.line_count > 8
    {
        tracing::debug!(
            effort = "High",
            broad_task = signals.broad_task,
            file_refs = signals.file_reference_count,
            prompt_len = signals.trimmed.len(),
            lines = signals.line_count,
            "F17: classified effort level (broad/complex)"
        );
        return EffortLevel::High;
    }

    tracing::debug!(
        effort = "Medium",
        prompt_len = signals.trimmed.len(),
        lines = signals.line_count,
        "F17: classified effort level (standard)"
    );
    EffortLevel::Medium
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn routing_analysis_prefers_capable_for_debug_work() {
        let intent = analyze_task(
            "Debug the failing provider fallback and explain the root cause.",
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert_eq!(intent.profile, RoutingProfile::Capable);
        assert!(intent
            .reasons
            .iter()
            .any(|reason| reason.contains("deeper reasoning")));
    }

    #[test]
    fn routing_analysis_prefers_cheap_for_short_summary_work() {
        let intent = analyze_task(
            "Summarize this diff in two bullets.",
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert_eq!(intent.profile, RoutingProfile::Cheap);
    }

    #[test]
    fn routing_analysis_prefers_cheap_for_small_edit_requests() {
        let intent = analyze_task(
            "Rename the helper function from loadConfig to load_config.",
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert_eq!(intent.profile, RoutingProfile::Cheap);
    }

    #[test]
    fn routing_analysis_keeps_bug_fixing_on_capable_route() {
        let intent = analyze_task(
            "Fix the authentication bug in the login flow.",
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert_eq!(intent.profile, RoutingProfile::Capable);
    }

    #[test]
    fn routing_analysis_avoids_obvious_substring_false_positive() {
        let intent = analyze_task(
            "Please shortlist the options.",
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert_eq!(intent.profile, RoutingProfile::Capable);
        assert!(!intent
            .reasons
            .iter()
            .any(|reason| reason.contains("deeper reasoning")));
    }

    #[test]
    fn tool_visibility_prefers_answer_only_for_exact_reply_tasks() {
        let profile = infer_tool_visibility(
            "Reply exactly with BENCHMARK_OK and nothing else.",
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert_eq!(profile, ToolVisibilityProfile::AnswerOnly);
    }

    #[test]
    fn tool_visibility_prefers_read_only_for_repo_lookup_tasks() {
        let profile = infer_tool_visibility(
            "Read package.json in the current directory and reply with only the package name.",
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert_eq!(profile, ToolVisibilityProfile::ReadOnly);
    }

    #[test]
    fn delegation_policy_disables_helpers_for_small_single_file_work() {
        let policy = infer_subagent_delegation(
            "Rename the helper function in one file.",
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert!(!policy.enable_subagent_tool);
        assert_eq!(policy.max_subagents, 0);
    }

    #[test]
    fn delegation_policy_enables_helpers_for_broad_multi_file_work() {
        let policy = infer_subagent_delegation(
            "Investigate why config.rs, client.rs, and tests.rs disagree about retry behavior, then fix the bug across files.",
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert!(policy.enable_subagent_tool);
        assert_eq!(policy.max_subagents, 2);
    }

    #[test]
    fn delegation_policy_honors_explicit_subagent_requests() {
        let policy = infer_subagent_delegation(
            "Use a scout subagent to inspect the repo, then review the final change.",
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert!(policy.enable_subagent_tool);
        assert_eq!(policy.max_subagents, 3);
    }

    #[test]
    fn delegation_policy_does_not_inherit_from_unrelated_now_follow_up() {
        let history = vec![
            Message::new(
                Role::User,
                "Use a scout subagent to inspect the repo, then review the final change.",
            ),
            Message::new(Role::Assistant, "I'll scout the repo first."),
        ];

        let analysis = analyze_task_full_with_history(
            "Now summarize the README.",
            &history,
            &[],
            ThinkingLevel::Off,
            false,
        );

        assert!(!analysis.delegation.enable_subagent_tool);
    }

    #[test]
    fn delegation_policy_keeps_explicit_follow_up_requests_enabled() {
        let history = vec![
            Message::new(
                Role::User,
                "Use a scout subagent to inspect the repo, then review the final change.",
            ),
            Message::new(Role::Assistant, "I'll scout the repo first."),
        ];

        let policy = analyze_task_full_with_history(
            "Now implement the fix.",
            &history,
            &[],
            ThinkingLevel::Off,
            false,
        )
        .delegation;

        assert!(policy.enable_subagent_tool);
        assert_eq!(policy.reason, EXPLICIT_DELEGATION_REASON);
    }

    #[test]
    fn delegation_policy_does_not_enable_follow_up_helpers_for_unrelated_short_turns() {
        let history = vec![
            Message::new(
                Role::User,
                "Use a scout subagent to inspect the repo, then review the final change.",
            ),
            Message::new(Role::Assistant, "I'll scout the repo first."),
        ];

        let policy = analyze_task_full_with_history(
            "Summarize the README in one bullet.",
            &history,
            &[],
            ThinkingLevel::Off,
            false,
        )
        .delegation;

        assert!(!policy.enable_subagent_tool);
    }

    // --- F17: Effort level tests ---

    #[test]
    fn effort_low_for_greeting() {
        let analysis = analyze_task_full("Hello!", &[], ThinkingLevel::Off, false);
        assert_eq!(analysis.effort, EffortLevel::Low);
    }

    #[test]
    fn effort_medium_for_single_file_edit() {
        let analysis = analyze_task_full(
            "Fix the typo in src/main.rs where 'recieve' should be 'receive'.",
            &[],
            ThinkingLevel::Off,
            false,
        );
        assert_eq!(analysis.effort, EffortLevel::Medium);
    }

    #[test]
    fn effort_high_for_multi_file_refactor() {
        let analysis = analyze_task_full(
            "Investigate why config.rs, client.rs, and tests.rs disagree about retry behavior across files, then refactor the shared logic into a common module.",
            &[],
            ThinkingLevel::Off,
            false,
        );
        assert_eq!(analysis.effort, EffortLevel::High);
    }

    #[test]
    fn effort_budget_scaling() {
        assert_eq!(EffortLevel::Low.scale_budget(Some(10_000)), Some(2_500));
        assert_eq!(EffortLevel::Medium.scale_budget(Some(10_000)), Some(6_000));
        assert_eq!(EffortLevel::High.scale_budget(Some(10_000)), Some(10_000));
        assert_eq!(EffortLevel::Low.scale_budget(None), None);
    }
}
