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
pub(super) fn check_blocked_patterns(lower: &str, original: &str) -> Option<String> {
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

    // Additional dangerous path detection (includes Windows paths and path normalization)
    if crate::dangerous_paths::is_dangerous_rm_command(original) {
        return Some("rm with recursive flag on dangerous system path".to_string());
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

    // Writing to /dev/ (but allow 2>/dev/null and &>/dev/null which are safe stderr/output redirects)
    if (lower.contains("> /dev/") || lower.contains(">/dev/"))
        && !lower.contains("2>/dev/null")
        && !lower.contains("2> /dev/null")
        && !lower.contains("&>/dev/null")
        && !lower.contains("&> /dev/null")
    {
        return Some("Writing to device files is dangerous".to_string());
    }

    // Fork bomb
    if lower.contains(":(){ :|:& };:") || lower.contains(":(){ :|:&};:") {
        return Some("Fork bomb detected".to_string());
    }

    // find -exec rm -rf / find -delete — semantic bypass of the rm block
    // e.g. `find / -exec rm -rf {} +`  or  `find /etc -delete`
    if lower.starts_with("find ") {
        // -delete flag recursively removes every matched file/directory
        let tokens: Vec<&str> = lower.split_ascii_whitespace().collect();
        if tokens.contains(&"-delete") {
            return Some("find -delete can recursively delete files".to_string());
        }
        // -exec rm with recursive+force is equivalent to rm -rf
        if lower.contains("-exec") && lower.contains(" rm ") {
            let rm_idx = tokens.iter().position(|t| *t == "rm");
            if let Some(idx) = rm_idx {
                let flags = collect_flags(&tokens[idx..]);
                if has_recursive_force(&flags) {
                    return Some("find -exec rm -rf is equivalent to rm -rf".to_string());
                }
            }
        }
    }

    // chmod 777 / or chown root /
    if lower.contains("chmod") && lower.contains("777") && lower.contains(" /") {
        let after_777 = lower.split("777").nth(1).unwrap_or("").trim();
        if after_777 == "/" || after_777.starts_with("/ ") {
            return Some("chmod 777 on root filesystem".to_string());
        }
    }

    // ── Reverse shells (Critical) ──────────────────────────────────────
    if is_reverse_shell(lower) {
        return Some("Reverse shell detected".to_string());
    }

    // ── Crypto mining (Critical) ────────────────────────────────────────
    if is_crypto_mining(lower) {
        return Some("Crypto mining detected".to_string());
    }

    // ── Security software tampering (Critical) ─────────────────────────
    if is_security_tampering(lower) {
        return Some("Security software tampering detected".to_string());
    }

    // ── Cron job injection (Critical) ───────────────────────────────────
    if is_cron_injection(lower) {
        return Some("Cron job injection detected".to_string());
    }

    None
}

// ═══════════════════════════════════════════════════════════════════════════
// Category: Reverse shells & system compromise
// ═══════════════════════════════════════════════════════════════════════════

fn is_reverse_shell(lower: &str) -> bool {
    // bash -i >& /dev/tcp/...
    if lower.contains("bash -i") && lower.contains("/dev/tcp") {
        return true;
    }
    if lower.contains("bash -i") && lower.contains("/dev/udp") {
        return true;
    }

    // nc/ncat -e / --exec (netcat reverse shell)
    if (lower.contains("nc ") || lower.contains("ncat ") || lower.contains("netcat "))
        && (lower.contains(" -e ") || lower.contains(" --exec") || lower.contains(" -c "))
    {
        return true;
    }

    // python/python3 -c "import socket" (socket-based reverse shell)
    if (lower.contains("python -c") || lower.contains("python3 -c"))
        && lower.contains("import socket")
    {
        return true;
    }
    if (lower.contains("python -c") || lower.contains("python3 -c"))
        && lower.contains("import os")
        && lower.contains("pty.spawn")
    {
        return true;
    }

    // perl -e with socket
    if lower.contains("perl -e") && lower.contains("socket") {
        return true;
    }

    // ruby -rsocket
    if lower.contains("ruby -rsocket") || lower.contains("ruby -r socket") {
        return true;
    }

    // php -r with fsockopen
    if lower.contains("php -r") && lower.contains("fsockopen") {
        return true;
    }

    // socat reverse shell
    if lower.contains("socat") && lower.contains("exec:") {
        return true;
    }

    // telnet-based reverse shell: telnet <host> <port> | /bin/sh
    if lower.contains("telnet") && lower.contains("/bin/") {
        return true;
    }

    // /dev/tcp direct (bash built-in networking)
    if lower.contains("/dev/tcp/") || lower.contains("/dev/udp/") {
        return true;
    }

    // mkfifo pipe-based reverse shell
    if lower.contains("mkfifo") && (lower.contains("/bin/sh") || lower.contains("/bin/bash")) {
        return true;
    }

    false
}

// ═══════════════════════════════════════════════════════════════════════════
// Category: Crypto mining
// ═══════════════════════════════════════════════════════════════════════════

/// Known miner binaries and mining pool URL patterns.
const MINER_BINARIES: &[&str] = &[
    "xmrig",
    "minerd",
    "cgminer",
    "bfgminer",
    "ethminer",
    "cpuminer",
    "ccminer",
    "t-rex",
    "nbminer",
    "phoenixminer",
    "lolminer",
    "gminer",
    "claymore",
    "nanominer",
    "teamredminer",
    "wildrig",
    "srbminer",
    "xmr-stak",
];

fn is_crypto_mining(lower: &str) -> bool {
    let first_word = lower.split_ascii_whitespace().next().unwrap_or("");
    // Strip path prefix: /tmp/xmrig -> xmrig
    let binary = first_word.rsplit('/').next().unwrap_or(first_word);
    if MINER_BINARIES.iter().any(|m| binary.starts_with(m)) {
        return true;
    }

    // Mining pool URLs (stratum protocol)
    if lower.contains("stratum+tcp://") || lower.contains("stratum+ssl://") {
        return true;
    }

    // Common mining pool domains
    if lower.contains("pool.minexmr.com")
        || lower.contains("xmrpool.eu")
        || lower.contains("nanopool.org")
        || lower.contains("mining.oc.tc")
        || lower.contains("pool.supportxmr.com")
        || lower.contains("monerohash.com")
        || lower.contains("hashvault.pro")
        || lower.contains("herominers.com")
    {
        return true;
    }

    false
}

// ═══════════════════════════════════════════════════════════════════════════
// Category: Security software tampering
// ═══════════════════════════════════════════════════════════════════════════

fn is_security_tampering(lower: &str) -> bool {
    // Firewall disabling
    if lower.contains("ufw disable") || lower.contains("ufw --force disable") {
        return true;
    }
    if lower.contains("iptables -f") || lower.contains("iptables --flush") {
        return true;
    }
    if lower.contains("iptables -p") && lower.contains("accept") {
        return true;
    }
    if lower.contains("nft flush ruleset") || lower.contains("nft delete") {
        return true;
    }
    if lower.contains("firewall-cmd") && lower.contains("--panic-off") {
        return true;
    }

    // Disabling SELinux / AppArmor
    if lower.contains("setenforce 0") || lower.contains("setenforce permissive") {
        return true;
    }
    if lower.contains("apparmor_parser -R") || lower.contains("aa-teardown") {
        return true;
    }
    // systemctl disable/stop on security services
    if lower.contains("systemctl")
        && (lower.contains("stop") || lower.contains("disable"))
        && (lower.contains("apparmor")
            || lower.contains("firewalld")
            || lower.contains("fail2ban")
            || lower.contains("ufw")
            || lower.contains("auditd")
            || lower.contains("clamav")
            || lower.contains("rkhunter")
            || lower.contains("ossec"))
    {
        return true;
    }

    // Killing security processes
    if (lower.contains("kill") || lower.contains("pkill") || lower.contains("killall"))
        && (lower.contains("clamd")
            || lower.contains("fail2ban")
            || lower.contains("ossec")
            || lower.contains("snort")
            || lower.contains("suricata")
            || lower.contains("auditd")
            || lower.contains("sshguard"))
    {
        return true;
    }

    // Modifying /etc/hosts (DNS hijacking)
    if (lower.contains("> /etc/hosts")
        || lower.contains(">>/etc/hosts")
        || lower.contains(">> /etc/hosts")
        || lower.contains(">/etc/hosts"))
        && !lower.contains("localhost")
    {
        return true;
    }
    if lower.contains("tee /etc/hosts") || lower.contains("tee -a /etc/hosts") {
        return true;
    }

    false
}

// ═══════════════════════════════════════════════════════════════════════════
// Category: Cron job injection
// ═══════════════════════════════════════════════════════════════════════════

fn is_cron_injection(lower: &str) -> bool {
    // Piping into crontab
    if lower.contains("| crontab") || lower.contains("|crontab") {
        return true;
    }
    // Redirecting into cron directories
    if lower.contains("> /etc/cron")
        || lower.contains(">/etc/cron")
        || lower.contains(">> /etc/cron")
        || lower.contains(">>/etc/cron")
    {
        return true;
    }
    // Writing to cron spool
    if lower.contains("/var/spool/cron") && (lower.contains(">") || lower.contains("tee")) {
        return true;
    }
    false
}

/// Check for injection and evasion patterns in command strings.
///
/// These patterns detect various techniques used to bypass command classification
/// or inject unintended behavior into shell commands.
pub(super) fn check_injection_patterns(
    command: &str,
    lower: &str,
) -> Option<CommandClassification> {
    // 1. JQ RCE: jq with system() call or -f flag loading external filters
    if lower.contains("jq") && (lower.contains("system(") || lower.contains(" -f ")) {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::ExecuteCommand],
            warnings: vec![
                "jq with system() or external filter file — potential code execution".to_string(),
            ],
            blocked: false,
            reason: None,
        });
    }

    // 2. Newline injection: embedded literal \n (two chars: backslash + n) in command strings
    // that could cause shell to interpret multiple lines differently
    if command.contains("\\n") && !command.contains("\\n\"") && !command.contains("echo ") {
        // Avoid false positives from echo "...\n" and similar benign uses
        let suspicious_newline = lower.contains("\\n;")
            || lower.contains("\\n|")
            || lower.contains("\\n&")
            || lower.contains("\\nrm ")
            || lower.contains("\\ncurl ")
            || lower.contains("\\nwget ")
            || lower.contains("\\nsudo ");
        if suspicious_newline {
            return Some(CommandClassification {
                risk_level: RiskLevel::High,
                tags: vec![SafetyTag::ExecuteCommand],
                warnings: vec![
                    "Embedded newline escape with suspicious payload — potential injection"
                        .to_string(),
                ],
                blocked: false,
                reason: None,
            });
        }
    }

    // 3. Proc environ access: reading process environment variables from procfs
    if lower.contains("/proc/self/environ") || lower.contains("/proc/*/environ") {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::Privileged],
            warnings: vec!["Accessing /proc/*/environ can leak environment secrets".to_string()],
            blocked: false,
            reason: None,
        });
    }
    // Also catch numeric PID variants like /proc/1/environ
    if regex::Regex::new(r"/proc/\d+/environ")
        .ok()
        .is_some_and(|re| re.is_match(lower))
    {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::Privileged],
            warnings: vec!["Accessing /proc/<pid>/environ can leak environment secrets".to_string()],
            blocked: false,
            reason: None,
        });
    }

    // 4. Comment/quote desync: unmatched quotes followed by # could hide malicious commands
    {
        let mut in_single = false;
        let mut in_double = false;
        let chars: Vec<char> = command.chars().collect();
        let mut i = 0;
        while i < chars.len() {
            let c = chars[i];
            if c == '\\' && i + 1 < chars.len() {
                i += 2;
                continue;
            }
            if c == '\'' && !in_double {
                in_single = !in_single;
            } else if c == '"' && !in_single {
                in_double = !in_double;
            } else if c == '#' && !in_single && !in_double {
                // Found unquoted # — check if there were unmatched quotes before it
                // This is OK, # is just a comment
            }
            i += 1;
        }
        // If we end with unmatched quotes AND there was a # somewhere, flag it
        if (in_single || in_double) && command.contains('#') {
            return Some(CommandClassification {
                risk_level: RiskLevel::High,
                tags: vec![SafetyTag::ExecuteCommand],
                warnings: vec![
                    "Unmatched quotes with comment character — potential quote/comment desync"
                        .to_string(),
                ],
                blocked: false,
                reason: None,
            });
        }
    }

    // 5. Backslash-escaped operators outside quotes: \; \| \& can bypass parsing
    {
        let mut in_single = false;
        let mut in_double = false;
        let chars: Vec<char> = command.chars().collect();
        let mut i = 0;
        while i < chars.len() {
            let c = chars[i];
            if c == '\'' && !in_double {
                in_single = !in_single;
                i += 1;
                continue;
            }
            if c == '"' && !in_single {
                in_double = !in_double;
                i += 1;
                continue;
            }
            if c == '\\' && !in_single && i + 1 < chars.len() {
                let next = chars[i + 1];
                if matches!(next, ';' | '|' | '&') {
                    return Some(CommandClassification {
                        risk_level: RiskLevel::High,
                        tags: vec![SafetyTag::ExecuteCommand],
                        warnings: vec![format!(
                            "Backslash-escaped operator '\\{}' outside quotes — potential parser evasion",
                            next
                        )],
                        blocked: false,
                        reason: None,
                    });
                }
                i += 2;
                continue;
            }
            i += 1;
        }
    }

    // 6. Control characters: bytes 0x00-0x1F (except 0x09 tab, 0x0A newline) or 0x7F
    for c in command.chars() {
        let cp = c as u32;
        if cp == 0x7F || (cp <= 0x1F && cp != 0x09 && cp != 0x0A) {
            return Some(CommandClassification {
                risk_level: RiskLevel::High,
                tags: vec![SafetyTag::ExecuteCommand],
                warnings: vec![format!(
                    "Control character U+{:04X} in command — potential injection",
                    cp
                )],
                blocked: false,
                reason: None,
            });
        }
    }

    // 7. Quoted newlines: actual newline characters inside single or double quotes
    {
        let mut in_single = false;
        let mut in_double = false;
        let chars: Vec<char> = command.chars().collect();
        let mut i = 0;
        while i < chars.len() {
            let c = chars[i];
            if c == '\\' && in_double && i + 1 < chars.len() {
                i += 2;
                continue;
            }
            if c == '\'' && !in_double {
                in_single = !in_single;
            } else if c == '"' && !in_single {
                in_double = !in_double;
            } else if c == '\n' && (in_single || in_double) {
                return Some(CommandClassification {
                    risk_level: RiskLevel::Medium,
                    tags: vec![SafetyTag::ExecuteCommand],
                    warnings: vec![
                        "Newline inside quoted string — may cause unexpected behavior".to_string(),
                    ],
                    blocked: false,
                    reason: None,
                });
            }
            i += 1;
        }
    }

    None
}

