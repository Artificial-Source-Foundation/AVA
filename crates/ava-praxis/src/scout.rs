//! Scout system — lightweight read-only agents for codebase reconnaissance.
//!
//! Scouts are cheap, fast agents (Haiku/Flash-class models) that the Director
//! dispatches to read specific parts of the codebase before planning.  They
//! have **no write access** — only `glob`, `grep`, and `read` tools.
//!
//! # Design
//! - One scout per query, all queries run in parallel
//! - Budget: max 10 turns, max 5 000 tokens
//! - Output: structured [`ScoutReport`] with findings, files, snippets, suggestions
//! - Director consumes reports for planning (and optionally shares with Board)

use std::path::Path;
use std::sync::Arc;

use ava_agent::{AgentConfig, AgentEvent, AgentLoop};
use ava_context::ContextManager;
use ava_llm::provider::{LLMProvider, SharedProvider};
use ava_platform::StandardPlatform;
use ava_tools::core::{glob, grep, hashline, read};
use ava_tools::registry::ToolRegistry;
use ava_types::ThinkingLevel;
use futures::StreamExt;
use serde::{Deserialize, Serialize};
use uuid::Uuid;

// ---------------------------------------------------------------------------
// Budget constants
// ---------------------------------------------------------------------------

/// Maximum turns a scout may take.
const SCOUT_MAX_TURNS: usize = 10;

/// Token limit for scout context window.
const SCOUT_TOKEN_LIMIT: usize = 5_000;

/// Maximum cost (USD) per scout run.  Scouts should be essentially free.
const SCOUT_MAX_COST_USD: f64 = 0.05;

// ---------------------------------------------------------------------------
// Scout prompt
// ---------------------------------------------------------------------------

const SCOUT_SYSTEM_PROMPT: &str = "\
You are a Scout — a codebase analyst. Your job is to read and summarize code, NOT modify it.

Given a query, investigate the relevant parts of the codebase and produce a structured report.
Use the tools available (glob, grep, read) to find and read relevant files.

When you are done investigating, respond with your findings in the following structure:

## Files Examined
- List each file you looked at

## Key Findings
- What code patterns and structures you found
- How the relevant code works

## Relevant Code
For each important snippet:
- File path and line range
- The code itself
- Why it matters

## Potential Issues
- Any problems or areas needing attention

## Suggestions
- How to approach the task (do NOT suggest code changes — just strategic observations)

Be thorough but concise. Focus on actionable findings.";

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/// A lightweight read-only agent for codebase reconnaissance.
pub struct Scout {
    /// Unique identifier for this scout instance.
    pub id: Uuid,
    /// LLM provider (should be a cheap/fast model).
    pub provider: Arc<dyn LLMProvider>,
    /// Platform for file system access.
    pub platform: Arc<StandardPlatform>,
}

/// Structured report produced by a scout after investigation.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ScoutReport {
    /// Report identifier (matches the scout's id).
    pub id: Uuid,
    /// The original query the scout was asked to investigate.
    pub query: String,
    /// Free-form findings text (the scout's full response).
    pub findings: String,
    /// Files the scout examined during investigation.
    pub files_examined: Vec<String>,
    /// Notable code snippets found during investigation.
    pub relevant_code: Vec<CodeSnippet>,
    /// Strategic suggestions for the Director.
    pub suggestions: Vec<String>,
}

/// A code snippet extracted from the codebase by a scout.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CodeSnippet {
    /// File path containing the snippet.
    pub file: String,
    /// Starting line number.
    pub line_start: usize,
    /// Ending line number.
    pub line_end: usize,
    /// The actual code content.
    pub content: String,
    /// Why this snippet is relevant to the query.
    pub relevance: String,
}

impl Scout {
    /// Create a new scout with the given provider and platform.
    pub fn new(provider: Arc<dyn LLMProvider>, platform: Arc<StandardPlatform>) -> Self {
        Self {
            id: Uuid::new_v4(),
            provider,
            platform,
        }
    }

