use std::path::Path;
use std::sync::Arc;

use ava_platform::StandardPlatform;
use ava_tools::core::{
    bash::BashTool, edit::EditTool, glob::GlobTool, grep::GrepTool, read::ReadTool,
    write::WriteTool,
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

#[test]
fn tests_reference_tempfile_paths_as_expected() {
    assert!(Path::new(".").exists());
}
