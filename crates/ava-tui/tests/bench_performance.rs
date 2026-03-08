//! Performance benchmark tests for AVA.
//!
//! These are not micro-benchmarks — they measure real subsystem startup and throughput
//! against regression targets established in sprint 32.
//!
//! Run with: `cargo test -p ava-tui --test bench_performance -- --nocapture`

use std::sync::Arc;
use std::time::Instant;

use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_codebase::indexer::index_project;
use ava_context::{create_condenser, CondenserConfig, create_hybrid_condenser};
use ava_llm::providers::MockProvider;
use ava_llm::ConnectionPool;
use ava_memory::MemorySystem;
use ava_platform::StandardPlatform;
use ava_session::SessionManager;
use ava_tools::core::{
    register_codebase_tools, register_core_tools, register_memory_tools, register_session_tools,
};
use ava_tools::registry::ToolRegistry;
use ava_types::{Message, Role};
use tokio::sync::RwLock;

/// Build a full tool registry with all tool groups (core + memory + session + codebase).
fn build_full_registry(data_dir: &std::path::Path) -> ToolRegistry {
    let platform = Arc::new(StandardPlatform);
    let mut registry = ToolRegistry::new();
    register_core_tools(&mut registry, platform);

    let memory = Arc::new(
        MemorySystem::new(data_dir.join("bench-memory.db")).expect("memory system"),
    );
    register_memory_tools(&mut registry, memory);

    let session_mgr = Arc::new(
        SessionManager::new(data_dir.join("bench-session.db")).expect("session manager"),
    );
    register_session_tools(&mut registry, session_mgr);

    let codebase_index = Arc::new(RwLock::new(None));
    register_codebase_tools(&mut registry, codebase_index);

    registry
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

#[tokio::test]
async fn bench_tool_registry_setup() {
    let dir = tempfile::tempdir().expect("tempdir");

    let start = Instant::now();
    let registry = build_full_registry(dir.path());
    let elapsed = start.elapsed();

    let tool_count = registry.list_tools().len();
    println!("[bench] tool_registry_setup: {elapsed:?} ({tool_count} tools registered)");
    assert!(
        elapsed.as_millis() < 200,
        "Tool registry setup took {elapsed:?}, expected < 200ms"
    );
}

#[tokio::test]
async fn bench_agent_stack_startup() {
    let dir = tempfile::tempdir().expect("tempdir");
    let mock = Arc::new(MockProvider::new("bench-model", vec![]));

    let start = Instant::now();
    let stack = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(mock),
        ..Default::default()
    })
    .await
    .expect("stack init");
    let elapsed = start.elapsed();

    let tool_count = stack.tools.read().await.list_tools().len();
    println!("[bench] agent_stack_startup: {elapsed:?} ({tool_count} tools)");
    assert!(
        elapsed.as_millis() < 500,
        "AgentStack::new() took {elapsed:?}, expected < 500ms"
    );
}

#[tokio::test]
async fn bench_codebase_indexing() {
    let dir = tempfile::tempdir().expect("tempdir");
    let root = dir.path();

    // Create ~50 synthetic source files across subdirectories
    let src = root.join("src");
    tokio::fs::create_dir_all(&src).await.unwrap();
    for i in 0..50 {
        let subdir = src.join(format!("mod_{}", i / 10));
        tokio::fs::create_dir_all(&subdir).await.unwrap();
        let content = format!(
            "use ava_tools::registry;\nuse serde::Serialize;\n\n\
             pub fn function_{i}() -> String {{\n    \
             format!(\"result {{}}\" , {i})\n}}\n\n\
             pub struct Widget{i} {{\n    pub name: String,\n    pub value: i64,\n}}\n"
        );
        tokio::fs::write(subdir.join(format!("file_{i}.rs")), &content)
            .await
            .unwrap();
    }

    let start = Instant::now();
    let index = index_project(root).await.expect("index_project");
    let elapsed = start.elapsed();

    let node_count = index.graph.node_count();
    println!("[bench] codebase_indexing: {elapsed:?} ({node_count} nodes indexed from 50 files)");
    assert!(
        elapsed.as_secs() < 5,
        "Codebase indexing took {elapsed:?}, expected < 5s"
    );
}

#[tokio::test]
async fn bench_session_create_and_list() {
    let db_path = std::env::temp_dir().join(format!(
        "ava-bench-session-{}.sqlite",
        uuid::Uuid::new_v4()
    ));
    let manager = SessionManager::new(&db_path).expect("session manager");

    let start = Instant::now();
    for i in 0..100 {
        let mut session = manager.create().expect("create session");
        session.add_message(Message::new(Role::User, format!("benchmark message {i}")));
        manager.save(&session).expect("save session");
    }
    let recent = manager.list_recent(100).expect("list_recent");
    let elapsed = start.elapsed();

    println!(
        "[bench] session_create_and_list: {elapsed:?} (100 created, {} listed)",
        recent.len()
    );
    assert_eq!(recent.len(), 100);
    assert!(
        elapsed.as_millis() < 500,
        "Session create+list took {elapsed:?}, expected < 500ms"
    );

    // Cleanup
    let _ = std::fs::remove_file(&db_path);
}

#[tokio::test]
async fn bench_memory_baseline() {
    // Only meaningful on Linux where /proc/self/status is available
    if !cfg!(target_os = "linux") {
        println!("[bench] memory_baseline: SKIPPED (not Linux)");
        return;
    }

    let dir = tempfile::tempdir().expect("tempdir");
    let mock = Arc::new(MockProvider::new("bench-model", vec![]));

    let _stack = AgentStack::new(AgentStackConfig {
        data_dir: dir.path().to_path_buf(),
        injected_provider: Some(mock),
        ..Default::default()
    })
    .await
    .expect("stack init");

    let vmrss_kb = read_vmrss_kb();
    let vmrss_mb = vmrss_kb as f64 / 1024.0;
    println!("[bench] memory_baseline: {vmrss_mb:.1} MB (VmRSS after AgentStack creation)");
    assert!(
        vmrss_mb < 50.0,
        "Memory usage {vmrss_mb:.1} MB exceeds 50 MB target"
    );
}

#[tokio::test]
async fn bench_connection_pool() {
    let pool = ConnectionPool::new();

    let start = Instant::now();
    pool.get_client("https://api.anthropic.com").await;
    pool.get_client("https://api.openai.com").await;
    pool.get_client("https://openrouter.ai/api").await;
    let elapsed = start.elapsed();

    println!("[bench] connection_pool: {elapsed:?} (3 clients created)");
    assert!(
        elapsed.as_millis() < 500,
        "Connection pool setup took {elapsed:?}, expected < 500ms"
    );
}

#[tokio::test]
async fn bench_context_manager_creation() {
    let start = Instant::now();

    // Create a basic condenser (sync-only pipeline)
    let _condenser = create_condenser(128_000);

    // Create a hybrid condenser with relevance scoring
    let config = CondenserConfig {
        max_tokens: 128_000,
        target_tokens: 96_000,
        enable_summarization: false,
        ..Default::default()
    };
    let _hybrid = create_hybrid_condenser(config, None);

    let elapsed = start.elapsed();

    println!("[bench] context_manager_creation: {elapsed:?}");
    assert!(
        elapsed.as_millis() < 10,
        "Context manager creation took {elapsed:?}, expected < 10ms"
    );
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn read_vmrss_kb() -> u64 {
    let status = std::fs::read_to_string("/proc/self/status").unwrap_or_default();
    for line in status.lines() {
        if line.starts_with("VmRSS:") {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() >= 2 {
                return parts[1].parse().unwrap_or(0);
            }
        }
    }
    0
}