    /// Investigate a query by running a read-only agent loop.
    ///
    /// Returns a [`ScoutReport`] with the scout's findings.  The scout has
    /// access only to `glob`, `grep`, and `read` — no write tools.
    pub async fn investigate(&self, query: &str, cwd: &Path) -> ava_types::Result<ScoutReport> {
        let registry = register_scout_tools(self.platform.clone());

        let model_name = self.provider.model_name().to_string();

        let config = AgentConfig {
            max_turns: SCOUT_MAX_TURNS,
            max_budget_usd: 0.0,
            token_limit: SCOUT_TOKEN_LIMIT,
            model: model_name,
            max_cost_usd: SCOUT_MAX_COST_USD,
            loop_detection: true,
            custom_system_prompt: Some(SCOUT_SYSTEM_PROMPT.to_string()),
            thinking_level: ThinkingLevel::Off,
            thinking_budget_tokens: None,
            system_prompt_suffix: Some(format!(
                "\nWorking directory: {}\nInvestigation query: {}",
                cwd.display(),
                query
            )),
            extended_tools: false,
            plan_mode: false,
            post_edit_validation: None,
            auto_compact: true,
            stream_timeout_secs: ava_agent::agent_loop::LLM_STREAM_TIMEOUT_SECS,
            prompt_caching: true,
        };

        let mut agent = AgentLoop::new(
            Box::new(SharedProvider::new(self.provider.clone())),
            registry,
            ContextManager::new(SCOUT_TOKEN_LIMIT),
            config,
        );

        let goal = format!(
            "Investigate the following query about this codebase and produce a structured report:\n\n{}",
            query
        );

        let mut stream = agent.run_streaming(&goal).await;
        let mut full_response = String::new();
        let mut files_examined = Vec::new();

        while let Some(event) = stream.next().await {
            match event {
                AgentEvent::Token(token) => {
                    full_response.push_str(&token);
                }
                AgentEvent::ToolCall(call) => {
                    // Track files examined from read/glob tool calls
                    if let Some(path) = extract_file_path_from_tool_call(&call) {
                        if !files_examined.contains(&path) {
                            files_examined.push(path);
                        }
                    }
                }
                AgentEvent::Complete(session) => {
                    // Extract the final assistant response from the session
                    if full_response.is_empty() {
                        for msg in session.messages.iter().rev() {
                            if msg.role == ava_types::Role::Assistant && !msg.content.is_empty() {
                                full_response = msg.content.clone();
                                break;
                            }
                        }
                    }
                    break;
                }
                AgentEvent::Error(error) => {
                    return Err(ava_types::AvaError::ToolError(format!(
                        "Scout investigation failed: {error}"
                    )));
                }
                _ => {}
            }
        }

        let relevant_code = parse_code_snippets(&full_response);
        let suggestions = parse_suggestions(&full_response);

        Ok(ScoutReport {
            id: self.id,
            query: query.to_string(),
            findings: full_response,
            files_examined,
            relevant_code,
            suggestions,
        })
    }
}

impl ScoutReport {
    /// Produce a concise summary suitable for inclusion in a Director prompt.
    pub fn as_summary(&self) -> String {
        let mut summary = format!("## Scout Report: {}\n\n", self.query);

        if !self.files_examined.is_empty() {
            summary.push_str("**Files examined:** ");
            summary.push_str(&self.files_examined.join(", "));
            summary.push('\n');
        }

        summary.push_str(&self.findings);

        if !self.suggestions.is_empty() {
            summary.push_str("\n\n**Suggestions:**\n");
            for s in &self.suggestions {
                summary.push_str(&format!("- {s}\n"));
            }
        }

        summary
    }
}

// ---------------------------------------------------------------------------
// Read-only tool registration
// ---------------------------------------------------------------------------

