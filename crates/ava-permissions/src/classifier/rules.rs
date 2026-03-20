use crate::tags::{RiskLevel, SafetyTag};

use super::CommandClassification;

/// Protected path prefixes that must never be targeted by a recursive rm.
const CRITICAL_PATH_PREFIXES: &[&str] = &[
    "/", "~", "/home", "/root", "/etc", "/usr", "/var", "/boot", "/sys", "/proc", "/dev", "/lib",
    "/lib64", "/bin", "/sbin", "/opt",
];

/// Known shell binary names used in pipe-to-shell detection.
const SHELL_BINARIES: &[&str] = &[
    "sh", "bash", "zsh", "fish", "dash", "ksh", "csh", "tcsh", "ash",
];

/// Collect all single-character flags from an argument list.
///
/// Handles both combined flags (`-rf`) and separate flags (`-r -f`).
fn collect_flags(tokens: &[&str]) -> std::collections::HashSet<char> {
    let mut flags = std::collections::HashSet::new();
    for token in tokens.iter().skip(1) {
        // Stop processing flags at `--` (end-of-options sentinel)
        if *token == "--" {
            break;
        }
        if let Some(rest) = token.strip_prefix('-') {
            if !rest.is_empty() && !rest.starts_with('-') {
                flags.extend(rest.chars());
            }
        }
    }
    flags
}

/// Return true if the flags include both recursive (`r`/`R`) and force (`f`) flags.
fn has_recursive_force(flags: &std::collections::HashSet<char>) -> bool {
    (flags.contains(&'r') || flags.contains(&'R')) && flags.contains(&'f')
}

/// Extract non-flag path arguments from a tokenised rm command.
///
/// Strips leading/trailing ASCII quotes and rejects tokens containing
/// shell metacharacters (`;`, `&&`, `||`, `|`, `` ` ``), env-var
/// references (`$`), and unicode trickery (zero-width chars).
fn extract_rm_paths(tokens: &[&str]) -> Vec<String> {
    let mut paths = Vec::new();
    let mut past_double_dash = false;
    for token in tokens.iter().skip(1) {
        if *token == "--" {
            past_double_dash = true;
            continue;
        }
        // Before `--`, pure flag tokens are skipped
        if !past_double_dash && token.starts_with('-') {
            continue;
        }
        // Strip wrapping quotes
        let stripped = token.trim_matches(|c| c == '"' || c == '\'');
        // Reject tokens that contain shell metacharacters or env-var references
        if stripped
            .chars()
            .any(|c| matches!(c, ';' | '|' | '`' | '$' | '&'))
        {
            continue;
        }
        // Reject tokens with zero-width or unusual unicode that could bypass matching
        if stripped
            .chars()
            .any(|c| (c as u32) < 0x20 || c == '\u{200b}')
        {
            continue;
        }
        if !stripped.is_empty() {
            paths.push(stripped.to_string());
        }
    }
    paths
}

/// Return true if the path (after normalisation) matches a critical prefix.
fn path_is_critical(path: &str) -> bool {
    // Normalise: collapse multiple slashes, remove trailing slash (except root)
    let normalised = {
        let mut s = path.trim().to_string();
        // Collapse runs of '/' to a single '/'
        while s.contains("//") {
            s = s.replace("//", "/");
        }
        // Remove trailing slash unless it IS the root
        if s.len() > 1 && s.ends_with('/') {
            s.pop();
        }
        s
    };

    for prefix in CRITICAL_PATH_PREFIXES {
        // Exact match
        if normalised == *prefix {
            return true;
        }
        // "prefix/*" or "prefix/" glob patterns
        if normalised == format!("{prefix}/*") || normalised == format!("{prefix}/") {
            return true;
        }
        // Path IS just the prefix followed by a wildcard at the top level
        // e.g. "/home/*" when prefix is "/home"
        if let Some(rest) = normalised.strip_prefix(*prefix) {
            if rest == "/*" || rest == "*" {
                return true;
            }
        }
    }

    false
}

/// Detect pipe-to-shell patterns: `| sh`, `| bash`, `| /bin/sh`, etc.
fn has_pipe_to_shell(cmd: &str) -> bool {
    if let Some(after_pipe) = cmd.split_once('|').map(|(_, r)| r.trim()) {
        let first_word = after_pipe.split_ascii_whitespace().next().unwrap_or("");
        // Strip any leading path component (e.g. /bin/bash → bash)
        let binary = first_word.rsplit('/').next().unwrap_or(first_word);
        // Strip trailing flags or arguments
        let binary = binary.split_ascii_whitespace().next().unwrap_or(binary);
        return SHELL_BINARIES.contains(&binary);
    }
    false
}

