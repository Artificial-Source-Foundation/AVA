use std::path::Path;

use regex::Regex;

use crate::benchmark_tasks::TestHarness;

pub(crate) async fn run_tier3_validation(
    temp_dir: &Path,
    task_name: &str,
    harness: &TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    if task_name == "rule_guided_typescript" {
        return validate_rule_guided_typescript(temp_dir).await;
    }

    if task_name == "delegated_config_bugfix" {
        return validate_delegated_config_bugfix(temp_dir, harness.test_count).await;
    }

    if [
        "tool_reliability_timeout",
        "tool_reliability_log_filter",
        "tool_reliability_normalize",
    ]
    .contains(&task_name)
    {
        return validate_standalone_rust_test_dir(temp_dir, task_name, harness.test_count).await;
    }

    let filename = match task_name {
        "bugfix_off_by_one" => "binary_search.rs",
        "bugfix_lifetime" => "lifetime_fix.rs",
        "refactor_extract" => "refactor.rs",
        "multi_step_debug" => "multi_step_debug/lib.rs",
        "constraint_edit" => "validators.rs",
        "self_correct_compile" => "cache.rs",
        "tool_efficiency" => "tool_efficiency/src/config.rs",
        "no_overengineer" => "math.rs",
        "error_recovery_loop" => "broken.rs",
        _ => return (None, None, None, None),
    };

    let file_path = temp_dir.join(filename);
    let file_content = match tokio::fs::read_to_string(&file_path).await {
        Ok(content) => content,
        Err(e) => {
            return (
                Some(false),
                None,
                None,
                Some(format!("Failed to read edited file: {}", e)),
            );
        }
    };

    let full_source = format!("{}\n{}", file_content, harness.test_code);
    compile_and_test(&full_source, harness.test_count).await
}

pub(crate) async fn compile_and_test(
    source: &str,
    expected_test_count: usize,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let temp_dir = match tempfile::tempdir() {
        Ok(d) => d,
        Err(e) => {
            return (
                Some(false),
                None,
                None,
                Some(format!("Failed to create temp dir: {}", e)),
            );
        }
    };

    let source_path = temp_dir.path().join("bench_test.rs");
    let test_binary = temp_dir.path().join("bench_test");

    if let Err(e) = tokio::fs::write(&source_path, source).await {
        return (
            Some(false),
            None,
            None,
            Some(format!("Failed to write source: {}", e)),
        );
    }

    compile_and_test_rust_entry(&source_path, &test_binary, expected_test_count).await
}

pub(crate) fn parse_test_output(output: &str) -> (usize, usize) {
    let re = Regex::new(r"test result:.*?(\d+) passed.*?(\d+) failed").ok();
    if let Some(re) = re {
        if let Some(cap) = re.captures(output) {
            let passed = cap[1].parse().unwrap_or(0);
            let failed = cap[2].parse().unwrap_or(0);
            return (passed, failed);
        }
    }
    (0, 0)
}

async fn validate_rule_guided_typescript(
    temp_dir: &Path,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let file_path = temp_dir.join("frontend").join("app.ts");
    let file_content = match tokio::fs::read_to_string(&file_path).await {
        Ok(content) => content,
        Err(e) => {
            return (
                Some(false),
                None,
                None,
                Some(format!("Failed to read edited TypeScript file: {}", e)),
            );
        }
    };

    let has_strict_admin_check = Regex::new(r#"===\s*[\"']admin[\"']|[\"']admin[\"']\s*==="#)
        .map(|re| re.is_match(&file_content))
        .unwrap_or(false);
    let has_semicolon = file_content.contains(';');
    let passed = has_strict_admin_check && !has_semicolon;
    let error = if passed {
        None
    } else if !has_strict_admin_check {
        Some("expected strict equality against the admin role".to_string())
    } else {
        Some("expected the local TypeScript rule to avoid semicolons".to_string())
    };

    (Some(passed), Some(usize::from(passed)), Some(1), error)
}

async fn validate_delegated_config_bugfix(
    temp_dir: &Path,
    expected_test_count: usize,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let tests_path = temp_dir.join("delegated_config_bugfix").join("tests.rs");
    let temp_dir = match tempfile::tempdir() {
        Ok(d) => d,
        Err(e) => {
            return (
                Some(false),
                None,
                None,
                Some(format!("Failed to create temp dir: {}", e)),
            );
        }
    };
    let test_binary = temp_dir.path().join("bench_test");
    compile_and_test_rust_entry(&tests_path, &test_binary, expected_test_count).await
}

async fn validate_standalone_rust_test_dir(
    temp_dir: &Path,
    task_name: &str,
    expected_test_count: usize,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let dir = temp_dir.join(match task_name {
        "tool_reliability_timeout" => "tool_reliability_timeout",
        "tool_reliability_log_filter" => "tool_reliability_log_filter",
        "tool_reliability_normalize" => "tool_reliability_normalize",
        _ => unreachable!("validated by caller"),
    });
    let tests_path = dir.join("tests.rs");
    let temp_dir = match tempfile::tempdir() {
        Ok(d) => d,
        Err(e) => {
            return (
                Some(false),
                None,
                None,
                Some(format!("Failed to create temp dir: {}", e)),
            );
        }
    };
    let test_binary = temp_dir.path().join("bench_test");
    compile_and_test_rust_entry(&tests_path, &test_binary, expected_test_count).await
}

async fn compile_and_test_rust_entry(
    source_path: &Path,
    test_binary: &Path,
    expected_test_count: usize,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let compile_output = tokio::process::Command::new("rustc")
        .args([
            "--edition",
            "2021",
            "--test",
            source_path.to_str().unwrap_or("tests.rs"),
            "-o",
            test_binary.to_str().unwrap_or("bench_test"),
        ])
        .output()
        .await;

    let compile_result = match compile_output {
        Ok(output) => output,
        Err(e) => {
            return (
                Some(false),
                None,
                None,
                Some(format!("Failed to run rustc: {}", e)),
            );
        }
    };

    if !compile_result.status.success() {
        let stderr = String::from_utf8_lossy(&compile_result.stderr);
        let error_msg = if stderr.len() > 500 {
            format!("{}...", &stderr[..500])
        } else {
            stderr.to_string()
        };
        return (
            Some(false),
            Some(0),
            Some(expected_test_count),
            Some(error_msg),
        );
    }

    let test_output = tokio::process::Command::new(test_binary.to_str().unwrap_or("./bench_test"))
        .output()
        .await;

    let test_result = match test_output {
        Ok(output) => output,
        Err(e) => {
            return (
                Some(true),
                Some(0),
                Some(expected_test_count),
                Some(format!("Failed to run tests: {}", e)),
            );
        }
    };

    let stdout = String::from_utf8_lossy(&test_result.stdout);
    let (passed, failed) = parse_test_output(&stdout);
    let total = if passed + failed == 0 {
        expected_test_count
    } else {
        passed + failed
    };

    if test_result.status.success() {
        (
            Some(true),
            Some(if passed > 0 { passed } else { total }),
            Some(total),
            None,
        )
    } else {
        let stderr = String::from_utf8_lossy(&test_result.stderr);
        let error_msg = if stderr.is_empty() {
            stdout.to_string()
        } else if stderr.len() > 500 {
            format!("{}...", &stderr[..500])
        } else {
            stderr.to_string()
        };
        (Some(true), Some(passed), Some(total), Some(error_msg))
    }
}
