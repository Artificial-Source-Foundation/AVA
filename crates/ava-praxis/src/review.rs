use std::sync::Arc;

use ava_agent::{AgentConfig, AgentEvent, AgentLoop};
use ava_context::ContextManager;
use ava_llm::provider::{LLMProvider, SharedProvider};
use ava_platform::StandardPlatform;
use ava_tools::core::register_core_tools;
use ava_tools::registry::ToolRegistry;
use futures::StreamExt;
use regex::Regex;
use serde::{Deserialize, Serialize};
use tracing::debug;

// ── Types ──────────────────────────────────────────────────────────

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReviewResult {
    pub summary: String,
    pub issues: Vec<ReviewIssue>,
    pub positives: Vec<String>,
    pub verdict: ReviewVerdict,
    pub raw_output: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReviewIssue {
    pub severity: Severity,
    pub file: Option<String>,
    pub line: Option<usize>,
    pub description: String,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub enum Severity {
    Nitpick,
    Suggestion,
    Warning,
    Critical,
}

impl std::fmt::Display for Severity {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Nitpick => write!(f, "nitpick"),
            Self::Suggestion => write!(f, "suggestion"),
            Self::Warning => write!(f, "warning"),
            Self::Critical => write!(f, "critical"),
        }
    }
}

impl Severity {
    pub fn from_str_loose(s: &str) -> Self {
        let lower = s.to_ascii_lowercase();
        if lower.contains("critical") || lower.contains("error") || lower.contains("bug") {
            Self::Critical
        } else if lower.contains("warn") {
            Self::Warning
        } else if lower.contains("suggest") || lower.contains("improvement") {
            Self::Suggestion
        } else {
            Self::Nitpick
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum ReviewVerdict {
    Approve,
    RequestChanges,
    Comment,
}

impl std::fmt::Display for ReviewVerdict {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Approve => write!(f, "APPROVE"),
            Self::RequestChanges => write!(f, "REQUEST_CHANGES"),
            Self::Comment => write!(f, "COMMENT"),
        }
    }
}

#[derive(Debug, Clone)]
pub struct ReviewContext {
    pub diff: String,
    pub stats: Vec<DiffStats>,
}

#[derive(Debug, Clone)]
pub struct DiffStats {
    pub file: String,
    pub insertions: usize,
    pub deletions: usize,
}

#[derive(Debug, Clone)]
pub enum DiffMode {
    Staged,
    Working,
    Commit(String),
    Range(String),
}

impl DiffMode {
    /// Build the git diff command for this mode.
    fn diff_args(&self) -> Vec<&str> {
        match self {
            Self::Staged => vec!["diff", "--staged"],
            Self::Working => vec!["diff"],
            Self::Commit(ref sha) => vec!["show", sha.as_str()],
            Self::Range(ref range) => vec!["diff", range.as_str()],
        }
    }