/// Check for high-risk patterns that span pipes/chains (whole-command analysis).
///
/// These patterns require seeing the full command to detect cross-pipe exfiltration
/// and similar multi-stage attacks.
pub(super) fn check_whole_command_high_risk(lower: &str) -> Option<CommandClassification> {
    // base64 encode piped to curl/wget (encode-and-exfil pattern)
    if lower.contains("base64") && (lower.contains("curl") || lower.contains("wget")) {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::NetworkAccess],
            warnings: vec![
                "Base64 encoding combined with network command — potential exfiltration"
                    .to_string(),
            ],
            blocked: false,
            reason: None,
        });
    }

    // tar/zip piped to nc — sending archives over network
    if (lower.contains("tar ") || lower.contains("zip "))
        && (lower.contains("| nc ") || lower.contains("|nc ") || lower.contains("| ncat"))
    {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::NetworkAccess, SafetyTag::Destructive],
            warnings: vec!["Piping archive to network command — potential exfiltration".to_string()],
            blocked: false,
            reason: None,
        });
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

    // ── Credential theft (High) ───────────────────────────────────────
    if let Some(result) = check_credential_access(first_word, lower) {
        return Some(result);
    }

    // ── Data exfiltration (High) ────────────────────────────────────────
    if let Some(result) = check_data_exfiltration(lower) {
        return Some(result);
    }

    // ── Privilege escalation (High) ─────────────────────────────────────
    if let Some(result) = check_privilege_escalation(lower) {
        return Some(result);
    }

    // ── PATH hijacking (High) ───────────────────────────────────────────
    if let Some(result) = check_path_hijacking(lower) {
        return Some(result);
    }

    // ── Keylogger / input capture (High) ────────────────────────────────
    if lower.contains("xinput") && lower.contains("test") {
        warnings.push("Potential keylogger via xinput".to_string());
        tags = vec![SafetyTag::SystemModification];
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags,
            warnings,
            blocked: false,
            reason: None,
        });
    }
    if lower.contains("script -q") || (lower.contains("strace") && lower.contains("read")) {
        warnings.push("Potential input/keystroke capture".to_string());
        tags = vec![SafetyTag::SystemModification];
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

// ═══════════════════════════════════════════════════════════════════════════
// Category: Credential theft (High risk)
// ═══════════════════════════════════════════════════════════════════════════

/// Sensitive paths that likely contain credentials or secrets.
const SENSITIVE_PATH_PATTERNS: &[&str] = &[
    "/.ssh/",
    "/.aws/",
    "/.gnupg/",
    "/.gpg/",
    "/.config/gcloud/",
    "/.azure/",
    "/.kube/config",
    "/.docker/config.json",
    "/.npmrc",
    "/.pypirc",
    "/.netrc",
    "/.git-credentials",
    "/.config/gh/hosts.yml",
    "/id_rsa",
    "/id_ed25519",
    "/id_ecdsa",
    "/id_dsa",
    "/.pem",
    "/.p12",
    "/.pfx",
];

/// File names that typically contain secrets.
const SENSITIVE_FILE_NAMES: &[&str] = &[
    ".env",
    ".env.local",
    ".env.production",
    "credentials",
    "credentials.json",
    "credentials.yaml",
    "credentials.yml",
    "secrets.json",
    "secrets.yaml",
    "secrets.yml",
    "token",
    "tokens.json",
    "master.key",
    "service-account.json",
    "keyfile.json",
    "private.key",
    "private.pem",
];

/// Browser credential store paths.
const BROWSER_CREDENTIAL_PATHS: &[&str] = &[
    "login data",
    "cookies",
    "web data",
    ".mozilla/firefox",
    "google-chrome",
    "chromium",
    "brave-browser",
    "microsoft-edge",
    "default/login",
    "keychain",
    "key3.db",
    "key4.db",
    "logins.json",
    "cert9.db",
];

fn check_credential_access(first_word: &str, lower: &str) -> Option<CommandClassification> {
    // Commands that read files
    let is_read_cmd = matches!(
        first_word,
        "cat"
            | "less"
            | "more"
            | "head"
            | "tail"
            | "strings"
            | "xxd"
            | "hexdump"
            | "od"
            | "base64"
            | "openssl"
            | "cp"
            | "mv"
            | "tar"
            | "zip"
    );

    // Also catch grep/find/ls on sensitive paths
    let is_search_cmd = matches!(first_word, "grep" | "find" | "ls" | "file" | "stat");

    if is_read_cmd || is_search_cmd {
        // Check for sensitive path patterns
        for pattern in SENSITIVE_PATH_PATTERNS {
            if lower.contains(pattern) {
                return Some(CommandClassification {
                    risk_level: RiskLevel::High,
                    tags: vec![SafetyTag::Privileged],
                    warnings: vec![format!("Accessing sensitive credential path: {pattern}")],
                    blocked: false,
                    reason: None,
                });
            }
        }

        // Check for sensitive file names
        for name in SENSITIVE_FILE_NAMES {
            if lower.contains(name) {
                return Some(CommandClassification {
                    risk_level: RiskLevel::High,
                    tags: vec![SafetyTag::Privileged],
                    warnings: vec![format!("Accessing file that may contain secrets: {name}")],
                    blocked: false,
                    reason: None,
                });
            }
        }

        // Check for browser credential stores
        for path in BROWSER_CREDENTIAL_PATHS {
            if lower.contains(path) {
                return Some(CommandClassification {
                    risk_level: RiskLevel::High,
                    tags: vec![SafetyTag::Privileged],
                    warnings: vec![format!("Accessing browser credential store: {path}")],
                    blocked: false,
                    reason: None,
                });
            }
        }
    }

    // Env var dumping that may leak secrets
    if (lower.starts_with("env") || lower.starts_with("printenv") || lower.starts_with("set"))
        && (lower.contains("secret")
            || lower.contains("token")
            || lower.contains("password")
            || lower.contains("api_key")
            || lower.contains("apikey"))
    {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::Privileged],
            warnings: vec!["Accessing environment variables that may contain secrets".to_string()],
            blocked: false,
            reason: None,
        });
    }

    None
}

