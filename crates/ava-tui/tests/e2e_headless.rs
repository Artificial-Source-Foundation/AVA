//! End-to-end headless tests that run real agent loops against a live LLM provider.
//!
//! These tests require `OPENROUTER_API_KEY` to be set. If absent, tests skip (not fail).
//!
//! Run with:
//! ```bash
//! OPENROUTER_API_KEY=sk-... cargo test -p ava-tui --test e2e_headless -- --nocapture --test-threads=1
//! ```

use std::path::Path;

use ava_agent::stack::{AgentRunResult, AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use serde_json::json;
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;
use uuid::Uuid;

const MODEL: &str = "anthropic/claude-haiku-4-5-20251001";
const TIMEOUT_SECS: u64 = 60;
const MAX_TURNS: usize = 5;

/// Returns the API key or an empty string (caller should return early).
fn require_api_key() -> String {
    match std::env::var("OPENROUTER_API_KEY") {
        Ok(key) if !key.is_empty() => key,
        _ => {
            eprintln!("Skipping: OPENROUTER_API_KEY not set");
            String::new()
        }
    }
}

/// Create a test AgentStack with real OpenRouter credentials in a temp directory.
async fn create_test_stack(temp_dir: &Path, api_key: &str) -> AgentStack {
    let creds = json!({
        "providers": {
            "openrouter": {
                "api_key": api_key,
                "base_url": null,
                "org_id": null
            }
        }
    });
    tokio::fs::create_dir_all(temp_dir).await.unwrap();
    tokio::fs::write(temp_dir.join("credentials.json"), creds.to_string())
        .await
        .unwrap();

    let (stack, _question_rx, _approval_rx, _plan_rx) = AgentStack::new(AgentStackConfig {
        data_dir: temp_dir.to_path_buf(),
        provider: Some("openrouter".to_string()),
        model: Some(MODEL.to_string()),
        max_turns: MAX_TURNS,
        yolo: true,
        ..Default::default()
    })
    .await
    .expect("Failed to create AgentStack");
    stack
}

/// Run the agent with a timeout, collecting all events.
async fn run_with_timeout(
    stack: AgentStack,
    goal: &str,
    timeout_secs: u64,
) -> (AgentRunResult, Vec<AgentEvent>) {
    let (tx, mut rx) = mpsc::unbounded_channel();
    let cancel = CancellationToken::new();
    let goal = goal.to_string();

    let handle = tokio::spawn(async move {
        stack
            .run(
                &goal,
                MAX_TURNS,
                Some(tx),
                cancel,
                Vec::new(),
                None,
                Vec::new(),
            )
            .await
    });

    let mut events = Vec::new();
    let collect = async {
        while let Some(event) = rx.recv().await {
            events.push(event);
        }
    };

    tokio::time::timeout(std::time::Duration::from_secs(timeout_secs), collect)
        .await
        .ok(); // timeout just stops collection

    let result = tokio::time::timeout(std::time::Duration::from_secs(5), handle)
        .await
        .expect("Agent task timed out")
        .expect("Agent task panicked")
        .expect("Agent run failed");

    (result, events)
}

/// Extract all text tokens from events into a single string.
fn collect_text(events: &[AgentEvent]) -> String {
    events
        .iter()
        .filter_map(|e| match e {
            AgentEvent::Token(t) => Some(t.as_str()),
            _ => None,
        })
        .collect::<Vec<_>>()
        .join("")
}

/// Check if any ToolCall event matches a tool name.
fn has_tool_call(events: &[AgentEvent], tool_name: &str) -> bool {
    events
        .iter()
        .any(|e| matches!(e, AgentEvent::ToolCall(tc) if tc.name == tool_name))
}

// ─── Tests ───────────────────────────────────────────────────────────────────

#[tokio::test]
async fn e2e_simple_question() {
    let key = require_api_key();
    if key.is_empty() {
        return;
    }

    let temp = tempfile::tempdir().unwrap();
    let stack = create_test_stack(temp.path(), &key).await;
    let (result, events) = run_with_timeout(
        stack,
        "What is 2+2? Answer with just the number.",
        TIMEOUT_SECS,
    )
    .await;

    assert!(result.success);
    let text = collect_text(&events);
    assert!(text.contains('4'), "Expected '4' in response, got: {text}");
}

#[tokio::test]
async fn e2e_read_file() {
    let key = require_api_key();
    if key.is_empty() {
        return;
    }

    let temp = tempfile::tempdir().unwrap();
    let stack = create_test_stack(temp.path(), &key).await;
    let (result, events) = run_with_timeout(
        stack,
        "Read the file Cargo.toml in the current directory and tell me the workspace members. Be brief.",
        TIMEOUT_SECS,
    ).await;

    assert!(result.success);
    assert!(has_tool_call(&events, "read"), "Expected read tool call");
}

#[tokio::test]
async fn e2e_write_file() {
    let key = require_api_key();
    if key.is_empty() {
        return;
    }

    let uuid = Uuid::new_v4();
    let path = format!("/tmp/ava-e2e-{uuid}.txt");

    let temp = tempfile::tempdir().unwrap();
    let stack = create_test_stack(temp.path(), &key).await;
    let goal = format!(
        "Create the file {path} with the exact content 'hello from ava'. Use the write tool."
    );
    let (result, _events) = run_with_timeout(stack, &goal, TIMEOUT_SECS).await;

    assert!(result.success);
    let content = tokio::fs::read_to_string(&path)
        .await
        .unwrap_or_else(|_| panic!("File {path} should exist"));
    assert!(
        content.contains("hello from ava"),
        "File content should contain 'hello from ava', got: {content}"
    );

    // Cleanup
    let _ = tokio::fs::remove_file(&path).await;
}

#[tokio::test]
async fn e2e_edit_file() {
    let key = require_api_key();
    if key.is_empty() {
        return;
    }

    let uuid = Uuid::new_v4();
    let path = format!("/tmp/ava-e2e-edit-{uuid}.txt");

    let temp = tempfile::tempdir().unwrap();
    let stack = create_test_stack(temp.path(), &key).await;
    let goal = format!(
        "First, create the file {path} with content 'foo'. Then edit that file to replace 'foo' with 'bar'."
    );
    let (result, _events) = run_with_timeout(stack, &goal, TIMEOUT_SECS).await;

    assert!(result.success);
    let content = tokio::fs::read_to_string(&path)
        .await
        .unwrap_or_else(|_| panic!("File {path} should exist"));
    assert!(
        content.contains("bar"),
        "File should contain 'bar' after edit, got: {content}"
    );

    // Cleanup
    let _ = tokio::fs::remove_file(&path).await;
}

#[tokio::test]
async fn e2e_glob() {
    let key = require_api_key();
    if key.is_empty() {
        return;
    }

    let temp = tempfile::tempdir().unwrap();
    let stack = create_test_stack(temp.path(), &key).await;
    let (result, events) = run_with_timeout(
        stack,
        "Use the glob tool to list all Cargo.toml files under the crates/ directory. Be brief.",
        TIMEOUT_SECS,
    )
    .await;

    assert!(result.success);
    assert!(has_tool_call(&events, "glob"), "Expected glob tool call");
    let text = collect_text(&events);
    assert!(
        text.contains("Cargo.toml"),
        "Expected 'Cargo.toml' in response, got: {text}"
    );
}

#[tokio::test]
async fn e2e_grep() {
    let key = require_api_key();
    if key.is_empty() {
        return;
    }

    let temp = tempfile::tempdir().unwrap();
    let stack = create_test_stack(temp.path(), &key).await;
    let (result, events) = run_with_timeout(
        stack,
        "Use the grep tool to search for 'AgentStack' in crates/ava-agent/src/. Be brief about what you find.",
        TIMEOUT_SECS,
    ).await;

    assert!(result.success);
    assert!(has_tool_call(&events, "grep"), "Expected grep tool call");
    let text = collect_text(&events);
    assert!(
        text.contains("stack") || text.contains("Stack") || text.contains("agent"),
        "Expected mention of stack/agent in response, got: {text}"
    );
}

#[tokio::test]
async fn e2e_bash() {
    let key = require_api_key();
    if key.is_empty() {
        return;
    }

    let temp = tempfile::tempdir().unwrap();
    let stack = create_test_stack(temp.path(), &key).await;
    let (result, events) = run_with_timeout(
        stack,
        "Run this bash command: echo 'ava-e2e-test'. Tell me the output.",
        TIMEOUT_SECS,
    )
    .await;

    assert!(result.success);
    assert!(has_tool_call(&events, "bash"), "Expected bash tool call");
    let text = collect_text(&events);
    assert!(
        text.contains("ava-e2e-test"),
        "Expected 'ava-e2e-test' in response, got: {text}"
    );
}

#[tokio::test]
async fn e2e_multi_tool() {
    let key = require_api_key();
    if key.is_empty() {
        return;
    }

    let temp = tempfile::tempdir().unwrap();
    let stack = create_test_stack(temp.path(), &key).await;
    let (result, events) = run_with_timeout(
        stack,
        "First read Cargo.toml, then use grep to search for 'ava' in it. Summarize briefly.",
        TIMEOUT_SECS,
    )
    .await;

    assert!(result.success);
    // Should have used at least one tool
    let tool_calls: Vec<_> = events
        .iter()
        .filter(|e| matches!(e, AgentEvent::ToolCall(_)))
        .collect();
    assert!(
        !tool_calls.is_empty(),
        "Expected at least 1 tool call, got {}",
        tool_calls.len()
    );
}

// Memory tools (remember/recall) removed from LLM-visible registration.
// #[tokio::test]
// async fn e2e_memory() { ... }

#[tokio::test]
async fn e2e_session_saved() {
    let key = require_api_key();
    if key.is_empty() {
        return;
    }

    let temp = tempfile::tempdir().unwrap();
    let stack = create_test_stack(temp.path(), &key).await;

    // Keep a reference to the session manager before running
    let session_mgr = stack.session_manager.clone();

    let (result, _events) = run_with_timeout(stack, "Say hello briefly.", TIMEOUT_SECS).await;
    assert!(result.success);

    // Save the session (run() doesn't auto-save)
    session_mgr
        .save(&result.session)
        .expect("Failed to save session");

    let recent = session_mgr
        .list_recent(10)
        .expect("Failed to list sessions");
    assert!(!recent.is_empty(), "Expected at least one saved session");
    assert_eq!(recent[0].id, result.session.id);
}

#[tokio::test]
async fn e2e_cost_tracking() {
    let key = require_api_key();
    if key.is_empty() {
        return;
    }

    let temp = tempfile::tempdir().unwrap();
    let stack = create_test_stack(temp.path(), &key).await;
    let (_result, events) = run_with_timeout(stack, "Say hello briefly.", TIMEOUT_SECS).await;

    let usage_events: Vec<_> = events
        .iter()
        .filter(|e| matches!(e, AgentEvent::TokenUsage { .. }))
        .collect();
    assert!(
        !usage_events.is_empty(),
        "Expected at least one TokenUsage event, got events: {:?}",
        events
            .iter()
            .map(std::mem::discriminant)
            .collect::<Vec<_>>()
    );
}

#[tokio::test]
async fn e2e_completion() {
    let key = require_api_key();
    if key.is_empty() {
        return;
    }

    let temp = tempfile::tempdir().unwrap();
    let stack = create_test_stack(temp.path(), &key).await;
    let (result, events) = run_with_timeout(stack, "Say hello briefly.", TIMEOUT_SECS).await;

    assert!(result.success, "Agent run should succeed");

    let has_error = events.iter().any(|e| matches!(e, AgentEvent::Error(_)));
    assert!(!has_error, "Should have no error events");

    let has_complete = events.iter().any(|e| matches!(e, AgentEvent::Complete(_)));
    assert!(has_complete, "Should have a Complete event");
}
