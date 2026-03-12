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
    pub fn parse_str(s: &str) -> Option<Self> {
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

/// Programming language for a benchmark task.
#[derive(Debug, Clone, Copy, PartialEq, serde::Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Language {
    Rust,
    Python,
    JavaScript,
    Go,
}

impl Language {
    /// Parse from a string (case-insensitive).
    pub fn parse_str(s: &str) -> Option<Self> {
        match s.to_lowercase().as_str() {
            "rust" | "rs" => Some(Self::Rust),
            "python" | "py" => Some(Self::Python),
            "javascript" | "js" | "typescript" | "ts" => Some(Self::JavaScript),
            "go" | "golang" => Some(Self::Go),
            _ => None,
        }
    }
}

impl std::fmt::Display for Language {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Rust => write!(f, "rust"),
            Self::Python => write!(f, "python"),
            Self::JavaScript => write!(f, "javascript"),
            Self::Go => write!(f, "go"),
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
    /// Multi-language tasks (Python, TypeScript/JS, Go).
    MultiLang,
    /// Security vulnerability detection and fixing.
    Security,
    /// Generating tests for given code.
    TestGeneration,
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
            Self::MultiLang => write!(f, "multi_lang"),
            Self::Security => write!(f, "security"),
            Self::TestGeneration => write!(f, "test_generation"),
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
            Self::Agentic | Self::ConstraintFollowing | Self::SelfCorrection | Self::MultiLang
            | Self::Security => BenchmarkSuite::Standard,
            Self::MultiStep => BenchmarkSuite::Frontier,
            Self::TestGeneration => BenchmarkSuite::Speed,
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
    /// Programming language for this harness. Defaults to Rust.
    pub language: Language,
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
    /// Minimum expected number of tool calls for efficiency scoring.
    /// Only meaningful for tool-using tasks. Used to compute `tool_efficiency_score`.
    pub expected_min_tools: Option<u32>,
}

impl BenchmarkTask {
    /// Returns the programming language for this task (from test harness, defaults to Rust).
    pub fn language(&self) -> Language {
        self.test_harness.as_ref().map_or(Language::Rust, |h| h.language)
    }
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
                language: Language::Rust,
            }),
            expected_min_tools: None,
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
                language: Language::Rust,
            }),
            expected_min_tools: None,
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
                language: Language::Rust,
            }),
            expected_min_tools: None,
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
            expected_min_tools: Some(1),
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
            expected_min_tools: Some(2),
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
                language: Language::Rust,
            }),
            expected_min_tools: Some(3),
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
                language: Language::Rust,
            }),
            expected_min_tools: Some(3),
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
                language: Language::Rust,
            }),
            expected_min_tools: Some(3),
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
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
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
                language: Language::Rust,
            }),
            expected_min_tools: Some(2),
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
                language: Language::Rust,
            }),
            expected_min_tools: Some(3),
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
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
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
                language: Language::Rust,
            }),
            expected_min_tools: Some(2),
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
                language: Language::Rust,
            }),
            expected_min_tools: Some(3),
        },
    ]
}

// ---------------------------------------------------------------------------
// Multi-language benchmark tasks
// ---------------------------------------------------------------------------

