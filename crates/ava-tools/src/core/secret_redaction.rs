//! Regex-based secret redaction for tool output.
//!
//! Sanitizes API keys, Bearer tokens, AWS credentials, GitHub tokens, and
//! generic secret assignments before tool output reaches the LLM.

use regex::Regex;
use std::sync::LazyLock;

const REDACTED: &str = "[REDACTED]";

/// Compiled regex patterns for secret detection.
struct SecretPatterns {
    /// OpenAI-style API keys: `sk-proj-...` and `sk-...`
    sk_proj: Regex,
    sk_key: Regex,
    /// AWS access key IDs: `AKIA...`
    aws_access_key: Regex,
    /// AWS secret access key assignments
    aws_secret_key: Regex,
    /// Bearer tokens
    bearer: Regex,
    /// GitHub personal access tokens and app tokens
    github_token: Regex,
    github_pat: Regex,
    /// Generic secret assignments (case-insensitive)
    generic_secret: Regex,
}

static PATTERNS: LazyLock<SecretPatterns> = LazyLock::new(|| SecretPatterns {
    // sk-proj- must come before sk- to avoid partial matches
    sk_proj: Regex::new(r"sk-proj-[a-zA-Z0-9\-_]{20,}").expect("valid sk-proj regex"),
    sk_key: Regex::new(r"sk-[a-zA-Z0-9]{20,}").expect("valid sk regex"),
    aws_access_key: Regex::new(r"AKIA[A-Z0-9]{16}").expect("valid aws key regex"),
    aws_secret_key: Regex::new(r"(?i)(aws_secret_access_key)\s*=\s*\S+")
        .expect("valid aws secret regex"),
    bearer: Regex::new(r"Bearer [a-zA-Z0-9._\-]{20,}").expect("valid bearer regex"),
    github_token: Regex::new(r"gh[ps]_[a-zA-Z0-9]{36,}").expect("valid github token regex"),
    github_pat: Regex::new(r"github_pat_[a-zA-Z0-9_]{20,}").expect("valid github pat regex"),
    generic_secret: Regex::new(
        r"(?i)(secret|password|token|api_key|apikey|auth)\s*[:=]\s*['\x22]?[^\s'\x22]{8,}['\x22]?",
    )
    .expect("valid generic secret regex"),
});

/// Redact secrets from tool output, replacing them with `[REDACTED]`.
///
/// Applies multiple regex patterns to catch common secret formats. The function
/// is designed to err on the side of redaction — false positives are preferred
/// over leaked secrets.
pub fn redact_secrets(input: &str) -> String {
    let mut result = input.to_string();

    // Order matters: more specific patterns first to avoid partial replacements
    // by broader patterns.
    result = PATTERNS.sk_proj.replace_all(&result, REDACTED).to_string();
    result = PATTERNS.sk_key.replace_all(&result, REDACTED).to_string();
    result = PATTERNS
        .aws_access_key
        .replace_all(&result, REDACTED)
        .to_string();
    result = PATTERNS
        .aws_secret_key
        .replace_all(&result, format!("$1={REDACTED}").as_str())
        .to_string();
    result = PATTERNS
        .bearer
        .replace_all(&result, format!("Bearer {REDACTED}").as_str())
        .to_string();
    result = PATTERNS
        .github_token
        .replace_all(&result, REDACTED)
        .to_string();
    result = PATTERNS
        .github_pat
        .replace_all(&result, REDACTED)
        .to_string();
    result = PATTERNS
        .generic_secret
        .replace_all(&result, format!("$1={REDACTED}").as_str())
        .to_string();

    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn redacts_openai_sk_key() {
        let input = "key is sk-abc123def456ghi789jkl012mno345pq";
        let result = redact_secrets(input);
        assert_eq!(result, "key is [REDACTED]");
        assert!(!result.contains("sk-abc"));
    }

    #[test]
    fn redacts_openai_sk_proj_key() {
        let input = "export OPENAI_API_KEY=sk-proj-abcdefghij_1234567890-klmnop";
        let result = redact_secrets(input);
        assert!(result.contains("[REDACTED]"));
        assert!(!result.contains("sk-proj-"));
    }

    #[test]
    fn redacts_aws_access_key() {
        let input = "aws_access_key_id = AKIAIOSFODNN7EXAMPLE";
        let result = redact_secrets(input);
        assert!(result.contains("[REDACTED]"));
        assert!(!result.contains("AKIAIOSFODNN7EXAMPLE"));
    }

    #[test]
    fn redacts_aws_secret_key() {
        let input = "aws_secret_access_key = wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";
        let result = redact_secrets(input);
        assert!(result.contains("[REDACTED]"));
        assert!(!result.contains("wJalrXUtnFEMI"));
    }

    #[test]
    fn redacts_bearer_token() {
        let input = "Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.payload.signature";
        let result = redact_secrets(input);
        assert!(result.contains("Bearer [REDACTED]"));
        assert!(!result.contains("eyJhbGciOiJ"));
    }

    #[test]
    fn redacts_github_tokens() {
        let input = "token: ghp_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghij1234";
        let result = redact_secrets(input);
        assert!(result.contains("[REDACTED]"));
        assert!(!result.contains("ghp_"));

        let input2 = "pat: github_pat_ABCDEFGHIJ1234567890_extra";
        let result2 = redact_secrets(input2);
        assert!(result2.contains("[REDACTED]"));
        assert!(!result2.contains("github_pat_"));
    }

    #[test]
    fn redacts_generic_secret_assignments() {
        let cases = [
            "password = supersecretvalue123",
            "api_key: 'my-secret-api-key-value'",
            "SECRET=\"longenoughsecretvalue\"",
            "auth=bearer_token_12345678",
        ];
        for case in &cases {
            let result = redact_secrets(case);
            assert!(
                result.contains("[REDACTED]"),
                "failed to redact: {case} -> {result}"
            );
        }
    }

    #[test]
    fn preserves_normal_code() {
        let normal = [
            "fn main() { println!(\"hello\"); }",
            "let x = 42;",
            "// This is a comment about tokens in general",
            "use std::sync::Arc;",
            "if password.len() < 8 { return Err(\"too short\"); }",
            "const MAX_TOKENS: usize = 4096;",
            "let sk = \"short\";", // too short to trigger sk- pattern
        ];
        for code in &normal {
            let result = redact_secrets(code);
            assert_eq!(
                &result, code,
                "false positive on normal code: {code} -> {result}"
            );
        }
    }

    #[test]
    fn handles_mixed_content() {
        let input = r#"
Config loaded successfully.
Database connected at localhost:5432.
API key: sk-abcdefghijklmnopqrstuvwxyz1234
AWS ID: AKIAIOSFODNN7EXAMPLE
Normal log line here.
Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.test.signature
End of output.
"#;
        let result = redact_secrets(input);
        assert!(result.contains("Config loaded successfully."));
        assert!(result.contains("Database connected at localhost:5432."));
        assert!(result.contains("Normal log line here."));
        assert!(result.contains("End of output."));
        assert!(!result.contains("sk-abcdefghij"));
        assert!(!result.contains("AKIAIOSFODNN7EXAMPLE"));
        assert!(!result.contains("eyJhbGciOiJ"));
    }

    #[test]
    fn redacts_multiple_secrets_on_same_line() {
        let input = "keys: sk-aaaabbbbccccddddeeeeffffgggg and AKIAIOSFODNN7EXAMPLE";
        let result = redact_secrets(input);
        assert_eq!(
            result.matches("[REDACTED]").count(),
            2,
            "expected 2 redactions in: {result}"
        );
    }
}
