//! Safety checker registry — composable safety checks for tool invocations.
//!
//! Provides a trait-based registry where multiple checkers can be registered.
//! Any `Deny` result short-circuits; otherwise the most restrictive result wins.

use std::path::{Path, PathBuf};

/// Result of a safety check.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CheckResult {
    /// The operation is safe to proceed.
    Allow,
    /// The operation must be denied with a reason.
    Deny(String),
    /// The operation requires user confirmation with a reason.
    Ask(String),
}

/// A composable safety checker for tool invocations.
pub trait SafetyChecker: Send + Sync {
    /// Human-readable name for this checker.
    fn name(&self) -> &str;

    /// Check whether a tool invocation with the given arguments is safe.
    fn check(&self, tool_name: &str, args: &serde_json::Value) -> CheckResult;
}

// ---------------------------------------------------------------------------
// Built-in checkers
// ---------------------------------------------------------------------------

/// Validates that file paths in tool arguments stay within the workspace.
pub struct PathChecker {
    workspace_root: PathBuf,
}

impl PathChecker {
    pub fn new(workspace_root: impl Into<PathBuf>) -> Self {
        Self {
            workspace_root: workspace_root.into(),
        }
    }

    fn normalize(path: &Path, base: &Path) -> PathBuf {
        let abs = if path.is_absolute() {
            path.to_path_buf()
        } else {
            base.join(path)
        };
        let mut out = PathBuf::new();
        for c in abs.components() {
            match c {
                std::path::Component::CurDir => {}
                std::path::Component::ParentDir => {
                    out.pop();
                }
                other => out.push(other),
            }
        }
        out
    }

    fn check_path(&self, path_str: &str) -> CheckResult {
        let normalized = Self::normalize(Path::new(path_str), &self.workspace_root);
        let ws = Self::normalize(&self.workspace_root, &self.workspace_root);
        if normalized.starts_with(&ws) {
            CheckResult::Allow
        } else {
            CheckResult::Ask(format!(
                "Path '{}' is outside workspace '{}'",
                path_str,
                self.workspace_root.display()
            ))
        }
    }
}

impl SafetyChecker for PathChecker {
    fn name(&self) -> &str {
        "path_checker"
    }

    fn check(&self, tool_name: &str, args: &serde_json::Value) -> CheckResult {
        // Check common path fields in tool arguments.
        let path_keys = ["path", "file_path", "target", "destination"];

        // Only check tools that operate on files.
        let file_tools = [
            "read",
            "write",
            "edit",
            "glob",
            "grep",
            "apply_patch",
            "multiedit",
        ];
        if !file_tools.contains(&tool_name) {
            return CheckResult::Allow;
        }

        for key in &path_keys {
            if let Some(serde_json::Value::String(path)) = args.get(*key) {
                let result = self.check_path(path);
                if !matches!(result, CheckResult::Allow) {
                    return result;
                }
            }
        }

        CheckResult::Allow
    }
}

/// Checks bash commands against a blocklist of dangerous patterns.
pub struct CommandChecker {
    blocklist: Vec<String>,
}

impl CommandChecker {
    pub fn new(blocklist: Vec<String>) -> Self {
        Self { blocklist }
    }

    /// Default blocklist of dangerous commands.
    pub fn default_blocklist() -> Self {
        Self {
            blocklist: vec![
                "rm -rf /".to_string(),
                "mkfs".to_string(),
                "dd if=".to_string(),
                ":(){:|:&};:".to_string(),
                "chmod -R 777 /".to_string(),
                "shutdown".to_string(),
                "reboot".to_string(),
                "init 0".to_string(),
                "halt".to_string(),
            ],
        }
    }
}

impl SafetyChecker for CommandChecker {
    fn name(&self) -> &str {
        "command_checker"
    }

    fn check(&self, tool_name: &str, args: &serde_json::Value) -> CheckResult {
        if tool_name != "bash" {
            return CheckResult::Allow;
        }

        let Some(serde_json::Value::String(command)) = args.get("command") else {
            return CheckResult::Allow;
        };

        for blocked in &self.blocklist {
            if command.contains(blocked.as_str()) {
                return CheckResult::Deny(format!("Command contains blocked pattern: '{blocked}'"));
            }
        }

        CheckResult::Allow
    }
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

/// Registry of safety checkers. Evaluates all registered checkers on each
/// tool invocation. Any `Deny` wins immediately; otherwise `Ask` wins over `Allow`.
pub struct CheckerRegistry {
    checkers: Vec<Box<dyn SafetyChecker>>,
}

impl CheckerRegistry {
    pub fn new() -> Self {
        Self {
            checkers: Vec::new(),
        }
    }

    /// Register a new safety checker.
    pub fn register(&mut self, checker: impl SafetyChecker + 'static) {
        self.checkers.push(Box::new(checker));
    }

    /// Run all checkers. Returns the most restrictive result.
    /// - Any `Deny` → `Deny` (short-circuits)
    /// - Any `Ask` → `Ask`
    /// - Otherwise → `Allow`
    pub fn check_all(&self, tool_name: &str, args: &serde_json::Value) -> CheckResult {
        let mut ask_reason: Option<String> = None;

        for checker in &self.checkers {
            match checker.check(tool_name, args) {
                CheckResult::Deny(reason) => return CheckResult::Deny(reason),
                CheckResult::Ask(reason) => {
                    if ask_reason.is_none() {
                        ask_reason = Some(reason);
                    }
                }
                CheckResult::Allow => {}
            }
        }

        match ask_reason {
            Some(reason) => CheckResult::Ask(reason),
            None => CheckResult::Allow,
        }
    }

    /// Number of registered checkers.
    pub fn len(&self) -> usize {
        self.checkers.len()
    }