/// Returns Python benchmark tasks.
pub fn python_tasks() -> Vec<BenchmarkTask> {
    vec![
        BenchmarkTask {
            name: "py_two_sum",
            prompt: "Write a Python function `two_sum(nums: list[int], target: int) -> list[int]` \
                     that returns indices of two numbers that add up to target. \
                     Only output the function code."
                .to_string(),
            expected_patterns: vec![r"def two_sum", r"(?i)(dict|hash|map|\{\})"],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
import unittest
class TestTwoSum(unittest.TestCase):
    def test_basic(self):
        self.assertEqual(sorted(two_sum([2,7,11,15], 9)), [0, 1])
    def test_negative(self):
        self.assertEqual(sorted(two_sum([-1,-2,-3,-4,-5], -8)), [2, 4])
    def test_duplicate(self):
        self.assertEqual(sorted(two_sum([3,3], 6)), [0, 1])
if __name__ == '__main__':
    unittest.main()
"#,
                setup_code: None,
                test_count: 3,
                language: Language::Python,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "py_flatten_nested",
            prompt: "Write a Python function `flatten(nested: list) -> list` that recursively \
                     flattens arbitrarily nested lists. For example, \
                     flatten([1, [2, [3, 4], 5], 6]) should return [1, 2, 3, 4, 5, 6]. \
                     Only output the function code."
                .to_string(),
            expected_patterns: vec![r"def flatten", r"(?i)(isinstance|type.*list|recursive)"],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
import unittest
class TestFlatten(unittest.TestCase):
    def test_basic(self):
        self.assertEqual(flatten([1, [2, [3, 4], 5], 6]), [1, 2, 3, 4, 5, 6])
    def test_empty(self):
        self.assertEqual(flatten([]), [])
    def test_deep(self):
        self.assertEqual(flatten([[[1]], [[2]], [[3]]]), [1, 2, 3])
    def test_mixed(self):
        self.assertEqual(flatten([1, 2, 3]), [1, 2, 3])
if __name__ == '__main__':
    unittest.main()
"#,
                setup_code: None,
                test_count: 4,
                language: Language::Python,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "py_async_rate_limiter",
            prompt: "Write a Python class `RateLimiter` that limits function calls to N calls \
                     per second using asyncio. It should have methods \
                     `__init__(self, max_calls: int)` and `async def acquire(self)` that blocks \
                     until a slot is available. Only output the code."
                .to_string(),
            expected_patterns: vec![r"class RateLimiter", r"async", r"(?i)(asyncio|await)"],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
import asyncio, time, unittest
class TestRateLimiter(unittest.TestCase):
    def test_basic(self):
        async def run():
            rl = RateLimiter(5)
            start = time.monotonic()
            for _ in range(5):
                await rl.acquire()
            elapsed = time.monotonic() - start
            self.assertLess(elapsed, 0.5)
        asyncio.run(run())
    def test_rate_limit(self):
        async def run():
            rl = RateLimiter(2)
            start = time.monotonic()
            for _ in range(4):
                await rl.acquire()
            elapsed = time.monotonic() - start
            self.assertGreater(elapsed, 0.9)
        asyncio.run(run())
if __name__ == '__main__':
    unittest.main()
"#,
                setup_code: None,
                test_count: 2,
                language: Language::Python,
            }),
            expected_min_tools: None,
        },
    ]
}

/// Returns TypeScript/JavaScript benchmark tasks.
///
/// Uses JavaScript (`.js` + `node`) for simpler execution without needing tsc.
pub fn typescript_tasks() -> Vec<BenchmarkTask> {
    vec![
        BenchmarkTask {
            name: "js_debounce",
            prompt: "Write a JavaScript function `debounce(fn, ms)` that returns a debounced \
                     version of fn that delays invocation until ms milliseconds have passed \
                     since the last call. Only output the function code."
                .to_string(),
            expected_patterns: vec![
                r"(?i)(function debounce|const debounce)",
                r"(?i)(setTimeout|clearTimeout)",
            ],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
const assert = require('assert');
async function test() {
    let count = 0;
    const fn = debounce(() => count++, 50);
    fn(); fn(); fn();
    await new Promise(r => setTimeout(r, 100));
    assert.strictEqual(count, 1, 'Should only call once');

    fn();
    await new Promise(r => setTimeout(r, 100));
    assert.strictEqual(count, 2, 'Should call again after delay');
    console.log('All tests passed');
}
test().catch(e => { console.error(e); process.exit(1); });
"#,
                setup_code: None,
                test_count: 2,
                language: Language::JavaScript,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "js_deep_clone",
            prompt: "Write a JavaScript function `deepClone(obj)` that creates a deep copy of \
                     an object, handling nested objects, arrays, Date, RegExp, Map, and Set. \
                     Do not use JSON.parse/JSON.stringify. Only output the function code."
                .to_string(),
            expected_patterns: vec![
                r"(?i)(function deepClone|const deepClone)",
                r"(?i)(typeof|instanceof)",
                r"(?i)(Array|Object)",
            ],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
const assert = require('assert');
const original = { a: 1, b: { c: [1,2,3], d: new Date('2024-01-01') }, e: /test/gi };
const cloned = deepClone(original);
assert.deepStrictEqual(cloned.b.c, [1,2,3]);
assert.notStrictEqual(cloned.b.c, original.b.c);
assert.notStrictEqual(cloned.b, original.b);
cloned.b.c.push(4);
assert.strictEqual(original.b.c.length, 3, 'Original should be unchanged');
assert.ok(cloned.b.d instanceof Date);
assert.ok(cloned.e instanceof RegExp);
console.log('All tests passed');
"#,
                setup_code: None,
                test_count: 6,
                language: Language::JavaScript,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "js_react_component",
            prompt: "Write a React functional component called `SearchFilter` that takes props \
                     `items: string[]` and `onSelect: (item: string) => void`. It should render \
                     an input field for filtering and a list of matching items. Clicking an item \
                     calls onSelect. Use useState for the filter state. Only output the component \
                     code (assume React is imported)."
                .to_string(),
            expected_patterns: vec![
                r"(?i)(function SearchFilter|const SearchFilter)",
                r"useState",
                r"(?i)(onChange|filter)",
                r"(?i)(onClick|onSelect)",
            ],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            // No test harness — React needs a build setup, so regex validation only.
            test_harness: None,
            expected_min_tools: None,
        },
    ]
}

