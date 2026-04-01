//! Regex-based secret scanning with structured findings.
//!
//! Detects API keys, tokens, and credentials in arbitrary text content.
//! Returns structured scan results with pattern names and matched byte ranges,
//! and can redact detected secrets with pattern-tagged placeholders.

use regex::Regex;
use std::sync::LazyLock;

/// Result of scanning content for secrets.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SecretScanResult {
    pub has_secrets: bool,
    pub findings: Vec<SecretFinding>,
}

/// A single detected secret with its pattern name and byte range.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SecretFinding {
    pub pattern_name: String,
    pub matched_range: (usize, usize),
}

struct PatternEntry {
    name: &'static str,
    regex: Regex,
}

/// Compiled secret-detection patterns, ordered from most specific to broadest.
///
/// Order matters: more specific patterns (e.g. `sk-ant-admin`) must come before
/// broader ones (e.g. `sk-ant-`) to ensure the most accurate pattern name is
/// reported. The generic key=value pattern is always last.
static PATTERNS: LazyLock<Vec<PatternEntry>> = LazyLock::new(|| {
    vec![
        // AWS temporary credentials (ASIA prefix)
        PatternEntry {
            name: "aws_temporary_key",
            regex: Regex::new(r"ASIA[0-9A-Z]{16}").expect("valid regex"),
        },
        // AWS access key IDs
        PatternEntry {
            name: "aws_access_key",
            regex: Regex::new(r"AKIA[0-9A-Z]{16}").expect("valid regex"),
        },
        // Anthropic admin keys (before general sk-ant)
        PatternEntry {
            name: "anthropic_admin_key",
            regex: Regex::new(r"sk-ant-admin[a-zA-Z0-9_-]{20,}").expect("valid regex"),
        },
        // Anthropic API keys
        PatternEntry {
            name: "anthropic_key",
            regex: Regex::new(r"sk-ant-[a-zA-Z0-9_-]{20,}").expect("valid regex"),
        },
        // OpenAI project keys (before general sk-)
        PatternEntry {
            name: "openai_project_key",
            regex: Regex::new(r"sk-proj-[a-zA-Z0-9_-]{20,}").expect("valid regex"),
        },
        // OpenAI API keys
        PatternEntry {
            name: "openai_key",
            regex: Regex::new(r"sk-[a-zA-Z0-9]{20,}").expect("valid regex"),
        },
        // GitHub personal access tokens (fine-grained)
        PatternEntry {
            name: "github_fine_grained_pat",
            regex: Regex::new(r"github_pat_[a-zA-Z0-9_]{22,}").expect("valid regex"),
        },
        // GitHub classic personal access tokens
        PatternEntry {
            name: "github_pat",
            regex: Regex::new(r"ghp_[a-zA-Z0-9]{36}").expect("valid regex"),
        },
        // GitHub user-to-server tokens
        PatternEntry {
            name: "github_user_token",
            regex: Regex::new(r"ghu_[a-zA-Z0-9]{36}").expect("valid regex"),
        },
        // GitHub server-to-server tokens
        PatternEntry {
            name: "github_server_token",
            regex: Regex::new(r"ghs_[a-zA-Z0-9]{36}").expect("valid regex"),
        },
        // GitLab personal access tokens
        PatternEntry {
            name: "gitlab_pat",
            regex: Regex::new(r"glpat-[a-zA-Z0-9_-]{20,}").expect("valid regex"),
        },
        // Slack bot tokens
        PatternEntry {
            name: "slack_bot_token",
            regex: Regex::new(r"xoxb-[0-9]+-[a-zA-Z0-9]+").expect("valid regex"),
        },
        // Slack user tokens
        PatternEntry {
            name: "slack_user_token",
            regex: Regex::new(r"xoxp-[0-9]+-[a-zA-Z0-9]+").expect("valid regex"),
        },
        // Slack app tokens
        PatternEntry {
            name: "slack_app_token",
            regex: Regex::new(r"xapp-[0-9]+-[a-zA-Z0-9]+").expect("valid regex"),
        },
        // npm tokens
        PatternEntry {
            name: "npm_token",
            regex: Regex::new(r"npm_[a-zA-Z0-9]{36,}").expect("valid regex"),
        },
        // SendGrid API keys
        PatternEntry {
            name: "sendgrid_key",
            regex: Regex::new(r"SG\.[a-zA-Z0-9_-]{22,}\.[a-zA-Z0-9_-]{22,}")
                .expect("valid regex"),
        },
        // Generic secret assignments (case-insensitive)
        PatternEntry {
            name: "generic_secret",
            regex: Regex::new(
                r"(?i)(api[_-]?key|secret[_-]?key|access[_-]?token|private[_-]?key)\s*[:=]\s*['\x22][a-zA-Z0-9_-]{16,}['\x22]",
            )
            .expect("valid regex"),
        },
    ]
});

