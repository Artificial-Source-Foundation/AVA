use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

const SECURITY_SQL_INJECTION_SETUP: &str = r#"use std::collections::HashMap;

pub fn find_user(db: &HashMap<String, String>, query: &str) -> Option<String> {
    let sql = format!("SELECT * FROM users WHERE name = '{}'", query);
    db.get(query).cloned()
}

pub fn delete_user(db: &mut HashMap<String, String>, name: &str) -> bool {
    let sql = format!("DELETE FROM users WHERE name = '{}'", name);
    db.remove(name).is_some()
}

pub fn insert_user(db: &mut HashMap<String, String>, name: &str, email: &str) -> bool {
    if name.contains('\'') || name.contains(';') || name.contains('-') {
        return false;
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

const SECURITY_PATH_TRAVERSAL_SETUP: &str = r#"use std::path::{Path, PathBuf};

pub fn resolve_file(base_dir: &str, requested_path: &str) -> PathBuf {
    Path::new(base_dir).join(requested_path)
}

pub fn is_safe_filename(name: &str) -> bool {
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
        let base = Path::new("/var/www").canonicalize().unwrap_or_else(|_| PathBuf::from("/var/www"));
        assert!(!result.to_str().unwrap().contains("/etc/passwd") || result.starts_with(&base));
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

const SECURITY_INTEGER_OVERFLOW_SETUP: &str = r#"pub fn safe_multiply(a: u32, b: u32) -> Option<u32> {
    Some(a * b)
}

pub fn safe_add(a: u32, b: u32) -> u32 {
    a + b
}

pub fn allocate_buffer(count: usize, element_size: usize) -> Vec<u8> {
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
        let _ = safe_add(u32::MAX, 1);
    }

    #[test]
    fn test_allocate_buffer_handles_overflow() {
        let result = std::panic::catch_unwind(|| allocate_buffer(usize::MAX, 2));
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
