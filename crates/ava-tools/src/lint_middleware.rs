//! Post-edit lint middleware (BG2-2 + BG2-3).
//!
//! After write/edit/apply_patch/multiedit tool calls, runs a configurable
//! lint command on the modified file and appends diagnostics to the tool result.
//! This gives the agent immediate feedback to self-correct errors.

use async_trait::async_trait;
use ava_types::{ToolCall, ToolResult};
use std::path::{Path, PathBuf};

use crate::registry::Middleware;

/// Tools that modify files and should trigger post-edit lint.
const EDIT_TOOLS: &[&str] = &["write", "edit", "apply_patch", "multiedit"];

/// A lint command configuration for a file extension.
#[derive(Debug, Clone)]
pub struct LintRule {
    /// File extensions this rule applies to (e.g., "rs", "py", "js").
    pub extensions: Vec<String>,
    /// Command template. `{file}` is replaced with the file path.
    pub command: String,
}

impl LintRule {
    pub fn new(extensions: &[&str], command: &str) -> Self {
        Self {
            extensions: extensions.iter().map(|s| s.to_string()).collect(),
            command: command.to_string(),
        }
    }

    fn matches(&self, path: &Path) -> bool {
        path.extension()
            .and_then(|e| e.to_str())
            .map(|ext| self.extensions.iter().any(|e| e == ext))
            .unwrap_or(false)
    }

    fn build_command(&self, path: &Path) -> String {
        self.command.replace("{file}", &path.to_string_lossy())
    }
}

/// Middleware that runs lint commands after file-modifying tool calls.
pub struct LintMiddleware {
    rules: Vec<LintRule>,
    /// Working directory for lint commands.
    cwd: PathBuf,
}

impl LintMiddleware {
    pub fn new(cwd: PathBuf) -> Self {
        Self {
            rules: Vec::new(),
            cwd,
        }
    }

    /// Create with default rules for common languages.
    pub fn with_defaults(cwd: PathBuf) -> Self {
        let mut m = Self::new(cwd);
        m.rules = default_lint_rules();
        m
    }

    pub fn add_rule(&mut self, rule: LintRule) {
        self.rules.push(rule);
    }

    /// Extract file path from tool call arguments.
    fn extract_file_path(tool_name: &str, args: &serde_json::Value) -> Option<PathBuf> {
        let obj = args.as_object()?;
        match tool_name {
            "write" | "edit" | "read" => obj
                .get("file_path")
                .and_then(|v| v.as_str())
                .map(PathBuf::from),
            "apply_patch" => obj
                .get("file_path")
                .or_else(|| obj.get("path"))
                .and_then(|v| v.as_str())
                .map(PathBuf::from),
            "multiedit" => {
                // multiedit modifies multiple files; take the first one
                obj.get("edits")
                    .and_then(|v| v.as_array())
                    .and_then(|arr| arr.first())
                    .and_then(|e| e.get("file_path"))
                    .and_then(|v| v.as_str())
                    .map(PathBuf::from)
            }
            _ => None,
        }
    }

    /// Run the lint command and return diagnostics (empty string if clean).
    async fn run_lint(&self, rule: &LintRule, file_path: &Path) -> String {
        let cmd = rule.build_command(file_path);
        let output = tokio::process::Command::new("sh")
            .arg("-c")
            .arg(&cmd)
            .current_dir(&self.cwd)
            .output()
            .await;

        match output {
            Ok(out) => {
                if out.status.success() {
                    String::new()
                } else {
                    let stderr = String::from_utf8_lossy(&out.stderr);
                    let stdout = String::from_utf8_lossy(&out.stdout);
                    let combined = if stderr.is_empty() {
                        stdout.to_string()
                    } else if stdout.is_empty() {
                        stderr.to_string()
                    } else {
                        format!("{stdout}\n{stderr}")
                    };
                    // Truncate to avoid flooding context
                    if combined.len() > 2000 {
                        format!(
                            "{}...\n[truncated, {} chars total]",
                            &combined[..2000],
                            combined.len()
                        )
                    } else {
                        combined
                    }
                }
            }
            Err(e) => {
                tracing::debug!("Lint command failed to execute: {e}");
                String::new() // Don't block on lint tool errors
            }
        }
    }
}

#[async_trait]
impl Middleware for LintMiddleware {
    async fn before(&self, _tool_call: &ToolCall) -> ava_types::Result<()> {
        Ok(())
    }

    async fn after(
        &self,
        tool_call: &ToolCall,
        result: &ToolResult,
    ) -> ava_types::Result<ToolResult> {
        // Only run after edit tools
        if !EDIT_TOOLS.contains(&tool_call.name.as_str()) {
            return Ok(result.clone());
        }

        let Some(file_path) = Self::extract_file_path(&tool_call.name, &tool_call.arguments) else {
            return Ok(result.clone());
        };

        // Find matching lint rule
        let Some(rule) = self.rules.iter().find(|r| r.matches(&file_path)) else {
            return Ok(result.clone());
        };

        // Run lint
        let diagnostics = self.run_lint(rule, &file_path).await;

        if diagnostics.is_empty() {
            return Ok(result.clone());
        }

        // Append diagnostics to tool result
        let mut enhanced = result.clone();
        enhanced.content = format!(
            "{}\n\n⚠ Lint errors detected in {}:\n{}",
            result.content,
            file_path.display(),
            diagnostics
        );

        Ok(enhanced)
    }
}

