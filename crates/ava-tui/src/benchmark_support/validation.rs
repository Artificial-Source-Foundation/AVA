use std::path::Path;

use regex::Regex;

use crate::benchmark_tasks::TestHarness;

pub(crate) async fn run_tier3_validation(
    temp_dir: &Path,
    task_name: &str,
    harness: &TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    if [
        "lsp_smoke_config_gate",
        "lsp_smoke_project_toolchain",
        "lsp_smoke_known_servers_snapshot",
    ]
    .contains(&task_name)
    {
        return validate_lsp_smoke_task(temp_dir, task_name).await;
    }

    if [
        "product_smoke_session_config_discovery",
        "product_smoke_permissions_flow",
        "product_smoke_tool_discovery",
    ]
    .contains(&task_name)
    {
        return validate_product_smoke_task(temp_dir, task_name).await;
    }

    if ["mcp_filesystem", "mcp_git", "mcp_multi_server"].contains(&task_name) {
        return validate_mcp_integration_task(temp_dir, task_name).await;
    }

    if task_name == "rule_guided_typescript" {
        return validate_rule_guided_typescript(temp_dir).await;
    }

    if task_name == "delegated_config_bugfix" {
        return validate_delegated_config_bugfix(temp_dir, harness.test_count).await;
    }

    if task_name == "test_heavy_csv_regressions" {
        return validate_test_heavy_csv_regressions(temp_dir, harness.test_count).await;
    }

    if task_name == "tool_recovery_verification_discipline" {
        return validate_tool_recovery_verification_discipline(temp_dir, harness.test_count).await;
    }

    if [
        "tool_reliability_timeout",
        "tool_reliability_log_filter",
        "tool_reliability_normalize",
        "tool_recovery_missing_file",
        "tool_recovery_targeted_edit",
        "prompt_regression_verify_before_finish",
        "prompt_regression_targeted_edit_only",
        "prompt_regression_minimal_patch",
        "prompt_regression_read_before_edit",
        "prompt_regression_wrong_first_edit_recovery",
        "prompt_regression_tool_choice_discipline",
        "stress_coding_log_pipeline",
        "large_project_feature_flags",
        "maintenance_config_migration",
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

async fn validate_lsp_smoke_task(
    temp_dir: &Path,
    task_name: &str,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    match task_name {
        "lsp_smoke_config_gate" => {
            let report = temp_dir
                .join("benchmark_lsp")
                .join("reports")
                .join("lsp_config_gate_report.json");
            let expected =
                r#"{"lsp_enabled":false,"sidebar_refresh":"disabled","reason":"feature_flag_off"}"#;
            validate_exact_file_contents(&report, expected).await
        }
        "lsp_smoke_project_toolchain" => {
            let report = temp_dir
                .join("benchmark_lsp")
                .join("reports")
                .join("lsp_project_toolchain.csv");
            let expected = "project,language,project_detected,toolchain_ready,recommended_server\n\
rust_service,rust,true,true,rust-analyzer\n\
ts_app,typescript,true,true,typescript-language-server\n\
python_worker,python,true,false,pyright-langserver\n";
            validate_exact_file_contents(&report, expected).await
        }
        "lsp_smoke_known_servers_snapshot" => {
            let report = temp_dir
                .join("benchmark_lsp")
                .join("reports")
                .join("lsp_known_servers_report.txt");
            let expected = "rust-analyzer\ntypescript\neslint\nbiome\npython\ngopls\nclangd\n";
            validate_exact_file_contents(&report, expected).await
        }
        _ => (None, None, None, None),
    }
}

async fn validate_product_smoke_task(
    temp_dir: &Path,
    task_name: &str,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    match task_name {
        "product_smoke_session_config_discovery" => {
            let report = temp_dir
                .join("benchmark_product")
                .join("reports")
                .join("session_resume_decision.json");
            let expected = r#"{"resume_session_id":"sess_001","provider":"openrouter","model":"anthropic/claude-haiku-4.5","reason":"most_recent_with_messages"}"#;
            validate_exact_file_contents(&report, expected).await
        }
        "product_smoke_permissions_flow" => {
            let report = temp_dir
                .join("benchmark_product")
                .join("reports")
                .join("permission_decisions.json");
            let expected = r#"[{"id":"req_read","decision":"allow"},{"id":"req_delete","decision":"deny"},{"id":"req_edit","decision":"ask"}]"#;
            validate_exact_file_contents(&report, expected).await
        }
        "product_smoke_tool_discovery" => {
            let report = temp_dir
                .join("benchmark_product")
                .join("reports")
                .join("tool_discovery_summary.txt");
            let expected =
                "enabled_tools=read,glob,edit,bash\nblocked_tools=write,web_fetch\nrequires_approval=bash\n";
            validate_exact_file_contents(&report, expected).await
        }
        _ => (None, None, None, None),
    }
}

async fn validate_exact_file_contents(
    path: &Path,
    expected: &str,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let total = 1usize;
    let content = match tokio::fs::read_to_string(path).await {
        Ok(content) => content,
        Err(e) => {
            return (
                Some(false),
                Some(0),
                Some(total),
                Some(format!(
                    "missing required smoke output {}: {}",
                    path.display(),
                    e
                )),
            );
        }
    };

    let normalized_content = content.trim_end_matches(['\n', '\r']);
    let normalized_expected = expected.trim_end_matches(['\n', '\r']);

    if normalized_content == normalized_expected {
        (Some(true), Some(1), Some(total), None)
    } else {
        (
            Some(false),
            Some(0),
            Some(total),
            Some(format!(
                "smoke output mismatch for {} (expected deterministic content)",
                path.display()
            )),
        )
    }
}

async fn validate_mcp_integration_task(
    temp_dir: &Path,
    task_name: &str,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    match task_name {
        "mcp_filesystem" => validate_mcp_filesystem_task(temp_dir).await,
        "mcp_git" => validate_mcp_git_task(temp_dir).await,
        "mcp_multi_server" => validate_mcp_multi_server_task(temp_dir).await,
        _ => (None, None, None, None),
    }
}

async fn validate_mcp_filesystem_task(
    temp_dir: &Path,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let summary_path = temp_dir
        .join("benchmark_mcp")
        .join("filesystem_root")
        .join("reports")
        .join("mcp_filesystem_summary.txt");
    let expected = "total=3\nfirst=refactor parser\nlast=write release notes\n";
    let log_path = temp_dir
        .join("benchmark_mcp")
        .join("logs")
        .join("fs_audit.jsonl");

    let mut passed = 0usize;
    let total = 2usize;

    let summary = match tokio::fs::read_to_string(&summary_path).await {
        Ok(content) => content,
        Err(e) => {
            return (
                Some(false),
                Some(0),
                Some(total),
                Some(format!(
                    "missing MCP filesystem output {}: {}",
                    summary_path.display(),
                    e
                )),
            );
        }
    };

    if summary.trim_end_matches(['\n', '\r']) == expected.trim_end_matches(['\n', '\r']) {
        passed += 1;
    } else {
        return (
            Some(false),
            Some(passed),
            Some(total),
            Some(
                "mcp_filesystem summary content does not match expected deterministic output"
                    .to_string(),
            ),
        );
    }

    if audit_log_has_tool_call(&log_path, "write_text", Some("mcp_filesystem_summary.txt")).await {
        passed += 1;
    } else {
        return (
            Some(false),
            Some(passed),
            Some(total),
            Some("mcp_filesystem did not record expected MCP write_text call".to_string()),
        );
    }

    (Some(true), Some(passed), Some(total), None)
}

async fn validate_mcp_git_task(
    temp_dir: &Path,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let repo_dir = temp_dir.join("benchmark_mcp").join("git_repo");
    let marker = repo_dir.join("mcp_marker.txt");
    let log_path = temp_dir
        .join("benchmark_mcp")
        .join("logs")
        .join("git_audit.jsonl");

    let mut passed = 0usize;
    let total = 3usize;

    let marker_content = match tokio::fs::read_to_string(&marker).await {
        Ok(content) => content,
        Err(e) => {
            return (
                Some(false),
                Some(0),
                Some(total),
                Some(format!(
                    "missing git MCP marker file {}: {}",
                    marker.display(),
                    e
                )),
            );
        }
    };
    if marker_content.trim_end_matches(['\n', '\r']) == "mcp benchmark ok" {
        passed += 1;
    } else {
        return (
            Some(false),
            Some(passed),
            Some(total),
            Some("mcp_git marker file content mismatch".to_string()),
        );
    }

    let commit_subject = match run_git_capture(&repo_dir, &["log", "-1", "--pretty=%s"]).await {
        Ok(subject) => subject,
        Err(e) => {
            return (
                Some(false),
                Some(passed),
                Some(total),
                Some(format!("failed to inspect git fixture repo: {}", e)),
            );
        }
    };
    if commit_subject.trim() == "bench: add mcp marker" {
        passed += 1;
    } else {
        return (
            Some(false),
            Some(passed),
            Some(total),
            Some(format!(
                "mcp_git latest commit subject mismatch (got: {})",
                commit_subject.trim()
            )),
        );
    }

    if audit_log_has_tool_call(&log_path, "commit", Some("bench: add mcp marker")).await {
        passed += 1;
    } else {
        return (
            Some(false),
            Some(passed),
            Some(total),
            Some("mcp_git did not record expected MCP commit call".to_string()),
        );
    }

    (Some(true), Some(passed), Some(total), None)
}

async fn validate_mcp_multi_server_task(
    temp_dir: &Path,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let report_path = temp_dir
        .join("benchmark_mcp")
        .join("filesystem_root")
        .join("reports")
        .join("mcp_multi_server_report.json");
    let fs_log_path = temp_dir
        .join("benchmark_mcp")
        .join("logs")
        .join("fs_audit.jsonl");
    let textops_log_path = temp_dir
        .join("benchmark_mcp")
        .join("logs")
        .join("textops_audit.jsonl");

    let expected_phrase = "ava mcp integration benchmark";
    let expected_hash = "c57fc7d7ee377dfb5cef138dc99ed6c97ce7dee3e0c0d3efd1743381cd4f6e13";

    let mut passed = 0usize;
    let total = 3usize;

    let report_raw = match tokio::fs::read_to_string(&report_path).await {
        Ok(content) => content,
        Err(e) => {
            return (
                Some(false),
                Some(0),
                Some(total),
                Some(format!(
                    "missing multi-server report {}: {}",
                    report_path.display(),
                    e
                )),
            );
        }
    };

    let report_json: serde_json::Value = match serde_json::from_str(&report_raw) {
        Ok(value) => value,
        Err(e) => {
            return (
                Some(false),
                Some(0),
                Some(total),
                Some(format!("multi-server report is not valid JSON: {}", e)),
            );
        }
    };

    let report_valid = report_json
        .get("original")
        .and_then(|v| v.as_str())
        .map(|v| v == expected_phrase)
        .unwrap_or(false)
        && report_json
            .get("sha256")
            .and_then(|v| v.as_str())
            .map(|v| v == expected_hash)
            .unwrap_or(false)
        && report_json
            .get("word_count")
            .and_then(|v| v.as_u64())
            .map(|v| v == 4)
            .unwrap_or(false);

    if report_valid {
        passed += 1;
    } else {
        return (
            Some(false),
            Some(passed),
            Some(total),
            Some(
                "mcp_multi_server report content mismatch (expected original/sha256/word_count)"
                    .to_string(),
            ),
        );
    }

    let fs_used = audit_log_has_tool_call(
        &fs_log_path,
        "write_text",
        Some("mcp_multi_server_report.json"),
    )
    .await;
    if fs_used {
        passed += 1;
    } else {
        return (
            Some(false),
            Some(passed),
            Some(total),
            Some("mcp_multi_server missing filesystem MCP write evidence".to_string()),
        );
    }

    let textops_used = audit_log_has_tool_call(&textops_log_path, "sha256_text", None).await
        && audit_log_has_tool_call(&textops_log_path, "word_count", None).await;
    if textops_used {
        passed += 1;
    } else {
        return (
            Some(false),
            Some(passed),
            Some(total),
            Some("mcp_multi_server missing textops MCP usage evidence".to_string()),
        );
    }

    (Some(true), Some(passed), Some(total), None)
}

async fn audit_log_has_tool_call(log_path: &Path, tool: &str, needle: Option<&str>) -> bool {
    let content = match tokio::fs::read_to_string(log_path).await {
        Ok(content) => content,
        Err(_) => return false,
    };
    audit_log_has_tool_call_from_content(&content, tool, needle)
}

fn audit_log_has_tool_call_from_content(content: &str, tool: &str, needle: Option<&str>) -> bool {
    for line in content.lines().filter(|line| !line.trim().is_empty()) {
        let Ok(value) = serde_json::from_str::<serde_json::Value>(line) else {
            continue;
        };
        let tool_match = value
            .get("tool")
            .and_then(|v| v.as_str())
            .map(|entry| entry == tool)
            .unwrap_or(false);
        if !tool_match {
            continue;
        }

        if let Some(expected_fragment) = needle {
            if value.to_string().contains(expected_fragment) {
                return true;
            }
            continue;
        }

        return true;
    }

    false
}

async fn run_git_capture(repo_dir: &Path, args: &[&str]) -> Result<String, String> {
    let output = tokio::process::Command::new("git")
        .args(args)
        .current_dir(repo_dir)
        .output()
        .await
        .map_err(|e| format!("failed to run git {:?}: {}", args, e))?;

    if output.status.success() {
        return Ok(String::from_utf8_lossy(&output.stdout).trim().to_string());
    }

    Err(format!(
        "git {:?} failed: {}",
        args,
        String::from_utf8_lossy(&output.stderr).trim()
    ))
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
        "tool_recovery_missing_file" => "tool_recovery_missing_file",
        "tool_recovery_targeted_edit" => "tool_recovery_targeted_edit",
        "stress_coding_log_pipeline" => "stress_coding_log_pipeline",
        "large_project_feature_flags" => "large_project_feature_flags",
        "maintenance_config_migration" => "maintenance_config_migration",
        "prompt_regression_verify_before_finish" => "prompt_regression_verify_before_finish",
        "prompt_regression_targeted_edit_only" => "prompt_regression_targeted_edit_only",
        "prompt_regression_minimal_patch" => "prompt_regression_minimal_patch",
        "prompt_regression_read_before_edit" => "prompt_regression_read_before_edit",
        "prompt_regression_wrong_first_edit_recovery" => {
            "prompt_regression_wrong_first_edit_recovery"
        }
        "prompt_regression_tool_choice_discipline" => "prompt_regression_tool_choice_discipline",
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

async fn validate_test_heavy_csv_regressions(
    temp_dir: &Path,
    expected_test_count: usize,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let dir = temp_dir.join("test_heavy_csv_regressions");
    let tests_path = dir.join("tests.rs");
    let tests_content = match tokio::fs::read_to_string(&tests_path).await {
        Ok(content) => content,
        Err(e) => {
            return (
                Some(false),
                None,
                Some(expected_test_count),
                Some(format!("Failed to read tests.rs: {}", e)),
            );
        }
    };

    let total_test_count = count_rust_tests(&tests_content);
    if total_test_count < expected_test_count {
        return (
            Some(false),
            Some(total_test_count),
            Some(expected_test_count),
            Some(format!(
                "expected at least {} total test cases, found {}",
                expected_test_count, total_test_count
            )),
        );
    }

    if !has_whitespace_regression_coverage(&tests_content) {
        return (
            Some(false),
            Some(total_test_count),
            Some(expected_test_count),
            Some(
                "missing whitespace regression coverage (add a parse_record-focused whitespace test)"
                    .to_string(),
            ),
        );
    }

    if !has_blank_line_regression_coverage(&tests_content) {
        return (
            Some(false),
            Some(total_test_count),
            Some(expected_test_count),
            Some(
                "missing blank-line regression coverage (add a parse_rows-focused blank-line test)"
                    .to_string(),
            ),
        );
    }

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

async fn validate_tool_recovery_verification_discipline(
    temp_dir: &Path,
    expected_test_count: usize,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let dir = temp_dir.join("tool_recovery_verification_discipline");
    let tests_path = dir.join("tests.rs");
    let tests_content = match tokio::fs::read_to_string(&tests_path).await {
        Ok(content) => content,
        Err(e) => {
            return (
                Some(false),
                None,
                Some(expected_test_count),
                Some(format!("Failed to read tests.rs: {}", e)),
            );
        }
    };

    let required_tests = [
        "allows_queued_to_running",
        "disallows_queued_to_completed",
        "disallows_completed_to_failed",
    ];
    if !contains_required_test_functions(&tests_content, &required_tests) {
        return (
            Some(false),
            Some(count_rust_tests(&tests_content)),
            Some(expected_test_count),
            Some(
                "verification-discipline gate: required transition tests are missing; run the test suite before claiming success"
                    .to_string(),
            ),
        );
    }

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

fn count_rust_tests(source: &str) -> usize {
    Regex::new(r"#\s*\[\s*test\s*\]")
        .map(|re| re.find_iter(source).count())
        .unwrap_or(0)
}

fn has_whitespace_regression_coverage(tests_content: &str) -> bool {
    let has_parse_record_call = tests_content.contains("parse_record(");
    let mentions_whitespace = Regex::new(r"(?i)\b(trim|whitespace)\b")
        .map(|re| re.is_match(tests_content))
        .unwrap_or(false);
    let whitespace_input =
        Regex::new(r#"parse_record\s*\(\s*"[^"]*(?:\\t|,\s|\s,|\s{2,})[^"]*"\s*\)"#)
            .map(|re| re.is_match(tests_content))
            .unwrap_or(false);

    has_parse_record_call && (mentions_whitespace || whitespace_input)
}

fn has_blank_line_regression_coverage(tests_content: &str) -> bool {
    let has_parse_rows_call = tests_content.contains("parse_rows(");
    let mentions_blank_line = Regex::new(r"(?i)\b(blank|empty)\b")
        .map(|re| re.is_match(tests_content))
        .unwrap_or(false);
    let blank_line_input = Regex::new(r#"parse_rows\s*\(\s*"[^"]*\\n\s*\\n[^"]*"\s*\)"#)
        .map(|re| re.is_match(tests_content))
        .unwrap_or(false);

    has_parse_rows_call && (mentions_blank_line || blank_line_input)
}

fn contains_required_test_functions(tests_content: &str, required_tests: &[&str]) -> bool {
    required_tests.iter().all(|test_name| {
        let pattern = format!(
            r"#\s*\[\s*test\s*\]\s*fn\s+{}\s*\(",
            regex::escape(test_name)
        );
        Regex::new(&pattern)
            .map(|re| re.is_match(tests_content))
            .unwrap_or(false)
    })
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
            format!("{}...", stderr.chars().take(500).collect::<String>())
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
            format!("{}...", stderr.chars().take(500).collect::<String>())
        } else {
            stderr.to_string()
        };
        (Some(true), Some(passed), Some(total), Some(error_msg))
    }
}

#[cfg(test)]
mod tests {
    use super::{
        audit_log_has_tool_call_from_content, contains_required_test_functions, count_rust_tests,
        has_blank_line_regression_coverage, has_whitespace_regression_coverage,
    };

    #[test]
    fn counts_rust_test_attributes() {
        let source = r#"
#[test]
fn a() {}

#[test]
fn b() {}
"#;
        assert_eq!(count_rust_tests(source), 2);
    }

    #[test]
    fn detects_whitespace_coverage_without_exact_test_name() {
        let source = r#"
#[test]
fn handles_spaces_around_fields() {
    let fields = parse_record("  alpha  ,  beta");
    assert_eq!(fields, vec!["alpha".to_string(), "beta".to_string()]);
}
"#;
        assert!(has_whitespace_regression_coverage(source));
    }

    #[test]
    fn rejects_parse_record_without_whitespace_regression_signal() {
        let source = r#"
#[test]
fn parses_basic_record() {
    let fields = parse_record("alpha,beta");
    assert_eq!(fields.len(), 2);
}
"#;
        assert!(!has_whitespace_regression_coverage(source));
    }

    #[test]
    fn detects_blank_line_coverage_without_exact_test_name() {
        let source = r#"
#[test]
fn skips_blank_input_rows() {
    let rows = parse_rows("a,b\n\n1,2\n");
    assert_eq!(rows.len(), 2);
}
"#;
        assert!(has_blank_line_regression_coverage(source));
    }

    #[test]
    fn rejects_parse_rows_without_blank_line_regression_signal() {
        let source = r#"
#[test]
fn parses_simple_rows() {
    let rows = parse_rows("a,b\n1,2\n");
    assert_eq!(rows.len(), 2);
}
"#;
        assert!(!has_blank_line_regression_coverage(source));
    }

    #[test]
    fn required_test_function_detector_accepts_complete_suite() {
        let source = r#"
#[test]
fn allows_queued_to_running() {
    assert!(true);
}

#[test]
fn disallows_queued_to_completed() {
    assert!(true);
}

#[test]
fn disallows_completed_to_failed() {
    assert!(true);
}
"#;

        assert!(contains_required_test_functions(
            source,
            &[
                "allows_queued_to_running",
                "disallows_queued_to_completed",
                "disallows_completed_to_failed",
            ],
        ));
    }

    #[test]
    fn required_test_function_detector_rejects_missing_case() {
        let source = r#"
#[test]
fn allows_queued_to_running() {
    assert!(true);
}

#[test]
fn disallows_queued_to_completed() {
    assert!(true);
}
"#;

        assert!(!contains_required_test_functions(
            source,
            &[
                "allows_queued_to_running",
                "disallows_queued_to_completed",
                "disallows_completed_to_failed",
            ],
        ));
    }

    #[test]
    fn audit_log_detector_matches_tool_with_fragment() {
        let jsonl = r#"{"server":"fs","tool":"write_text","arguments":{"path":"reports/mcp_filesystem_summary.txt"}}
{"server":"fs","tool":"read_text","arguments":{"path":"inbox/todo.txt"}}
"#;

        assert!(audit_log_has_tool_call_from_content(
            jsonl,
            "write_text",
            Some("mcp_filesystem_summary.txt"),
        ));
    }

    #[test]
    fn audit_log_detector_ignores_non_matching_entries() {
        let jsonl = r#"{"server":"textops","tool":"word_count","arguments":{"text":"hello"}}
{"server":"fs","tool":"read_text","arguments":{"path":"inbox/todo.txt"}}
"#;

        assert!(!audit_log_has_tool_call_from_content(
            jsonl,
            "write_text",
            Some("summary"),
        ));
    }
}