/// Register only read-only tools for scout agents.
///
/// Scouts get: `glob`, `grep`, `read`.
/// Scouts do NOT get: `write`, `edit`, `bash`, `apply_patch`, or any other
/// tool that can modify the filesystem or execute commands.
pub fn register_scout_tools(platform: Arc<StandardPlatform>) -> ToolRegistry {
    let mut registry = ToolRegistry::new();
    let hashline_cache = hashline::new_cache();

    registry.register(read::ReadTool::new(platform, hashline_cache));
    registry.register(glob::GlobTool::new());
    registry.register(grep::GrepTool::new());

    registry
}

// ---------------------------------------------------------------------------
// Response parsing helpers
// ---------------------------------------------------------------------------

/// Extract a file path from a `ToolCall`'s arguments (best-effort).
fn extract_file_path_from_tool_call(call: &ava_types::ToolCall) -> Option<String> {
    let args = &call.arguments;
    // Try common argument names: file_path, path, pattern
    if let Some(v) = args.get("file_path").and_then(|v| v.as_str()) {
        return Some(v.to_string());
    }
    if let Some(v) = args.get("path").and_then(|v| v.as_str()) {
        return Some(v.to_string());
    }
    if let Some(v) = args.get("pattern").and_then(|v| v.as_str()) {
        return Some(v.to_string());
    }
    None
}

/// Parse code snippets from the scout's markdown response.
fn parse_code_snippets(response: &str) -> Vec<CodeSnippet> {
    let mut snippets = Vec::new();

    // Look for fenced code blocks preceded by file path references
    let lines: Vec<&str> = response.lines().collect();
    let mut i = 0;

    while i < lines.len() {
        if lines[i].starts_with("```") && i > 0 {
            // Look backwards for a file path reference
            let mut file = String::new();
            let mut line_start = 0;
            let mut relevance = String::new();

            for j in (0..i).rev() {
                let prev = lines[j].trim();
                if prev.is_empty() {
                    continue;
                }
                // Check if line contains a file path
                if prev.contains('/')
                    && (prev.contains(".rs")
                        || prev.contains(".ts")
                        || prev.contains(".js")
                        || prev.contains(".py")
                        || prev.contains(".toml"))
                {
                    // Extract path — strip markdown formatting
                    file = prev
                        .trim_start_matches('-')
                        .trim_start_matches('*')
                        .trim_start_matches('#')
                        .trim_start_matches('`')
                        .trim_end_matches('`')
                        .trim_end_matches(':')
                        .trim()
                        .to_string();

                    // Check for line numbers like "lines 10-20" or "L10-L20"
                    if let Some(ln) = extract_line_number(prev) {
                        line_start = ln;
                    }

                    relevance = prev.to_string();
                    break;
                }
            }

            // Collect code block content
            i += 1;
            let block_start = i;
            while i < lines.len() && !lines[i].starts_with("```") {
                i += 1;
            }
            let content: String = lines[block_start..i].join("\n");
            let line_count = i - block_start;

            if !file.is_empty() && !content.is_empty() {
                snippets.push(CodeSnippet {
                    file,
                    line_start,
                    line_end: line_start + line_count.saturating_sub(1),
                    content,
                    relevance,
                });
            }
        }
        i += 1;
    }

    snippets
}

fn extract_line_number(s: &str) -> Option<usize> {
    // Match patterns like "line 42", "L42", "lines 42-50"
    let lower = s.to_lowercase();
    if let Some(idx) = lower.find("line") {
        let rest = &lower[idx + 4..];
        let rest = rest.trim_start_matches('s').trim_start();
        let num: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
        num.parse().ok()
    } else if let Some(idx) = lower.find('l') {
        let rest = &lower[idx + 1..];
        if rest.starts_with(|c: char| c.is_ascii_digit()) {
            let num: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
            num.parse().ok()
        } else {
            None
        }
    } else {
        None
    }
}

