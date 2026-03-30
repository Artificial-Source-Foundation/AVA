use std::path::{Path, PathBuf};
use std::sync::Arc;

use ava_platform::StandardPlatform;
use ava_tools::core::{
    bash::BashTool, edit::EditTool, glob::GlobTool, grep::GrepTool, hashline, read::ReadTool,
    write::WriteTool,
};
use ava_tools::registry::Tool;
use serde_json::json;
use tempfile::tempdir_in;

fn workspace_for_tests() -> PathBuf {
    std::env::var_os("AVA_WORKSPACE")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("."))
}

#[tokio::test]
async fn read_tool_reads_file_with_line_numbers() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("sample.txt");
    tokio::fs::write(&path, "alpha\nbeta\ngamma\n")
        .await
        .expect("write test file");

    let tool = ReadTool::new(Arc::new(StandardPlatform), hashline::new_cache());
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
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("sample.txt");
    tokio::fs::write(&path, "alpha\nbeta\ngamma\n")
        .await
        .expect("write test file");

    let tool = ReadTool::new(Arc::new(StandardPlatform), hashline::new_cache());
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
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    tokio::fs::write(
        dir.path().join("definitely-missing-ava-tools-file.toml"),
        "x",
    )
    .await
    .expect("seed sibling file");
    let tool = ReadTool::new(Arc::new(StandardPlatform), hashline::new_cache());
    let missing_path = dir.path().join("definitely-missing-ava-tools-file.txt");

    let error = tool
        .execute(json!({"path": missing_path.to_string_lossy().to_string()}))
        .await
        .expect_err("missing path should error");

    // AvaError::NotFound displays as "Not found: ..." — check case-insensitively
    // so this test remains correct regardless of capitalisation changes.
    assert!(
        error.to_string().to_ascii_lowercase().contains("not found"),
        "expected 'not found' in error message, got: {error}"
    );
    assert!(
        error.to_string().contains("Did you mean"),
        "expected suggestion in error message, got: {error}"
    );
    assert!(error
        .to_string()
        .contains("definitely-missing-ava-tools-file.toml"));
}

#[tokio::test]
async fn write_tool_writes_file_and_creates_parent_directories() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
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
    assert_eq!(
        tokio::fs::read_to_string(&path).await.expect("read back"),
        "hello from write"
    );
}

#[tokio::test]
async fn write_tool_overwrites_existing_file() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("overwrite.txt");
    tokio::fs::write(&path, "before").await.expect("seed file");

    let tool = WriteTool::new(Arc::new(StandardPlatform));
    tool.execute(json!({
        "path": path.to_string_lossy().to_string(),
        "content": "after"
    }))
    .await
    .expect("write executes");

    assert_eq!(
        tokio::fs::read_to_string(&path).await.expect("read back"),
        "after"
    );
}

#[tokio::test]
async fn edit_tool_exact_match_replacement() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("edit.txt");
    tokio::fs::write(&path, "hello world\n")
        .await
        .expect("seed file");

    let tool = EditTool::new(Arc::new(StandardPlatform), hashline::new_cache());
    let result = tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "old_text": "world",
            "new_text": "ava"
        }))
        .await
        .expect("edit executes");

    assert!(result.content.contains("exact_match"));
    assert_eq!(
        tokio::fs::read_to_string(&path).await.expect("read back"),
        "hello ava\n"
    );
}

#[tokio::test]
async fn edit_tool_uses_multi_strategy_fallback() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("edit_fallback.txt");
    tokio::fs::write(&path, "alpha   beta\ngamma\n")
        .await
        .expect("seed file");

    let tool = EditTool::new(Arc::new(StandardPlatform), hashline::new_cache());
    let result = tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "old_text": "alpha beta\ngamma",
            "new_text": "delta"
        }))
        .await
        .expect("edit executes");

    assert!(result.content.contains("flexible_match"));
    assert_eq!(
        tokio::fs::read_to_string(&path).await.expect("read back"),
        "delta\n"
    );
}

#[tokio::test]
async fn edit_tool_errors_when_no_match_found() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("edit_none.txt");
    tokio::fs::write(&path, "hello world\n")
        .await
        .expect("seed file");

    let tool = EditTool::new(Arc::new(StandardPlatform), hashline::new_cache());
    let error = tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "old_text": "not-here",
            "new_text": "world"
        }))
        .await
        .expect_err("no match should error");

    assert!(error.to_string().contains("matching"));
    assert!(error.to_string().contains("Best similarity score achieved"));
    assert!(error.to_string().contains("line 1"));
    assert!(error
        .to_string()
        .contains("Replacement text already exists in the file"));
}

