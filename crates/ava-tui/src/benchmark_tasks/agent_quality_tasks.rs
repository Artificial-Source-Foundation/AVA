use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

const MULTI_STEP_DEBUG_LIB: &str = r#"/// Calculates the area of a rectangle.
pub fn area(width: f64, height: f64) -> f64 {
    width * height
}

/// Calculates the perimeter of a rectangle.
pub fn perimeter(width: f64, height: f64) -> f64 {
    2.0 * width + height
}

/// Calculates the diagonal of a rectangle.
pub fn diagonal(width: f64, height: f64) -> f64 {
    (width * width + height * height).sqrt()
}
"#;

const MULTI_STEP_DEBUG_VALIDATE: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_area() {
        assert!((area(3.0, 4.0) - 12.0).abs() < 1e-9);
    }

    #[test]
    fn test_perimeter() {
        assert!((perimeter(3.0, 4.0) - 14.0).abs() < 1e-9);
    }

    #[test]
    fn test_diagonal() {
        assert!((diagonal(3.0, 4.0) - 5.0).abs() < 1e-9);
    }
}
"#;

const CONSTRAINT_EDIT_SETUP: &str = r#"/// Validates an email address.
pub fn validate_email(email: &str) -> bool {
    true
}

/// Validates a phone number.
pub fn validate_phone(phone: &str) -> bool {
    true
}

/// Validates a URL.
pub fn validate_url(url: &str) -> bool {
    true
}

fn main() {}
"#;

const CONSTRAINT_EDIT_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_valid_email() {
        assert!(validate_email("user@example.com"));
    }

    #[test]
    fn test_invalid_email_no_at() {
        assert!(!validate_email("userexample.com"));
    }

    #[test]
    fn test_invalid_email_no_dot_after_at() {
        assert!(!validate_email("user@examplecom"));
    }

    #[test]
    fn test_phone_unchanged() {
        assert!(validate_phone("anything"));
    }

    #[test]
    fn test_url_unchanged() {
        assert!(validate_url("anything"));
    }
}
"#;

const SELF_CORRECT_COMPILE_SETUP: &str = r#"/// A simple in-memory cache.
pub struct Cache {
    store: HashMap<String, String>,
}

impl Cache {
    pub fn new() -> Self {
        Self {
            store: HashMap::new(),
        }
    }

    pub fn get(&self, key: &str) -> Option<&String> {
        self.store.get(key)
    }

    pub fn set(&mut self, key: String, value: String) {
        self.store.insert(key, value);
    }

    pub fn remove(&mut self, key: &str) -> Option<String> {
        self.store.remove(key)
    }

    pub fn len(&self) -> usize {
        self.store.len()
    }

    pub fn is_empty(&self) -> bool {
        self.store.is_empty()
    }
}

fn main() {}
"#;

const SELF_CORRECT_COMPILE_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_cache_set_get() {
        let mut cache = Cache::new();
        cache.set("key".to_string(), "value".to_string());
        assert_eq!(cache.get("key"), Some(&"value".to_string()));
    }

    #[test]
    fn test_cache_remove() {
        let mut cache = Cache::new();
        cache.set("key".to_string(), "value".to_string());
        assert_eq!(cache.remove("key"), Some("value".to_string()));
        assert!(cache.is_empty());
    }
}
"#;

const TOOL_EFFICIENCY_CONFIG: &str = r#"/// Application configuration.
#[derive(Debug)]
pub struct Config {
    pub name: String,
    pub debug: bool,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            name: "World".to_string(),
            debug: false,
        }
    }
}
"#;

const TOOL_EFFICIENCY_VALIDATE: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_config_has_timeout() {
        let cfg = Config::default();
        assert_eq!(cfg.timeout_seconds, 30);
    }

    #[test]
    fn test_config_name() {
        let cfg = Config::default();
        assert_eq!(cfg.name, "World");
    }
}
"#;

const NO_OVERENGINEER_SETUP: &str = r#"pub fn add(a: i32, b: i32) -> i32 {
    a + b
}

fn main() {}
"#;

const NO_OVERENGINEER_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_add_basic() {
        assert_eq!(add(2, 3), 5);
    }

    #[test]
    fn test_add_negative() {
        assert_eq!(add(-1, 1), 0);
    }
}
"#;

const ERROR_RECOVERY_SETUP: &str = r#"use nonexistent_crate::Thing;

/// Counts occurrences of each word in the input.
pub fn word_count(input: &str) -> Thing<String, usize> {
    let mut counts = Thing::new();
    for word in input.split_whitespace() {
        let w = word.to_lowercase();
        *counts.entry(w).or_insert(0) += 1;
    }
    counts
}

fn main() {}
"#;

const ERROR_RECOVERY_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_word_count_basic() {
        let counts = word_count("hello world hello");
        assert_eq!(counts.get("hello"), Some(&2));
        assert_eq!(counts.get("world"), Some(&1));
    }

    #[test]
    fn test_word_count_empty() {
        let counts = word_count("");
        assert_eq!(counts.len(), 0);
    }
}
"#;

const RULE_GUIDED_TYPESCRIPT_SETUP: &str = r#"export type User = {
    role: string
}

export function isAdmin(user: User): boolean {
    return false
}
"#;

const DELEGATED_CONFIG_SETUP: &str = r#"#[derive(Debug, Clone)]
pub struct Config {
    pub retry_limit: usize,
    pub timeout_ms: u64,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            retry_limit: 3,
            timeout_ms: 5000,
        }
    }
}
"#;

