//! Structured JSONL session logger.
//!
//! When enabled via config (`features.session_logging: true`), writes one JSON
//! line per agent turn to `~/.ava/log/{session-id}.jsonl`. Each line captures
//! the turn number, role, tool calls, token usage, and duration.

use std::path::PathBuf;
use std::time::Duration;

use serde::Serialize;
use tracing::{debug, warn};

/// A single JSONL log entry written after each agent turn.
#[derive(Debug, Clone, Serialize)]
pub struct TurnLogEntry {
    pub timestamp: String,
    pub turn: usize,
    pub role: String,
    pub tool_calls: Vec<TurnToolCall>,
    pub tokens: TurnTokens,
    pub duration_ms: u64,
}

/// Minimal tool call info for the log.
#[derive(Debug, Clone, Serialize)]
pub struct TurnToolCall {
    pub name: String,
    pub id: String,
}

/// Token usage snapshot for a turn.
#[derive(Debug, Clone, Serialize, Default)]
pub struct TurnTokens {
    pub input: usize,
    pub output: usize,
    pub cost_usd: f64,
}

/// Session logger that writes JSONL entries to disk.
pub struct SessionLogger {
    log_path: PathBuf,
}

impl SessionLogger {
    /// Create a new session logger for the given session ID.
    ///
    /// Creates the `~/.ava/log/` directory if it does not exist. Returns `None`
    /// if the home directory cannot be determined.
    pub fn new(session_id: &str) -> Option<Self> {
        let log_dir = dirs::home_dir()?.join(".ava").join("log");
        if let Err(e) = std::fs::create_dir_all(&log_dir) {
            warn!("Failed to create session log directory: {e}");
            return None;
        }
        let filename = format!("{session_id}.jsonl");
        let log_path = log_dir.join(filename);
        debug!(path = %log_path.display(), "session logger initialized");
        Some(Self { log_path })
    }

    /// Append a single turn entry to the JSONL file.
    ///
    /// The blocking file I/O is offloaded via `tokio::task::spawn_blocking`
    /// so the async executor thread is not stalled.
    pub fn log_turn(&self, entry: &TurnLogEntry) {
        let Ok(json) = serde_json::to_string(entry) else {
            warn!("Failed to serialize session log entry");
            return;
        };
        let log_path = self.log_path.clone();
        tokio::task::spawn_blocking(move || {
            use std::io::Write as _;
            match std::fs::OpenOptions::new()
                .create(true)
                .append(true)
                .open(&log_path)
            {
                Ok(mut file) => {
                    if let Err(e) = writeln!(file, "{json}") {
                        warn!("Failed to write session log entry: {e}");
                    }
                }
                Err(e) => {
                    warn!("Failed to open session log file: {e}");
                }
            }
        });
    }

    /// Convenience builder for a turn entry.
    pub fn build_entry(
        turn: usize,
        role: &str,
        tool_calls: &[ava_types::ToolCall],
        input_tokens: usize,
        output_tokens: usize,
        cost_usd: f64,
        duration: Duration,
    ) -> TurnLogEntry {
        TurnLogEntry {
            timestamp: chrono::Utc::now().to_rfc3339(),
            turn,
            role: role.to_string(),
            tool_calls: tool_calls
                .iter()
                .map(|tc| TurnToolCall {
                    name: tc.name.clone(),
                    id: tc.id.clone(),
                })
                .collect(),
            tokens: TurnTokens {
                input: input_tokens,
                output: output_tokens,
                cost_usd,
            },
            duration_ms: duration.as_millis() as u64,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn turn_log_entry_serializes_to_json() {
        let entry = TurnLogEntry {
            timestamp: "2026-03-19T12:00:00Z".to_string(),
            turn: 1,
            role: "assistant".to_string(),
            tool_calls: vec![TurnToolCall {
                name: "read".to_string(),
                id: "call_1".to_string(),
            }],
            tokens: TurnTokens {
                input: 1000,
                output: 200,
                cost_usd: 0.015,
            },
            duration_ms: 1234,
        };
        let json = serde_json::to_string(&entry).unwrap();
        assert!(json.contains("\"turn\":1"));
        assert!(json.contains("\"role\":\"assistant\""));
        assert!(json.contains("\"name\":\"read\""));
        assert!(json.contains("\"duration_ms\":1234"));
    }

    #[test]
    fn build_entry_populates_fields() {
        let calls = vec![ava_types::ToolCall {
            id: "tc1".to_string(),
            name: "edit".to_string(),
            arguments: serde_json::json!({}),
        }];
        let entry = SessionLogger::build_entry(
            3,
            "assistant",
            &calls,
            500,
            100,
            0.01,
            Duration::from_millis(567),
        );
        assert_eq!(entry.turn, 3);
        assert_eq!(entry.tool_calls.len(), 1);
        assert_eq!(entry.tool_calls[0].name, "edit");
        assert_eq!(entry.duration_ms, 567);
    }

    #[tokio::test]
    async fn session_logger_creates_file() {
        let dir = tempfile::tempdir().unwrap();
        let log_path = dir.path().join("test-session.jsonl");
        let logger = SessionLogger {
            log_path: log_path.clone(),
        };
        let entry = SessionLogger::build_entry(
            1,
            "assistant",
            &[],
            100,
            50,
            0.005,
            Duration::from_millis(100),
        );
        logger.log_turn(&entry);
        // Give spawn_blocking time to complete
        tokio::time::sleep(std::time::Duration::from_millis(100)).await;
        let content = std::fs::read_to_string(&log_path).unwrap();
        assert!(content.contains("\"turn\":1"));
        assert_eq!(content.lines().count(), 1);

        // Second entry appends
        logger.log_turn(&entry);
        tokio::time::sleep(std::time::Duration::from_millis(100)).await;
        let content = std::fs::read_to_string(&log_path).unwrap();
        assert_eq!(content.lines().count(), 2);
    }
}