// ═══════════════════════════════════════════════════════════════════════════
// Category: Data exfiltration (High risk)
// ═══════════════════════════════════════════════════════════════════════════

fn check_data_exfiltration(lower: &str) -> Option<CommandClassification> {
    // curl/wget POST with file data — uploading files to remote
    if (lower.contains("curl") || lower.contains("wget"))
        && (lower.contains("-d @")
            || lower.contains("--data @")
            || lower.contains("--data-binary @")
            || lower.contains("-f @")
            || lower.contains("--upload-file")
            || lower.contains("-t "))
    {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::NetworkAccess],
            warnings: vec!["Uploading file data to remote server".to_string()],
            blocked: false,
            reason: None,
        });
    }

    // curl POST with inline data that looks like it could contain secrets
    if lower.contains("curl") && lower.contains("-x post") && lower.contains("-d") {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::NetworkAccess],
            warnings: vec!["HTTP POST may exfiltrate data".to_string()],
            blocked: false,
            reason: None,
        });
    }

    // base64 encode piped to curl/wget (encode-and-exfil pattern)
    if lower.contains("base64") && (lower.contains("curl") || lower.contains("wget")) {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::NetworkAccess],
            warnings: vec![
                "Base64 encoding combined with network command — potential exfiltration"
                    .to_string(),
            ],
            blocked: false,
            reason: None,
        });
    }

    // DNS exfiltration via dig/nslookup with crafted subdomains
    if (lower.contains("dig ") || lower.contains("nslookup ") || lower.contains("host "))
        && lower.contains("$(")
    {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::NetworkAccess],
            warnings: vec!["Potential DNS exfiltration via command substitution".to_string()],
            blocked: false,
            reason: None,
        });
    }

    // tar/zip piped to nc — sending archives over network
    if (lower.contains("tar ") || lower.contains("zip "))
        && (lower.contains("| nc ") || lower.contains("|nc ") || lower.contains("| ncat"))
    {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::NetworkAccess, SafetyTag::Destructive],
            warnings: vec!["Piping archive to network command — potential exfiltration".to_string()],
            blocked: false,
            reason: None,
        });
    }

    None
}

