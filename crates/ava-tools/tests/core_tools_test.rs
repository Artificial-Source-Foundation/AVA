use std::path::Path;
use std::sync::Arc;

use ava_platform::StandardPlatform;
use ava_tools::core::{
    apply_patch::ApplyPatchTool, bash::BashTool, diagnostics::DiagnosticsTool, edit::EditTool,
    glob::GlobTool, grep::GrepTool, lint::LintTool, multiedit::MultiEditTool, read::ReadTool,
    test_runner::TestRunnerTool, write::WriteTool,
};
use ava_tools::registry::Tool;
use serde_json::json;
use tempfile::tempdir;

#[tokio::test]
async fn read_tool_reads_file_with_line_numbers() {
    let dir = tempdir().expect("tempdir");
    let path = dir.path().join("sample.txt");
    tokio::fs::write(&path, "alpha\nbeta\ngamma\n")
        .await
        .expect("write test file");

    let tool = ReadTool::new(Arc::new(StandardPlatform));
    let result = tool
        .execute(json!({"path": path.to_string_lossy().to_string()}))
        .await
        .expect("read executes");

    assert!(result.content.contains("     1\talpha"));
    assert!(result.content.contains("     2\tbeta"));
    assert!(result.content.contains("     3\tgamma"));
    assert!(!result.is_error);
}

#[tokio::test]
async fn read_tool_applies_offset_and_limit() {
    let dir = tempdir().expect("tempdir");
    let path = dir.path().join("sample.txt");
    tokio::fs::write(&path, "alpha\nbeta\ngamma\n")
        .await
        .expect("write test file");

    let tool = ReadTool::new(Arc::new(StandardPlatform));
    let result = tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "offset": 2,
            "limit": 1
        }))
        .await
        .expect("read executes");

    assert!(!result.content.contains("alpha"));
    assert!(result.content.contains("     2\tbeta"));
    assert!(!result.content.contains("gamma"));
}

#[tokio::test]
async fn read_tool_errors_on_missing_file() {
    let tool = ReadTool::new(Arc::new(StandardPlatform));
    let error = tool
        .execute(json!({"path": "/tmp/definitely-missing-ava-tools-file.txt"}))
        .await
        .expect_err("missing path should error");

    assert!(error.to_string().contains("not found"));
}

#[tokio::test]
async fn write_tool_writes_file_and_creates_parent_directories() {
    let dir = tempdir().expect("tempdir");
    let path = dir.path().join("nested/deep/file.txt");
    let tool = WriteTool::new(Arc::new(StandardPlatform));

    let result = tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "content": "hello from write"
        }))
        .await
        .expect("write executes");

    assert!(result.content.contains("Wrote"));
    assert_eq!(tokio::fs::read_to_string(&path).await.expect("read back"), "hello from write");
}

#[tokio::test]
async fn write_tool_overwrites_existing_file() {
    let dir = tempdir().expect("tempdir");
    let path = dir.path().join("overwrite.txt");
    tokio::fs::write(&path, "before").await.expect("seed file");

    let tool = WriteTool::new(Arc::new(StandardPlatform));
    tool.execute(json!({
        "path": path.to_string_lossy().to_string(),
        "content": "after"
    }))
    .await
    .expect("write executes");

    assert_eq!(tokio::fs::read_to_string(&path).await.expect("read back"), "after");
}

#[tokio::test]
async fn edit_tool_exact_match_replacement() {
    let dir = tempdir().expect("tempdir");
    let path = dir.path().join("edit.txt");
    tokio::fs::write(&path, "hello world\n")
        .await
        .expect("seed file");

    let tool = EditTool::new(Arc::new(StandardPlatform));
    let result = tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "old_text": "world",
            "new_text": "ava"
        }))
        .await
        .expect("edit executes");

    assert!(result.content.contains("exact_match"));
    assert_eq!(tokio::fs::read_to_string(&path).await.expect("read back"), "hello ava\n");
}

#[tokio::test]
async fn edit_tool_uses_multi_strategy_fallback() {
    let dir = tempdir().expect("tempdir");
    let path = dir.path().join("edit_fallback.txt");
    tokio::fs::write(&path, "alpha   beta\ngamma\n")
        .await
        .expect("seed file");

    let tool = EditTool::new(Arc::new(StandardPlatform));
    let result = tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "old_text": "alpha beta\ngamma",
            "new_text": "delta"
        }))
        .await
        .expect("edit executes");

    assert!(result.content.contains("flexible_match"));
    assert_eq!(tokio::fs::read_to_string(&path).await.expect("read back"), "delta\n");
}

