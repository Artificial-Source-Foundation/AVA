use crate::tags::{RiskLevel, SafetyTag};

use super::CommandClassification;

/// Check for patterns that should be BLOCKED (Critical risk).
pub(super) fn check_blocked_patterns(lower: &str, _original: &str) -> Option<String> {
    // rm -rf / or rm -rf ~ or rm -rf /*
    if (lower.contains("rm ") || lower.starts_with("rm "))
        && (lower.contains("-rf") || lower.contains("-fr"))
    {
        // Extract the path after rm -rf
        let after_flags = lower
            .replace("rm", "")
            .replace("-rf", "")
            .replace("-fr", "")
            .replace("-r", "")
            .replace("-f", "")
            .trim()
            .to_string();
        let target = after_flags.trim();
        if target == "/" || target == "~" || target == "/*" || target == "~/*" {
            return Some(format!("rm -rf on critical path: {target}"));
        }
    }

    // sudo
    if lower.starts_with("sudo ") || lower == "sudo" {
        return Some("sudo command requires elevated privileges".to_string());
    }

    // curl/wget piped to shell
    if (lower.contains("curl ") || lower.contains("wget "))
        && (lower.contains("| sh")
            || lower.contains("| bash")
            || lower.contains("|sh")
            || lower.contains("|bash"))
    {
        return Some("Piping downloaded content to shell is dangerous".to_string());
    }

    // dd if=
    if lower.starts_with("dd ") && lower.contains("if=") {
        return Some("dd can overwrite disk data".to_string());
    }

    // mkfs
    if lower.starts_with("mkfs") {
        return Some("mkfs will format a filesystem".to_string());
    }

    // Writing to /dev/
    if lower.contains("> /dev/") || lower.contains(">/dev/") {
        return Some("Writing to device files is dangerous".to_string());
    }

    // Fork bomb
    if lower.contains(":(){ :|:& };:") || lower.contains(":(){ :|:&};:") {
        return Some("Fork bomb detected".to_string());
    }

    None
}

/// Check if command is safe (read-only, no side effects).
pub(super) fn is_safe_command(first_word: &str) -> bool {
    matches!(
        first_word,
        "ls" | "cat"
            | "echo"
            | "grep"
            | "rg"
            | "find"
            | "head"
            | "tail"
            | "wc"
            | "pwd"
            | "date"
            | "which"
            | "whoami"
            | "env"
            | "printenv"
            | "uname"
            | "id"
            | "file"
            | "stat"
            | "du"
            | "df"
            | "tree"
            | "less"
            | "more"
            | "sort"
            | "uniq"
            | "diff"
            | "comm"
            | "cut"
            | "tr"
            | "basename"
            | "dirname"
            | "realpath"
            | "readlink"
            | "tee"
            | "true"
            | "false"
            | "test"
            | "["
            | "printf"
    )
}

/// Check if command matches safe git subcommands.
pub fn is_safe_git_command(lower: &str) -> bool {
    let safe_git = [
        "git status",
        "git log",
        "git diff",
        "git branch",
        "git show",
        "git tag",
        "git remote",
        "git stash list",
        "git shortlog",
        "git describe",
        "git rev-parse",
        "git ls-files",
        "git blame",
    ];
    safe_git.iter().any(|cmd| lower.starts_with(cmd))
}

/// Check if command is low risk (build/test tools).
pub(super) fn is_low_risk_command(first_word: &str, lower: &str) -> bool {
    // Safe git subcommands
    if first_word == "git" && is_safe_git_command(lower) {
        return true;
    }

    // Build/test/dev tools
    if matches!(
        first_word,
        "cargo"
            | "npm"
            | "npx"
            | "yarn"
            | "pnpm"
            | "bun"
            | "python"
            | "python3"
            | "node"
            | "deno"
            | "go"
            | "rustc"
            | "gcc"
            | "make"
            | "cmake"
            | "just"
            | "nix"
    ) {
        // Check for specific safe subcommands
        let safe_subs = [
            "test", "build", "clippy", "check", "run", "install", "fmt", "lint", "format", "bench",
            "doc", "audit", "outdated",
        ];
        if safe_subs.iter().any(|sub| lower.contains(sub)) {
            return true;
        }
    }

    // Standalone safe dev commands
    matches!(
        first_word,
        "rustfmt" | "prettier" | "eslint" | "biome" | "tsc" | "esbuild" | "vite" | "webpack"
    )
}