#[tokio::test]
async fn edit_tool_suggests_similar_file_names_when_path_is_missing() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    tokio::fs::write(dir.path().join("config.toml"), "mode = 'dev'\n")
        .await
        .expect("seed sibling file");

    let tool = EditTool::new(Arc::new(StandardPlatform), hashline::new_cache());
    let missing_path = dir.path().join("config.tom");
    let error = tool
        .execute(json!({
            "path": missing_path.to_string_lossy().to_string(),
            "old_text": "mode",
            "new_text": "profile"
        }))
        .await
        .expect_err("missing path should error");

    assert!(error.to_string().contains("Did you mean"));
    assert!(error.to_string().contains("config.toml"));
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
    let result = tool
        .execute(json!({"command": "rm -rf /"}))
        .await
        .expect("dangerous command should execute and report failure");

    assert!(result.is_error);
    assert!(result.content.contains("exit_code: 1"));
}

#[tokio::test]
async fn glob_tool_matches_patterns_and_respects_path() {
    // Use tempdir_in(".") so the temp dir is inside the workspace boundary
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let src = dir.path().join("src");
    tokio::fs::create_dir_all(&src).await.expect("mkdir");
    tokio::fs::write(src.join("a.rs"), "a")
        .await
        .expect("write");
    tokio::fs::write(src.join("b.txt"), "b")
        .await
        .expect("write");

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
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
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
async fn glob_tool_sorts_results_lexicographically() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    tokio::fs::write(dir.path().join("z.rs"), "z")
        .await
        .expect("write");
    tokio::fs::write(dir.path().join("a.rs"), "a")
        .await
        .expect("write");

    let tool = GlobTool::new();
    let result = tool
        .execute(json!({
            "pattern": "*.rs",
            "path": dir.path().to_string_lossy().to_string()
        }))
        .await
        .expect("glob executes");

    let a_index = result.content.find("a.rs").expect("a.rs present");
    let z_index = result.content.find("z.rs").expect("z.rs present");
    assert!(
        a_index < z_index,
        "expected lexicographic order: {}",
        result.content
    );
}

#[tokio::test]
async fn grep_tool_matches_regex_and_include_filter() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
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
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
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

#[tokio::test]
async fn grep_tool_returns_results_in_stable_path_order() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    tokio::fs::write(dir.path().join("b.rs"), "let status = 2;\n")
        .await
        .expect("write");
    tokio::fs::write(dir.path().join("a.rs"), "let status = 1;\n")
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

    let a_index = result.content.find("a.rs:1:").expect("a.rs match present");
    let b_index = result.content.find("b.rs:1:").expect("b.rs match present");
    assert!(
        a_index < b_index,
        "expected sorted grep output: {}",
        result.content
    );
}

#[tokio::test]
async fn grep_tool_caps_output_at_max_matches() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let content: String = (0..600).map(|i| format!("status line {i}\n")).collect();
    tokio::fs::write(dir.path().join("many.rs"), content)
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

    let match_lines = result
        .content
        .lines()
        .filter(|line| line.contains("many.rs:"))
        .count();
    assert!(
        match_lines <= 500,
        "expected at most 500 matches, got {match_lines}"
    );
    assert!(result.content.contains("Results truncated"));
}

// --- Tool Registry Tests ---

#[test]
fn core_tools_are_registered() {
    use ava_tools::core::register_core_tools;
    use ava_tools::registry::ToolRegistry;

    let mut registry = ToolRegistry::new();
    register_core_tools(&mut registry, Arc::new(StandardPlatform));
    let tools = registry.list_tools();
    let names: Vec<&str> = tools.iter().map(|t| t.name.as_str()).collect();

    assert!(names.contains(&"read"), "read should be registered");
    assert!(names.contains(&"write"), "write should be registered");
    assert!(names.contains(&"edit"), "edit should be registered");
    assert!(names.contains(&"bash"), "bash should be registered");
    assert!(names.contains(&"glob"), "glob should be registered");
    assert!(names.contains(&"grep"), "grep should be registered");
    assert!(
        names.contains(&"web_fetch"),
        "web_fetch should be registered"
    );
    assert!(
        names.contains(&"web_search"),
        "web_search should be registered"
    );
    assert!(names.contains(&"git"), "git should be registered");
}

