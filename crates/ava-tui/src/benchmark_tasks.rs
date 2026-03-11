//! Benchmark task definitions for model comparison.
//!
//! Each task has a prompt, expected output patterns for quality validation,
//! and a difficulty category. Tasks may include a test harness for compile-and-test
//! validation (Tier 2) or setup code for agentic editing tasks (Tier 3).

/// Benchmark suite tiers — controls which tasks are included.
///
/// Suites are cumulative: `Speed` includes only speed-tier tasks,
/// `Standard` includes speed + standard, and `Frontier`/`All` include everything.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum BenchmarkSuite {
    Speed,
    Standard,
    Frontier,
    All,
}

impl BenchmarkSuite {
    /// Parse from a string (case-insensitive).
    pub fn from_str(s: &str) -> Option<Self> {
        match s.to_lowercase().as_str() {
            "speed" => Some(Self::Speed),
            "standard" => Some(Self::Standard),
            "frontier" => Some(Self::Frontier),
            "all" => Some(Self::All),
            _ => None,
        }
    }
}

impl std::fmt::Display for BenchmarkSuite {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Speed => write!(f, "speed"),
            Self::Standard => write!(f, "standard"),
            Self::Frontier => write!(f, "frontier"),
            Self::All => write!(f, "all"),
        }
    }
}

/// Difficulty category for a benchmark task.
#[derive(Debug, Clone, Copy, serde::Serialize)]
#[serde(rename_all = "snake_case")]
pub enum TaskCategory {
    Simple,
    Medium,
    Hard,
    ToolUse,
    RealWorld,
    Agentic,
    /// Tasks requiring multiple tool calls in sequence.
    MultiStep,
    /// Tasks where the first attempt should fail, testing recovery.
    SelfCorrection,
    /// Tasks that must follow specific constraints.
    ConstraintFollowing,
}

impl std::fmt::Display for TaskCategory {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Simple => write!(f, "simple"),
            Self::Medium => write!(f, "medium"),
            Self::Hard => write!(f, "hard"),
            Self::ToolUse => write!(f, "tool_use"),
            Self::RealWorld => write!(f, "real_world"),
            Self::Agentic => write!(f, "agentic"),
            Self::MultiStep => write!(f, "multi_step"),
            Self::SelfCorrection => write!(f, "self_correction"),
            Self::ConstraintFollowing => write!(f, "constraint_following"),
        }
    }
}

impl TaskCategory {
    /// Which benchmark suite this category belongs to (minimum suite that includes it).
    pub fn min_suite(&self) -> BenchmarkSuite {
        match self {
            Self::Simple | Self::Medium | Self::Hard | Self::ToolUse | Self::RealWorld => {
                BenchmarkSuite::Speed
            }
            Self::Agentic | Self::ConstraintFollowing | Self::SelfCorrection => {
                BenchmarkSuite::Standard
            }
            Self::MultiStep => BenchmarkSuite::Frontier,
        }
    }
}

/// Filter tasks to those included in the given suite.
///
/// Speed includes only speed-tier tasks.
/// Standard includes speed + standard tasks.
/// Frontier and All include everything.
pub fn filter_tasks_by_suite(tasks: Vec<BenchmarkTask>, suite: BenchmarkSuite) -> Vec<BenchmarkTask> {
    match suite {
        BenchmarkSuite::All | BenchmarkSuite::Frontier => tasks,
        BenchmarkSuite::Standard => tasks
            .into_iter()
            .filter(|t| {
                matches!(
                    t.category.min_suite(),
                    BenchmarkSuite::Speed | BenchmarkSuite::Standard
                )
            })
            .collect(),
        BenchmarkSuite::Speed => tasks
            .into_iter()
            .filter(|t| matches!(t.category.min_suite(), BenchmarkSuite::Speed))
            .collect(),
    }
}

