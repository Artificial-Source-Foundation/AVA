//! LLM-based read-only permission judge — classifies tool calls as read-only or write operations.
//!
//! Uses heuristics to determine whether a tool call is read-only, enabling
//! automatic approval of safe operations without user confirmation.

use serde_json::Value;

/// Result of read-only classification.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ReadOnlyResult {
    /// The tool call is definitively read-only.
    ReadOnly,
    /// The tool call performs a write or destructive operation.
    WriteOperation,
    /// Cannot determine — treat conservatively.
    Unknown,
}

/// Read-only bash commands that never modify state.
const READONLY_COMMANDS: &[&str] = &[
    "ls",
    "cat",
    "head",
    "tail",
    "grep",
    "find",
    "which",
    "whereis",
    "wc",
    "file",
    "stat",
    "du",
    "df",
    "pwd",
    "echo",
    "env",
    "printenv",
    "whoami",
    "hostname",
    "uname",
    "date",
    "uptime",
    "free",
    "top",
    "ps",
    "id",
    "diff",
    "comm",
    "sort",
    "uniq",
    "tr",
    "cut",
    "awk",
    "sed",
    "less",
    "more",
    "tree",
    "realpath",
    "readlink",
    "type",
    "test",
    "true",
    "false",
    "git status",
    "git log",
    "git diff",
    "git show",
    "git branch",
    "git remote",
    "cargo check",
    "cargo clippy",
    "cargo test",
    "cargo doc",
    "npm test",
    "npm run lint",
    "npx tsc --noEmit",
    "rg",
    "fd",
    "bat",
    "exa",
];

/// Write-indicative bash patterns.
const WRITE_PATTERNS: &[&str] = &[
    "rm ",
    "rm\t",
    "mv ",
    "cp ",
    "chmod ",
    "chown ",
    "mkdir ",
    "rmdir ",
    "touch ",
    "truncate ",
    "> ",
    ">> ",
    "tee ",
    "install ",
    "uninstall ",
    "apt ",
    "yum ",
    "brew ",
    "pip install",
    "npm install",
    "cargo install",
    "git push",
    "git commit",
    "git reset",
    "git checkout",
    "git rebase",
    "kill ",
    "pkill ",
    "sudo ",
];

/// Classify a tool call as read-only, write, or unknown.
///
/// Heuristic rules:
/// - `read`, `glob`, `grep`, `codebase_search` -> ReadOnly
/// - `write`, `edit`, `apply_patch` -> WriteOperation
/// - `bash` -> parse the command for read-only or write indicators
pub fn is_readonly_tool(tool_name: &str, args: &Value) -> ReadOnlyResult {
    match tool_name {
        "read" | "glob" | "grep" | "codebase_search" | "todo_read" | "diagnostics" => {
            ReadOnlyResult::ReadOnly
        }
        "write" | "edit" | "apply_patch" | "multiedit" | "todo_write" => {
            ReadOnlyResult::WriteOperation
        }
        "bash" => classify_bash_args(args),
        _ => ReadOnlyResult::Unknown,
    }
}

/// Classify a bash command from its tool arguments.
fn classify_bash_args(args: &Value) -> ReadOnlyResult {
    let command = args.get("command").and_then(|v| v.as_str()).unwrap_or("");

    if command.is_empty() {
        return ReadOnlyResult::Unknown;
    }

    let trimmed = command.trim();

    // Check for write patterns first (higher priority)
    for pattern in WRITE_PATTERNS {
        if trimmed.contains(pattern) {
            return ReadOnlyResult::WriteOperation;
        }
    }

    // Check if the base command is read-only
    let base_cmd = trimmed.split_whitespace().next().unwrap_or("");
    for ro_cmd in READONLY_COMMANDS {
        if *ro_cmd == base_cmd || trimmed.starts_with(ro_cmd) {
            return ReadOnlyResult::ReadOnly;
        }
    }

    ReadOnlyResult::Unknown
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn read_tool_is_readonly() {
        assert_eq!(
            is_readonly_tool("read", &json!({"file_path": "src/main.rs"})),
            ReadOnlyResult::ReadOnly
        );
    }

    #[test]
    fn write_tool_is_write_operation() {
        assert_eq!(
            is_readonly_tool("write", &json!({"file_path": "out.txt", "content": "hi"})),
            ReadOnlyResult::WriteOperation
        );
    }

    #[test]
    fn bash_ls_is_readonly() {
        assert_eq!(
            is_readonly_tool("bash", &json!({"command": "ls -la"})),
            ReadOnlyResult::ReadOnly
        );
    }

    #[test]
    fn bash_rm_is_write() {
        assert_eq!(
            is_readonly_tool("bash", &json!({"command": "rm -rf target/"})),
            ReadOnlyResult::WriteOperation
        );
    }

    #[test]
    fn unknown_tool_returns_unknown() {
        assert_eq!(
            is_readonly_tool("custom_mcp_tool", &json!({})),
            ReadOnlyResult::Unknown
        );
    }
}
