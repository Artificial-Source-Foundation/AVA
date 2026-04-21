//! Run-event storage foundation for agent execution tracing.
//!
//! Provides a minimal event model and JSONL append function for recording
//! agent run lifecycle events. Events are written to date-partitioned files
//! in a `traces/` subdirectory of AVA's XDG state directory.

use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use std::time::SystemTime;

fn fallback_state_dir(name: &str) -> PathBuf {
    if let Ok(state_home) = std::env::var("XDG_STATE_HOME") {
        return PathBuf::from(state_home).join("ava").join(name);
    }

    if let Ok(home) = std::env::var("HOME") {
        return PathBuf::from(home)
            .join(".local")
            .join("state")
            .join("ava")
            .join(name);
    }

    std::env::temp_dir().join("ava").join(name)
}

/// A single run event with timestamp and run identity.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RunEvent {
    pub timestamp: SystemTime,
    pub run_id: String,
    pub kind: RunEventKind,
}

/// The kind of event that occurred during an agent run.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum RunEventKind {
    RunStarted {
        goal: String,
        model: String,
    },
    TurnStarted {
        turn: usize,
    },
    LlmRequest {
        model: String,
        token_count: usize,
    },
    LlmResponse {
        tokens_in: usize,
        tokens_out: usize,
        duration_ms: u64,
    },
    ToolInvoked {
        tool: String,
        duration_ms: u64,
        success: bool,
    },
    RunCompleted {
        turns: usize,
        total_ms: u64,
    },
    RunFailed {
        error: String,
    },
}

/// Append a run event to the JSONL trace file for today's date.
///
/// Events are written to AVA's XDG state `traces/` directory.
/// The blocking file I/O is offloaded via `tokio::task::spawn_blocking` so
/// the async executor thread is not stalled. This is fire-and-forget —
/// errors are silently ignored to avoid disrupting the agent loop.
pub fn append_trace_event(_data_dir: &std::path::Path, event: &RunEvent) {
    // Serialize before entering spawn_blocking (cheap, non-blocking).
    let Ok(json) = serde_json::to_string(event) else {
        return;
    };

    let date = chrono::Utc::now().format("%Y-%m-%d").to_string();
    let traces_dir = ava_config::traces_dir().unwrap_or_else(|_| fallback_state_dir("traces"));

    tokio::task::spawn_blocking(move || {
        if std::fs::create_dir_all(&traces_dir).is_err() {
            return;
        }
        let path = traces_dir.join(format!("run-{date}.jsonl"));
        use std::io::Write as _;
        if let Ok(mut f) = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path)
        {
            writeln!(f, "{json}").ok();
        }
    });
}
