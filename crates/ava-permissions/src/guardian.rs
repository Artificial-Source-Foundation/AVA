//! Guardian subagent for automatic approval review.
//!
//! A dedicated review layer that assesses tool call risk and auto-approves
//! low-risk actions. Two implementations:
//! - `HeuristicGuardian`: fast, rule-based scoring (no LLM needed)
//! - `LlmGuardian`: uses a lightweight LLM call with heuristic fallback

use std::future::Future;
use std::pin::Pin;

use async_trait::async_trait;
use serde::{Deserialize, Serialize};

/// Decision rendered by the Guardian after reviewing a tool call.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum GuardianDecision {
    /// Tool call is safe — execute without user confirmation.
    AutoApprove,
    /// Tool call has moderate risk — ask the user for confirmation.
    AskUser,
    /// Tool call is dangerous — block execution.
    Block,
}

/// Result of a Guardian review, including a numeric risk score, rationale, and decision.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GuardianReview {
    /// Risk score from 0 (safe) to 100 (extremely dangerous).
    pub risk_score: u8,
    /// Human-readable explanation of the risk assessment.
    pub rationale: String,
    /// The permission decision.
    pub decision: GuardianDecision,
}

impl GuardianReview {
    /// Create a new review from a risk score, automatically deriving the decision
    /// from default thresholds (<40 = AutoApprove, 40-79 = AskUser, 80+ = Block).
    pub fn from_score(risk_score: u8, rationale: String) -> Self {
        let decision = decision_from_score(risk_score);
        Self {
            risk_score,
            rationale,
            decision,
        }
    }
}

/// Map a risk score to a decision using default thresholds.
fn decision_from_score(score: u8) -> GuardianDecision {
    if score < 40 {
        GuardianDecision::AutoApprove
    } else if score < 80 {
        GuardianDecision::AskUser
    } else {
        GuardianDecision::Block
    }
}

/// Trait for reviewing tool calls and rendering a permission decision.
#[async_trait]
pub trait Guardian: Send + Sync {
    /// Review a tool call and return a risk assessment with decision.
    ///
    /// - `tool_name`: the tool being invoked (e.g. "bash", "read", "edit")
    /// - `tool_args`: the JSON arguments to the tool
    /// - `context_summary`: brief description of what the agent is working on
    async fn review(
        &self,
        tool_name: &str,
        tool_args: &serde_json::Value,
        context_summary: &str,
    ) -> Result<GuardianReview, String>;
}

// ---------------------------------------------------------------------------
// HeuristicGuardian — fast, rule-based scoring
// ---------------------------------------------------------------------------

/// A Guardian that scores risk using heuristic rules without any LLM calls.
/// Provides fast, deterministic assessments based on tool name, command patterns,
/// and file path analysis.
#[derive(Debug, Default)]
pub struct HeuristicGuardian;

impl HeuristicGuardian {
    pub fn new() -> Self {
        Self
    }

    /// Score the base risk of a tool by name.
    fn tool_base_score(tool_name: &str) -> u8 {
        match tool_name {
            "read" | "glob" | "grep" | "codebase_search" | "diagnostics" => 10,
            "todo_read" | "todo_write" | "question" | "task" => 5,
            name if name.starts_with("memory_") || name.starts_with("session_") => 5,
            "edit" | "write" | "multiedit" | "apply_patch" => 40,
            "bash" => 60,
            "web_fetch" | "web_search" => 55,
            "git" => 50,
            "test_runner" | "lint" => 30,
            _ => 50, // unknown tools get moderate risk
        }
    }