/// Scan content for secrets and return structured findings.
///
/// Each finding includes the pattern name and the byte range of the matched
/// secret in the input. When a region matches multiple patterns, only the
/// most specific (earliest in pattern order) is reported.
pub fn scan_for_secrets(content: &str) -> SecretScanResult {
    let mut findings = Vec::new();

    for entry in PATTERNS.iter() {
        for mat in entry.regex.find_iter(content) {
            let range = (mat.start(), mat.end());

            // Skip if this range is already covered by a more specific pattern
            let already_covered = findings.iter().any(|f: &SecretFinding| {
                f.matched_range.0 <= range.0 && f.matched_range.1 >= range.1
            });
            if already_covered {
                continue;
            }

            findings.push(SecretFinding {
                pattern_name: entry.name.to_string(),
                matched_range: range,
            });
        }
    }

    // Sort by position for deterministic output
    findings.sort_by_key(|f| f.matched_range.0);

    SecretScanResult {
        has_secrets: !findings.is_empty(),
        findings,
    }
}

/// Redact detected secrets, replacing each with `[REDACTED:<pattern_name>]`.
///
/// Processes findings from right to left so byte offsets remain valid during
/// replacement.
pub fn redact_secrets(content: &str) -> String {
    let scan = scan_for_secrets(content);
    if !scan.has_secrets {
        return content.to_string();
    }

    let mut result = content.to_string();

    // Process from end to start so earlier offsets remain valid
    let mut sorted_findings = scan.findings;
    sorted_findings.sort_by(|a, b| b.matched_range.0.cmp(&a.matched_range.0));

    for finding in &sorted_findings {
        let (start, end) = finding.matched_range;
        let replacement = format!("[REDACTED:{}]", finding.pattern_name);
        result.replace_range(start..end, &replacement);
    }

    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn detects_aws_access_key() {
        let result = scan_for_secrets("key = AKIAIOSFODNN7EXAMPLE");
        assert!(result.has_secrets);
        assert_eq!(result.findings.len(), 1);
        assert_eq!(result.findings[0].pattern_name, "aws_access_key");
    }

    #[test]
    fn detects_aws_temporary_key() {
        let result = scan_for_secrets("ASIATEMPORARYKEYEXAM");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "aws_temporary_key");
    }

    #[test]
    fn detects_anthropic_key() {
        let result = scan_for_secrets("sk-ant-abcdefghijklmnopqrstuvwxyz");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "anthropic_key");
    }

    #[test]
    fn detects_anthropic_admin_key() {
        let result = scan_for_secrets("sk-ant-adminABCDEFGHIJKLMNOPQRST");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "anthropic_admin_key");
    }

    #[test]
    fn detects_openai_project_key() {
        let result = scan_for_secrets("sk-proj-abcdefghijklmnopqrstuvwxyz");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "openai_project_key");
    }

    #[test]
    fn detects_openai_key() {
        let result = scan_for_secrets("sk-abcdefghijklmnopqrstuvwxyz1234");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "openai_key");
    }

    #[test]
    fn detects_github_pat() {
        let result = scan_for_secrets("ghp_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghij");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "github_pat");
    }

    #[test]
    fn detects_github_fine_grained_pat() {
        let result = scan_for_secrets("github_pat_ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "github_fine_grained_pat");
    }

    #[test]
    fn detects_github_user_token() {
        let result = scan_for_secrets("ghu_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghij");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "github_user_token");
    }

    #[test]
    fn detects_github_server_token() {
        let result = scan_for_secrets("ghs_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghij");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "github_server_token");
    }

    #[test]
    fn detects_gitlab_pat() {
        let result = scan_for_secrets("glpat-abcdefghijklmnopqrstuvwxyz");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "gitlab_pat");
    }

    #[test]
    fn detects_slack_bot_token() {
        let result = scan_for_secrets("xoxb-123456789-abcdef");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "slack_bot_token");
    }

    #[test]
    fn detects_slack_user_token() {
        let result = scan_for_secrets("xoxp-123456789-abcdef");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "slack_user_token");
    }

    #[test]
    fn detects_slack_app_token() {
        let result = scan_for_secrets("xapp-123456789-abcdef");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "slack_app_token");
    }

    #[test]
    fn detects_npm_token() {
        let result = scan_for_secrets("npm_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijkl");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "npm_token");
    }

    #[test]
    fn detects_sendgrid_key() {
        let result = scan_for_secrets("SG.abcdefghijklmnopqrstuvw.xyzABCDEFGHIJKLMNOPQRSTU");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "sendgrid_key");
    }

    #[test]
    fn detects_generic_secret() {
        let result = scan_for_secrets("api_key = 'abcdefghijklmnopqrstuvwxyz'");
        assert!(result.has_secrets);
        assert_eq!(result.findings[0].pattern_name, "generic_secret");
    }

    #[test]
    fn generic_secret_with_various_formats() {
        let cases = [
            "secret_key: \"mysecretkeythatislongenough\"",
            "access_token = 'abcdefghijklmnopqrst'",
            "private_key = \"mysupersecretprivatekey\"",
            "API-KEY = 'my_api_key_value_here'",
        ];
        for case in &cases {
            let result = scan_for_secrets(case);
            assert!(result.has_secrets, "failed to detect secret in: {case}");
            assert_eq!(result.findings[0].pattern_name, "generic_secret");
        }
    }

    #[test]
    fn redaction_replaces_correctly() {
        let input = "key = AKIAIOSFODNN7EXAMPLE";
        let redacted = redact_secrets(input);
        assert_eq!(redacted, "key = [REDACTED:aws_access_key]");
        assert!(!redacted.contains("AKIAIOSFODNN7EXAMPLE"));
    }

    #[test]
    fn redaction_preserves_clean_code() {
        let clean = "fn main() { println!(\"hello\"); }";
        assert_eq!(redact_secrets(clean), clean);
    }

    #[test]
    fn multiple_secrets_all_found() {
        let input = "aws=AKIAIOSFODNN7EXAMPLE and token=sk-ant-abcdefghijklmnopqrstuvwxyz";
        let result = scan_for_secrets(input);
        assert!(result.has_secrets);
        assert_eq!(result.findings.len(), 2);

        let names: Vec<&str> = result
            .findings
            .iter()
            .map(|f| f.pattern_name.as_str())
            .collect();
        assert!(names.contains(&"aws_access_key"));
        assert!(names.contains(&"anthropic_key"));
    }

    #[test]
    fn multiple_secrets_redacted() {
        let input = "aws=AKIAIOSFODNN7EXAMPLE and token=sk-ant-abcdefghijklmnopqrstuvwxyz";
        let redacted = redact_secrets(input);
        assert!(redacted.contains("[REDACTED:aws_access_key]"));
        assert!(redacted.contains("[REDACTED:anthropic_key]"));
        assert!(!redacted.contains("AKIAIOSFODNN7EXAMPLE"));
        assert!(!redacted.contains("sk-ant-"));
    }

    #[test]
    fn scan_returns_correct_ranges() {
        let input = "key: AKIAIOSFODNN7EXAMPLE";
        let result = scan_for_secrets(input);
        assert_eq!(result.findings.len(), 1);
        let (start, end) = result.findings[0].matched_range;
        assert_eq!(&input[start..end], "AKIAIOSFODNN7EXAMPLE");
    }

    #[test]
    fn clean_content_passes_through() {
        let clean_inputs = [
            "fn main() { println!(\"hello\"); }",
            "let x = 42;",
            "use std::sync::Arc;",
            "// This is a comment about tokens in general",
            "const MAX_TOKENS: usize = 4096;",
        ];
        for input in &clean_inputs {
            let result = scan_for_secrets(input);
            assert!(
                !result.has_secrets,
                "false positive on clean content: {input}"
            );
            assert!(result.findings.is_empty());
        }
    }

    #[test]
    fn idempotent_redaction() {
        let input = "key = AKIAIOSFODNN7EXAMPLE";
        let first = redact_secrets(input);
        let second = redact_secrets(&first);
        assert_eq!(first, second);
    }

    #[test]
    fn specific_pattern_wins_over_broad() {
        // sk-ant-admin should match anthropic_admin_key, not anthropic_key or openai_key
        let result = scan_for_secrets("sk-ant-adminABCDEFGHIJKLMNOPQRST");
        assert_eq!(result.findings.len(), 1);
        assert_eq!(result.findings[0].pattern_name, "anthropic_admin_key");
    }
}
