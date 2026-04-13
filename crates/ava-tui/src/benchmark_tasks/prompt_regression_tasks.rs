use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

const VERIFY_BEFORE_FINISH_SETUP: &str = r#"pub fn is_healthy_status(status: u16) -> bool {
    status >= 200 && status <= 400
}
"#;

const VERIFY_BEFORE_FINISH_TESTS: &str = r#"
mod health;

use health::is_healthy_status;

#[test]
fn allows_200_ok() {
    assert!(is_healthy_status(200));
}

#[test]
fn allows_299_redirect_boundary() {
    assert!(is_healthy_status(299));
}

#[test]
fn rejects_400_client_error() {
    assert!(!is_healthy_status(400));
}
"#;

const TARGETED_EDIT_ONLY_SETUP: &str = r#"pub fn default_api_base_url() -> &'static str {
    "https://api.dev.local"
}

pub fn default_web_base_url() -> &'static str {
    "https://web.dev.local"
}
"#;

const TARGETED_EDIT_ONLY_TESTS: &str = r#"
mod endpoints;

use endpoints::{default_api_base_url, default_web_base_url};

#[test]
fn api_url_is_promoted_to_prod() {
    assert_eq!(default_api_base_url(), "https://api.prod.local");
}

#[test]
fn web_url_remains_unchanged() {
    assert_eq!(default_web_base_url(), "https://web.dev.local");
}
"#;

const MINIMAL_PATCH_SETUP: &str = r#"pub fn normalize_tag(tag: &str) -> String {
    tag.trim().to_lowercase().replace(' ', "-")
}
"#;

const MINIMAL_PATCH_TESTS: &str = r#"
mod normalize;

use normalize::normalize_tag;

#[test]
fn lowercases_and_trims() {
    assert_eq!(normalize_tag("  Release Candidate  "), "release-candidate");
}

#[test]
fn collapses_internal_whitespace_sequences() {
    assert_eq!(normalize_tag("Release\t\tCandidate   One"), "release-candidate-one");
}
"#;

const READ_BEFORE_EDIT_SETUP: &str = r#"pub fn parse_timeout_ms(input: &str) -> Option<u64> {
    input.parse::<u64>().ok()
}
"#;

const READ_BEFORE_EDIT_TESTS: &str = r#"
mod parser;

use parser::parse_timeout_ms;

#[test]
fn parses_plain_value() {
    assert_eq!(parse_timeout_ms("2500"), Some(2500));
}

#[test]
fn trims_before_parsing() {
    assert_eq!(parse_timeout_ms(" 3000 "), Some(3000));
}
"#;

const WRONG_FIRST_EDIT_RECOVERY_SETUP: &str = r#"pub fn multiply(a: i32, b: i32) -> i32 {
    a + b
}
"#;

const WRONG_FIRST_EDIT_RECOVERY_TESTS: &str = r#"
mod arithmetic;

use arithmetic::multiply;

#[test]
fn multiplies_positive_numbers() {
    assert_eq!(multiply(6, 7), 42);
}

#[test]
fn multiplies_by_zero() {
    assert_eq!(multiply(0, 9), 0);
}
"#;

const TOOL_CHOICE_DISCIPLINE_SETUP: &str = r#"pub fn default_retry_policy() -> (&'static str, u8) {
    ("aggressive", 5)
}
"#;

const TOOL_CHOICE_DISCIPLINE_TESTS: &str = r#"
mod policy;

use policy::default_retry_policy;

#[test]
fn policy_is_balanced() {
    assert_eq!(default_retry_policy(), ("balanced", 3));
}
"#;