// ---------------------------------------------------------------------------
// Security benchmark tasks
// ---------------------------------------------------------------------------

/// Setup code for SQL injection vulnerability task.
const SECURITY_SQL_INJECTION_SETUP: &str = r#"use std::collections::HashMap;

pub fn find_user(db: &HashMap<String, String>, query: &str) -> Option<String> {
    // BUG: SQL injection - using string format instead of parameterized query
    let sql = format!("SELECT * FROM users WHERE name = '{}'", query);
    // Simulate: just do a direct lookup for now
    db.get(query).cloned()
}

pub fn delete_user(db: &mut HashMap<String, String>, name: &str) -> bool {
    let sql = format!("DELETE FROM users WHERE name = '{}'", name);
    db.remove(name).is_some()
}

pub fn insert_user(db: &mut HashMap<String, String>, name: &str, email: &str) -> bool {
    if name.contains('\'') || name.contains(';') || name.contains('-') {
        return false; // broken sanitization
    }
    db.insert(name.to_string(), email.to_string());
    true
}

fn main() {}
"#;

const SECURITY_SQL_INJECTION_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_insert_rejects_injection() {
        let mut db = HashMap::new();
        assert!(!insert_user(&mut db, "'; DROP TABLE--", "evil@example.com"));
    }

    #[test]
    fn test_find_user_works() {
        let mut db = HashMap::new();
        db.insert("alice".to_string(), "alice@example.com".to_string());
        assert_eq!(find_user(&db, "alice"), Some("alice@example.com".to_string()));
    }

    #[test]
    fn test_insert_allows_hyphenated_names() {
        let mut db = HashMap::new();
        assert!(insert_user(&mut db, "Mary-Jane", "mj@example.com"));
    }
}
"#;

/// Setup code for path traversal vulnerability task.
const SECURITY_PATH_TRAVERSAL_SETUP: &str = r#"use std::path::{Path, PathBuf};

pub fn resolve_file(base_dir: &str, requested_path: &str) -> PathBuf {
    // BUG: path traversal - no validation of ".." components
    Path::new(base_dir).join(requested_path)
}

pub fn is_safe_filename(name: &str) -> bool {
    // BUG: incomplete check
    !name.contains('/')
}

fn main() {}
"#;

const SECURITY_PATH_TRAVERSAL_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_normal_file_resolves() {
        let result = resolve_file("/var/www", "index.html");
        assert!(result.to_str().unwrap().contains("index.html"));
    }

    #[test]
    fn test_path_traversal_blocked() {
        let result = resolve_file("/var/www", "../../../etc/passwd");
        // The resolved path must start with base_dir (no escape)
        let base = Path::new("/var/www").canonicalize().unwrap_or_else(|_| PathBuf::from("/var/www"));
        // At minimum, the result should NOT contain /etc/passwd literally
        assert!(!result.to_str().unwrap().contains("/etc/passwd")
            || result.starts_with(&base));
    }

    #[test]
    fn test_is_safe_filename_rejects_dotdot() {
        assert!(!is_safe_filename(".."));
        assert!(!is_safe_filename("../etc"));
    }

    #[test]
    fn test_null_byte_rejected() {
        assert!(!is_safe_filename("file\0.txt"));
    }
}
"#;

/// Setup code for integer overflow vulnerability task.
const SECURITY_INTEGER_OVERFLOW_SETUP: &str = r#"pub fn safe_multiply(a: u32, b: u32) -> Option<u32> {
    // BUG: can overflow in debug, wrap in release
    Some(a * b)
}

pub fn safe_add(a: u32, b: u32) -> u32 {
    // BUG: no overflow check
    a + b
}

pub fn allocate_buffer(count: usize, element_size: usize) -> Vec<u8> {
    // BUG: count * element_size can overflow
    let total = count * element_size;
    vec![0u8; total]
}

fn main() {}
"#;

const SECURITY_INTEGER_OVERFLOW_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_safe_multiply_overflow_returns_none() {
        assert_eq!(safe_multiply(u32::MAX, 2), None);
    }

    #[test]
    fn test_safe_add_no_panic_on_overflow() {
        // Should not panic — either return a wrapped value or use checked arithmetic
        let _ = safe_add(u32::MAX, 1);
    }

    #[test]
    fn test_allocate_buffer_handles_overflow() {
        // This should not panic or allocate a tiny buffer
        let result = std::panic::catch_unwind(|| {
            allocate_buffer(usize::MAX, 2)
        });
        // Either it panics gracefully or returns a reasonable result
        // The key is it must not silently allocate a wrong-sized buffer
        assert!(result.is_err() || result.unwrap().len() != 0);
    }
}
"#;