/// Test harness for compile-and-test validation.
#[derive(Debug, Clone)]
pub struct TestHarness {
    /// Test code to append to the extracted model output (Tier 2)
    /// or to the file after agent edits (Tier 3).
    pub test_code: &'static str,
    /// For Tier 3: initial buggy file content to write before running.
    pub setup_code: Option<&'static str>,
    /// Number of test functions in the harness.
    pub test_count: usize,
}

/// A single benchmark task.
#[derive(Debug, Clone)]
pub struct BenchmarkTask {
    /// Short identifier for the task (used in table headers).
    pub name: &'static str,
    /// The prompt sent to the model. Use `String` for runtime path interpolation.
    pub prompt: String,
    /// Regex patterns that should appear in a successful response.
    pub expected_patterns: Vec<&'static str>,
    /// Difficulty category.
    pub category: TaskCategory,
    /// Whether this task requires tool use (agent mode).
    pub needs_tools: bool,
    /// Optional test harness for compile-and-test validation.
    pub test_harness: Option<TestHarness>,
}

/// Returns the default set of benchmark tasks.
pub fn default_tasks() -> Vec<BenchmarkTask> {
    vec![
        BenchmarkTask {
            name: "is_palindrome",
            prompt: "Write a Rust function `is_palindrome(s: &str) -> bool` that checks if a string \
                     is a palindrome, ignoring case and non-alphanumeric characters. Only output the \
                     function code, no explanation needed.".to_string(),
            expected_patterns: vec![
                r"fn\s+is_palindrome",
                r"-> bool",
                r"(?i)(to_lowercase|to_ascii_lowercase|eq_ignore_ascii_case)",
            ],
            category: TaskCategory::Simple,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_simple_palindrome() {
        assert!(is_palindrome("racecar"));
    }

    #[test]
    fn test_not_palindrome() {
        assert!(!is_palindrome("hello"));
    }

    #[test]
    fn test_mixed_case_with_spaces() {
        assert!(is_palindrome("A man a plan a canal Panama"));
    }

    #[test]
    fn test_empty_string() {
        assert!(is_palindrome(""));
    }

    #[test]
    fn test_punctuation_ignored() {
        assert!(is_palindrome("No lemon, no melon"));
    }
}
"#,
                setup_code: None,
                test_count: 5,
            }),
        },
        BenchmarkTask {
            name: "merge_sorted",
            prompt: "Write a Rust function `merge_sorted(a: &[i32], b: &[i32]) -> Vec<i32>` that \
                     merges two sorted slices into a single sorted vector in O(n+m) time. Only output \
                     the function code, no explanation needed.".to_string(),
            expected_patterns: vec![
                r"fn\s+merge_sorted",
                r"Vec<i32>",
                r"(&\[i32\]|&\[i32\])",
            ],
            category: TaskCategory::Medium,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic_merge() {
        assert_eq!(merge_sorted(&[1, 3, 5], &[2, 4, 6]), vec![1, 2, 3, 4, 5, 6]);
    }

    #[test]
    fn test_empty_first() {
        assert_eq!(merge_sorted(&[], &[1, 2]), vec![1, 2]);
    }

    #[test]
    fn test_empty_second() {
        assert_eq!(merge_sorted(&[1], &[]), vec![1]);
    }

    #[test]
    fn test_both_empty() {
        assert_eq!(merge_sorted(&[], &[]), Vec::<i32>::new());
    }
}
"#,
                setup_code: None,
                test_count: 4,
            }),
        },
        BenchmarkTask {
            name: "lru_cache",
            prompt: "Write a Rust module with a `LruCache<K, V>` struct (where K: Eq + Hash + Clone, V: Clone) \
                     that supports `new(capacity: usize)`, \
                     `get(&mut self, key: &K) -> Option<V>`, and `put(&mut self, key: K, value: V)` operations. \
                     Use a HashMap and a Vec or VecDeque for ordering. Only output the code, \
                     no explanation needed. Do NOT use any external crates.".to_string(),
            expected_patterns: vec![
                r"(?i)struct\s+LRUCache|struct\s+LruCache",
                r"fn\s+(new|get|put)",
                r"HashMap",
            ],
            category: TaskCategory::Hard,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_basic_put_get() {
        let mut cache = LruCache::new(2);
        cache.put(1, "a".to_string());
        cache.put(2, "b".to_string());
        assert_eq!(cache.get(&1), Some("a".to_string()));
        assert_eq!(cache.get(&2), Some("b".to_string()));
    }

    #[test]
    fn test_capacity_eviction() {
        let mut cache = LruCache::new(2);
        cache.put(1, "a".to_string());
        cache.put(2, "b".to_string());
        cache.put(3, "c".to_string()); // evicts key 1
        assert_eq!(cache.get(&1), None);
        assert_eq!(cache.get(&3), Some("c".to_string()));
    }

    #[test]
    fn test_get_updates_recency() {
        let mut cache = LruCache::new(2);
        cache.put(1, "a".to_string());
        cache.put(2, "b".to_string());
        cache.get(&1); // makes 1 most recently used
        cache.put(3, "c".to_string()); // should evict key 2, not 1
        assert_eq!(cache.get(&1), Some("a".to_string()));
        assert_eq!(cache.get(&2), None);
        assert_eq!(cache.get(&3), Some("c".to_string()));
    }
}
"#,
                setup_code: None,
                test_count: 3,
            }),
        },
        BenchmarkTask {
            name: "bash_echo",
            prompt: "Use the bash tool to run `echo hello` and report the output.".to_string(),
            expected_patterns: vec![
                r"(?i)hello",
            ],
            category: TaskCategory::ToolUse,
            needs_tools: true,
            test_harness: None,
        },
        BenchmarkTask {
            name: "read_cargo",
            prompt: "Read the file Cargo.toml in the current directory and list all workspace members.".to_string(),
            expected_patterns: vec![
                r"(?i)(member|crate|workspace)",
            ],
            category: TaskCategory::RealWorld,
            needs_tools: true,
            test_harness: None,
        },
    ]
}