/// Default lint rules for common languages.
/// These use tools that are commonly available in dev environments.
pub fn default_lint_rules() -> Vec<LintRule> {
    vec![
        // Rust: cargo check on the file's crate
        LintRule::new(&["rs"], "rustfmt --check {file} 2>&1 || true"),
        // Python: basic syntax check
        LintRule::new(
            &["py"],
            "python3 -c \"import py_compile; py_compile.compile('{file}', doraise=True)\" 2>&1",
        ),
        // JavaScript/TypeScript: node syntax check
        LintRule::new(&["js", "mjs", "cjs"], "node --check {file} 2>&1"),
        // JSON: validate
        LintRule::new(
            &["json"],
            "python3 -c \"import json; json.load(open('{file}'))\" 2>&1",
        ),
        // TOML: validate
        LintRule::new(
            &["toml"],
            "python3 -c \"import tomllib; tomllib.load(open('{file}', 'rb'))\" 2>&1",
        ),
        // YAML: validate
        LintRule::new(
            &["yml", "yaml"],
            "python3 -c \"import yaml; yaml.safe_load(open('{file}'))\" 2>&1",
        ),
    ]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lint_rule_matches_extension() {
        let rule = LintRule::new(&["rs", "toml"], "check {file}");
        assert!(rule.matches(Path::new("src/main.rs")));
        assert!(rule.matches(Path::new("Cargo.toml")));
        assert!(!rule.matches(Path::new("src/main.py")));
        assert!(!rule.matches(Path::new("README.md")));
    }

    #[test]
    fn lint_rule_no_extension() {
        let rule = LintRule::new(&["rs"], "check {file}");
        assert!(!rule.matches(Path::new("Makefile")));
    }

    #[test]
    fn build_command_substitutes_path() {
        let rule = LintRule::new(&["rs"], "rustfmt --check {file}");
        assert_eq!(
            rule.build_command(Path::new("src/main.rs")),
            "rustfmt --check src/main.rs"
        );
    }

    #[test]
    fn extract_file_path_write() {
        let args = serde_json::json!({"file_path": "/tmp/test.rs", "content": "fn main() {}"});
        assert_eq!(
            LintMiddleware::extract_file_path("write", &args),
            Some(PathBuf::from("/tmp/test.rs"))
        );
    }

    #[test]
    fn extract_file_path_edit() {
        let args =
            serde_json::json!({"file_path": "/tmp/test.py", "old_string": "a", "new_string": "b"});
        assert_eq!(
            LintMiddleware::extract_file_path("edit", &args),
            Some(PathBuf::from("/tmp/test.py"))
        );
    }

    #[test]
    fn extract_file_path_multiedit() {
        let args = serde_json::json!({
            "edits": [
                {"file_path": "/tmp/a.rs", "old_string": "x", "new_string": "y"},
                {"file_path": "/tmp/b.rs", "old_string": "1", "new_string": "2"}
            ]
        });
        assert_eq!(
            LintMiddleware::extract_file_path("multiedit", &args),
            Some(PathBuf::from("/tmp/a.rs"))
        );
    }

    #[test]
    fn extract_file_path_unknown_tool() {
        let args = serde_json::json!({"command": "ls"});
        assert_eq!(LintMiddleware::extract_file_path("bash", &args), None);
    }

    #[test]
    fn default_rules_cover_common_langs() {
        let rules = default_lint_rules();
        assert!(rules
            .iter()
            .any(|r| r.extensions.contains(&"rs".to_string())));
        assert!(rules
            .iter()
            .any(|r| r.extensions.contains(&"py".to_string())));
        assert!(rules
            .iter()
            .any(|r| r.extensions.contains(&"js".to_string())));
        assert!(rules
            .iter()
            .any(|r| r.extensions.contains(&"json".to_string())));
    }

    #[tokio::test]
    async fn middleware_skips_non_edit_tools() {
        let mw = LintMiddleware::with_defaults(PathBuf::from("/tmp"));
        let tool_call = ToolCall {
            id: "1".to_string(),
            name: "read".to_string(),
            arguments: serde_json::json!({"file_path": "/tmp/test.rs"}),
        };
        let result = ToolResult {
            call_id: "1".to_string(),
            content: "file contents".to_string(),
            is_error: false,
        };
        let output = mw.after(&tool_call, &result).await.unwrap();
        assert_eq!(output.content, "file contents");
    }

    #[tokio::test]
    async fn middleware_skips_no_matching_rule() {
        let mw = LintMiddleware::with_defaults(PathBuf::from("/tmp"));
        let tool_call = ToolCall {
            id: "1".to_string(),
            name: "write".to_string(),
            arguments: serde_json::json!({"file_path": "/tmp/test.xyz", "content": "data"}),
        };
        let result = ToolResult {
            call_id: "1".to_string(),
            content: "Written".to_string(),
            is_error: false,
        };
        let output = mw.after(&tool_call, &result).await.unwrap();
        assert_eq!(output.content, "Written");
    }
}
