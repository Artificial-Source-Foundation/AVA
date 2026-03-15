use crate::tags::{RiskLevel, SafetyTag};

use super::CommandClassification;

/// Check for patterns that should be BLOCKED (Critical risk).
/// These are ALWAYS denied, even in auto-approve mode.
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