/// Parse suggestions from the scout's markdown response.
fn parse_suggestions(response: &str) -> Vec<String> {
    let mut suggestions = Vec::new();
    let mut in_suggestions = false;

    for line in response.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("## Suggestion") || trimmed.starts_with("**Suggestion") {
            in_suggestions = true;
            continue;
        }
        if in_suggestions {
            if trimmed.starts_with("## ") || trimmed.starts_with("**") && trimmed.ends_with("**") {
                // New section — stop collecting
                break;
            }
            if trimmed.starts_with("- ") || trimmed.starts_with("* ") {
                suggestions.push(trimmed[2..].to_string());
            }
        }
    }

    suggestions
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scout_report_summary_includes_query() {
        let report = ScoutReport {
            id: Uuid::new_v4(),
            query: "How does auth work?".to_string(),
            findings: "Auth uses JWT tokens.".to_string(),
            files_examined: vec!["src/auth.rs".to_string()],
            relevant_code: vec![],
            suggestions: vec!["Check the middleware chain.".to_string()],
        };

        let summary = report.as_summary();
        assert!(summary.contains("How does auth work?"));
        assert!(summary.contains("src/auth.rs"));
        assert!(summary.contains("Auth uses JWT tokens."));
        assert!(summary.contains("Check the middleware chain."));
    }

    #[test]
    fn register_scout_tools_has_only_read_tools() {
        let platform = Arc::new(StandardPlatform);
        let registry = register_scout_tools(platform);
        let names = registry.tool_names();

        assert!(names.iter().any(|n| n == "read"), "should have read tool");
        assert!(names.iter().any(|n| n == "glob"), "should have glob tool");
        assert!(names.iter().any(|n| n == "grep"), "should have grep tool");

        // Must NOT contain write tools
        assert!(!names.iter().any(|n| n == "write"), "must not have write");
        assert!(!names.iter().any(|n| n == "edit"), "must not have edit");
        assert!(!names.iter().any(|n| n == "bash"), "must not have bash");
        assert!(
            !names.iter().any(|n| n == "apply_patch"),
            "must not have apply_patch"
        );
    }

    #[test]
    fn parse_suggestions_extracts_bullets() {
        let response = "\
## Key Findings
- Some finding

## Suggestions
- First suggestion
- Second suggestion

## Other
- Not a suggestion";

        let suggestions = parse_suggestions(response);
        assert_eq!(suggestions.len(), 2);
        assert_eq!(suggestions[0], "First suggestion");
        assert_eq!(suggestions[1], "Second suggestion");
    }

    #[test]
    fn parse_code_snippets_extracts_blocks() {
        let response = "\
## Relevant Code

`src/auth.rs` line 42:
```rust
fn authenticate() {}
```
";

        let snippets = parse_code_snippets(response);
        assert_eq!(snippets.len(), 1);
        assert!(snippets[0].file.contains("src/auth.rs"));
        assert!(snippets[0].content.contains("authenticate"));
        assert_eq!(snippets[0].line_start, 42);
    }

    #[test]
    fn extract_file_path_from_tool_call_args() {
        let call = ava_types::ToolCall {
            id: "1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({"file_path": "/src/main.rs"}),
        };
        assert_eq!(
            extract_file_path_from_tool_call(&call),
            Some("/src/main.rs".to_string())
        );

        let glob_call = ava_types::ToolCall {
            id: "2".to_string(),
            name: "glob".to_string(),
            arguments: serde_json::json!({"pattern": "**/*.rs"}),
        };
        assert_eq!(
            extract_file_path_from_tool_call(&glob_call),
            Some("**/*.rs".to_string())
        );
    }

    #[test]
    fn scout_budget_constants_are_reasonable() {
        assert_eq!(SCOUT_MAX_TURNS, 10);
        assert_eq!(SCOUT_TOKEN_LIMIT, 5_000);
        assert!(SCOUT_MAX_COST_USD <= 0.10);
    }
}