    fn stat_args(&self) -> Vec<&str> {
        match self {
            Self::Staged => vec!["diff", "--staged", "--stat"],
            Self::Working => vec!["diff", "--stat"],
            Self::Commit(ref sha) => vec!["show", "--stat", sha.as_str()],
            Self::Range(ref range) => vec!["diff", "--stat", range.as_str()],
        }
    }
}

// ── Diff collection ────────────────────────────────────────────────

const MAX_DIFF_BYTES: usize = 50 * 1024;

pub async fn collect_diff(mode: &DiffMode) -> Result<ReviewContext, String> {
    let diff_args = mode.diff_args();
    let diff_output = run_git_command(&diff_args).await?;
    if diff_output.trim().is_empty() {
        return Err("No changes found for the specified diff mode".to_string());
    }

    let diff = if diff_output.len() > MAX_DIFF_BYTES {
        let mut truncated = diff_output[..MAX_DIFF_BYTES].to_string();
        truncated.push_str("\n\n[diff truncated — too large for review]");
        truncated
    } else {
        diff_output
    };

    let stat_args = mode.stat_args();
    let stat_output = run_git_command(&stat_args).await.unwrap_or_default();
    let stats = parse_diff_stats(&stat_output);

    Ok(ReviewContext { diff, stats })
}

fn parse_diff_stats(stat_output: &str) -> Vec<DiffStats> {
    let re = Regex::new(r"^\s*(.+?)\s+\|\s+(\d+)\s*(\+*)(-*)").unwrap();
    stat_output
        .lines()
        .filter_map(|line| {
            let caps = re.captures(line)?;
            let file = caps.get(1)?.as_str().trim().to_string();
            let insertions = caps.get(3).map_or(0, |m| m.as_str().len());
            let deletions = caps.get(4).map_or(0, |m| m.as_str().len());
            Some(DiffStats {
                file,
                insertions,
                deletions,
            })
        })
        .collect()
}

async fn run_git_command(args: &[&str]) -> Result<String, String> {
    let output = tokio::process::Command::new("git")
        .args(args)
        .output()
        .await
        .map_err(|e| format!("Failed to run git: {e}"))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        return Err(format!("git {} failed: {}", args.join(" "), stderr));
    }

    Ok(String::from_utf8_lossy(&output.stdout).to_string())
}

// ── System prompt ──────────────────────────────────────────────────

pub fn build_review_system_prompt(focus: &str) -> String {
    let focus_instructions = match focus {
        "security" => "Focus primarily on security vulnerabilities: injection flaws, auth issues, data exposure, unsafe operations, OWASP top 10.",
        "performance" => "Focus primarily on performance issues: N+1 queries, unnecessary allocations, blocking calls, algorithmic complexity.",
        "bugs" => "Focus primarily on correctness: logic errors, edge cases, off-by-one errors, null handling, race conditions.",
        "style" => "Focus primarily on code style: naming, formatting, idioms, readability, consistency with existing code.",
        _ => "Review all aspects: correctness, security, performance, and code quality.",
    };

    format!(
        r#"You are a senior code reviewer. Your job is to review a git diff and provide structured feedback.

{focus_instructions}

You have access to tools for reading files, searching the codebase, and running git commands. Use them to understand context around the changes when needed.

After reviewing, output your findings in EXACTLY this format:

## Summary
A 1-3 sentence summary of the changes and their overall quality.

## Issues
List each issue as a subsection. If no issues, write "No issues found."

### [severity] file:line - description
Detailed explanation of the issue.

Where severity is one of: critical, warning, suggestion, nitpick

## Positives
List good aspects of the changes (bullet points). If none notable, write "None noted."

## Verdict
Write exactly one of: APPROVE, REQUEST_CHANGES, or COMMENT

Rules:
- APPROVE: No critical or warning issues.
- REQUEST_CHANGES: Any critical issues, or multiple warnings.
- COMMENT: Only suggestions/nitpicks, or uncertain about severity.

Be concise. Focus on substance over style unless reviewing for style specifically."#
    )
}

// ── Output parsing ─────────────────────────────────────────────────

pub fn parse_review_output(text: &str) -> ReviewResult {
    let summary = extract_section(text, "Summary")
        .unwrap_or_else(|| text.lines().take(3).collect::<Vec<_>>().join("\n"));
    let issues = parse_issues(text);
    let positives = extract_list_section(text, "Positives");
    let verdict = parse_verdict(text);

    ReviewResult {
        summary,
        issues,
        positives,
        verdict,
        raw_output: text.to_string(),
    }
}

fn extract_section(text: &str, heading: &str) -> Option<String> {
    let pattern = format!("## {heading}");
    let start = text.find(&pattern)?;
    let content_start = start + pattern.len();
    let rest = &text[content_start..];

    // Find the next ## heading
    let end = rest.find("\n## ").unwrap_or(rest.len());
    let content = rest[..end].trim();
    if content.is_empty() {
        None
    } else {
        Some(content.to_string())
    }
}