#[test]
fn default_tools_gives_9_tools() {
    use ava_tools::core::register_default_tools;
    use ava_tools::registry::{ToolRegistry, ToolTier};

    let mut registry = ToolRegistry::new();
    register_default_tools(&mut registry, Arc::new(StandardPlatform));

    let all = registry.list_tools();
    assert_eq!(
        all.len(),
        9,
        "default tier should have exactly 9 tools, got: {:?}",
        all.iter().map(|t| t.name.as_str()).collect::<Vec<_>>()
    );

    let default_only = registry.list_tools_for_tiers(&[ToolTier::Default]);
    assert_eq!(default_only.len(), 9);

    let names: Vec<&str> = default_only.iter().map(|t| t.name.as_str()).collect();
    for expected in &[
        "read",
        "write",
        "edit",
        "bash",
        "glob",
        "grep",
        "web_fetch",
        "web_search",
        "git",
    ] {
        assert!(
            names.contains(expected),
            "{expected} should be in default tools"
        );
    }
}

#[test]
fn extended_tier_is_empty() {
    use ava_tools::core::register_default_tools;
    use ava_tools::registry::{ToolRegistry, ToolTier};

    let mut registry = ToolRegistry::new();
    register_default_tools(&mut registry, Arc::new(StandardPlatform));

    // Extended tier should be empty — extended tools were removed
    let extended_only = registry.list_tools_for_tiers(&[ToolTier::Extended]);
    assert_eq!(extended_only.len(), 0, "extended tier should be empty");
}

#[tokio::test]
async fn read_large_file_truncates_at_default_limit() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("large.txt");
    // Create a 5000-line file
    let content: String = (1..=5000).map(|i| format!("line {i}\n")).collect();
    tokio::fs::write(&path, &content).await.expect("write");

    let tool = ReadTool::new(Arc::new(StandardPlatform), hashline::new_cache());
    let result = tool
        .execute(json!({"path": path.to_string_lossy().to_string()}))
        .await
        .expect("read executes");

    // Should be capped at 2000 lines + truncation notice
    let output_lines: Vec<&str> = result.content.lines().collect();
    // 2000 content lines + 1 empty line + 1 truncation notice = ~2002
    assert!(
        output_lines.len() <= 2003,
        "output should be ~2002 lines, got {}",
        output_lines.len()
    );
    assert!(
        result.content.contains("[Truncated:"),
        "should contain truncation notice"
    );
    assert!(
        result.content.contains("2000 lines"),
        "should mention 2000 lines"
    );
}

#[tokio::test]
async fn read_explicit_limit_overrides_default() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("large2.txt");
    let content: String = (1..=5000).map(|i| format!("line {i}\n")).collect();
    tokio::fs::write(&path, &content).await.expect("write");

    let tool = ReadTool::new(Arc::new(StandardPlatform), hashline::new_cache());
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

// --- Hashline Tests (integration) ---

#[tokio::test]
async fn read_tool_hash_lines_adds_hash_prefixes() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("hash.txt");
    tokio::fs::write(&path, "alpha\nbeta\ngamma\n")
        .await
        .expect("write");

    let cache = hashline::new_cache();
    let tool = ReadTool::new(Arc::new(StandardPlatform), cache.clone());
    let result = tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "hash_lines": true
        }))
        .await
        .expect("read executes");

    // Each line should have [hash] prefix
    let h_alpha = hashline::hash_line("alpha");
    let h_beta = hashline::hash_line("beta");
    assert!(result.content.contains(&format!("[{h_alpha}] alpha")));
    assert!(result.content.contains(&format!("[{h_beta}] beta")));

    // Cache should be populated
    let cache_guard = cache.read().unwrap();
    let entries = cache_guard.get(&path).expect("cache should have file");
    assert_eq!(entries.len(), 3);
    assert_eq!(entries[0].content, "alpha");
    assert_eq!(entries[1].content, "beta");
}

#[tokio::test]
async fn read_tool_no_hash_lines_unchanged() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("nohash.txt");
    tokio::fs::write(&path, "alpha\nbeta\n")
        .await
        .expect("write");

    let cache = hashline::new_cache();
    let tool = ReadTool::new(Arc::new(StandardPlatform), cache.clone());
    let result = tool
        .execute(json!({"path": path.to_string_lossy().to_string()}))
        .await
        .expect("read executes");

    // Should NOT have hash prefixes
    assert!(!result.content.contains("["));
    assert!(result.content.contains("     1\talpha"));

    // Cache should NOT be populated
    let cache_guard = cache.read().unwrap();
    assert!(cache_guard.get(&path).is_none());
}

