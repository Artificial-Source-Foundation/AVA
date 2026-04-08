use super::{BenchmarkTask, Language, TaskCategory, TestHarness};

fn product_validation_harness() -> TestHarness {
    // Tier-3 validation for product smoke tasks is file-based and deterministic.
    TestHarness {
        test_code: "",
        setup_code: None,
        test_count: 1,
        language: Language::Rust,
    }
}

/// AVA 3.3.1 Round 5: first real product-surface smoke tasks.
pub fn product_smoke_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let product_dir = temp_dir.join("benchmark_product");

    vec![
        BenchmarkTask {
            name: "product_smoke_session_config_discovery",
            prompt: format!(
                "Product smoke fixture root: {root}. Read `sessions/session_index.json` and `config/default_profile.json`. \
                 Determine the resume candidate using this rule: most-recent session with `message_count > 0`. \
                 Write `reports/session_resume_decision.json` as EXACT compact JSON:\n\n\
                 {{\"resume_session_id\":\"sess_001\",\"provider\":\"openrouter\",\"model\":\"anthropic/claude-haiku-4.5\",\"reason\":\"most_recent_with_messages\"}}\n\n\
                 Verify by reading the file back before finishing.",
                root = product_dir.display()
            ),
            expected_patterns: vec![
                r"session_resume_decision\.json",
                r"sess_001",
                r"most_recent_with_messages",
            ],
            category: TaskCategory::ProductSmoke,
            needs_tools: true,
            test_harness: Some(product_validation_harness()),
            expected_min_tools: Some(3),
        },
        BenchmarkTask {
            name: "product_smoke_permissions_flow",
            prompt: format!(
                "Product smoke fixture root: {root}. Read `permissions/policy.json` and `permissions/requests.json`, \
                 then write `reports/permission_decisions.json` with EXACT compact JSON:\n\n\
                 [{{\"id\":\"req_read\",\"decision\":\"allow\"}},{{\"id\":\"req_delete\",\"decision\":\"deny\"}},{{\"id\":\"req_edit\",\"decision\":\"ask\"}}]\n\n\
                 This is a permission-aware smoke flow. Verify by reading the output file back.",
                root = product_dir.display()
            ),
            expected_patterns: vec![
                r"permission_decisions\.json",
                r"req_delete",
                r"deny",
                r"req_edit",
                r"ask",
            ],
            category: TaskCategory::ProductSmoke,
            needs_tools: true,
            test_harness: Some(product_validation_harness()),
            expected_min_tools: Some(3),
        },
        BenchmarkTask {
            name: "product_smoke_tool_discovery",
            prompt: format!(
                "Product smoke fixture root: {root}. Read `tools/registry.json` and `tools/tool_policy.json`. \
                 Write `reports/tool_discovery_summary.txt` with EXACT lines:\n\n\
                 enabled_tools=read,glob,edit,bash\n\
                 blocked_tools=write,web_fetch\n\
                 requires_approval=bash\n\n\
                 Keep ordering exactly as shown and verify by reading the file back.",
                root = product_dir.display()
            ),
            expected_patterns: vec![r"enabled_tools=", r"blocked_tools=", r"requires_approval=bash"],
            category: TaskCategory::ProductSmoke,
            needs_tools: true,
            test_harness: Some(product_validation_harness()),
            expected_min_tools: Some(3),
        },
    ]
}
