use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

const STRESS_LOG_PARSER_SETUP: &str = r#"pub fn parse_log_line(line: &str) -> Option<(String, u64)> {
    let mut parts = line.split('|');
    let level = parts.next()?.to_string();
    let latency_ms = parts.next()?.parse::<u64>().ok()?;
    Some((level, latency_ms))
}
"#;

pub fn stress_coding_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let task_dir = temp_dir.join("stress_coding_log_pipeline");

    vec![BenchmarkTask {
        name: "stress_coding_log_pipeline",
        prompt: format!(
            "Under {dir} there is a small Rust log-processing mini-project split across files. \
             Use tools to inspect and fix it so the parser handles whitespace safely and the \
             aggregate metrics are correct (including percentile and error-rate math). Keep the \
             structure multi-file and verify the tests pass.",
            dir = task_dir.display()
        ),
        expected_patterns: vec![r"trim", r"sort|percentile|p95", r"error_rate|as\s+f64"],
        category: TaskCategory::StressCoding,
        needs_tools: true,
        test_harness: Some(TestHarness {
            test_code: r#"
mod parser;
mod aggregates;

use aggregates::{error_rate, p95_latency};
use parser::parse_log_line;

#[test]
fn parser_trims_and_parses() {
    let parsed = parse_log_line(" warn | 120 ").unwrap();
    assert_eq!(parsed, ("warn".to_string(), 120));
}

#[test]
fn parser_rejects_malformed_lines() {
    assert!(parse_log_line("warn-only").is_none());
    assert!(parse_log_line("warn|abc").is_none());
}

#[test]
fn p95_uses_last_valid_index() {
    let values = vec![10, 20, 30, 40, 50, 60, 70, 80, 90, 100];
    assert_eq!(p95_latency(&values), 100);
}

#[test]
fn error_rate_returns_fraction() {
    let levels = vec![
        "info".to_string(),
        "error".to_string(),
        "warn".to_string(),
        "error".to_string(),
    ];
    assert!((error_rate(&levels) - 0.5).abs() < 1e-9);
}
"#,
            setup_code: Some(STRESS_LOG_PARSER_SETUP),
            test_count: 4,
            language: Language::Rust,
        }),
        expected_min_tools: Some(5),
    }]
}