fn parse_issues(text: &str) -> Vec<ReviewIssue> {
    let issue_re = Regex::new(r"###\s+\[(\w+)\]\s+([^:\s]+)?:?(\d+)?\s*-?\s*(.+)").unwrap();
    let mut issues = Vec::new();

    let issues_section = match extract_section(text, "Issues") {
        Some(s) => s,
        None => return issues,
    };

    if issues_section.to_ascii_lowercase().contains("no issues") {
        return issues;
    }

    for caps in issue_re.captures_iter(&issues_section) {
        let severity = Severity::from_str_loose(caps.get(1).map_or("", |m| m.as_str()));
        let file = caps.get(2).map(|m| m.as_str().to_string());
        let line = caps.get(3).and_then(|m| m.as_str().parse().ok());
        let description = caps
            .get(4)
            .map_or(String::new(), |m| m.as_str().trim().to_string());

        issues.push(ReviewIssue {
            severity,
            file,
            line,
            description,
        });
    }

    issues
}

fn extract_list_section(text: &str, heading: &str) -> Vec<String> {
    let section = match extract_section(text, heading) {
        Some(s) => s,
        None => return Vec::new(),
    };

    section
        .lines()
        .filter_map(|line| {
            let trimmed = line.trim();
            if trimmed.starts_with('-') || trimmed.starts_with('*') {
                Some(trimmed.trim_start_matches(['-', '*', ' ']).to_string())
            } else {
                None
            }
        })
        .collect()
}

fn parse_verdict(text: &str) -> ReviewVerdict {
    let section = extract_section(text, "Verdict").unwrap_or_default();
    let upper = section.to_ascii_uppercase();
    if upper.contains("REQUEST_CHANGES") || upper.contains("REQUEST CHANGES") {
        ReviewVerdict::RequestChanges
    } else if upper.contains("APPROVE") {
        ReviewVerdict::Approve
    } else {
        ReviewVerdict::Comment
    }
}

// ── Output formatting ──────────────────────────────────────────────

pub fn format_text(result: &ReviewResult) -> String {
    let mut out = String::new();

    out.push_str(&format!("\n  Summary\n  {}\n", "─".repeat(60)));
    out.push_str(&format!("  {}\n", result.summary));

    if !result.issues.is_empty() {
        out.push_str(&format!(
            "\n  Issues ({})\n  {}\n",
            result.issues.len(),
            "─".repeat(60)
        ));
        for issue in &result.issues {
            let location = match (&issue.file, issue.line) {
                (Some(f), Some(l)) => format!("{f}:{l}"),
                (Some(f), None) => f.clone(),
                _ => String::new(),
            };
            let severity_tag = match issue.severity {
                Severity::Critical => "\x1b[31m[critical]\x1b[0m",
                Severity::Warning => "\x1b[33m[warning]\x1b[0m",
                Severity::Suggestion => "\x1b[36m[suggestion]\x1b[0m",
                Severity::Nitpick => "\x1b[90m[nitpick]\x1b[0m",
            };
            if location.is_empty() {
                out.push_str(&format!("  {severity_tag} {}\n", issue.description));
            } else {
                out.push_str(&format!(
                    "  {severity_tag} {location} — {}\n",
                    issue.description
                ));
            }
        }
    } else {
        out.push_str(&format!(
            "\n  Issues\n  {}\n  No issues found.\n",
            "─".repeat(60)
        ));
    }

    if !result.positives.is_empty() {
        out.push_str(&format!("\n  Positives\n  {}\n", "─".repeat(60)));
        for p in &result.positives {
            out.push_str(&format!("  + {p}\n"));
        }
    }

    let verdict_colored = match result.verdict {
        ReviewVerdict::Approve => "\x1b[32mAPPROVE\x1b[0m",
        ReviewVerdict::RequestChanges => "\x1b[31mREQUEST_CHANGES\x1b[0m",
        ReviewVerdict::Comment => "\x1b[33mCOMMENT\x1b[0m",
    };
    out.push_str(&format!("\n  Verdict: {verdict_colored}\n"));

    out
}

