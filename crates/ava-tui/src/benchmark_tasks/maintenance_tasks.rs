use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

const MAINTENANCE_CONFIG_SETUP: &str = r#"#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ServiceConfig {
    pub timeout_seconds: u64,
    pub retry_limit: u8,
}

impl ServiceConfig {
    pub fn from_legacy_timeout_ms(timeout_ms: u64, retry_limit: u8) -> Self {
        Self {
            timeout_seconds: timeout_ms / 1000,
            retry_limit,
        }
    }
}
"#;

pub fn maintenance_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let task_dir = temp_dir.join("maintenance_config_migration");

    vec![BenchmarkTask {
        name: "maintenance_config_migration",
        prompt: format!(
            "Directory {dir} contains a Rust config migration that moved from milliseconds to \
             seconds, but behavior regressed across modules. Perform the maintenance pass: fix \
             conversion edge cases and keep formatting/output aligned with the new seconds-based \
             model. Verify tests pass after refactoring.",
            dir = task_dir.display()
        ),
        expected_patterns: vec![r"from_legacy_timeout_ms", r"timeout_seconds", r"/1000|div"],
        category: TaskCategory::Maintenance,
        needs_tools: true,
        test_harness: Some(TestHarness {
            test_code: r#"
mod config;
mod migrate;
mod render;

use config::ServiceConfig;
use migrate::parse_legacy_config;
use render::render_summary;

#[test]
fn conversion_keeps_minimum_one_second_for_non_zero_timeout() {
    let cfg = ServiceConfig::from_legacy_timeout_ms(1, 3);
    assert_eq!(cfg.timeout_seconds, 1);
}

#[test]
fn parse_legacy_supports_timeout_ms() {
    let cfg = parse_legacy_config("timeout_ms=2500\nretry_limit=4").unwrap();
    assert_eq!(cfg.timeout_seconds, 3);
    assert_eq!(cfg.retry_limit, 4);
}

#[test]
fn render_uses_seconds_label() {
    let cfg = ServiceConfig {
        timeout_seconds: 3,
        retry_limit: 2,
    };
    assert_eq!(render_summary(&cfg), "timeout=3s retry_limit=2");
}
"#,
            setup_code: Some(MAINTENANCE_CONFIG_SETUP),
            test_count: 3,
            language: Language::Rust,
        }),
        expected_min_tools: Some(5),
    }]
}