// ═══════════════════════════════════════════════════════════════════════════
// Category: Privilege escalation (High risk)
// ═══════════════════════════════════════════════════════════════════════════

fn check_privilege_escalation(lower: &str) -> Option<CommandClassification> {
    // setuid/setgid bit setting
    if lower.contains("chmod")
        && (lower.contains("+s") || lower.contains("u+s") || lower.contains("g+s"))
    {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::Privileged, SafetyTag::SystemModification],
            warnings: vec!["Setting SUID/SGID bit — potential privilege escalation".to_string()],
            blocked: false,
            reason: None,
        });
    }
    // Numeric setuid: chmod 4755, chmod 2755, chmod 6755
    if lower.contains("chmod") {
        let tokens: Vec<&str> = lower.split_ascii_whitespace().collect();
        for token in &tokens {
            if token.len() == 4 && token.chars().all(|c| c.is_ascii_digit()) {
                let first_digit = token.chars().next().unwrap_or('0');
                if matches!(first_digit, '4' | '2' | '6') {
                    return Some(CommandClassification {
                        risk_level: RiskLevel::High,
                        tags: vec![SafetyTag::Privileged, SafetyTag::SystemModification],
                        warnings: vec!["Setting SUID/SGID bit via numeric mode".to_string()],
                        blocked: false,
                        reason: None,
                    });
                }
            }
        }
    }

    // chown root / chgrp root (matches "chown root", "chown root:root", etc.)
    if (lower.contains("chown root")
        || lower.contains("chgrp root")
        || lower.contains("chown 0:")
        || lower.contains("chgrp 0:"))
        && !lower.contains("/tmp/")
    {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::Privileged, SafetyTag::SystemModification],
            warnings: vec!["Changing file ownership to root".to_string()],
            blocked: false,
            reason: None,
        });
    }

    // Writing to /etc/passwd, /etc/shadow, /etc/sudoers
    if (lower.contains("/etc/passwd")
        || lower.contains("/etc/shadow")
        || lower.contains("/etc/sudoers")
        || lower.contains("/etc/group"))
        && (lower.contains(">")
            || lower.contains("tee")
            || lower.contains("sed -i")
            || lower.contains("echo")
            || lower.contains("usermod")
            || lower.contains("useradd"))
    {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::Privileged, SafetyTag::SystemModification],
            warnings: vec![
                "Modifying authentication files — potential privilege escalation".to_string(),
            ],
            blocked: false,
            reason: None,
        });
    }

    // visudo / direct sudoers modification
    if lower.starts_with("visudo") || lower.contains("sudoers.d/") {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::Privileged],
            warnings: vec!["Modifying sudo configuration".to_string()],
            blocked: false,
            reason: None,
        });
    }

    // LD_PRELOAD / LD_LIBRARY_PATH injection
    if lower.contains("ld_preload=") || lower.contains("ld_library_path=") {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::Privileged, SafetyTag::SystemModification],
            warnings: vec![
                "Dynamic linker variable injection — potential privilege escalation".to_string(),
            ],
            blocked: false,
            reason: None,
        });
    }

    // Capability manipulation
    if lower.contains("setcap") || lower.contains("getcap") {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::Privileged],
            warnings: vec!["Manipulating Linux capabilities".to_string()],
            blocked: false,
            reason: None,
        });
    }

    // nsenter / unshare for namespace escape
    if lower.starts_with("nsenter") || (lower.starts_with("unshare") && lower.contains("-r")) {
        return Some(CommandClassification {
            risk_level: RiskLevel::High,
            tags: vec![SafetyTag::Privileged],
            warnings: vec!["Namespace manipulation — potential container escape".to_string()],
            blocked: false,
            reason: None,
        });
    }

    None
}