    /// Analyze bash command arguments for risky patterns.
    fn bash_command_score(command: &str) -> u8 {
        let cmd_lower = command.to_lowercase();

        // Critical / destructive patterns
        if cmd_lower.contains("rm -rf /")
            || cmd_lower.contains("mkfs")
            || cmd_lower.contains("dd if=")
            || cmd_lower.contains(":(){")
            || cmd_lower.contains(":()")
            || cmd_lower.contains("> /dev/sd")
        {
            return 95;
        }

        // Sudo / privilege escalation
        if cmd_lower.starts_with("sudo ") || cmd_lower.contains("| sudo") {
            return 90;
        }

        // Destructive file operations
        if cmd_lower.contains("rm -rf") || cmd_lower.contains("rm -r") {
            return 85;
        }

        // Database destructive
        if cmd_lower.contains("drop table")
            || cmd_lower.contains("drop database")
            || cmd_lower.contains("truncate ")
        {
            return 90;
        }

        // Package install / system modification
        if cmd_lower.contains("apt install")
            || cmd_lower.contains("apt-get install")
            || cmd_lower.contains("brew install")
            || cmd_lower.contains("npm install -g")
            || cmd_lower.contains("pip install")
            || cmd_lower.contains("cargo install")
        {
            return 70;
        }

        // Git push (potentially destructive)
        if cmd_lower.contains("git push") {
            if cmd_lower.contains("--force") || cmd_lower.contains("-f") {
                return 85;
            }
            return 75;
        }

        // Git reset --hard
        if cmd_lower.contains("git reset --hard") || cmd_lower.contains("git checkout -- .") {
            return 80;
        }

        // Network access
        if cmd_lower.starts_with("curl ")
            || cmd_lower.starts_with("wget ")
            || cmd_lower.contains("| curl")
            || cmd_lower.contains("| wget")
        {
            return 65;
        }

        // Safe dev commands
        if cmd_lower.starts_with("ls")
            || cmd_lower.starts_with("cat ")
            || cmd_lower.starts_with("echo ")
            || cmd_lower.starts_with("pwd")
            || cmd_lower.starts_with("head ")
            || cmd_lower.starts_with("tail ")
            || cmd_lower.starts_with("wc ")
            || cmd_lower.starts_with("find ")
            || cmd_lower.starts_with("grep ")
            || cmd_lower.starts_with("rg ")
        {
            return 15;
        }

        if cmd_lower.starts_with("cargo test")
            || cmd_lower.starts_with("cargo clippy")
            || cmd_lower.starts_with("cargo check")
            || cmd_lower.starts_with("cargo build")
            || cmd_lower.starts_with("npm run")
            || cmd_lower.starts_with("npm test")
            || cmd_lower.starts_with("npx ")
            || cmd_lower.starts_with("git status")
            || cmd_lower.starts_with("git diff")
            || cmd_lower.starts_with("git log")
            || cmd_lower.starts_with("git branch")
        {
            return 20;
        }

        if cmd_lower.starts_with("git commit") || cmd_lower.starts_with("git add") {
            return 30;
        }

        // cd prefix — score the rest
        if let Some(rest) = cmd_lower.strip_prefix("cd ") {
            if let Some(after_and) = rest.split("&&").nth(1) {
                return Self::bash_command_score(after_and.trim());
            }
        }

        // Default for unrecognized commands
        50
    }

    /// Analyze file paths in tool arguments for risk.
    fn path_risk_score(args: &serde_json::Value) -> u8 {
        let paths = extract_paths(args);
        if paths.is_empty() {
            return 0; // no path component, don't add path risk
        }

        let mut max_score: u8 = 0;
        for path in &paths {
            let score = if path.starts_with("/etc/")
                || path.starts_with("/usr/")
                || path.starts_with("/bin/")
                || path.starts_with("/sbin/")
                || path.starts_with("/boot/")
                || path.starts_with("/sys/")
                || path.starts_with("/proc/")
                || path.starts_with("/dev/")
                || path == "/etc/passwd"
                || path == "/etc/shadow"
            {
                80
            } else if path.starts_with("/tmp/") || path.starts_with("/var/tmp/") {
                10
            } else if path.starts_with("/home/") || path.starts_with("./") || !path.starts_with('/')
            {
                20
            } else {
                40
            };
            max_score = max_score.max(score);
        }
        max_score
    }

