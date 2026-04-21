//! Structured JSONL session logger.
//!
//! When enabled via config (`features.session_logging: true`), writes one JSON
//! line per agent turn to AVA's XDG state log dir. Each line captures
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
    /// Creates the XDG state `log/` directory if it does not exist. Returns
    /// `None` if the state directory cannot be determined.
    ///
    /// On creation, runs log rotation: any `.jsonl` files in the log directory
    /// older than 7 days are deleted automatically.
    pub fn new(session_id: &str) -> Option<Self> {
        let log_dir = ava_config::logs_dir().ok()?;
        if let Err(e) = std::fs::create_dir_all(&log_dir) {
            warn!("Failed to create session log directory: {e}");
            return None;
        }

        // Log rotation: remove .jsonl files older than 7 days.
        Self::rotate_old_logs(&log_dir);

        let filename = format!("{session_id}.jsonl");
        let log_path = log_dir.join(filename);
        debug!(path = %log_path.display(), "session logger initialized");
        Some(Self { log_path })
    }

    /// Delete `.jsonl` files in `log_dir` whose last modification time is more
    /// than 7 days ago. Errors on individual files are logged but do not abort
    /// the cleanup pass.
    fn rotate_old_logs(log_dir: &std::path::Path) {
        use std::time::SystemTime;

        let cutoff = Duration::from_secs(7 * 24 * 60 * 60);
        let now = SystemTime::now();

        let entries = match std::fs::read_dir(log_dir) {
            Ok(entries) => entries,
            Err(e) => {
                warn!("Failed to read log directory for rotation: {e}");
                return;
            }
        };

        for entry in entries.flatten() {
            let path = entry.path();
            if path.extension().and_then(|e| e.to_str()) != Some("jsonl") {
                continue;
            }
            let Ok(modified) = entry.metadata().and_then(|m| m.modified()) else {
                continue;
            };
            if let Ok(age) = now.duration_since(modified) {
                if age > cutoff {
                    debug!(path = %path.display(), "rotating old session log");
                    if let Err(e) = std::fs::remove_file(&path) {
                        warn!("Failed to remove old session log {}: {e}", path.display());
                    }
                }
            }
        }
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
        async fn wait_for_line_count(path: &std::path::Path, expected: usize) {
            for _ in 0..20 {
                if let Ok(content) = std::fs::read_to_string(path) {
                    if content.lines().count() == expected {
                        return;
                    }
                }
                tokio::time::sleep(std::time::Duration::from_millis(25)).await;
            }

            let content = std::fs::read_to_string(path).unwrap();
            assert_eq!(content.lines().count(), expected);
        }

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
        wait_for_line_count(&log_path, 1).await;
        let content = std::fs::read_to_string(&log_path).unwrap();
        assert!(content.contains("\"turn\":1"));
        assert_eq!(content.lines().count(), 1);

        // Second entry appends
        logger.log_turn(&entry);
        wait_for_line_count(&log_path, 2).await;
        let content = std::fs::read_to_string(&log_path).unwrap();
        assert_eq!(content.lines().count(), 2);
    }

    #[test]
    fn rotate_old_logs_removes_stale_files() {
        use filetime::FileTime;

        let dir = tempfile::tempdir().unwrap();
        let old_file = dir.path().join("old-session.jsonl");
        let new_file = dir.path().join("new-session.jsonl");
        let non_jsonl = dir.path().join("keep-me.txt");

        std::fs::write(&old_file, "old").unwrap();
        std::fs::write(&new_file, "new").unwrap();
        std::fs::write(&non_jsonl, "keep").unwrap();

        // Set old_file mtime to 10 days ago
        let ten_days_ago =
            std::time::SystemTime::now() - std::time::Duration::from_secs(10 * 24 * 60 * 60);
        let ft = FileTime::from_system_time(ten_days_ago);
        filetime::set_file_mtime(&old_file, ft).unwrap();

        SessionLogger::rotate_old_logs(dir.path());

        assert!(!old_file.exists(), "old .jsonl should be deleted");
        assert!(new_file.exists(), "recent .jsonl should be kept");
        assert!(non_jsonl.exists(), "non-.jsonl file should be kept");
    }
}