    /// Whether the registry has no checkers.
    pub fn is_empty(&self) -> bool {
        self.checkers.is_empty()
    }
}

impl Default for CheckerRegistry {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    // -- PathChecker tests --

    #[test]
    fn path_checker_allows_workspace_paths() {
        let checker = PathChecker::new("/home/user/project");
        let result = checker.check(
            "read",
            &json!({"file_path": "/home/user/project/src/main.rs"}),
        );
        assert_eq!(result, CheckResult::Allow);
    }

    #[test]
    fn path_checker_allows_relative_paths() {
        let checker = PathChecker::new("/home/user/project");
        let result = checker.check("write", &json!({"file_path": "src/lib.rs"}));
        assert_eq!(result, CheckResult::Allow);
    }

    #[test]
    fn path_checker_asks_for_outside_paths() {
        let checker = PathChecker::new("/home/user/project");
        let result = checker.check("read", &json!({"file_path": "/etc/passwd"}));
        assert!(matches!(result, CheckResult::Ask(_)));
    }

    #[test]
    fn path_checker_catches_traversal() {
        let checker = PathChecker::new("/home/user/project");
        let result = checker.check("read", &json!({"file_path": "../../../etc/shadow"}));
        assert!(matches!(result, CheckResult::Ask(_)));
    }

    #[test]
    fn path_checker_ignores_non_file_tools() {
        let checker = PathChecker::new("/home/user/project");
        let result = checker.check("bash", &json!({"command": "ls /etc"}));
        assert_eq!(result, CheckResult::Allow);
    }

    #[test]
    fn path_checker_checks_multiple_path_keys() {
        let checker = PathChecker::new("/home/user/project");
        let result = checker.check("write", &json!({"path": "/tmp/outside.txt"}));
        assert!(matches!(result, CheckResult::Ask(_)));
    }

    // -- CommandChecker tests --

    #[test]
    fn command_checker_blocks_dangerous_commands() {
        let checker = CommandChecker::default_blocklist();
        let result = checker.check("bash", &json!({"command": "rm -rf /"}));
        assert!(matches!(result, CheckResult::Deny(_)));
    }

    #[test]
    fn command_checker_blocks_fork_bomb() {
        let checker = CommandChecker::default_blocklist();
        let result = checker.check("bash", &json!({"command": ":(){:|:&};:"}));
        assert!(matches!(result, CheckResult::Deny(_)));
    }

    #[test]
    fn command_checker_allows_safe_commands() {
        let checker = CommandChecker::default_blocklist();
        let result = checker.check("bash", &json!({"command": "cargo test"}));
        assert_eq!(result, CheckResult::Allow);
    }

    #[test]
    fn command_checker_ignores_non_bash_tools() {
        let checker = CommandChecker::default_blocklist();
        let result = checker.check("read", &json!({"file_path": "rm -rf /"}));
        assert_eq!(result, CheckResult::Allow);
    }

    #[test]
    fn command_checker_custom_blocklist() {
        let checker = CommandChecker::new(vec!["curl".to_string(), "wget".to_string()]);
        let result = checker.check("bash", &json!({"command": "curl http://evil.com"}));
        assert!(matches!(result, CheckResult::Deny(_)));
    }

    // -- CheckerRegistry tests --

    #[test]
    fn empty_registry_allows_everything() {
        let registry = CheckerRegistry::new();
        let result = registry.check_all("bash", &json!({"command": "rm -rf /"}));
        assert_eq!(result, CheckResult::Allow);
    }

    #[test]
    fn registry_deny_wins() {
        let mut registry = CheckerRegistry::new();
        registry.register(PathChecker::new("/home/user/project"));
        registry.register(CommandChecker::default_blocklist());

        // Bash with rm -rf / should be denied by CommandChecker
        let result = registry.check_all("bash", &json!({"command": "rm -rf /"}));
        assert!(matches!(result, CheckResult::Deny(_)));
    }

    #[test]
    fn registry_ask_when_no_deny() {
        let mut registry = CheckerRegistry::new();
        registry.register(PathChecker::new("/home/user/project"));

        let result = registry.check_all("read", &json!({"file_path": "/etc/passwd"}));
        assert!(matches!(result, CheckResult::Ask(_)));
    }

    #[test]
    fn registry_allow_when_all_pass() {
        let mut registry = CheckerRegistry::new();
        registry.register(PathChecker::new("/home/user/project"));
        registry.register(CommandChecker::default_blocklist());

        let result = registry.check_all(
            "read",
            &json!({"file_path": "/home/user/project/src/main.rs"}),
        );
        assert_eq!(result, CheckResult::Allow);
    }

    #[test]
    fn registry_len_and_is_empty() {
        let mut registry = CheckerRegistry::new();
        assert!(registry.is_empty());
        assert_eq!(registry.len(), 0);

        registry.register(CommandChecker::default_blocklist());
        assert!(!registry.is_empty());
        assert_eq!(registry.len(), 1);
    }

    #[test]
    fn registry_multiple_asks_uses_first() {
        let mut registry = CheckerRegistry::new();
        registry.register(PathChecker::new("/workspace"));
        registry.register(PathChecker::new("/other-workspace"));

        let result = registry.check_all("read", &json!({"file_path": "/etc/passwd"}));
        assert!(matches!(result, CheckResult::Ask(_)));
    }

    #[test]
    fn deny_short_circuits() {
        // If a Deny is encountered, we get Deny even if another checker would Ask
        let mut registry = CheckerRegistry::new();
        registry.register(CommandChecker::default_blocklist());
        registry.register(PathChecker::new("/workspace"));

        let result = registry.check_all("bash", &json!({"command": "rm -rf /"}));
        assert!(matches!(result, CheckResult::Deny(_)));
    }
}
