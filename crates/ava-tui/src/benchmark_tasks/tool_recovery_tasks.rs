use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

const TOOL_RECOVERY_MISSING_FILE_SETUP: &str = r#"pub fn format_ticket_slug(title: &str) -> String {
    let slug = crate::slug::slugify(title);
    format!("ticket-{}", slug)
}
"#;

const TOOL_RECOVERY_MISSING_FILE_TESTS: &str = r#"
mod formatter;
mod slug;

use formatter::format_ticket_slug;

#[test]
fn creates_lowercase_ticket_slug() {
    assert_eq!(format_ticket_slug("Login Failure"), "ticket-login-failure");
}

#[test]
fn trims_outer_whitespace_before_slugifying() {
    assert_eq!(format_ticket_slug("  Cache Miss  "), "ticket-cache-miss");
}
"#;

const TOOL_RECOVERY_TARGETED_EDIT_SETUP: &str = r#"pub fn default_api_timeout_seconds() -> u64 {
    15
}

pub fn default_ui_timeout_seconds() -> u64 {
    15
}

pub fn timeout_profile() -> (u64, u64) {
    (default_api_timeout_seconds(), default_ui_timeout_seconds())
}
"#;

const TOOL_RECOVERY_TARGETED_EDIT_TESTS: &str = r#"
mod timeouts;

use timeouts::{default_api_timeout_seconds, default_ui_timeout_seconds, timeout_profile};

#[test]
fn api_timeout_default_is_updated() {
    assert_eq!(default_api_timeout_seconds(), 30);
}

#[test]
fn ui_timeout_default_is_unchanged() {
    assert_eq!(default_ui_timeout_seconds(), 15);
}

#[test]
fn profile_reflects_mixed_defaults() {
    assert_eq!(timeout_profile(), (30, 15));
}
"#;

const TOOL_RECOVERY_VERIFICATION_DISCIPLINE_SETUP: &str = r#"pub fn is_transition_allowed(from: &str, to: &str) -> bool {
    matches!(
        (from, to),
        ("queued", "running")
            | ("running", "completed")
            | ("running", "failed")
            | ("queued", "failed")
            | ("completed", "failed")
            | ("completed", "archived")
    )
}
"#;

const TOOL_RECOVERY_VERIFICATION_DISCIPLINE_TESTS: &str = r#"
mod status;

use status::is_transition_allowed;

#[test]
fn allows_queued_to_running() {
    assert!(is_transition_allowed("queued", "running"));
}

#[test]
fn disallows_queued_to_completed() {
    assert!(!is_transition_allowed("queued", "completed"));
}

#[test]
fn disallows_completed_to_failed() {
    assert!(!is_transition_allowed("completed", "failed"));
}
"#;

pub fn tool_recovery_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let missing_file_dir = temp_dir.join("tool_recovery_missing_file");
    let targeted_edit_dir = temp_dir.join("tool_recovery_targeted_edit");
    let verification_dir = temp_dir.join("tool_recovery_verification_discipline");

    vec![
        BenchmarkTask {
            name: "tool_recovery_missing_file",
            prompt: format!(
                "Directory {dir} has Rust tests that reference a missing module file. Recover by creating the missing file and implementing `slugify` so ticket slug formatting works deterministically. Verify all tests pass before finishing.",
                dir = missing_file_dir.display()
            ),
            expected_patterns: vec![r"slugify", r"ticket-", r"trim|whitespace|lower"],
            category: TaskCategory::ToolRecovery,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: TOOL_RECOVERY_MISSING_FILE_TESTS,
                setup_code: Some(TOOL_RECOVERY_MISSING_FILE_SETUP),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(5),
        },
        BenchmarkTask {
            name: "tool_recovery_targeted_edit",
            prompt: format!(
                "In {dir}, update only the API timeout default from 15 to 30 while keeping the UI timeout default at 15. This is a targeted edit/recovery task: avoid broad replacements and recover if a first edit overreaches. Verify tests before finishing.",
                dir = targeted_edit_dir.display()
            ),
            expected_patterns: vec![r"default_api_timeout_seconds", r"30", r"default_ui_timeout_seconds"],
            category: TaskCategory::ToolRecovery,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: TOOL_RECOVERY_TARGETED_EDIT_TESTS,
                setup_code: Some(TOOL_RECOVERY_TARGETED_EDIT_SETUP),
                test_count: 3,
                language: Language::Rust,
            }),
            expected_min_tools: Some(5),
        },
        BenchmarkTask {
            name: "tool_recovery_verification_discipline",
            prompt: format!(
                "In {dir}, fix `is_transition_allowed` so terminal states are not treated as recoverable. Verification discipline is required here: run the Rust tests and only claim success after the suite is green.",
                dir = verification_dir.display()
            ),
            expected_patterns: vec![r"is_transition_allowed", r"completed", r"test|tests"],
            category: TaskCategory::ToolRecovery,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: TOOL_RECOVERY_VERIFICATION_DISCIPLINE_TESTS,
                setup_code: Some(TOOL_RECOVERY_VERIFICATION_DISCIPLINE_SETUP),
                test_count: 3,
                language: Language::Rust,
            }),
            expected_min_tools: Some(5),
        },
    ]
}