/// Buggy binary search code with an off-by-one error (Tier 3 setup).
const BUGFIX_OFF_BY_ONE_SETUP: &str = r#"use std::cmp::Ordering;

/// Binary search for `target` in a sorted slice. Returns the index if found.
pub fn binary_search(arr: &[i32], target: i32) -> Option<usize> {
    if arr.is_empty() {
        return None;
    }
    let mut low: usize = 0;
    let mut high: usize = arr.len(); // BUG: should be arr.len() - 1

    while low <= high {
        let mid = low + (high - low) / 2;
        if mid >= arr.len() {
            return None;
        }
        match arr[mid].cmp(&target) {
            Ordering::Equal => return Some(mid),
            Ordering::Less => low = mid + 1,
            Ordering::Greater => {
                if mid == 0 {
                    return None;
                }
                high = mid - 1;
            }
        }
    }
    None
}

fn main() {}
"#;

const BUGFIX_OFF_BY_ONE_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_found_middle() {
        assert_eq!(binary_search(&[1, 3, 5, 7, 9], 5), Some(2));
    }

    #[test]
    fn test_found_first() {
        assert_eq!(binary_search(&[1, 3, 5, 7, 9], 1), Some(0));
    }

    #[test]
    fn test_found_last() {
        assert_eq!(binary_search(&[1, 3, 5, 7, 9], 9), Some(4));
    }

    #[test]
    fn test_not_found() {
        assert_eq!(binary_search(&[1, 3, 5, 7, 9], 4), None);
    }

    #[test]
    fn test_empty() {
        assert_eq!(binary_search(&[], 1), None);
    }

    #[test]
    fn test_single_element_found() {
        assert_eq!(binary_search(&[42], 42), Some(0));
    }
}
"#;

/// Rust lifetime error code (Tier 3 setup).
const BUGFIX_LIFETIME_SETUP: &str = r#"/// Returns the longer of two string slices.
pub fn longest(x: &str, y: &str) -> &str {
    if x.len() >= y.len() {
        x
    } else {
        y
    }
}

