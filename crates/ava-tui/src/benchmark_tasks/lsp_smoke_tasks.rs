use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

fn lsp_validation_harness() -> TestHarness {
    // Tier-3 validation for LSP smoke tasks is file-based and task-specific.
    TestHarness {
        test_code: "",
        setup_code: None,
        test_count: 1,
        language: Language::Rust,
    }
}

/// AVA 3.3.1 Round 5: first real LSP-adjacent smoke tasks.
///
/// Scope is intentionally honest to current surface:
/// - feature/config gating (`features.enable_lsp`)
/// - project detection from language manifests
/// - lightweight toolchain-readiness signals
///
/// This does NOT assume a full in-process LSP client implementation.
pub fn lsp_smoke_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let lsp_dir = temp_dir.join("benchmark_lsp");

    vec![
        BenchmarkTask {
            name: "lsp_smoke_config_gate",
            prompt: format!(
                "LSP smoke fixture root: {root}. This is a config-gating task (no full LSP client assumptions). \
                 Read `config_disabled/.ava/config.yaml`, then write \
                 `reports/lsp_config_gate_report.json` with EXACT compact JSON:\n\n\
                 {{\"lsp_enabled\":false,\"sidebar_refresh\":\"disabled\",\"reason\":\"feature_flag_off\"}}\n\n\
                 Verify by reading the file back before finishing.",
                root = lsp_dir.display()
            ),
            expected_patterns: vec![r"enable_lsp", r"lsp_config_gate_report\.json", r"feature_flag_off"],
            category: TaskCategory::LspSmoke,
            needs_tools: true,
            test_harness: Some(lsp_validation_harness()),
            expected_min_tools: Some(3),
        },
        BenchmarkTask {
            name: "lsp_smoke_project_toolchain",
            prompt: format!(
                "LSP smoke fixture root: {root}. Inspect `project_matrix/` and produce \
                 `reports/lsp_project_toolchain.csv` with EXACT content:\n\n\
                 project,language,project_detected,toolchain_ready,recommended_server\n\
                 rust_service,rust,true,true,rust-analyzer\n\
                 ts_app,typescript,true,true,typescript-language-server\n\
                 python_worker,python,true,false,pyright-langserver\n\n\
                 Treat readiness as fixture-based smoke only (manifest + toolchain marker files), not a live LSP client check. \
                 Verify by reading the CSV back before finishing.",
                root = lsp_dir.display()
            ),
            expected_patterns: vec![
                r"lsp_project_toolchain\.csv",
                r"rust-analyzer",
                r"typescript-language-server",
                r"pyright-langserver",
            ],
            category: TaskCategory::LspSmoke,
            needs_tools: true,
            test_harness: Some(lsp_validation_harness()),
            expected_min_tools: Some(4),
        },
        BenchmarkTask {
            name: "lsp_smoke_known_servers_snapshot",
            prompt: format!(
                "LSP smoke fixture root: {root}. Read `known_servers_snapshot.txt` and write \
                 `reports/lsp_known_servers_report.txt` with EXACTLY these lines (same order):\n\n\
                 rust-analyzer\n\
                 typescript\n\
                 eslint\n\
                 biome\n\
                 python\n\
                 gopls\n\
                 clangd\n\n\
                 This task validates current known-server surface only. Verify by reading the output file back.",
                root = lsp_dir.display()
            ),
            expected_patterns: vec![r"rust-analyzer", r"typescript", r"clangd"],
            category: TaskCategory::LspSmoke,
            needs_tools: true,
            test_harness: Some(lsp_validation_harness()),
            expected_min_tools: Some(3),
        },
    ]
}