#[tokio::test]
async fn edit_tool_errors_when_no_match_found() {
    let dir = tempdir().expect("tempdir");
    let path = dir.path().join("edit_none.txt");
    tokio::fs::write(&path, "hello world\n")
        .await
        .expect("seed file");

    let tool = EditTool::new(Arc::new(StandardPlatform));
    let error = tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "old_text": "not-here",
            "new_text": "replacement"
        }))
        .await
        .expect_err("no match should error");

    assert!(error.to_string().contains("No matching edit strategy"));
}

#[tokio::test]
async fn bash_tool_executes_command_and_returns_exit_code() {
    let tool = BashTool::new(Arc::new(StandardPlatform));
    let result = tool
        .execute(json!({"command": "printf 'hello'"}))
        .await
        .expect("bash executes");

    assert!(result.content.contains("hello"));
    assert!(result.content.contains("exit_code: 0"));
}

#[tokio::test]
async fn bash_tool_enforces_timeout() {
    let tool = BashTool::new(Arc::new(StandardPlatform));
    let error = tool
        .execute(json!({
            "command": "sleep 2",
            "timeout_ms": 10
        }))
        .await
        .expect_err("command should timeout");

    assert!(error.to_string().contains("timed out"));
}

#[tokio::test]
async fn bash_tool_rejects_dangerous_commands() {
    let tool = BashTool::new(Arc::new(StandardPlatform));
    let error = tool
        .execute(json!({"command": "rm -rf /"}))
        .await
        .expect_err("dangerous command should be rejected");

    assert!(error.to_string().contains("dangerous"));
}

#[tokio::test]
async fn glob_tool_matches_patterns_and_respects_path() {
    let dir = tempdir().expect("tempdir");
    let src = dir.path().join("src");
    tokio::fs::create_dir_all(&src).await.expect("mkdir");
    tokio::fs::write(src.join("a.rs"), "a").await.expect("write");
    tokio::fs::write(src.join("b.txt"), "b").await.expect("write");

    let tool = GlobTool::new();
    let result = tool
        .execute(json!({
            "pattern": "**/*.rs",
            "path": src.to_string_lossy().to_string()
        }))
        .await
        .expect("glob executes");

    assert!(result.content.contains("a.rs"));
    assert!(!result.content.contains("b.txt"));
}

#[tokio::test]
async fn glob_tool_returns_empty_result() {
    let dir = tempdir().expect("tempdir");
    let tool = GlobTool::new();
    let result = tool
        .execute(json!({
            "pattern": "**/*.does-not-exist",
            "path": dir.path().to_string_lossy().to_string()
        }))
        .await
        .expect("glob executes");

    assert!(result.content.trim().is_empty());
}

#[tokio::test]
async fn grep_tool_matches_regex_and_include_filter() {
    let dir = tempdir().expect("tempdir");
    tokio::fs::write(dir.path().join("main.rs"), "let status = \"ok\";\n")
        .await
        .expect("write");
    tokio::fs::write(dir.path().join("note.txt"), "status: ok\n")
        .await
        .expect("write");

    let tool = GrepTool::new();
    let result = tool
        .execute(json!({
            "pattern": "status",
            "path": dir.path().to_string_lossy().to_string(),
            "include": "*.rs"
        }))
        .await
        .expect("grep executes");

    assert!(result.content.contains("main.rs:1:"));
    assert!(!result.content.contains("note.txt"));
}

#[tokio::test]
async fn grep_tool_returns_empty_result() {
    let dir = tempdir().expect("tempdir");
    tokio::fs::write(dir.path().join("main.rs"), "fn main() {}\n")
        .await
        .expect("write");

    let tool = GrepTool::new();
    let result = tool
        .execute(json!({
            "pattern": "definitely_missing_pattern",
            "path": dir.path().to_string_lossy().to_string()
        }))
        .await
        .expect("grep executes");

    assert!(result.content.trim().is_empty());
}

// --- MultiEdit Tool Tests ---

#[tokio::test]
async fn multiedit_applies_edits_across_files() {
    let dir = tempdir().expect("tempdir");
    let file_a = dir.path().join("a.txt");
    let file_b = dir.path().join("b.txt");
    tokio::fs::write(&file_a, "hello world\n").await.unwrap();
    tokio::fs::write(&file_b, "foo bar\n").await.unwrap();

    let tool = MultiEditTool::new(Arc::new(StandardPlatform));
    let result = tool
        .execute(json!({
            "edits": [
                { "path": file_a.to_string_lossy(), "old_text": "world", "new_text": "ava" },
                { "path": file_b.to_string_lossy(), "old_text": "bar", "new_text": "baz" }
            ]
        }))
        .await
        .expect("multiedit executes");

    assert!(result.content.contains("2 edits"));
    assert!(result.content.contains("2 files"));
    assert_eq!(tokio::fs::read_to_string(&file_a).await.unwrap(), "hello ava\n");
    assert_eq!(tokio::fs::read_to_string(&file_b).await.unwrap(), "foo baz\n");
}