#[tokio::test]
async fn edit_tool_hashline_anchored_edit() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("hashline_edit.txt");
    let content = "fn main() {\n    println!(\"hello\");\n}\n";
    tokio::fs::write(&path, content).await.expect("write");

    let cache = hashline::new_cache();
    // First, read with hash_lines to populate cache
    let read_tool = ReadTool::new(Arc::new(StandardPlatform), cache.clone());
    read_tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "hash_lines": true
        }))
        .await
        .expect("read executes");

    // Now edit using hash anchors
    let h = hashline::hash_line("    println!(\"hello\");");
    let old_text = format!("[{h}]     println!(\"hello\");");

    let edit_tool = EditTool::new(Arc::new(StandardPlatform), cache);
    let result = edit_tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "old_text": old_text,
            "new_text": "    println!(\"world\");"
        }))
        .await
        .expect("edit executes");

    assert!(result.content.contains("hashline+"));
    let updated = tokio::fs::read_to_string(&path).await.unwrap();
    assert!(updated.contains("println!(\"world\")"));
    assert!(!updated.contains("println!(\"hello\")"));
}

#[tokio::test]
async fn edit_tool_stale_hashline_rejected() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("stale.txt");
    tokio::fs::write(&path, "original line\n")
        .await
        .expect("write");

    let cache = hashline::new_cache();
    // Read to populate cache
    let read_tool = ReadTool::new(Arc::new(StandardPlatform), cache.clone());
    read_tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "hash_lines": true
        }))
        .await
        .expect("read executes");

    // Modify the file behind the cache's back
    tokio::fs::write(&path, "modified line\n")
        .await
        .expect("write");

    // Try to edit using stale hash
    let h = hashline::hash_line("original line");
    let old_text = format!("[{h}] original line");

    let edit_tool = EditTool::new(Arc::new(StandardPlatform), cache);
    let error = edit_tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "old_text": old_text,
            "new_text": "replacement"
        }))
        .await
        .expect_err("stale edit should fail");

    // The hash still exists in cache (same hash for "original line"),
    // but the edit tool fails because the resolved text is no longer present
    // in the current file contents.
    let message = error.to_string().to_lowercase();
    assert!(message.contains("matching") || message.contains("stale"));
}

#[tokio::test]
async fn edit_tool_falls_back_without_hashes() {
    // When no hash prefixes are in old_text, normal edit cascade works
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("fallback.txt");
    tokio::fs::write(&path, "hello world\n")
        .await
        .expect("write");

    let cache = hashline::new_cache();
    let edit_tool = EditTool::new(Arc::new(StandardPlatform), cache);
    let result = edit_tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "old_text": "world",
            "new_text": "ava"
        }))
        .await
        .expect("edit executes");

    // Should use normal exact_match, not hashline
    assert!(result.content.contains("exact_match"));
    assert!(!result.content.contains("hashline"));
}

#[tokio::test]
async fn edit_tool_strips_hashes_from_new_text() {
    let dir = tempdir_in(workspace_for_tests()).expect("tempdir");
    let path = dir.path().join("strip_new.txt");
    let content = "fn foo() {\n    bar();\n}\n";
    tokio::fs::write(&path, content).await.expect("write");

    let cache = hashline::new_cache();
    let read_tool = ReadTool::new(Arc::new(StandardPlatform), cache.clone());
    read_tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "hash_lines": true
        }))
        .await
        .expect("read executes");

    let h = hashline::hash_line("    bar();");
    let old_text = format!("[{h}]     bar();");
    // LLM might copy hash prefix into new_text too
    let new_text = format!("[{h}]     baz();");

    let edit_tool = EditTool::new(Arc::new(StandardPlatform), cache);
    edit_tool
        .execute(json!({
            "path": path.to_string_lossy().to_string(),
            "old_text": old_text,
            "new_text": new_text
        }))
        .await
        .expect("edit executes");

    let updated = tokio::fs::read_to_string(&path).await.unwrap();
    // Hash prefix should be stripped from the written content
    assert!(updated.contains("    baz();"));
    assert!(!updated.contains("["));
}