/// Wraps a string reference with a prefix.
pub struct Wrapper {
    prefix: String,
    value: &str,
}

impl Wrapper {
    pub fn new(prefix: &str, value: &str) -> Self {
        Self {
            prefix: prefix.to_string(),
            value,
        }
    }

    pub fn display(&self) -> String {
        format!("{}: {}", self.prefix, self.value)
    }
}

fn main() {}
"#;

const BUGFIX_LIFETIME_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_longest_first() {
        let s1 = String::from("hello world");
        let s2 = String::from("hi");
        let result = longest(&s1, &s2);
        assert_eq!(result, "hello world");
    }

    #[test]
    fn test_longest_second() {
        let result = longest("hi", "hello world");
        assert_eq!(result, "hello world");
    }

    #[test]
    fn test_wrapper_display() {
        let w = Wrapper::new("Label", "value");
        assert_eq!(w.display(), "Label: value");
    }
}
"#;

/// Long function to refactor (Tier 3 setup).
const REFACTOR_EXTRACT_SETUP: &str = r#"use std::collections::HashMap;

/// Processes user data: validates, transforms, and aggregates.
pub fn process_data(entries: &[(String, i32)]) -> Result<HashMap<String, i32>, String> {
    // --- Validation logic (should be extracted into validate()) ---
    if entries.is_empty() {
        return Err("entries must not be empty".to_string());
    }
    for (name, value) in entries {
        if name.trim().is_empty() {
            return Err("name must not be empty".to_string());
        }
        if *value < 0 {
            return Err(format!("negative value for {}: {}", name, value));
        }
        if *value > 10000 {
            return Err(format!("value too large for {}: {}", name, value));
        }
    }
    // --- End validation logic ---

    let mut result = HashMap::new();
    for (name, value) in entries {
        let key = name.trim().to_lowercase();
        *result.entry(key).or_insert(0) += value;
    }
    Ok(result)
}

fn main() {}
"#;

const REFACTOR_EXTRACT_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_validate_exists() {
        // The validate function must exist and be callable
        let entries = vec![("Alice".to_string(), 10)];
        assert!(validate(&entries).is_ok());
    }

    #[test]
    fn test_validate_empty() {
        let entries: Vec<(String, i32)> = vec![];
        assert!(validate(&entries).is_err());
    }

    #[test]
    fn test_validate_negative() {
        let entries = vec![("Bob".to_string(), -5)];
        assert!(validate(&entries).is_err());
    }

    #[test]
    fn test_process_data_aggregates() {
        let entries = vec![
            ("Alice".to_string(), 10),
            ("alice".to_string(), 20),
        ];
        let result = process_data(&entries).unwrap();
        assert_eq!(result.get("alice"), Some(&30));
    }

    #[test]
    fn test_process_data_calls_validate() {
        // Empty input should still fail
        let entries: Vec<(String, i32)> = vec![];
        assert!(process_data(&entries).is_err());
    }
}
"#;