/// Returns Tier 3 agentic security tasks. These require a temp directory path.
pub fn security_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let sql_injection_path = temp_dir.join("vulnerable.rs");
    let path_traversal_path = temp_dir.join("fileserver.rs");
    let integer_overflow_path = temp_dir.join("calc.rs");

    vec![
        BenchmarkTask {
            name: "fix_sql_injection",
            prompt: format!(
                "The file {} has SQL injection vulnerabilities. Fix all functions to properly \
                 sanitize inputs. The `insert_user` function's sanitization is broken — it blocks \
                 legitimate names with hyphens. Fix it to only block actual injection patterns \
                 while allowing normal names like 'Mary-Jane'.",
                sql_injection_path.display()
            ),
            expected_patterns: vec![r"contains", r"sanitize|valid|check"],
            category: TaskCategory::Security,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: SECURITY_SQL_INJECTION_TESTS,
                setup_code: Some(SECURITY_SQL_INJECTION_SETUP),
                test_count: 3,
                language: Language::Rust,
            }),
            expected_min_tools: Some(3),
        },
        BenchmarkTask {
            name: "fix_path_traversal",
            prompt: format!(
                "Fix path traversal vulnerabilities in {}. The `resolve_file` function allows \
                 `../` to escape the base directory, and `is_safe_filename` doesn't check for \
                 `..` or null bytes.",
                path_traversal_path.display()
            ),
            expected_patterns: vec![r"canonicalize|starts_with|strip_prefix", r"\.\."],
            category: TaskCategory::Security,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: SECURITY_PATH_TRAVERSAL_TESTS,
                setup_code: Some(SECURITY_PATH_TRAVERSAL_SETUP),
                test_count: 4,
                language: Language::Rust,
            }),
            expected_min_tools: Some(3),
        },
        BenchmarkTask {
            name: "fix_integer_overflow",
            prompt: format!(
                "Fix integer overflow vulnerabilities in {}. Use checked arithmetic to prevent \
                 panics and undefined behavior.",
                integer_overflow_path.display()
            ),
            expected_patterns: vec![r"checked_mul|checked_add|overflowing", r"Option|None"],
            category: TaskCategory::Security,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: SECURITY_INTEGER_OVERFLOW_TESTS,
                setup_code: Some(SECURITY_INTEGER_OVERFLOW_SETUP),
                test_count: 3,
                language: Language::Rust,
            }),
            expected_min_tools: Some(3),
        },
    ]
}

// ---------------------------------------------------------------------------
// Test generation benchmark tasks
// ---------------------------------------------------------------------------