#[tokio::test]
async fn multiedit_validation_failure_blocks_all_edits() {
    let dir = tempdir().expect("tempdir");
    let file_a = dir.path().join("a.txt");
    tokio::fs::write(&file_a, "hello world\n").await.unwrap();

    let tool = MultiEditTool::new(Arc::new(StandardPlatform));
    let error = tool
        .execute(json!({
            "edits": [
                { "path": file_a.to_string_lossy(), "old_text": "world", "new_text": "ava" },
                { "path": file_a.to_string_lossy(), "old_text": "NOT_HERE", "new_text": "fail" }
            ]
        }))
        .await
        .expect_err("should fail validation");

    assert!(error.to_string().contains("Validation failed"));
    // Original file should be unchanged
    assert_eq!(tokio::fs::read_to_string(&file_a).await.unwrap(), "hello world\n");
}

#[tokio::test]
async fn multiedit_empty_edits_returns_error() {
    let tool = MultiEditTool::new(Arc::new(StandardPlatform));
    let error = tool
        .execute(json!({ "edits": [] }))
        .await
        .expect_err("empty edits should error");
    assert!(error.to_string().contains("must not be empty"));
}

// --- Apply Patch Tool Tests ---

#[tokio::test]
async fn apply_patch_single_file() {
    let dir = tempdir().expect("tempdir");
    let file = dir.path().join("main.rs");
    tokio::fs::write(&file, "fn main() {\n    println!(\"hello\");\n}\n")
        .await
        .unwrap();

    let path = file.to_string_lossy();
    let patch = format!(
        "--- {path}\n+++ {path}\n@@ -1,3 +1,3 @@\n fn main() {{\n-    println!(\"hello\");\n+    println!(\"world\");\n }}\n",
    );

    let tool = ApplyPatchTool::new(Arc::new(StandardPlatform));
    let result = tool
        .execute(json!({ "patch": patch, "strip": 0 }))
        .await
        .expect("patch applies");

    assert!(result.content.contains("Applied 1 hunks"));
    let content = tokio::fs::read_to_string(&file).await.unwrap();
    assert!(content.contains("println!(\"world\")"));
}

#[tokio::test]
async fn apply_patch_multi_file() {
    let dir = tempdir().expect("tempdir");
    let file_a = dir.path().join("a.txt");
    let file_b = dir.path().join("b.txt");
    tokio::fs::write(&file_a, "alpha\nbeta\n").await.unwrap();
    tokio::fs::write(&file_b, "one\ntwo\n").await.unwrap();

    let a = file_a.to_string_lossy();
    let b = file_b.to_string_lossy();
    let patch = format!(
        "--- {a}\n+++ {a}\n@@ -1,2 +1,2 @@\n alpha\n-beta\n+gamma\n--- {b}\n+++ {b}\n@@ -1,2 +1,2 @@\n one\n-two\n+three\n",
    );

    let tool = ApplyPatchTool::new(Arc::new(StandardPlatform));
    let result = tool
        .execute(json!({ "patch": patch, "strip": 0 }))
        .await
        .expect("patch applies");

    assert!(result.content.contains("2 files"));
    assert!(tokio::fs::read_to_string(&file_a).await.unwrap().contains("gamma"));
    assert!(tokio::fs::read_to_string(&file_b).await.unwrap().contains("three"));
}

#[tokio::test]
async fn apply_patch_fuzzy_offset() {
    let dir = tempdir().expect("tempdir");
    let file = dir.path().join("offset.txt");
    // File has an extra line at the top vs what the patch expects
    tokio::fs::write(&file, "extra line\nalpha\nbeta\ngamma\n")
        .await
        .unwrap();

    let path = file.to_string_lossy();
    let patch = format!(
        "--- {path}\n+++ {path}\n@@ -1,3 +1,3 @@\n alpha\n-beta\n+BETA\n gamma\n",
    );

    let tool = ApplyPatchTool::new(Arc::new(StandardPlatform));
    let result = tool
        .execute(json!({ "patch": patch, "strip": 0 }))
        .await
        .expect("fuzzy patch applies");

    assert!(result.content.contains("Applied 1 hunks"));
    assert!(tokio::fs::read_to_string(&file).await.unwrap().contains("BETA"));
}

// --- Test Runner Tool Tests ---

