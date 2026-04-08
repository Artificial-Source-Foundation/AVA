use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

fn mcp_validation_harness() -> TestHarness {
    // Tier-3 validation is task-specific for MCP integration tasks. We don't compile
    // model output for these tasks; instead, benchmark_support::validation checks
    // fixture files and MCP audit logs written by the local mock servers.
    TestHarness {
        test_code: "",
        setup_code: None,
        test_count: 1,
        language: Language::Rust,
    }
}

/// AVA 3.3.1 Round 4: first real MCP integration benchmark tasks.
pub fn mcp_integration_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let fs_root = temp_dir.join("benchmark_mcp").join("filesystem_root");
    let git_repo = temp_dir.join("benchmark_mcp").join("git_repo");

    vec![
        BenchmarkTask {
            name: "mcp_filesystem",
            prompt: format!(
                "Use MCP filesystem tools (server `fs`) to read and write files under {fs_root}. \
                 Read `inbox/todo.txt` with `mcp_fs_read_text`, then create \
                 `reports/mcp_filesystem_summary.txt` with EXACTLY this content:\n\n\
                 total=3\n\
                 first=refactor parser\n\
                 last=write release notes\n\n\
                 Write the file with `mcp_fs_write_text`, then verify by reading it back \
                 via `mcp_fs_read_text` before finishing.",
                fs_root = fs_root.display()
            ),
            expected_patterns: vec![
                r"mcp_filesystem_summary\.txt",
                r"total=3",
                r"first=refactor parser",
            ],
            category: TaskCategory::McpIntegration,
            needs_tools: true,
            test_harness: Some(mcp_validation_harness()),
            expected_min_tools: Some(3),
        },
        BenchmarkTask {
            name: "mcp_git",
            prompt: format!(
                "Use MCP git tools from server `git` for repo {git_repo}. \
                 Do this through MCP tools (not shell git):\n\
                 1) Create `mcp_marker.txt` containing exactly `mcp benchmark ok\\n` \
                 using `mcp_git_write_file`.\n\
                 2) Stage it with `mcp_git_add` (path `mcp_marker.txt`).\n\
                 3) Commit with message `bench: add mcp marker` via `mcp_git_commit`.\n\
                 4) Run `mcp_git_log` (limit 1) and include the top commit summary \
                 in your final response.",
                git_repo = git_repo.display()
            ),
            expected_patterns: vec![
                r"bench: add mcp marker",
                r"mcp_marker\.txt",
                r"[0-9a-f]{7,40}",
            ],
            category: TaskCategory::McpIntegration,
            needs_tools: true,
            test_harness: Some(mcp_validation_harness()),
            expected_min_tools: Some(4),
        },
        BenchmarkTask {
            name: "mcp_multi_server",
            prompt: format!(
                "Use BOTH MCP servers `fs` and `textops` for this task. \
                 Working directory for files is {fs_root}.\n\
                 1) Read `inbox/phrase.txt` using `mcp_fs_read_text`.\n\
                 2) Call `mcp_textops_sha256_text` and `mcp_textops_word_count` on that phrase.\n\
                 3) Write `reports/mcp_multi_server_report.json` via `mcp_fs_write_text` as \
                 compact JSON with keys `original`, `sha256`, `word_count`.\n\
                 4) Verify by reading the JSON file back with `mcp_fs_read_text`.",
                fs_root = fs_root.display()
            ),
            expected_patterns: vec![r"mcp_multi_server_report\.json", r"sha256", r"word_count"],
            category: TaskCategory::McpIntegration,
            needs_tools: true,
            test_harness: Some(mcp_validation_harness()),
            expected_min_tools: Some(4),
        },
    ]
}
