//! Context overflow detection from LLM API error responses.

use ava_types::AvaError;

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