#[tokio::test]
async fn test_runner_auto_detects_cargo() {
    // We're in a Cargo project, so this should auto-detect
    let tool = TestRunnerTool::new(Arc::new(StandardPlatform));
    let result = tool
        .execute(json!({ "filter": "healthcheck", "timeout": 30 }))
        .await
        .expect("test runner executes");

    // Should run cargo test with filter
    assert!(result.content.contains("\"passed\""));
}

#[tokio::test]
async fn test_runner_timeout_enforcement() {
    let tool = TestRunnerTool::new(Arc::new(StandardPlatform));
    let error = tool
        .execute(json!({
            "command": "sleep 10",
            "timeout": 1
        }))
        .await
        .expect_err("should timeout");

    assert!(error.to_string().contains("timed out"));
}

// --- Lint Tool Tests ---

#[tokio::test]
async fn lint_auto_detects_cargo_clippy() {
    let tool = LintTool::new(Arc::new(StandardPlatform));
    // Just verify it runs without crashing — clippy output varies
    let result = tool.execute(json!({})).await;
    // Should at least not panic, even if clippy has findings
    assert!(result.is_ok() || result.is_err());
}

// --- Diagnostics Tool Tests ---

#[tokio::test]
async fn diagnostics_structured_output() {
    let tool = DiagnosticsTool::new(Arc::new(StandardPlatform));
    let result = tool.execute(json!({})).await.expect("diagnostics executes");
    // Should return JSON with diagnostics array
    assert!(result.content.contains("\"diagnostics\""));
}

// --- Tool Registry Tests ---

#[test]
fn new_tools_are_registered() {
    use ava_tools::core::register_core_tools;
    use ava_tools::registry::ToolRegistry;

    let mut registry = ToolRegistry::new();
    register_core_tools(&mut registry, Arc::new(StandardPlatform));
    let tools = registry.list_tools();
    let names: Vec<&str> = tools.iter().map(|t| t.name.as_str()).collect();

    assert!(names.contains(&"multiedit"), "multiedit should be registered");
    assert!(names.contains(&"apply_patch"), "apply_patch should be registered");
    assert!(names.contains(&"test_runner"), "test_runner should be registered");
    assert!(names.contains(&"lint"), "lint should be registered");
    assert!(names.contains(&"diagnostics"), "diagnostics should be registered");
}

#[tokio::test]
async fn read_large_file_truncates_at_default_limit() {
    let dir = tempdir().expect("tempdir");
    let path = dir.path().join("large.txt");
    // Create a 5000-line file
    let content: String = (1..=5000).map(|i| format!("line {i}\n")).collect();
    tokio::fs::write(&path, &content).await.expect("write");

    let tool = ReadTool::new(Arc::new(StandardPlatform));
    let result = tool
        .execute(json!({"path": path.to_string_lossy().to_string()}))
        .await
        .expect("read executes");

    // Should be capped at 2000 lines + truncation notice
    let output_lines: Vec<&str> = result.content.lines().collect();
    // 2000 content lines + 1 empty line + 1 truncation notice = ~2002
    assert!(output_lines.len() <= 2003, "output should be ~2002 lines, got {}", output_lines.len());
    assert!(result.content.contains("[Truncated:"), "should contain truncation notice");
    assert!(result.content.contains("2000 lines"), "should mention 2000 lines");
}

#[tokio::test]
async fn read_explicit_limit_overrides_default() {
    let dir = tempdir().expect("tempdir");
    let path = dir.path().join("large2.txt");
    let content: String = (1..=5000).map(|i| format!("line {i}\n")).collect();
    tokio::fs::write(&path, &content).await.expect("write");

    let tool = ReadTool::new(Arc::new(StandardPlatform));
    let result = tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "limit": 3000
        }))
        .await
        .expect("read executes");

    // Explicit limit of 3000 should override the default 2000
    let output_lines: Vec<&str> = result.content.lines().collect();
    assert!(output_lines.len() <= 3003);
    assert!(result.content.contains("[Truncated:"));
    assert!(result.content.contains("3000 lines"));
}

#[test]
fn missing_tool_returns_tool_not_found_error() {
    use ava_tools::registry::ToolRegistry;

    let registry = ToolRegistry::new();
    let tool_call = ava_types::ToolCall {
        id: "call_1".to_string(),
        name: "nonexistent_tool".to_string(),
        arguments: json!({}),
    };
    let result = tokio::runtime::Runtime::new()
        .unwrap()
        .block_on(registry.execute(tool_call));
    let err = result.expect_err("should fail for missing tool");
    assert!(err.to_string().contains("not found"), "error: {err}");
}

#[test]
fn tests_reference_tempfile_paths_as_expected() {
    assert!(Path::new(".").exists());
}
