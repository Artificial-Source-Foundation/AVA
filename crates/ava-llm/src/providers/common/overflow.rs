//! Context overflow detection from LLM API error responses, including token gap
//! parsing for adaptive compaction aggressiveness.

use ava_types::AvaError;
use once_cell::sync::Lazy;
use regex::Regex;

const OVERFLOW_PATTERNS: &[&str] = &[
    "context_length_exceeded",
    "maximum context length",
    "token limit",
    "input is too long",
    "request too large",
    "prompt is too long",
    "context window",
    "too many tokens",
    "exceeds the model",
    "content too large",
    "payload too large",
    "reduce your prompt",
];

/// Check if an error indicates the context window was exceeded.
pub fn is_context_overflow(error: &AvaError) -> bool {
    let msg = error.to_string().to_lowercase();
    OVERFLOW_PATTERNS.iter().any(|p| msg.contains(p))
}

/// Parsed token gap from an overflow error message.
///
/// Used to decide compaction aggressiveness: a small gap (5%) needs light pruning,
/// while a large gap (50%+) needs aggressive compaction.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TokenGap {
    /// Actual token count in the request that caused the overflow.
    pub actual_tokens: usize,
    /// Maximum tokens allowed by the model/provider.
    pub max_tokens: usize,
    /// How many tokens over the limit (`actual_tokens - max_tokens`).
    pub gap: usize,
}

impl TokenGap {
    /// Gap as a fraction of the max tokens (0.0 to 1.0+).
    pub fn gap_ratio(&self) -> f64 {
        if self.max_tokens == 0 {
            return 1.0;
        }
        self.gap as f64 / self.max_tokens as f64
    }
}

// Anthropic: "prompt is too long: 150000 tokens > 100000 maximum"
static RE_ANTHROPIC: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r"(\d[\d,]*)\s*tokens?\s*>\s*(\d[\d,]*)\s*(?:maximum|max|limit)")
        .expect("anthropic overflow regex")
});

// OpenAI: "This model's maximum context length is 128000 tokens. However, your messages resulted in 150000 tokens"
static RE_OPENAI: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r"maximum context length is (\d[\d,]*) tokens.*?resulted in (\d[\d,]*) tokens")
        .expect("openai overflow regex")
});

// Generic: look for two numbers near "token" and "maximum"/"limit"
static RE_GENERIC: Lazy<Regex> = Lazy::new(|| {
    Regex::new(r"(\d[\d,]*)\s*tokens?.*?(\d[\d,]*)\s*(?:maximum|max|limit|tokens?)")
        .expect("generic overflow regex")
});

/// Parse a concrete token gap from an overflow error message.
///
/// Tries Anthropic format first, then OpenAI, then a generic pattern.
/// Returns `None` if no token numbers can be extracted.
pub fn parse_token_gap(error_message: &str) -> Option<TokenGap> {
    let msg = error_message.to_lowercase();

    // Anthropic: "150000 tokens > 100000 maximum"
    if let Some(caps) = RE_ANTHROPIC.captures(&msg) {
        let actual = parse_number(&caps[1])?;
        let max = parse_number(&caps[2])?;
        if actual > max {
            return Some(TokenGap {
                actual_tokens: actual,
                max_tokens: max,
                gap: actual - max,
            });
        }
    }

    // OpenAI: "maximum context length is 128000 tokens...resulted in 150000 tokens"
    if let Some(caps) = RE_OPENAI.captures(&msg) {
        let max = parse_number(&caps[1])?;
        let actual = parse_number(&caps[2])?;
        if actual > max {
            return Some(TokenGap {
                actual_tokens: actual,
                max_tokens: max,
                gap: actual - max,
            });
        }
    }

    // Generic: two numbers near "token" keywords
    if let Some(caps) = RE_GENERIC.captures(&msg) {
        let n1 = parse_number(&caps[1])?;
        let n2 = parse_number(&caps[2])?;
        let (actual, max) = if n1 > n2 { (n1, n2) } else { (n2, n1) };
        if actual > max && max > 0 {
            return Some(TokenGap {
                actual_tokens: actual,
                max_tokens: max,
                gap: actual - max,
            });
        }
    }

    None
}

/// Parse a number string that may contain commas (e.g., "150,000" -> 150000).
fn parse_number(s: &str) -> Option<usize> {
    s.replace(',', "").parse().ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_anthropic_format() {
        let msg = "prompt is too long: 150000 tokens > 100000 maximum";
        let gap = parse_token_gap(msg).expect("should parse anthropic format");
        assert_eq!(gap.actual_tokens, 150000);
        assert_eq!(gap.max_tokens, 100000);
        assert_eq!(gap.gap, 50000);
    }

    #[test]
    fn parse_openai_format() {
        let msg = "This model's maximum context length is 128000 tokens. However, your messages resulted in 150000 tokens";
        let gap = parse_token_gap(msg).expect("should parse openai format");
        assert_eq!(gap.actual_tokens, 150000);
        assert_eq!(gap.max_tokens, 128000);
        assert_eq!(gap.gap, 22000);
    }

    #[test]
    fn parse_anthropic_with_commas() {
        let msg = "prompt is too long: 150,000 tokens > 100,000 maximum";
        let gap = parse_token_gap(msg).expect("should parse numbers with commas");
        assert_eq!(gap.actual_tokens, 150000);
        assert_eq!(gap.max_tokens, 100000);
    }

    #[test]
    fn unparseable_returns_none() {
        let msg = "something went wrong with the request";
        assert!(parse_token_gap(msg).is_none());
    }

    #[test]
    fn gap_ratio_calculation() {
        let gap = TokenGap {
            actual_tokens: 150000,
            max_tokens: 100000,
            gap: 50000,
        };
        assert!((gap.gap_ratio() - 0.5).abs() < 0.001);
    }

    #[test]
    fn is_context_overflow_matches_provider_errors() {
        let err = AvaError::ProviderError {
            provider: "anthropic".to_string(),
            message:
                "context window exceeded (400): prompt is too long: 150000 tokens > 100000 maximum"
                    .to_string(),
        };
        assert!(is_context_overflow(&err));
    }
}