/// Returns Tier 2 test generation tasks where the model must write tests for given code.
pub fn test_generation_tasks() -> Vec<BenchmarkTask> {
    vec![
        BenchmarkTask {
            name: "generate_tests_stack",
            prompt: "Write comprehensive unit tests for this Rust `Stack<T>` implementation. \
                     Include tests for push, pop, peek, is_empty, and len. Test edge cases like \
                     popping from empty stack. Output ONLY the test module.\n\n\
                     ```rust\n\
                     pub struct Stack<T> { items: Vec<T> }\n\
                     impl<T> Stack<T> {\n    \
                         pub fn new() -> Self { Self { items: Vec::new() } }\n    \
                         pub fn push(&mut self, item: T) { self.items.push(item); }\n    \
                         pub fn pop(&mut self) -> Option<T> { self.items.pop() }\n    \
                         pub fn peek(&self) -> Option<&T> { self.items.last() }\n    \
                         pub fn is_empty(&self) -> bool { self.items.is_empty() }\n    \
                         pub fn len(&self) -> usize { self.items.len() }\n\
                     }\n\
                     ```"
                .to_string(),
            expected_patterns: vec![r"#\[test\]", r"assert"],
            category: TaskCategory::TestGeneration,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
pub struct Stack<T> { items: Vec<T> }
impl<T> Stack<T> {
    pub fn new() -> Self { Self { items: Vec::new() } }
    pub fn push(&mut self, item: T) { self.items.push(item); }
    pub fn pop(&mut self) -> Option<T> { self.items.pop() }
    pub fn peek(&self) -> Option<&T> { self.items.last() }
    pub fn is_empty(&self) -> bool { self.items.is_empty() }
    pub fn len(&self) -> usize { self.items.len() }
}
"#,
                setup_code: None,
                test_count: 5,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "generate_tests_parser",
            prompt: "Write comprehensive unit tests for these Rust CSV parsing functions. \
                     Include tests for empty input, single row, multiple rows, whitespace handling, \
                     and the `get_column` function with valid and out-of-bounds indices. \
                     Output ONLY the test module.\n\n\
                     ```rust\n\
                     pub fn parse_csv(input: &str) -> Vec<Vec<String>> {\n    \
                         input.lines()\n        \
                             .filter(|line| !line.trim().is_empty())\n        \
                             .map(|line| {\n            \
                                 line.split(',')\n                \
                                     .map(|field| field.trim().to_string())\n                \
                                     .collect()\n        \
                             })\n        \
                             .collect()\n\
                     }\n\n\
                     pub fn get_column(rows: &[Vec<String>], col: usize) -> Vec<&str> {\n    \
                         rows.iter()\n        \
                             .filter_map(|row| row.get(col).map(|s| s.as_str()))\n        \
                             .collect()\n\
                     }\n\
                     ```"
                .to_string(),
            expected_patterns: vec![r"#\[test\]", r"parse_csv", r"get_column"],
            category: TaskCategory::TestGeneration,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
pub fn parse_csv(input: &str) -> Vec<Vec<String>> {
    input.lines()
        .filter(|line| !line.trim().is_empty())
        .map(|line| {
            line.split(',')
                .map(|field| field.trim().to_string())
                .collect()
        })
        .collect()
}

pub fn get_column(rows: &[Vec<String>], col: usize) -> Vec<&str> {
    rows.iter()
        .filter_map(|row| row.get(col).map(|s| s.as_str()))
        .collect()
}
"#,
                setup_code: None,
                test_count: 4,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "generate_tests_result",
            prompt: "Write comprehensive unit tests for this Rust `Outcome<T, E>` type. \
                     Test `is_success`, `is_failure`, `unwrap` on success, `unwrap` panicking on \
                     failure, and `map` transforming the success value. \
                     Output ONLY the test module.\n\n\
                     ```rust\n\
                     #[derive(Debug, PartialEq)]\n\
                     pub enum Outcome<T, E> {\n    \
                         Success(T),\n    \
                         Failure(E),\n\
                     }\n\n\
                     impl<T, E> Outcome<T, E> {\n    \
                         pub fn is_success(&self) -> bool { matches!(self, Self::Success(_)) }\n    \
                         pub fn is_failure(&self) -> bool { matches!(self, Self::Failure(_)) }\n    \
                         pub fn unwrap(self) -> T where E: std::fmt::Debug {\n        \
                             match self { Self::Success(v) => v, Self::Failure(e) => panic!(\"called unwrap on Failure: {:?}\", e) }\n    \
                         }\n    \
                         pub fn map<U>(self, f: impl FnOnce(T) -> U) -> Outcome<U, E> {\n        \
                             match self { Self::Success(v) => Outcome::Success(f(v)), Self::Failure(e) => Outcome::Failure(e) }\n    \
                         }\n\
                     }\n\
                     ```"
                .to_string(),
            expected_patterns: vec![r"#\[test\]", r"Outcome", r"Success|Failure"],
            category: TaskCategory::TestGeneration,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[derive(Debug, PartialEq)]
pub enum Outcome<T, E> {
    Success(T),
    Failure(E),
}

impl<T, E> Outcome<T, E> {
    pub fn is_success(&self) -> bool { matches!(self, Self::Success(_)) }
    pub fn is_failure(&self) -> bool { matches!(self, Self::Failure(_)) }
    pub fn unwrap(self) -> T where E: std::fmt::Debug {
        match self { Self::Success(v) => v, Self::Failure(e) => panic!("called unwrap on Failure: {:?}", e) }
    }
    pub fn map<U>(self, f: impl FnOnce(T) -> U) -> Outcome<U, E> {
        match self { Self::Success(v) => Outcome::Success(f(v)), Self::Failure(e) => Outcome::Failure(e) }
    }
}
"#,
                setup_code: None,
                test_count: 4,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
    ]
}

// ---------------------------------------------------------------------------
// Advanced Rust benchmark tasks
// ---------------------------------------------------------------------------

/// Returns advanced Rust benchmark tasks (Tier 2, compile+test).
pub fn advanced_rust_tasks() -> Vec<BenchmarkTask> {
    vec![
        BenchmarkTask {
            name: "concurrent_counter",
            prompt: "Implement a thread-safe `Counter` struct in Rust with `increment()`, \
                     `decrement()`, and `get()` methods. It must be safe to use from multiple \
                     threads simultaneously. Include a function \
                     `parallel_increment(counter: &Counter, n: usize)` that spawns `n` threads, \
                     each incrementing the counter 1000 times, and waits for all to complete. \
                     Only output the code, no explanation needed."
                .to_string(),
            expected_patterns: vec![r"Arc|Mutex|AtomicUsize|Atomic", r"thread|spawn"],
            category: TaskCategory::Hard,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
use std::sync::Arc;
use std::thread;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_counter_starts_at_zero() {
        let counter = Counter::new();
        assert_eq!(counter.get(), 0);
    }

    #[test]
    fn test_increment_decrement() {
        let counter = Counter::new();
        counter.increment();
        counter.increment();
        counter.decrement();
        assert_eq!(counter.get(), 1);
    }

    #[test]
    fn test_parallel_increment() {
        let counter = Counter::new();
        parallel_increment(&counter, 4);
        assert_eq!(counter.get(), 4000);
    }
}
"#,
                setup_code: None,
                test_count: 3,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "iterator_adapter",
            prompt: "Implement a custom iterator adapter `Batched<I>` that yields `Vec<T>` batches \
                     of a given size from any iterator. Implement it as a method on a `BatchIterator` \
                     trait that extends `Iterator`. The last batch may be smaller than the batch size.\n\n\
                     Example:\n\
                     ```rust\n\
                     let v = vec![1,2,3,4,5];\n\
                     let batches: Vec<Vec<i32>> = v.into_iter().batched(2).collect();\n\
                     assert_eq!(batches, vec![vec![1,2], vec![3,4], vec![5]]);\n\
                     ```\n\n\
                     Only output the code, no explanation needed."
                .to_string(),
            expected_patterns: vec![r"Iterator|IntoIterator", r"impl|trait", r"Vec"],
            category: TaskCategory::Medium,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_empty_iterator() {
        let v: Vec<i32> = vec![];
        let batches: Vec<Vec<i32>> = v.into_iter().batched(2).collect();
        assert!(batches.is_empty());
    }

    #[test]
    fn test_exact_division() {
        let v = vec![1, 2, 3, 4];
        let batches: Vec<Vec<i32>> = v.into_iter().batched(2).collect();
        assert_eq!(batches, vec![vec![1, 2], vec![3, 4]]);
    }

    #[test]
    fn test_remainder() {
        let v = vec![1, 2, 3, 4, 5];
        let batches: Vec<Vec<i32>> = v.into_iter().batched(2).collect();
        assert_eq!(batches, vec![vec![1, 2], vec![3, 4], vec![5]]);
    }

    #[test]
    fn test_batch_size_one() {
        let v = vec![1, 2, 3];
        let batches: Vec<Vec<i32>> = v.into_iter().batched(1).collect();
        assert_eq!(batches, vec![vec![1], vec![2], vec![3]]);
    }
}
"#,
                setup_code: None,
                test_count: 4,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "binary_tree",
            prompt: "Implement a generic binary search tree `BST<T: Ord>` with \
                     `insert(&mut self, value: T)`, `contains(&self, value: &T) -> bool`, \
                     `min(&self) -> Option<&T>`, and `into_sorted_vec(self) -> Vec<T>` \
                     (in-order traversal). Use `Box<Node<T>>` for child pointers. \
                     Only output the code, no explanation needed."
                .to_string(),
            expected_patterns: vec![r"struct.*Node|BST", r"Box", r"Ord"],
            category: TaskCategory::Hard,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_insert_and_contains() {
        let mut bst = BST::new();
        bst.insert(5);
        bst.insert(3);
        bst.insert(7);
        assert!(bst.contains(&5));
        assert!(bst.contains(&3));
        assert!(bst.contains(&7));
        assert!(!bst.contains(&4));
    }

    #[test]
    fn test_min() {
        let mut bst = BST::new();
        assert_eq!(bst.min(), None);
        bst.insert(5);
        bst.insert(3);
        bst.insert(7);
        bst.insert(1);
        assert_eq!(bst.min(), Some(&1));
    }

    #[test]
    fn test_into_sorted_vec() {
        let mut bst = BST::new();
        bst.insert(5);
        bst.insert(3);
        bst.insert(7);
        bst.insert(1);
        bst.insert(9);
        assert_eq!(bst.into_sorted_vec(), vec![1, 3, 5, 7, 9]);
    }

    #[test]
    fn test_empty_tree() {
        let bst: BST<i32> = BST::new();
        assert!(!bst.contains(&1));
        assert_eq!(bst.into_sorted_vec(), Vec::<i32>::new());
    }
}
"#,
                setup_code: None,
                test_count: 4,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "state_machine",
            prompt: "Implement a simple state machine for a turnstile. States: `Locked` and \
                     `Unlocked`. Events: `Coin` and `Push`. Transitions: Locked+Coin->Unlocked, \
                     Unlocked+Push->Locked, Locked+Push->Locked (no change), \
                     Unlocked+Coin->Unlocked (no change). Implement `Turnstile::new()` \
                     (starts Locked), `process(&mut self, event: Event) -> &State`, and \
                     `state(&self) -> &State`. Only output the code, no explanation needed."
                .to_string(),
            expected_patterns: vec![r"enum.*State|Locked|Unlocked", r"enum.*Event|Coin|Push"],
            category: TaskCategory::Medium,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_initial_state() {
        let t = Turnstile::new();
        assert_eq!(*t.state(), State::Locked);
    }

    #[test]
    fn test_coin_unlocks() {
        let mut t = Turnstile::new();
        t.process(Event::Coin);
        assert_eq!(*t.state(), State::Unlocked);
    }

    #[test]
    fn test_push_locks() {
        let mut t = Turnstile::new();
        t.process(Event::Coin);
        t.process(Event::Push);
        assert_eq!(*t.state(), State::Locked);
    }

    #[test]
    fn test_sequence() {
        let mut t = Turnstile::new();
        // Push while locked -> stays locked
        t.process(Event::Push);
        assert_eq!(*t.state(), State::Locked);
        // Coin -> unlocked
        t.process(Event::Coin);
        assert_eq!(*t.state(), State::Unlocked);
        // Coin while unlocked -> stays unlocked
        t.process(Event::Coin);
        assert_eq!(*t.state(), State::Unlocked);
        // Push -> locked
        t.process(Event::Push);
        assert_eq!(*t.state(), State::Locked);
    }
}
"#,
                setup_code: None,
                test_count: 4,
                language: Language::Rust,
            }),
            expected_min_tools: None,
        },
    ]
}

