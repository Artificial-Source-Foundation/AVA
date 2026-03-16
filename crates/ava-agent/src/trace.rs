//! Run-event storage foundation for agent execution tracing.
//!
//! Provides a minimal event model and JSONL append function for recording
//! agent run lifecycle events. Events are written to date-partitioned files
//! in a `traces/` subdirectory of the AVA data directory.

use serde::{Deserialize, Serialize};
use std::time::SystemTime;

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
/// Events are written to `{data_dir}/traces/run-{YYYY-MM-DD}.jsonl`.
/// Errors during file I/O are silently ignored to avoid disrupting the
/// agent loop.
pub fn append_trace_event(data_dir: &std::path::Path, event: &RunEvent) {
    let traces_dir = data_dir.join("traces");
    std::fs::create_dir_all(&traces_dir).ok();

    let date = chrono::Utc::now().format("%Y-%m-%d");
    let path = traces_dir.join(format!("run-{date}.jsonl"));

    if let Ok(json) = serde_json::to_string(event) {
        use std::io::Write;
        if let Ok(mut f) = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path)
        {
            writeln!(f, "{json}").ok();
        }
    }
}