pub fn prompt_regression_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let verify_dir = temp_dir.join("prompt_regression_verify_before_finish");
    let targeted_dir = temp_dir.join("prompt_regression_targeted_edit_only");
    let minimal_dir = temp_dir.join("prompt_regression_minimal_patch");
    let read_dir = temp_dir.join("prompt_regression_read_before_edit");
    let recovery_dir = temp_dir.join("prompt_regression_wrong_first_edit_recovery");
    let tool_choice_dir = temp_dir.join("prompt_regression_tool_choice_discipline");

    vec![
        BenchmarkTask {
            name: "prompt_regression_verify_before_finish",
            prompt: format!(
                "In {dir}, fix `is_healthy_status` so only 2xx responses are treated as healthy. This is a verify-before-finish task: run the Rust tests and only report success after they pass.",
                dir = verify_dir.display()
            ),
            expected_patterns: vec![
                r"is_healthy_status",
                r"200",
                r"299|<\s*300",
                r"tool:bash",
            ],
            category: TaskCategory::PromptRegression,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: VERIFY_BEFORE_FINISH_TESTS,
                setup_code: Some(VERIFY_BEFORE_FINISH_SETUP),
                test_count: 3,
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
        },
        BenchmarkTask {
            name: "prompt_regression_targeted_edit_only",
            prompt: format!(
                "In {dir}, update only `default_api_base_url` to return `https://api.prod.local`. Keep `default_web_base_url` unchanged. Make a targeted edit only, then verify tests.",
                dir = targeted_dir.display()
            ),
            expected_patterns: vec![r"default_api_base_url", r"api\.prod\.local", r"default_web_base_url"],
            category: TaskCategory::PromptRegression,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: TARGETED_EDIT_ONLY_TESTS,
                setup_code: Some(TARGETED_EDIT_ONLY_SETUP),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
        },
        BenchmarkTask {
            name: "prompt_regression_minimal_patch",
            prompt: format!(
                "Under {dir}, apply a minimal patch to `normalize_tag` so it collapses one-or-more whitespace runs into a single dash while preserving the existing function signature and behavior for simple inputs. Verify with tests.",
                dir = minimal_dir.display()
            ),
            expected_patterns: vec![r"normalize_tag", r"split_whitespace|whitespace", r"-"],
            category: TaskCategory::PromptRegression,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: MINIMAL_PATCH_TESTS,
                setup_code: Some(MINIMAL_PATCH_SETUP),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
        },
        BenchmarkTask {
            name: "prompt_regression_read_before_edit",
            prompt: format!(
                "Read {dir}/README.md first, then update `parse_timeout_ms` in the same directory to follow the documented behavior. This is a read-before-edit task; verify tests before finishing.",
                dir = read_dir.display()
            ),
            expected_patterns: vec![
                r"parse_timeout_ms",
                r"trim",
                r"tool:bash",
            ],
            category: TaskCategory::PromptRegression,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: READ_BEFORE_EDIT_TESTS,
                setup_code: Some(READ_BEFORE_EDIT_SETUP),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
        },
        BenchmarkTask {
            name: "prompt_regression_wrong_first_edit_recovery",
            prompt: format!(
                "In {dir}, `multiply` is wrong. If your first attempt is incorrect, recover by re-running tests and correcting the edit. Only finish after tests pass.",
                dir = recovery_dir.display()
            ),
            expected_patterns: vec![r"multiply", r"\b[a-zA-Z_]+\s*\*\s*[a-zA-Z_]", r"tool:bash"],
            category: TaskCategory::PromptRegression,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: WRONG_FIRST_EDIT_RECOVERY_TESTS,
                setup_code: Some(WRONG_FIRST_EDIT_RECOVERY_SETUP),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
        },
        BenchmarkTask {
            name: "prompt_regression_tool_choice_discipline",
            prompt: format!(
                "In {dir}, fix `default_retry_policy` to return the intended defaults. This is a tool-choice discipline task: use direct file tools and avoid unnecessary delegation/subagent usage. Verify tests before finishing.",
                dir = tool_choice_dir.display()
            ),
            expected_patterns: vec![r"default_retry_policy", r"balanced", r"\b3\b"],
            category: TaskCategory::PromptRegression,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: TOOL_CHOICE_DISCIPLINE_TESTS,
                setup_code: Some(TOOL_CHOICE_DISCIPLINE_SETUP),
                test_count: 1,
                language: Language::Rust,
            }),
            expected_min_tools: Some(3),
        },
    ]
}