// ---------------------------------------------------------------------------
// Multi-file benchmark tasks
// ---------------------------------------------------------------------------

/// Setup code for cross-file refactor: lib.rs
const MULTI_FILE_REFACTOR_LIB: &str = r#"mod utils;

fn compute(x: i32, y: i32) -> i32 {
    let sum = x + y;
    let product = x * y;
    sum + product
}

pub fn process(a: i32, b: i32) -> i32 {
    compute(a, b) * 2
}

pub fn format_output(val: i32) -> String {
    format!("Result: {}", val)
}
"#;

/// Test harness for cross-file refactor — validates against combined lib.rs + utils.rs.
const MULTI_FILE_REFACTOR_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_process_still_works() {
        assert_eq!(process(3, 4), 38); // (3+4+3*4)*2 = (7+12)*2 = 38
    }

    #[test]
    fn test_format_output() {
        assert_eq!(format_output(38), "Result: 38");
    }
}
"#;

/// Setup code for find-and-fix across files: client.rs (buggy)
const MULTI_FILE_CLIENT: &str = r#"// BUG: references Config::default() but uses wrong field names
pub struct Client {
    retries: u32,
    timeout: u64,
}

impl Client {
    pub fn new(config: &super::config::Config) -> Self {
        Self {
            retries: config.max_retry,      // BUG: field is max_retries
            timeout: config.timeout,         // BUG: field is timeout_ms
        }
    }