// ═══════════════════════════════════════════════════════════════════════════
// Category: PATH hijacking (High risk)
// ═══════════════════════════════════════════════════════════════════════════

fn check_path_hijacking(lower: &str) -> Option<CommandClassification> {
    // Prepending to PATH (PATH=/tmp:$PATH, export PATH=/evil:...)
    if lower.contains("path=") && (lower.contains("export") || lower.contains("=")) {
        // Look for suspicious path prepending
        let has_tmp_or_dev =
            lower.contains("/tmp") || lower.contains("/dev/shm") || lower.contains("/var/tmp");
        if has_tmp_or_dev && lower.contains("path") {
            return Some(CommandClassification {
                risk_level: RiskLevel::High,
                tags: vec![SafetyTag::SystemModification],
                warnings: vec![
                    "PATH manipulation with writable directory — potential hijacking".to_string(),
                ],
                blocked: false,
                reason: None,
            });
        }
    }

    // alias hijacking of common commands
    if lower.starts_with("alias ") {
        let hijack_targets = [
            "alias sudo=",
            "alias su=",
            "alias ssh=",
            "alias login=",
            "alias passwd=",
            "alias curl=",
            "alias wget=",
            "alias git=",
            "alias docker=",
            "alias kubectl=",
        ];
        for target in &hijack_targets {
            if lower.contains(target) {
                return Some(CommandClassification {
                    risk_level: RiskLevel::High,
                    tags: vec![SafetyTag::SystemModification],
                    warnings: vec![format!("Alias hijacking of security-sensitive command")],
                    blocked: false,
                    reason: None,
                });
            }
        }
    }

    None
}