    /// Compute the final heuristic risk score for a tool call.
    fn compute_score(&self, tool_name: &str, tool_args: &serde_json::Value) -> (u8, String) {
        let base = Self::tool_base_score(tool_name);

        // For bash, use command-specific scoring instead of the base
        if tool_name == "bash" {
            if let Some(command) = tool_args.get("command").and_then(|v| v.as_str()) {
                let cmd_score = Self::bash_command_score(command);
                let path_score = Self::path_risk_score(tool_args);
                let final_score = cmd_score.max(path_score).min(100);
                let rationale = format!(
                    "bash command score={cmd_score}, path score={path_score} -> {final_score}"
                );
                return (final_score, rationale);
            }
        }

        let path_score = Self::path_risk_score(tool_args);
        // Take the maximum of base tool risk and path risk
        let final_score = base.max(path_score).min(100);
        let rationale =
            format!("tool '{tool_name}' base={base}, path={path_score} -> {final_score}");
        (final_score, rationale)
    }
}

#[async_trait]
impl Guardian for HeuristicGuardian {
    async fn review(
        &self,
        tool_name: &str,
        tool_args: &serde_json::Value,
        _context_summary: &str,
    ) -> Result<GuardianReview, String> {
        let (score, rationale) = self.compute_score(tool_name, tool_args);
        Ok(GuardianReview::from_score(score, rationale))
    }
}

// ---------------------------------------------------------------------------
// LlmGuardian — LLM-backed review with heuristic fallback
// ---------------------------------------------------------------------------

/// Type alias for the LLM call function.
pub type LlmCallFn =
    Box<dyn Fn(&str) -> Pin<Box<dyn Future<Output = Result<String, String>> + Send>> + Send + Sync>;

/// A Guardian that uses a lightweight LLM call to assess risk, falling back
/// to `HeuristicGuardian` on parse failure or timeout (5 seconds).
pub struct LlmGuardian {
    llm_call: LlmCallFn,
    fallback: HeuristicGuardian,
    timeout: std::time::Duration,
}

impl LlmGuardian {
    /// Create a new LLM-backed guardian.
    ///
    /// `llm_call` is a function that takes a prompt string and returns the LLM's response.
    pub fn new(llm_call: LlmCallFn) -> Self {
        Self {
            llm_call,
            fallback: HeuristicGuardian::new(),
            timeout: std::time::Duration::from_secs(5),
        }
    }

    /// Create with a custom timeout (for testing).
    pub fn with_timeout(llm_call: LlmCallFn, timeout: std::time::Duration) -> Self {
        Self {
            llm_call,
            fallback: HeuristicGuardian::new(),
            timeout,
        }
    }

    /// Build the compact prompt for the LLM.
    fn build_prompt(
        tool_name: &str,
        tool_args: &serde_json::Value,
        context_summary: &str,
    ) -> String {
        // Truncate args to avoid huge prompts
        let args_str = {
            let full = tool_args.to_string();
            if full.len() > 500 {
                format!("{}...", &full[..500])
            } else {
                full
            }
        };
        format!(
            "Assess risk 0-100 for tool call: {tool_name}({args_str}). \
             Context: {context_summary}. \
             Reply with exactly: SCORE|RATIONALE (e.g. 25|Safe read-only file access)"
        )
    }

    /// Parse the LLM response into a GuardianReview.
    fn parse_response(response: &str) -> Option<GuardianReview> {
        let trimmed = response.trim();
        let parts: Vec<&str> = trimmed.splitn(2, '|').collect();
        if parts.len() != 2 {
            return None;
        }
        let score: u8 = parts[0].trim().parse().ok()?;
        let rationale = parts[1].trim().to_string();
        if rationale.is_empty() {
            return None;
        }
        Some(GuardianReview::from_score(score, rationale))
    }
}