    pub fn max_retries(&self) -> u32 {
        self.retries
    }

    pub fn timeout_ms(&self) -> u64 {
        self.timeout
    }
}
"#;

/// Test harness for find-and-fix across files — validates the combined output.
const MULTI_FILE_CLIENT_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_client_max_retries() {
        let config = Config::default();
        let client = Client::new(&config);
        assert_eq!(client.max_retries(), 3);
    }

    #[test]
    fn test_client_timeout_ms() {
        let config = Config::default();
        let client = Client::new(&config);
        assert_eq!(client.timeout_ms(), 5000);
    }
}
"#;

/// Returns Tier 3 multi-file agentic tasks. These require a temp directory path.
pub fn multi_file_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let refactor_dir = temp_dir.join("cross_file_refactor");
    let fix_dir = temp_dir.join("find_fix_across_files");

    vec![
        BenchmarkTask {
            name: "cross_file_refactor",
            prompt: format!(
                "There are three files in {dir}: `main.rs`, `lib.rs`, and `utils.rs`. The \
                 `compute()` function in `lib.rs` should be extracted to `utils.rs` as a public \
                 function. Update `lib.rs` to import and call `utils::compute()` instead of \
                 having its own copy. Make sure everything still works.",
                dir = refactor_dir.display()
            ),
            expected_patterns: vec![r"pub fn compute", r"utils::compute"],
            category: TaskCategory::MultiStep,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: MULTI_FILE_REFACTOR_TESTS,
                setup_code: Some(MULTI_FILE_REFACTOR_LIB),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(5),
        },
        BenchmarkTask {
            name: "find_and_fix_across_files",
            prompt: format!(
                "There are two files in {dir}: `config.rs` and `client.rs`. The `Client::new()` \
                 function references wrong field names from `Config`. Find and fix the bugs so \
                 the code compiles. Don't change `Config`'s field names — fix the `Client` code.",
                dir = fix_dir.display()
            ),
            expected_patterns: vec![r"max_retries", r"timeout_ms"],
            category: TaskCategory::MultiStep,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: MULTI_FILE_CLIENT_TESTS,
                setup_code: Some(MULTI_FILE_CLIENT),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
        },
    ]
}