/// Returns agent-quality benchmark tasks that test multi-step reasoning,
/// self-correction, and constraint following. Requires a temp directory path.
pub fn agent_quality_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let debug_dir = temp_dir.join("multi_step_debug");
    let constraint_path = temp_dir.join("validators.rs");
    let self_correct_path = temp_dir.join("cache.rs");
    let efficiency_dir = temp_dir.join("tool_efficiency").join("src");
    let no_overengineer_path = temp_dir.join("math.rs");
    let error_recovery_path = temp_dir.join("broken.rs");
    let ts_rule_path = temp_dir.join("frontend").join("app.ts");
    let delegation_dir = temp_dir.join("delegated_config_bugfix");

    vec![
        BenchmarkTask {
            name: "multi_step_debug",
            prompt: format!(
                "The file {}/lib.rs has a bug causing test failures. Read the test file at \
                 {}/tests.rs to understand what's expected, find the bug in lib.rs, fix it with \
                 the edit tool, then run `rustc --edition 2021 --test {}/tests.rs` to verify \
                 your fix works.",
                debug_dir.display(),
                debug_dir.display(),
                debug_dir.display(),
            ),
            expected_patterns: vec![r"(?i)(fix|bug|perimeter|2\.0\s*\*\s*\()"],
            category: TaskCategory::MultiStep,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: MULTI_STEP_DEBUG_VALIDATE,
                setup_code: Some(MULTI_STEP_DEBUG_LIB),
                test_count: 3,
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
        },
        BenchmarkTask {
            name: "constraint_edit",
            prompt: format!(
                "Read {path}. Implement ONLY the `validate_email` function with proper email \
                 validation (must contain @ and a dot after @). Do NOT modify `validate_phone` \
                 or `validate_url` — leave them exactly as they are.",
                path = constraint_path.display(),
            ),
            expected_patterns: vec![r"(?i)(validate_email|@|contains)"],
            category: TaskCategory::ConstraintFollowing,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: CONSTRAINT_EDIT_TESTS,
                setup_code: Some(CONSTRAINT_EDIT_SETUP),
                test_count: 5,
                language: Language::Rust,
            }),
            expected_min_tools: Some(2),
        },
        BenchmarkTask {
            name: "self_correct_compile",
            prompt: format!(
                "The file {path} should implement a simple cache. Run \
                 `rustc --edition 2021 {path}` to check if it compiles. If there are errors, \
                 fix them using the edit tool, then verify it compiles.",
                path = self_correct_path.display(),
            ),
            expected_patterns: vec![r"(?i)(use std::collections::HashMap|HashMap|import|compile)"],
            category: TaskCategory::SelfCorrection,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: SELF_CORRECT_COMPILE_TESTS,
                setup_code: Some(SELF_CORRECT_COMPILE_SETUP),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(3),
        },
        BenchmarkTask {
            name: "tool_efficiency",
            prompt: format!(
                "In the project at {dir}/, find the config module and add a `timeout_seconds: u32` \
                 field with a default value of 30 to the Config struct. You'll need to explore \
                 the project structure first.",
                dir = efficiency_dir.display(),
            ),
            expected_patterns: vec![r"timeout_seconds"],
            category: TaskCategory::MultiStep,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: TOOL_EFFICIENCY_VALIDATE,
                setup_code: Some(TOOL_EFFICIENCY_CONFIG),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
        },
        BenchmarkTask {
            name: "no_overengineer",
            prompt: format!(
                "Read {path}. The `add` function is correct but has no documentation. \
                 Add a doc comment (/// ...) to the `add` function. Do not change anything else.",
                path = no_overengineer_path.display(),
            ),
            expected_patterns: vec![r"///"],
            category: TaskCategory::ConstraintFollowing,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: NO_OVERENGINEER_TESTS,
                setup_code: Some(NO_OVERENGINEER_SETUP),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(2),
        },
        BenchmarkTask {
            name: "error_recovery_loop",
            prompt: format!(
                "The file {path} fails to compile. Try to compile it with \
                 `rustc --edition 2021 {path}`, diagnose the issue, and fix it. The function \
                 should use a HashMap instead of the external dependency. Verify it compiles \
                 after fixing.",
                path = error_recovery_path.display(),
            ),
            expected_patterns: vec![r"(?i)(HashMap|std::collections)"],
            category: TaskCategory::SelfCorrection,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: ERROR_RECOVERY_TESTS,
                setup_code: Some(ERROR_RECOVERY_SETUP),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(3),
        },
        BenchmarkTask {
            name: "rule_guided_typescript",
            prompt: format!(
                "Read {path}. Implement `isAdmin(user: User): boolean` so it returns true only \
                 when the role is `admin`. Keep the exported API unchanged and follow the local project rules.",
                path = ts_rule_path.display(),
            ),
            expected_patterns: vec![r"isAdmin"],
            category: TaskCategory::ConstraintFollowing,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: "",
                setup_code: Some(RULE_GUIDED_TYPESCRIPT_SETUP),
                test_count: 1,
                language: Language::JavaScript,
            }),
            expected_min_tools: Some(2),
        },
        BenchmarkTask {
            name: "delegated_config_bugfix",
            prompt: format!(
                "In the project at {dir}, first understand how retry settings should flow from \
                 `Config` into `Client`, then fix the bug so `Client::new()` uses the configured \
                 `retry_limit` and `timeout_ms`. Verify with `rustc --edition 2021 --test {dir}/tests.rs`. \
                 This task is intentionally split across several files.",
                dir = delegation_dir.display(),
            ),
            expected_patterns: vec![r"retry_limit", r"timeout_ms"],
            category: TaskCategory::MultiStep,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: "",
                setup_code: Some(DELEGATED_CONFIG_SETUP),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
        },
    ]
}
