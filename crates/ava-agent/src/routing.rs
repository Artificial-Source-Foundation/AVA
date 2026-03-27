use ava_config::RoutingProfile;
use ava_llm::RouteRequirements;
use ava_types::{ImageContent, ThinkingLevel};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ToolVisibilityProfile {
    Full,
    ReadOnly,
    AnswerOnly,
}

#[derive(Debug, Clone)]
pub struct TaskRoutingIntent {
    pub profile: RoutingProfile,
    pub requirements: RouteRequirements,
    pub reasons: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SubagentDelegationPolicy {
    pub enable_task_tool: bool,
    pub max_subagents: usize,
    pub reason: String,
}

pub fn analyze_task(
    goal: &str,
    images: &[ImageContent],
    thinking: ThinkingLevel,
    plan_mode: bool,
) -> TaskRoutingIntent {
    let trimmed = goal.trim();
    let lower = trimmed.to_lowercase();
    let line_count = trimmed.lines().count();
    let has_images = !images.is_empty();

    let requirements = RouteRequirements {
        needs_vision: has_images,
        prefer_reasoning: thinking != ThinkingLevel::Off,
    };

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
    ];
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

    let mut reasons = Vec::new();
    if has_images {
        reasons.push("images attached; keep vision-capable route".to_string());
    }
    if plan_mode {
        reasons.push("plan mode prefers a more capable model".to_string());
    }
    if thinking != ThinkingLevel::Off {
        reasons.push("thinking mode is enabled".to_string());
    }
    if trimmed.len() > 700 || line_count > 8 {
        reasons.push("prompt is long or multi-step".to_string());
    }
    if capable_keywords
        .iter()
        .any(|keyword| contains_keyword(&lower, keyword))
    {
        reasons.push("task wording suggests deeper reasoning/coding work".to_string());
    }
    if !reasons.is_empty() {
        return TaskRoutingIntent {
            profile: RoutingProfile::Capable,
            requirements,
            reasons,
        };
    }

    if trimmed.len() <= 400
        && line_count <= 4
        && cheap_keywords
            .iter()
            .any(|keyword| contains_keyword(&lower, keyword))
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

pub fn infer_tool_visibility(
    goal: &str,
    images: &[ImageContent],
    thinking: ThinkingLevel,
    plan_mode: bool,
) -> ToolVisibilityProfile {
    let trimmed = goal.trim();
    let lower = trimmed.to_lowercase();
    let line_count = trimmed.lines().count();

    if !images.is_empty() || thinking != ThinkingLevel::Off || plan_mode {
        return ToolVisibilityProfile::Full;
    }

    let answer_only_phrases = [
        "reply exactly",
        "and nothing else",
        "answer with only",
        "respond with only",
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

    let mentions_repo_read = read_only_keywords
        .iter()
        .any(|keyword| contains_keyword(&lower, keyword));
    let mentions_write = write_keywords
        .iter()
        .any(|keyword| contains_keyword(&lower, keyword));

    if trimmed.len() <= 160
        && line_count <= 3
        && !mentions_repo_read
        && !mentions_write
        && answer_only_phrases
            .iter()
            .any(|phrase| lower.contains(phrase))
    {
        return ToolVisibilityProfile::AnswerOnly;
    }

    if trimmed.len() <= 260 && line_count <= 4 && mentions_repo_read && !mentions_write {
        return ToolVisibilityProfile::ReadOnly;
    }

    ToolVisibilityProfile::Full
}

pub fn infer_subagent_delegation(
    goal: &str,
    images: &[ImageContent],
    thinking: ThinkingLevel,
    plan_mode: bool,
) -> SubagentDelegationPolicy {
    let trimmed = goal.trim();
    let lower = trimmed.to_lowercase();
    let line_count = trimmed.lines().count();
    let explicit_delegate_keywords = [
        "subagent",
        "sub-agent",
        "delegate",
        "parallel",
        "background agent",
        "scout",
        "reviewer",
        "planner",
        "worker",
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
    ];
    let small_task_keywords = [
        "rename",
        "typo",
        "comment",
        "doc comment",
        "single file",
        "one file",
    ];

    let explicit_delegate = explicit_delegate_keywords
        .iter()
        .any(|keyword| lower.contains(keyword));
    if explicit_delegate {
        return SubagentDelegationPolicy {
            enable_task_tool: true,
            max_subagents: 3,
            reason: "prompt explicitly asks for delegation or specialist help".to_string(),
        };
    }

    if infer_tool_visibility(goal, images, thinking, plan_mode) != ToolVisibilityProfile::Full {
        return SubagentDelegationPolicy {
            enable_task_tool: false,
            max_subagents: 0,
            reason: "tool access is limited for this task, so hidden delegation stays off"
                .to_string(),
        };
    }

    let file_reference_count = [".rs", ".ts", ".tsx", ".js", ".jsx", ".py", ".go"]
        .iter()
        .map(|needle| lower.matches(needle).count())
        .sum::<usize>();
    let broad_task = broad_task_keywords
        .iter()
        .any(|keyword| lower.contains(keyword));
    let small_task = small_task_keywords
        .iter()
        .any(|keyword| lower.contains(keyword));

    if small_task && trimmed.len() <= 220 && line_count <= 3 && file_reference_count <= 1 {
        return SubagentDelegationPolicy {
            enable_task_tool: false,
            max_subagents: 0,
            reason: "request looks like a small in-thread edit".to_string(),
        };
    }

    if trimmed.len() > 700 || line_count > 8 || broad_task || file_reference_count >= 3 {
        return SubagentDelegationPolicy {
            enable_task_tool: true,
            max_subagents: 2,
            reason: "task looks broad enough to justify one scout or reviewer".to_string(),
        };
    }

    if trimmed.len() > 320 || line_count > 4 {
        return SubagentDelegationPolicy {
            enable_task_tool: true,
            max_subagents: 1,
            reason: "task is moderately multi-step, so one focused helper may help".to_string(),
        };
    }

    SubagentDelegationPolicy {
        enable_task_tool: false,
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

        assert!(!policy.enable_task_tool);
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

        assert!(policy.enable_task_tool);
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

        assert!(policy.enable_task_tool);
        assert_eq!(policy.max_subagents, 3);
    }
}