pub fn format_json(result: &ReviewResult) -> String {
    serde_json::to_string_pretty(result).unwrap_or_else(|_| "{}".to_string())
}

pub fn format_markdown(result: &ReviewResult) -> String {
    let mut out = String::new();

    out.push_str("## Code Review\n\n");
    out.push_str("### Summary\n\n");
    out.push_str(&format!("{}\n\n", result.summary));

    if !result.issues.is_empty() {
        out.push_str(&format!("### Issues ({})\n\n", result.issues.len()));
        for issue in &result.issues {
            let location = match (&issue.file, issue.line) {
                (Some(f), Some(l)) => format!("`{f}:{l}`"),
                (Some(f), None) => format!("`{f}`"),
                _ => String::new(),
            };
            let severity = match issue.severity {
                Severity::Critical => "🔴 **critical**",
                Severity::Warning => "🟡 **warning**",
                Severity::Suggestion => "🔵 suggestion",
                Severity::Nitpick => "⚪ nitpick",
            };
            if location.is_empty() {
                out.push_str(&format!("- {severity}: {}\n", issue.description));
            } else {
                out.push_str(&format!("- {severity} {location}: {}\n", issue.description));
            }
        }
        out.push('\n');
    } else {
        out.push_str("### Issues\n\nNo issues found.\n\n");
    }

    if !result.positives.is_empty() {
        out.push_str("### Positives\n\n");
        for p in &result.positives {
            out.push_str(&format!("- {p}\n"));
        }
        out.push('\n');
    }

    out.push_str(&format!("### Verdict\n\n**{}**\n", result.verdict));

    out
}

// ── Exit code ──────────────────────────────────────────────────────

pub fn determine_exit_code(result: &ReviewResult, threshold: Severity) -> i32 {
    let has_issues_above = result
        .issues
        .iter()
        .any(|issue| issue.severity >= threshold);

    if has_issues_above {
        1
    } else {
        0
    }
}

// ── Agent runner ───────────────────────────────────────────────────

pub async fn run_review_agent(
    provider: Arc<dyn LLMProvider>,
    platform: Arc<StandardPlatform>,
    review_context: &ReviewContext,
    system_prompt: &str,
    max_turns: usize,
) -> Result<String, String> {
    let mut registry = ToolRegistry::new();
    register_core_tools(&mut registry, platform);

    let config = AgentConfig {
        max_turns,
        max_budget_usd: 0.0,
        token_limit: 128_000,
        model: provider.model_name().to_string(),
        max_cost_usd: 5.0,
        loop_detection: true,
        custom_system_prompt: Some(system_prompt.to_string()),
        thinking_level: ava_types::ThinkingLevel::Off,
        system_prompt_suffix: None,
        extended_tools: true,
        plan_mode: false,
        post_edit_validation: None,
    };

    let context = ContextManager::new(config.token_limit);

    let mut agent = AgentLoop::new(
        Box::new(SharedProvider::new(provider)),
        registry,
        context,
        config,
    );

    let goal = build_review_goal(review_context);

    let mut stream = agent.run_streaming(&goal).await;
    let mut output = String::new();

    while let Some(event) = stream.next().await {
        match event {
            AgentEvent::Token(t) => {
                output.push_str(&t);
                eprint!("{t}");
            }
            AgentEvent::ToolCall(tc) => {
                debug!(tool = %tc.name, "Review agent tool call");
                eprintln!("[tool: {}]", tc.name);
            }
            AgentEvent::Complete(_) => break,
            AgentEvent::Error(e) => return Err(e),
            _ => {}
        }
    }

    eprintln!();
    Ok(output)
}

