use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

/// Buggy binary search code with an off-by-one error (Tier 3 setup).
const BUGFIX_OFF_BY_ONE_SETUP: &str = r#"use std::cmp::Ordering;

/// Binary search for `target` in a sorted slice. Returns the index if found.
pub fn binary_search(arr: &[i32], target: i32) -> Option<usize> {
    if arr.is_empty() {
        return None;
    }
    let mut low: usize = 0;
    let mut high: usize = arr.len();

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
        let entries = vec![("Alice".to_string(), 10), ("alice".to_string(), 20)];
        let result = process_data(&entries).unwrap();
        assert_eq!(result.get("alice"), Some(&30));
    }

    #[test]
    fn test_process_data_calls_validate() {
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
            expected_patterns: vec![r"(?i)(fix|bug|off.by.one|bound|len\s*-\s*1)"],
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
            expected_patterns: vec![r"(?i)(lifetime|'[a-z])"],
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
                 into a separate `pub fn validate(entries: &[(String, i32)]) -> Result<(), String>` function. \
                 The `process_data` function should call `validate()` at the start. Use the edit tool.",
                refactor_path.display()
            ),
            expected_patterns: vec![r"fn\s+validate"],
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
