use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

const TEST_HEAVY_CSV_SETUP: &str = r#"pub fn parse_record(line: &str) -> Vec<String> {
    line.split(',').map(|field| field.to_string()).collect()
}

pub fn parse_rows(input: &str) -> Vec<Vec<String>> {
    input
        .lines()
        .filter(|line| !line.is_empty())
        .map(parse_record)
        .collect()
}
"#;

pub fn test_heavy_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let task_dir = temp_dir.join("test_heavy_csv_regressions");

    vec![BenchmarkTask {
        name: "test_heavy_csv_regressions",
        prompt: format!(
            "In {dir} there is a CSV parser and a sparse test suite. This benchmark is \
             test-heavy: add focused regression tests for whitespace handling and blank-line \
             behavior (add at least two regression tests), then fix the implementation so all tests pass. Keep the tests meaningful \
             and deterministic.",
            dir = task_dir.display()
        ),
        expected_patterns: vec![r"#\[test\]", r"trim", r"blank|empty"],
        category: TaskCategory::TestHeavy,
        needs_tools: true,
        test_harness: Some(TestHarness {
            test_code: r#"
mod parser;

use parser::{parse_record, parse_rows};

#[test]
fn parses_basic_row() {
    assert_eq!(parse_record("a,b"), vec!["a".to_string(), "b".to_string()]);
}

#[test]
fn parses_multiple_rows() {
    let rows = parse_rows("x,y\n1,2\n");
    assert_eq!(rows.len(), 2);
}
"#,
            setup_code: Some(TEST_HEAVY_CSV_SETUP),
            test_count: 4,
            language: Language::Rust,
        }),
        expected_min_tools: Some(5),
    }]
}