fn build_review_goal(ctx: &ReviewContext) -> String {
    let mut goal = String::from("Review the following git diff:\n\n");

    if !ctx.stats.is_empty() {
        goal.push_str("Changed files:\n");
        for stat in &ctx.stats {
            goal.push_str(&format!(
                "  {} (+{}, -{})\n",
                stat.file, stat.insertions, stat.deletions
            ));
        }
        goal.push('\n');
    }

    goal.push_str("```diff\n");
    goal.push_str(&ctx.diff);
    goal.push_str("\n```\n\n");
    goal.push_str("Use the available tools to examine any files referenced in the diff for additional context. Then provide your review in the required format.");

    goal
}

// ── Tests ──────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_review_output_full() {
        let text = r#"## Summary
Good changes with minor issues.

## Issues
### [critical] src/main.rs:42 - Potential SQL injection
The user input is not sanitized before query.

### [warning] src/lib.rs:10 - Unused import
This import is no longer needed.

### [nitpick] src/lib.rs - Consider renaming variable

## Positives
- Good test coverage
- Clean error handling

## Verdict
REQUEST_CHANGES"#;

        let result = parse_review_output(text);
        assert_eq!(result.summary, "Good changes with minor issues.");
        assert_eq!(result.issues.len(), 3);
        assert_eq!(result.issues[0].severity, Severity::Critical);
        assert_eq!(result.issues[0].file.as_deref(), Some("src/main.rs"));
        assert_eq!(result.issues[0].line, Some(42));
        assert_eq!(result.issues[1].severity, Severity::Warning);
        assert_eq!(result.issues[2].severity, Severity::Nitpick);
        assert_eq!(result.issues[2].line, None);
        assert_eq!(result.positives.len(), 2);
        assert_eq!(result.verdict, ReviewVerdict::RequestChanges);
    }

    #[test]
    fn parse_review_output_approve() {
        let text = r#"## Summary
Clean changes.

## Issues
No issues found.

## Positives
- Well structured

## Verdict
APPROVE"#;

        let result = parse_review_output(text);
        assert!(result.issues.is_empty());
        assert_eq!(result.verdict, ReviewVerdict::Approve);
    }

    #[test]
    fn parse_review_output_fallback() {
        let text = "This is just some unstructured text from the agent.";
        let result = parse_review_output(text);
        assert_eq!(result.summary, text);
        assert!(result.issues.is_empty());
        assert_eq!(result.verdict, ReviewVerdict::Comment);
    }

    #[test]
    fn exit_code_with_threshold() {
        let result = ReviewResult {
            summary: String::new(),
            issues: vec![ReviewIssue {
                severity: Severity::Warning,
                file: None,
                line: None,
                description: "test".to_string(),
            }],
            positives: vec![],
            verdict: ReviewVerdict::Comment,
            raw_output: String::new(),
        };

        assert_eq!(determine_exit_code(&result, Severity::Critical), 0);
        assert_eq!(determine_exit_code(&result, Severity::Warning), 1);
        assert_eq!(determine_exit_code(&result, Severity::Suggestion), 1);
    }

    #[test]
    fn severity_ordering() {
        assert!(Severity::Critical > Severity::Warning);
        assert!(Severity::Warning > Severity::Suggestion);
        assert!(Severity::Suggestion > Severity::Nitpick);
    }

    #[test]
    fn format_json_roundtrip() {
        let result = ReviewResult {
            summary: "Test".to_string(),
            issues: vec![],
            positives: vec![],
            verdict: ReviewVerdict::Approve,
            raw_output: String::new(),
        };
        let json = format_json(&result);
        let parsed: ReviewResult = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed.verdict, ReviewVerdict::Approve);
    }

    #[test]
    fn diff_stats_parsing() {
        let stat = " src/main.rs | 10 ++++------\n lib/util.rs | 3 +++\n";
        let stats = parse_diff_stats(stat);
        assert_eq!(stats.len(), 2);
        assert_eq!(stats[0].file, "src/main.rs");
        assert_eq!(stats[0].insertions, 4);
        assert_eq!(stats[0].deletions, 6);
    }
}
