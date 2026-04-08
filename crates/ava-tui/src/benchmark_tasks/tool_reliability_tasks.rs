use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

const TIMEOUT_CONFIG_SETUP: &str = r#"#[derive(Debug, Clone)]
pub struct AppConfig {
    pub timeout_seconds: u64,
    pub retry_limit: usize,
}

impl Default for AppConfig {
    fn default() -> Self {
        Self {
            timeout_seconds: 30,
            retry_limit: 2,
        }
    }
}
"#;

const TIMEOUT_CONFIG_TESTS: &str = r#"
mod config;

use config::AppConfig;

#[test]
fn default_timeout_is_updated() {
    assert_eq!(AppConfig::default().timeout_seconds, 30);
}

#[test]
fn retry_limit_is_unchanged() {
    assert_eq!(AppConfig::default().retry_limit, 2);
}
"#;

const LOG_FILTER_SETUP: &str = r#"pub fn should_log(level: &str, verbose: bool) -> bool {
    if verbose {
        return true;
    }
    level == "error" || level == "warn"
}"#;

const LOG_FILTER_TESTS: &str = r#"
mod logger;

use logger::should_log;

#[test]
fn keeps_error_logs_when_not_verbose() {
    assert!(should_log("error", false));
}

#[test]
fn keeps_warn_logs_when_not_verbose() {
    assert!(should_log("warn", false));
}

#[test]
fn keeps_all_logs_when_verbose() {
    assert!(should_log("info", true));
}
"#;

const NORMALIZE_SETUP: &str = r#"pub fn normalize_user_id(input: &str) -> String {
    input
        .trim()
        .split_whitespace()
        .map(|part| part.to_lowercase())
        .collect::<Vec<_>>()
        .join("-")
}
"#;

const NORMALIZE_TESTS: &str = r#"
mod normalize;

use normalize::normalize_user_id;

#[test]
fn trims_outer_whitespace() {
    assert_eq!(normalize_user_id("  Alice  "), "alice");
}

#[test]
fn collapses_internal_spaces_to_single_dashes() {    assert_eq!(normalize_user_id("Alice Smith"), "alice-smith");
}

#[test]
fn strips_duplicate_dashes() {
    assert_eq!(normalize_user_id("Alice   Smith"), "alice-smith");
}
"#;

pub fn tool_reliability_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let timeout_dir = temp_dir.join("tool_reliability_timeout");
    let log_dir = temp_dir.join("tool_reliability_log_filter");
    let normalize_dir = temp_dir.join("tool_reliability_normalize");

    vec![
        BenchmarkTask {
            name: "tool_reliability_timeout",
            prompt: format!(
                "Inside {dir} there is a Rust config file defining `AppConfig`. Use AVA tools to locate it, read it, change the default `timeout_seconds` from 15 to 30, keep `retry_limit` unchanged, and verify the fix before finishing.",
                dir = timeout_dir.display()
            ),
            expected_patterns: vec![r"timeout_seconds", r"30"],
            category: TaskCategory::ToolReliability,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: TIMEOUT_CONFIG_TESTS,
                setup_code: Some(TIMEOUT_CONFIG_SETUP),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
        },
        BenchmarkTask {
            name: "tool_reliability_log_filter",
            prompt: format!(
                "Find the Rust file under {dir} that defines `should_log`. Use AVA tools to fix it so non-verbose mode keeps both `warn` and `error` logs, while verbose mode still logs everything. Verify the result before finishing.",
                dir = log_dir.display()
            ),
            expected_patterns: vec![r"warn", r"error"],
            category: TaskCategory::ToolReliability,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: LOG_FILTER_TESTS,
                setup_code: Some(LOG_FILTER_SETUP),
                test_count: 3,
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
        },
        BenchmarkTask {
            name: "tool_reliability_normalize",
            prompt: format!(
                "Use AVA tools to locate the implementation of `normalize_user_id` somewhere under {dir}. Fix it so it trims outer whitespace, lowercases the result, and converts one or more internal spaces into single dashes. Verify the change before finishing.",
                dir = normalize_dir.display()
            ),
            expected_patterns: vec![r"trim", r"to_lowercase|to_ascii_lowercase", r"-"],
            category: TaskCategory::ToolReliability,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: NORMALIZE_TESTS,
                setup_code: Some(NORMALIZE_SETUP),
                test_count: 3,
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
        },
    ]
}
