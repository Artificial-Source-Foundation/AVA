//! Auto-retry middleware for read-only tools on transient failures.
//!
//! Read-only tools (read, glob, grep, web_fetch, code_search, git) are
//! automatically retried up to 2 times (3 total attempts) with exponential
//! backoff when the error looks transient. The retry is completely transparent
//! to the agent — if retry succeeds, the result is returned normally; if all
//! retries fail, the original error is returned.

use std::time::Duration;

/// Set of tool names that are safe to retry (read-only, no side effects).
const RETRYABLE_TOOLS: &[&str] = &[
    "read",
    "glob",
    "grep",
    "web_fetch",
    "code_search",
    "git",
    "git_read",
];

/// Backoff durations for each retry attempt.
const BACKOFF_DURATIONS: &[Duration] = &[Duration::from_millis(100), Duration::from_millis(200)];

/// Maximum number of retry attempts (not counting the initial attempt).
pub const MAX_RETRIES: usize = 2;

/// Returns true if the given tool name is safe to auto-retry.
pub fn is_retryable_tool(tool_name: &str) -> bool {
    RETRYABLE_TOOLS.contains(&tool_name)
}

/// Returns true if the error message indicates a transient failure worth retrying.
///
/// We retry on:
/// - Permission denied (temporary file locks)
/// - Connection refused / reset (server restarting)
/// - Timeout / timed out
/// - Temporarily unavailable
/// - Resource busy
/// - Too many open files
///
/// We do NOT retry on:
/// - File not found / No such file
/// - Invalid arguments / invalid input
/// - Not a directory / Is a directory
/// - Syntax errors
pub fn is_transient_error(error_msg: &str) -> bool {
    let lower = error_msg.to_lowercase();

    // Transient patterns — worth retrying
    let transient_patterns = [
        "permission denied",
        "connection refused",
        "connection reset",
        "timed out",
        "timeout",
        "temporarily unavailable",
        "resource busy",
        "too many open files",
        "broken pipe",
        "network unreachable",
        "host unreachable",
        "connection aborted",
        "resource temporarily unavailable",
        "try again",
        "service unavailable",
        "429", // rate limit
        "502", // bad gateway
        "503", // service unavailable
        "504", // gateway timeout
        "econnrefused",
        "econnreset",
        "etimedout",
        "eagain",
    ];

    // Non-transient patterns — will never fix themselves
    let permanent_patterns = [
        "not found",
        "no such file",
        "no such directory",
        "invalid argument",
        "invalid input",
        "not a directory",
        "is a directory",
        "syntax error",
        "does not exist",
        "unknown tool",
        "missing required",
    ];

    // If it matches a permanent pattern, don't retry
    if permanent_patterns.iter().any(|p| lower.contains(p)) {
        return false;
    }

    // If it matches a transient pattern, retry
    transient_patterns.iter().any(|p| lower.contains(p))
}

/// Returns the backoff duration for the given attempt number (0-indexed).
/// Returns None if the attempt exceeds MAX_RETRIES.
pub fn backoff_for_attempt(attempt: usize) -> Option<Duration> {
    BACKOFF_DURATIONS.get(attempt).copied()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn retryable_tools_recognized() {
        assert!(is_retryable_tool("read"));
        assert!(is_retryable_tool("glob"));
        assert!(is_retryable_tool("grep"));
        assert!(is_retryable_tool("web_fetch"));
        assert!(is_retryable_tool("code_search"));
        assert!(is_retryable_tool("git_read"));
    }

    #[test]
    fn non_retryable_tools_rejected() {
        assert!(!is_retryable_tool("write"));
        assert!(!is_retryable_tool("edit"));
        assert!(!is_retryable_tool("bash"));
        assert!(!is_retryable_tool("apply_patch"));
        assert!(!is_retryable_tool("multiedit"));
        assert!(!is_retryable_tool("unknown_tool"));
    }

    #[test]
    fn transient_errors_detected() {
        assert!(is_transient_error("permission denied"));
        assert!(is_transient_error("Permission Denied: /tmp/lock"));
        assert!(is_transient_error("connection refused"));
        assert!(is_transient_error("request timed out"));
        assert!(is_transient_error("operation timeout after 30s"));
        assert!(is_transient_error("resource temporarily unavailable"));
        assert!(is_transient_error("too many open files"));
        assert!(is_transient_error("connection reset by peer"));
        assert!(is_transient_error("HTTP 503 service unavailable"));
        assert!(is_transient_error("HTTP 429 rate limited"));
        assert!(is_transient_error("ECONNREFUSED"));
        assert!(is_transient_error("ETIMEDOUT"));
    }

    #[test]
    fn permanent_errors_not_retried() {
        assert!(!is_transient_error("file not found: /tmp/missing.txt"));
        assert!(!is_transient_error("No such file or directory"));
        assert!(!is_transient_error("invalid argument: bad path"));
        assert!(!is_transient_error("not a directory"));
        assert!(!is_transient_error("is a directory"));
        assert!(!is_transient_error("syntax error in regex"));
        assert!(!is_transient_error("file does not exist"));
        assert!(!is_transient_error("missing required parameter 'path'"));
    }

    #[test]
    fn ambiguous_errors_not_retried() {
        // Generic errors without transient indicators should not be retried
        assert!(!is_transient_error("something went wrong"));
        assert!(!is_transient_error("unexpected error"));
        assert!(!is_transient_error("failed to process"));
    }

    #[test]
    fn backoff_durations_correct() {
        assert_eq!(backoff_for_attempt(0), Some(Duration::from_millis(100)));
        assert_eq!(backoff_for_attempt(1), Some(Duration::from_millis(200)));
        assert_eq!(backoff_for_attempt(2), None);
    }

    #[test]
    fn permanent_pattern_takes_priority() {
        // "permission denied" is transient, but "not found" is permanent.
        // If both appear, permanent should win.
        assert!(!is_transient_error(
            "not found: permission denied on lookup"
        ));
    }
}