/// Check for high-risk patterns that should warn but not block.
pub(super) fn check_high_risk_patterns(
    first_word: &str,
    lower: &str,
    _words: &[String],
) -> Option<CommandClassification> {
    let mut warnings = Vec::new();
    let mut tags = vec![SafetyTag::Destructive];

    // rm -rf (non-root paths — root is already blocked)
    if first_word == "rm" && (lower.contains("-rf") || lower.contains("-fr")) {
        warnings.push("rm -rf can recursively delete files".to_string());
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags,
            warnings,
            blocked: false,
            reason: None,
        });
    }

    // git push --force / -f
    if lower.starts_with("git push") && (lower.contains("--force") || lower.contains("-f")) {
        warnings.push("Force push can overwrite remote history".to_string());
        tags = vec![SafetyTag::Destructive, SafetyTag::NetworkAccess];
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags,
            warnings,
            blocked: false,
            reason: None,
        });
    }

    // git reset --hard
    if lower.starts_with("git reset") && lower.contains("--hard") {
        warnings.push("git reset --hard discards uncommitted changes".to_string());
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags,
            warnings,
            blocked: false,
            reason: None,
        });
    }

    // chmod 777
    if lower.contains("chmod") && lower.contains("777") {
        warnings.push("chmod 777 makes files world-writable".to_string());
        tags = vec![SafetyTag::SystemModification];
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags,
            warnings,
            blocked: false,
            reason: None,
        });
    }

    // SQL destructive operations
    if lower.contains("drop table") || lower.contains("drop database") {
        warnings.push("SQL DROP operation will permanently delete data".to_string());
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags,
            warnings,
            blocked: false,
            reason: None,
        });
    }

    if lower.contains("delete from") && !lower.contains("where") {
        warnings.push("DELETE without WHERE clause affects all rows".to_string());
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags,
            warnings,
            blocked: false,
            reason: None,
        });
    }

    if lower.contains("truncate") {
        warnings.push("TRUNCATE will remove all data from the table".to_string());
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags,
            warnings,
            blocked: false,
            reason: None,
        });
    }

    None
}

/// Check for medium-risk patterns.
pub(super) fn check_medium_risk_patterns(
    first_word: &str,
    lower: &str,
    _words: &[String],
) -> Option<CommandClassification> {
    // rm (single file, no -rf)
    if first_word == "rm" && !lower.contains("-rf") && !lower.contains("-fr") {
        return Some(CommandClassification {
            risk_level: RiskLevel::Medium,
            tags: vec![SafetyTag::DeleteFile],
            warnings: vec!["rm will delete files".to_string()],
            blocked: false,
            reason: None,
        });
    }

    // kill -9
    if first_word == "kill" && lower.contains("-9") {
        return Some(CommandClassification {
            risk_level: RiskLevel::Medium,
            tags: vec![SafetyTag::SystemModification],
            warnings: vec!["kill -9 forcefully terminates a process".to_string()],
            blocked: false,
            reason: None,
        });
    }

    // pkill / killall
    if matches!(first_word, "pkill" | "killall") {
        return Some(CommandClassification {
            risk_level: RiskLevel::Medium,
            tags: vec![SafetyTag::SystemModification],
            warnings: vec![format!("{first_word} can terminate multiple processes")],
            blocked: false,
            reason: None,
        });
    }

    // git operations that aren't safe or high risk
    if first_word == "git" && !is_safe_git_command(lower) {
        return Some(CommandClassification {
            risk_level: RiskLevel::Medium,
            tags: vec![SafetyTag::ExecuteCommand],
            warnings: vec![],
            blocked: false,
            reason: None,
        });
    }

    None
}

/// Check for network access commands.
pub(super) fn is_network_command(first_word: &str, lower: &str) -> bool {
    matches!(
        first_word,
        "curl" | "wget" | "nc" | "ncat" | "ssh" | "scp" | "rsync" | "ftp" | "sftp"
    ) || lower.contains("http://")
        || lower.contains("https://")
}