#[async_trait]
impl Guardian for LlmGuardian {
    async fn review(
        &self,
        tool_name: &str,
        tool_args: &serde_json::Value,
        context_summary: &str,
    ) -> Result<GuardianReview, String> {
        let prompt = Self::build_prompt(tool_name, tool_args, context_summary);
        let future = (self.llm_call)(&prompt);

        match tokio::time::timeout(self.timeout, future).await {
            Ok(Ok(response)) => {
                if let Some(review) = Self::parse_response(&response) {
                    Ok(review)
                } else {
                    tracing::warn!(
                        "Guardian LLM returned unparseable response: {response:?}, falling back to heuristic"
                    );
                    self.fallback
                        .review(tool_name, tool_args, context_summary)
                        .await
                }
            }
            Ok(Err(e)) => {
                tracing::warn!("Guardian LLM call failed: {e}, falling back to heuristic");
                self.fallback
                    .review(tool_name, tool_args, context_summary)
                    .await
            }
            Err(_) => {
                tracing::warn!(
                    "Guardian LLM call timed out after {:?}, falling back to heuristic",
                    self.timeout
                );
                self.fallback
                    .review(tool_name, tool_args, context_summary)
                    .await
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Extract file paths from tool arguments.
fn extract_paths(args: &serde_json::Value) -> Vec<String> {
    let mut paths = Vec::new();
    if let Some(p) = args.get("path").and_then(|v| v.as_str()) {
        paths.push(p.to_string());
    }
    if let Some(p) = args.get("file_path").and_then(|v| v.as_str()) {
        paths.push(p.to_string());
    }
    // bash command may contain paths but we handle that via command scoring
    paths
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    // === HeuristicGuardian: safe tools ===

    #[tokio::test]
    async fn read_tool_auto_approved() {
        let g = HeuristicGuardian::new();
        let review = g
            .review("read", &json!({"path": "src/main.rs"}), "reading code")
            .await
            .unwrap();
        assert!(review.risk_score < 40, "score={}", review.risk_score);
        assert_eq!(review.decision, GuardianDecision::AutoApprove);
    }

    #[tokio::test]
    async fn glob_tool_auto_approved() {
        let g = HeuristicGuardian::new();
        let review = g
            .review("glob", &json!({"pattern": "**/*.rs"}), "searching files")
            .await
            .unwrap();
        assert!(review.risk_score < 40);
        assert_eq!(review.decision, GuardianDecision::AutoApprove);
    }

    #[tokio::test]
    async fn grep_tool_auto_approved() {
        let g = HeuristicGuardian::new();
        let review = g
            .review("grep", &json!({"pattern": "TODO"}), "searching code")
            .await
            .unwrap();
        assert!(review.risk_score < 40);
        assert_eq!(review.decision, GuardianDecision::AutoApprove);
    }

    #[tokio::test]
    async fn internal_tools_auto_approved() {
        let g = HeuristicGuardian::new();
        for tool in &[
            "todo_read",
            "todo_write",
            "question",
            "task",
            "memory_search",
            "session_list",
        ] {
            let review = g.review(tool, &json!({}), "internal").await.unwrap();
            assert!(
                review.risk_score < 40,
                "{tool}: score={}",
                review.risk_score
            );
            assert_eq!(review.decision, GuardianDecision::AutoApprove, "{tool}");
        }
    }

    // === HeuristicGuardian: moderate-risk tools ===

    #[tokio::test]
    async fn edit_tool_asks_user() {
        let g = HeuristicGuardian::new();
        let review = g
            .review(
                "edit",
                &json!({"file_path": "src/main.rs", "old": "a", "new": "b"}),
                "editing code",
            )
            .await
            .unwrap();
        assert!(review.risk_score >= 40 && review.risk_score < 80);
        assert_eq!(review.decision, GuardianDecision::AskUser);
    }

    #[tokio::test]
    async fn write_tool_asks_user() {
        let g = HeuristicGuardian::new();
        let review = g
            .review(
                "write",
                &json!({"file_path": "src/new.rs", "content": "hello"}),
                "creating file",
            )
            .await
            .unwrap();
        assert!(review.risk_score >= 40 && review.risk_score < 80);
        assert_eq!(review.decision, GuardianDecision::AskUser);
    }

    // === HeuristicGuardian: bash commands ===

    #[tokio::test]
    async fn safe_bash_ls_auto_approved() {
        let g = HeuristicGuardian::new();
        let review = g
            .review("bash", &json!({"command": "ls -la"}), "listing files")
            .await
            .unwrap();
        assert!(review.risk_score < 40, "score={}", review.risk_score);
        assert_eq!(review.decision, GuardianDecision::AutoApprove);
    }

    #[tokio::test]
    async fn safe_bash_cargo_test_auto_approved() {
        let g = HeuristicGuardian::new();
        let review = g
            .review(
                "bash",
                &json!({"command": "cargo test --workspace"}),
                "running tests",
            )
            .await
            .unwrap();
        assert!(review.risk_score < 40, "score={}", review.risk_score);
        assert_eq!(review.decision, GuardianDecision::AutoApprove);
    }

    #[tokio::test]
    async fn safe_bash_git_status_auto_approved() {
        let g = HeuristicGuardian::new();
        let review = g
            .review("bash", &json!({"command": "git status"}), "checking git")
            .await
            .unwrap();
        assert!(review.risk_score < 40, "score={}", review.risk_score);
        assert_eq!(review.decision, GuardianDecision::AutoApprove);
    }

    #[tokio::test]
    async fn install_command_asks_user() {
        let g = HeuristicGuardian::new();
        let review = g
            .review(
                "bash",
                &json!({"command": "npm install -g typescript"}),
                "installing",
            )
            .await
            .unwrap();
        assert!(review.risk_score >= 40 && review.risk_score < 80);
        assert_eq!(review.decision, GuardianDecision::AskUser);
    }

    #[tokio::test]
    async fn git_push_asks_user() {
        let g = HeuristicGuardian::new();
        let review = g
            .review(
                "bash",
                &json!({"command": "git push origin main"}),
                "pushing",
            )
            .await
            .unwrap();
        assert!(review.risk_score >= 40 && review.risk_score < 80);
        assert_eq!(review.decision, GuardianDecision::AskUser);
    }

    #[tokio::test]
    async fn rm_rf_blocked() {
        let g = HeuristicGuardian::new();
        let review = g
            .review("bash", &json!({"command": "rm -rf /"}), "deleting")
            .await
            .unwrap();
        assert!(review.risk_score >= 80, "score={}", review.risk_score);
        assert_eq!(review.decision, GuardianDecision::Block);
    }

    #[tokio::test]
    async fn sudo_blocked() {
        let g = HeuristicGuardian::new();
        let review = g
            .review(
                "bash",
                &json!({"command": "sudo apt-get update"}),
                "updating",
            )
            .await
            .unwrap();
        assert!(review.risk_score >= 80, "score={}", review.risk_score);
        assert_eq!(review.decision, GuardianDecision::Block);
    }

    #[tokio::test]
    async fn fork_bomb_blocked() {
        let g = HeuristicGuardian::new();
        let review = g
            .review("bash", &json!({"command": ":(){ :|:& };:"}), "fork bomb")
            .await
            .unwrap();
        assert!(review.risk_score >= 80, "score={}", review.risk_score);
        assert_eq!(review.decision, GuardianDecision::Block);
    }

    #[tokio::test]
    async fn drop_table_blocked() {
        let g = HeuristicGuardian::new();
        let review = g
            .review(
                "bash",
                &json!({"command": "sqlite3 db.sqlite 'DROP TABLE users'"}),
                "sql",
            )
            .await
            .unwrap();
        assert!(review.risk_score >= 80, "score={}", review.risk_score);
        assert_eq!(review.decision, GuardianDecision::Block);
    }

    #[tokio::test]
    async fn git_force_push_blocked() {
        let g = HeuristicGuardian::new();
        let review = g
            .review(
                "bash",
                &json!({"command": "git push --force origin main"}),
                "force push",
            )
            .await
            .unwrap();
        assert!(review.risk_score >= 80, "score={}", review.risk_score);
        assert_eq!(review.decision, GuardianDecision::Block);
    }

    #[tokio::test]
    async fn git_reset_hard_blocked() {
        let g = HeuristicGuardian::new();
        let review = g
            .review(
                "bash",
                &json!({"command": "git reset --hard HEAD~5"}),
                "resetting",
            )
            .await
            .unwrap();
        assert!(review.risk_score >= 80, "score={}", review.risk_score);
        assert_eq!(review.decision, GuardianDecision::Block);
    }

    // === HeuristicGuardian: path risk ===

    #[tokio::test]
    async fn system_path_write_blocked() {
        let g = HeuristicGuardian::new();
        let review = g
            .review(
                "write",
                &json!({"file_path": "/etc/passwd", "content": "bad"}),
                "writing system file",
            )
            .await
            .unwrap();
        assert!(review.risk_score >= 80, "score={}", review.risk_score);
        assert_eq!(review.decision, GuardianDecision::Block);
    }

    #[tokio::test]
    async fn temp_path_write_gets_base_tool_score() {
        let g = HeuristicGuardian::new();
        let review = g
            .review(
                "write",
                &json!({"file_path": "/tmp/test.txt", "content": "ok"}),
                "writing temp file",
            )
            .await
            .unwrap();
        // write base=40, /tmp/ path=10, max=40 -> AskUser
        assert!(review.risk_score >= 40 && review.risk_score < 80);
        assert_eq!(review.decision, GuardianDecision::AskUser);
    }

    #[tokio::test]
    async fn project_path_read_auto_approved() {
        let g = HeuristicGuardian::new();
        let review = g
            .review("read", &json!({"path": "./src/lib.rs"}), "reading")
            .await
            .unwrap();
        assert!(review.risk_score < 40);
        assert_eq!(review.decision, GuardianDecision::AutoApprove);
    }

    // === Decision thresholds ===

    #[test]
    fn decision_threshold_boundaries() {
        assert_eq!(decision_from_score(0), GuardianDecision::AutoApprove);
        assert_eq!(decision_from_score(39), GuardianDecision::AutoApprove);
        assert_eq!(decision_from_score(40), GuardianDecision::AskUser);
        assert_eq!(decision_from_score(79), GuardianDecision::AskUser);
        assert_eq!(decision_from_score(80), GuardianDecision::Block);
        assert_eq!(decision_from_score(100), GuardianDecision::Block);
    }

    #[test]
    fn review_from_score_derives_decision() {
        let r = GuardianReview::from_score(25, "safe".into());
        assert_eq!(r.decision, GuardianDecision::AutoApprove);

        let r = GuardianReview::from_score(55, "moderate".into());
        assert_eq!(r.decision, GuardianDecision::AskUser);

        let r = GuardianReview::from_score(90, "dangerous".into());
        assert_eq!(r.decision, GuardianDecision::Block);
    }

    // === LlmGuardian: response parsing ===

    #[test]
    fn parse_valid_response() {
        let review = LlmGuardian::parse_response("25|Safe read-only file access").unwrap();
        assert_eq!(review.risk_score, 25);
        assert_eq!(review.rationale, "Safe read-only file access");
        assert_eq!(review.decision, GuardianDecision::AutoApprove);
    }

    #[test]
    fn parse_high_score_response() {
        let review = LlmGuardian::parse_response("85|Destructive command detected").unwrap();
        assert_eq!(review.risk_score, 85);
        assert_eq!(review.decision, GuardianDecision::Block);
    }

    #[test]
    fn parse_response_with_whitespace() {
        let review = LlmGuardian::parse_response("  50 | Moderate risk editing  \n").unwrap();
        assert_eq!(review.risk_score, 50);
        assert_eq!(review.rationale, "Moderate risk editing");
        assert_eq!(review.decision, GuardianDecision::AskUser);
    }

    #[test]
    fn parse_invalid_no_pipe() {
        assert!(LlmGuardian::parse_response("just text").is_none());
    }

    #[test]
    fn parse_invalid_not_a_number() {
        assert!(LlmGuardian::parse_response("high|risky").is_none());
    }

    #[test]
    fn parse_invalid_empty_rationale() {
        assert!(LlmGuardian::parse_response("50|").is_none());
    }

    #[test]
    fn parse_response_with_pipe_in_rationale() {
        let review = LlmGuardian::parse_response("30|Safe command | no risk").unwrap();
        assert_eq!(review.risk_score, 30);
        assert_eq!(review.rationale, "Safe command | no risk");
    }

    // === LlmGuardian: integration ===

    #[tokio::test]
    async fn llm_guardian_uses_llm_response() {
        let g = LlmGuardian::new(Box::new(|_prompt: &str| {
            Box::pin(async { Ok("15|Low risk read operation".to_string()) })
        }));
        let review = g
            .review("read", &json!({"path": "src/main.rs"}), "reading code")
            .await
            .unwrap();
        assert_eq!(review.risk_score, 15);
        assert_eq!(review.decision, GuardianDecision::AutoApprove);
    }

    #[tokio::test]
    async fn llm_guardian_falls_back_on_parse_failure() {
        let g = LlmGuardian::new(Box::new(|_prompt: &str| {
            Box::pin(async { Ok("I think this is safe".to_string()) })
        }));
        let review = g
            .review("read", &json!({"path": "src/main.rs"}), "reading code")
            .await
            .unwrap();
        // Should fall back to heuristic: read=10, project path=20, max=20
        assert!(review.risk_score < 40);
        assert_eq!(review.decision, GuardianDecision::AutoApprove);
    }

    #[tokio::test]
    async fn llm_guardian_falls_back_on_error() {
        let g = LlmGuardian::new(Box::new(|_prompt: &str| {
            Box::pin(async { Err("API error".to_string()) })
        }));
        let review = g
            .review("bash", &json!({"command": "ls"}), "listing")
            .await
            .unwrap();
        // Fallback heuristic for ls: score=15
        assert!(review.risk_score < 40);
    }

    #[tokio::test]
    async fn llm_guardian_falls_back_on_timeout() {
        let g = LlmGuardian::with_timeout(
            Box::new(|_prompt: &str| {
                Box::pin(async {
                    tokio::time::sleep(std::time::Duration::from_secs(10)).await;
                    Ok("15|Safe".to_string())
                })
            }),
            std::time::Duration::from_millis(50), // very short timeout for test
        );
        let review = g
            .review("read", &json!({"path": "src/main.rs"}), "reading")
            .await
            .unwrap();
        // Should fall back to heuristic
        assert_eq!(review.decision, GuardianDecision::AutoApprove);
    }

    // === Edge cases ===

    #[tokio::test]
    async fn unknown_tool_gets_moderate_score() {
        let g = HeuristicGuardian::new();
        let review = g
            .review("some_custom_mcp_tool", &json!({}), "unknown")
            .await
            .unwrap();
        assert!(review.risk_score >= 40 && review.risk_score < 80);
        assert_eq!(review.decision, GuardianDecision::AskUser);
    }

    #[tokio::test]
    async fn bash_with_no_command_arg() {
        let g = HeuristicGuardian::new();
        let review = g.review("bash", &json!({}), "no command").await.unwrap();
        // Falls through to base score for bash (60)
        assert!(review.risk_score >= 40);
        assert_eq!(review.decision, GuardianDecision::AskUser);
    }

    #[tokio::test]
    async fn cd_then_safe_command() {
        let g = HeuristicGuardian::new();
        let review = g
            .review(
                "bash",
                &json!({"command": "cd /workspace && cargo test"}),
                "test",
            )
            .await
            .unwrap();
        assert!(review.risk_score < 40, "score={}", review.risk_score);
        assert_eq!(review.decision, GuardianDecision::AutoApprove);
    }

    #[test]
    fn prompt_truncates_long_args() {
        let long_content = "x".repeat(1000);
        let args = json!({"content": long_content});
        let prompt = LlmGuardian::build_prompt("write", &args, "test");
        assert!(prompt.len() < 700, "prompt too long: {}", prompt.len());
    }
}