/// Returns Tier 3 agentic editing tasks. These require a temp directory path.
pub fn agentic_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let off_by_one_path = temp_dir.join("binary_search.rs");
    let lifetime_path = temp_dir.join("lifetime_fix.rs");
    let refactor_path = temp_dir.join("refactor.rs");

    vec![
        BenchmarkTask {
            name: "bugfix_off_by_one",
            prompt: format!(
                "The file {} contains a binary_search function with a bug (off-by-one error). \
                 Read the file, find and fix the bug using the edit tool. \
                 The function should correctly search a sorted slice and return the index if found.",
                off_by_one_path.display()
            ),
            expected_patterns: vec![
                r"(?i)(fix|bug|off.by.one|bound|len\s*-\s*1)",
            ],
            category: TaskCategory::Agentic,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: BUGFIX_OFF_BY_ONE_TESTS,
                setup_code: Some(BUGFIX_OFF_BY_ONE_SETUP),
                test_count: 6,
            }),
        },
        BenchmarkTask {
            name: "bugfix_lifetime",
            prompt: format!(
                "The file {} contains Rust code that fails to compile due to missing lifetime annotations. \
                 Read the file and fix it so it compiles correctly. Use the edit tool to make changes. \
                 The `longest` function needs lifetime annotations, and the `Wrapper` struct needs a \
                 lifetime parameter on its `value` field.",
                lifetime_path.display()
            ),
            expected_patterns: vec![
                r"(?i)(lifetime|'[a-z])",
            ],
            category: TaskCategory::Agentic,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: BUGFIX_LIFETIME_TESTS,
                setup_code: Some(BUGFIX_LIFETIME_SETUP),
                test_count: 3,
            }),
        },
        BenchmarkTask {
            name: "refactor_extract",
            prompt: format!(
                "Read {} and refactor the `process_data` function by extracting the validation logic \
                 (the section marked with comments) into a separate `pub fn validate(entries: &[(String, i32)]) -> Result<(), String>` function. \
                 The `process_data` function should call `validate()` at the start. Use the edit tool.",
                refactor_path.display()
            ),
            expected_patterns: vec![
                r"fn\s+validate",
            ],
            category: TaskCategory::Agentic,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: REFACTOR_EXTRACT_TESTS,
                setup_code: Some(REFACTOR_EXTRACT_SETUP),
                test_count: 5,
            }),
        },
    ]
}

// ---------------------------------------------------------------------------
// Agent quality task setup code
// ---------------------------------------------------------------------------

/// Multi-step debug: Rust file with 3 functions, one has a bug.
const MULTI_STEP_DEBUG_LIB: &str = r#"/// Calculates the area of a rectangle.
pub fn area(width: f64, height: f64) -> f64 {
    width * height
}

/// Calculates the perimeter of a rectangle.
pub fn perimeter(width: f64, height: f64) -> f64 {
    2.0 * width + height  // BUG: should be 2.0 * (width + height)
}

/// Calculates the diagonal of a rectangle.
pub fn diagonal(width: f64, height: f64) -> f64 {
    (width * width + height * height).sqrt()
}
"#;

/// Compile-time test for multi_step_debug: just compile lib.rs with the test harness appended.
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

/// Constraint edit: validators with stubs.
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
        // validate_phone should still be a stub returning true
        assert!(validate_phone("anything"));
    }

    #[test]
    fn test_url_unchanged() {
        // validate_url should still be a stub returning true
        assert!(validate_url("anything"));
    }
}
"#;

/// Self-correct compile: missing import.
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
    // TODO: add default timeout field
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

/// Validation test for tool_efficiency: compile config.rs standalone with the field check.
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

/// No overengineer: simple add function.
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

/// Error recovery loop: uses a nonexistent crate.
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

/// Returns agent-quality benchmark tasks that test multi-step reasoning,
/// self-correction, and constraint following. Requires a temp directory path.
pub fn agent_quality_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    // Create subdirectories for tasks that need project structure
    let debug_dir = temp_dir.join("multi_step_debug");
    let constraint_path = temp_dir.join("validators.rs");
    let self_correct_path = temp_dir.join("cache.rs");
    let efficiency_dir = temp_dir.join("tool_efficiency").join("src");
    let no_overengineer_path = temp_dir.join("math.rs");
    let error_recovery_path = temp_dir.join("broken.rs");

    vec![
        // 1. multi_step_debug (MultiStep)
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
            }),
        },
        // 2. constraint_edit (ConstraintFollowing)
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
            }),
        },
        // 3. self_correct_compile (SelfCorrection)
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
            }),
        },
        // 4. tool_efficiency (MultiStep)
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
            }),
        },
        // 5. no_overengineer (ConstraintFollowing)
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
            }),
        },
        // 6. error_recovery_loop (SelfCorrection)
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
            }),
        },
    ]
}