/// Check for patterns that should be BLOCKED (Critical risk).
/// These are ALWAYS denied, even in auto-approve mode.
pub(super) fn check_blocked_patterns(lower: &str, _original: &str) -> Option<String> {
    // rm with recursive+force flags on a critical path
    {
        let tokens: Vec<&str> = lower.split_ascii_whitespace().collect();
        if tokens.first().copied() == Some("rm") {
            let flags = collect_flags(&tokens);
            if has_recursive_force(&flags) {
                for path in extract_rm_paths(&tokens) {
                    if path_is_critical(&path) {
                        return Some(format!("rm -rf on critical path: {path}"));
                    }
                }
            }
        }
    }

    // sudo
    if lower.starts_with("sudo ") || lower == "sudo" {
        return Some("sudo command requires elevated privileges".to_string());
    }

    // curl/wget piped to shell
    if (lower.contains("curl ") || lower.contains("wget ")) && has_pipe_to_shell(lower) {
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

    // chmod 777 / or chown root /
    if lower.contains("chmod") && lower.contains("777") && lower.contains(" /") {
        let after_777 = lower.split("777").nth(1).unwrap_or("").trim();
        if after_777 == "/" || after_777.starts_with("/ ") {
            return Some("chmod 777 on root filesystem".to_string());
        }
    }

    None
}

/// Check if command matches safe git subcommands (read-only git operations).
/// Exposed publicly for use by the git_read tool.
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

/// Check for high-risk patterns that should warn but not block.
/// These require user confirmation in standard policy.
pub(super) fn check_high_risk_patterns(
    first_word: &str,
    lower: &str,
    _words: &[String],
) -> Option<CommandClassification> {
    let mut warnings = Vec::new();
    let mut tags = vec![SafetyTag::Destructive];

    // rm -rf (non-root paths -- root is already blocked)
    let rm_has_recursive_force = first_word == "rm" && {
        let tokens: Vec<&str> = lower.split_ascii_whitespace().collect();
        let flags = collect_flags(&tokens);
        has_recursive_force(&flags)
    };
    if rm_has_recursive_force {
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

    // chmod 777 (non-root -- root is blocked above)
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

    // kill -9
    if first_word == "kill" && lower.contains("-9") {
        warnings.push("kill -9 forcefully terminates a process".to_string());
        tags = vec![SafetyTag::SystemModification];
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags,
            warnings,
            blocked: false,
            reason: None,
        });
    }

    // pkill / killall
    if matches!(first_word, "pkill" | "killall") {
        warnings.push(format!("{first_word} can terminate multiple processes"));
        tags = vec![SafetyTag::SystemModification];
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags,
            warnings,
            blocked: false,
            reason: None,
        });
    }

    // npm publish / cargo publish
    if (lower.starts_with("npm ") && lower.contains("publish"))
        || (lower.starts_with("cargo ") && lower.contains("publish"))
    {
        warnings.push("Publishing packages is irreversible".to_string());
        tags = vec![SafetyTag::NetworkAccess];
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags,
            warnings,
            blocked: false,
            reason: None,
        });
    }

    // docker rm / docker rmi
    if lower.starts_with("docker ") && (lower.contains(" rm") || lower.contains(" rmi")) {
        warnings.push("Removing Docker containers/images".to_string());
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags,
            warnings,
            blocked: false,
            reason: None,
        });
    }

    // Redirect to system paths (> /etc/*, > /usr/*, etc.)
    if lower.contains("> /etc/")
        || lower.contains(">/etc/")
        || lower.contains("> /usr/")
        || lower.contains(">/usr/")
        || lower.contains("> /var/")
        || lower.contains(">/var/")
        || lower.contains("> /sys/")
        || lower.contains(">/sys/")
    {
        warnings.push("Redirecting output to system path".to_string());
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

    // Plain network commands (curl, wget without pipe to shell)
    if matches!(
        first_word,
        "curl" | "wget" | "nc" | "ncat" | "ssh" | "scp" | "rsync" | "ftp" | "sftp"
    ) {
        warnings.push("Command performs network access".to_string());
        tags = vec![SafetyTag::NetworkAccess];
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