/// Returns Go benchmark tasks.
pub fn go_tasks() -> Vec<BenchmarkTask> {
    vec![
        BenchmarkTask {
            name: "go_reverse_linked_list",
            prompt: "Write a Go function `ReverseList(head *ListNode) *ListNode` that reverses \
                     a singly linked list in-place. Define the ListNode struct as well. \
                     Only output the code."
                .to_string(),
            expected_patterns: vec![r"func ReverseList", r"ListNode", r"Next"],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
func listToSlice(head *ListNode) []int {
    var result []int
    for head != nil {
        result = append(result, head.Val)
        head = head.Next
    }
    return result
}
func sliceToList(vals []int) *ListNode {
    dummy := &ListNode{}
    curr := dummy
    for _, v := range vals {
        curr.Next = &ListNode{Val: v}
        curr = curr.Next
    }
    return dummy.Next
}
func assertEqual(got, want []int) {
    if len(got) != len(want) {
        fmt.Printf("FAIL: got %v, want %v\n", got, want)
        os.Exit(1)
    }
    for i := range got {
        if got[i] != want[i] {
            fmt.Printf("FAIL: got %v, want %v\n", got, want)
            os.Exit(1)
        }
    }
}
func main() {
    l1 := sliceToList([]int{1, 2, 3, 4, 5})
    r1 := listToSlice(ReverseList(l1))
    assertEqual(r1, []int{5, 4, 3, 2, 1})

    r2 := listToSlice(ReverseList(nil))
    assertEqual(r2, []int{})

    l3 := sliceToList([]int{1})
    r3 := listToSlice(ReverseList(l3))
    assertEqual(r3, []int{1})

    fmt.Println("All tests passed")
}
"#,
                setup_code: None,
                test_count: 3,
                language: Language::Go,
            }),
            expected_min_tools: None,
        },
        BenchmarkTask {
            name: "go_concurrent_map",
            prompt: "Write a Go type `SafeMap` that is a goroutine-safe map[string]interface{} \
                     using sync.RWMutex. Implement methods Get(key) (value, ok), \
                     Set(key, value), Delete(key), and Len() int. Also write a \
                     `NewSafeMap() *SafeMap` constructor. Only output the code."
                .to_string(),
            expected_patterns: vec![
                r"(?i)(SafeMap|safeMap)",
                r"(?i)(RWMutex|sync\.Mutex)",
                r"(?i)(func.*Get|func.*Set)",
            ],
            category: TaskCategory::MultiLang,
            needs_tools: false,
            test_harness: Some(TestHarness {
                test_code: r#"
func assert(cond bool, msg string) {
    if !cond {
        fmt.Println("FAIL:", msg)
        os.Exit(1)
    }
}
func main() {
    m := NewSafeMap()
    m.Set("a", 1)
    m.Set("b", "hello")

    v, ok := m.Get("a")
    assert(ok, "key 'a' should exist")
    assert(v.(int) == 1, "value should be 1")
    assert(m.Len() == 2, "length should be 2")

    m.Delete("a")
    _, ok = m.Get("a")
    assert(!ok, "key 'a' should be deleted")
    assert(m.Len() == 1, "length should be 1")

    // Concurrent safety test
    var wg sync.WaitGroup
    for i := 0; i < 100; i++ {
        wg.Add(1)
        go func(i int) {
            defer wg.Done()
            m.Set(fmt.Sprintf("key%d", i), i)
        }(i)
    }
    wg.Wait()
    assert(m.Len() == 101, "length should be 101 after concurrent writes")

    fmt.Println("All tests passed")
}
"#,
                setup_code: None,
                test_count: 6,
                language: Language::Go,
            }),
            expected_min_tools: None,
        },
    ]
}
