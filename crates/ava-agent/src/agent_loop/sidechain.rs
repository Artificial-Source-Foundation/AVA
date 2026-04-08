//! Sidechain transcripts — JSONL recording of sub-agent conversations.
//!
//! When an agent has an `agent_id` (sub-agents or delegated workers), each message
//! is recorded to `.ava/sessions/{session_id}/agents/{agent_id}.jsonl`.
//! The main session uses the existing session persistence and does not
//! create a sidechain transcript.

use std::fs;
use std::io::{BufWriter, Write};
use std::path::{Path, PathBuf};

use ava_types::Message;
use chrono::Utc;
use serde::Serialize;
use tracing::debug;

/// A single JSONL record in the sidechain transcript.
#[derive(Debug, Serialize)]
struct TranscriptRecord {
    timestamp: String,
    agent_id: String,
    session_id: String,
    parent_id: Option<String>,
    role: String,
    content: String,
}

/// JSONL transcript writer for a sub-agent's conversation.
pub struct SidechainTranscript {
    session_id: String,
    agent_id: String,
    writer: BufWriter<fs::File>,
    path: PathBuf,
}

impl SidechainTranscript {
    /// Create a new sidechain transcript writer.
    ///
    /// Creates the directory structure `.ava/sessions/{session_id}/agents/`
    /// and opens `{agent_id}.jsonl` for appending.
    pub fn new(session_id: &str, agent_id: &str) -> std::io::Result<Self> {
        let base = Self::base_path(session_id);
        fs::create_dir_all(&base)?;

        let path = base.join(format!("{agent_id}.jsonl"));
        let file = fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path)?;

        debug!(
            session_id,
            agent_id,
            path = %path.display(),
            "sidechain transcript opened"
        );

        Ok(Self {
            session_id: session_id.to_string(),
            agent_id: agent_id.to_string(),
            writer: BufWriter::new(file),
            path,
        })
    }

    /// Record a message to the JSONL transcript.
    pub fn record(&mut self, message: &Message) -> std::io::Result<()> {
        let role = match message.role {
            ava_types::Role::System => "system",
            ava_types::Role::User => "user",
            ava_types::Role::Assistant => "assistant",
            ava_types::Role::Tool => "tool",
        };

        let record = TranscriptRecord {
            timestamp: Utc::now().to_rfc3339(),
            agent_id: self.agent_id.clone(),
            session_id: self.session_id.clone(),
            parent_id: message.parent_id.map(|id| id.to_string()),
            role: role.to_string(),
            content: message.content.clone(),
        };

        let line = serde_json::to_string(&record)
            .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e.to_string()))?;
        writeln!(self.writer, "{line}")?;
        self.writer.flush()?;

        Ok(())
    }

    /// Path to the transcript file.
    pub fn path(&self) -> &Path {
        &self.path
    }

    /// Base directory for a session's agent transcripts.
    fn base_path(session_id: &str) -> PathBuf {
        PathBuf::from(".ava")
            .join("sessions")
            .join(session_id)
            .join("agents")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_types::{Message, Role};
    use std::io::BufRead;
    use tempfile::TempDir;

    /// Helper that creates a SidechainTranscript in a temp directory.
    fn make_transcript(dir: &TempDir) -> SidechainTranscript {
        // Override the base path by constructing the transcript manually
        let base = dir.path().join("agents");
        fs::create_dir_all(&base).unwrap();
        let path = base.join("worker-1.jsonl");
        let file = fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path)
            .unwrap();
        SidechainTranscript {
            session_id: "test-session".to_string(),
            agent_id: "worker-1".to_string(),
            writer: BufWriter::new(file),
            path,
        }
    }

    #[test]
    fn creates_file_and_records_messages() {
        let dir = TempDir::new().unwrap();
        let mut transcript = make_transcript(&dir);

        let msg = Message::new(Role::User, "hello agent".to_string());
        transcript.record(&msg).unwrap();

        let msg2 = Message::new(Role::Assistant, "hello user".to_string());
        transcript.record(&msg2).unwrap();

        // Read back and verify JSONL
        let file = fs::File::open(transcript.path()).unwrap();
        let lines: Vec<String> = std::io::BufReader::new(file)
            .lines()
            .collect::<Result<Vec<_>, _>>()
            .unwrap();

        assert_eq!(lines.len(), 2);

        let record: serde_json::Value = serde_json::from_str(&lines[0]).unwrap();
        assert_eq!(record["role"], "user");
        assert_eq!(record["content"], "hello agent");
        assert_eq!(record["agent_id"], "worker-1");
        assert_eq!(record["session_id"], "test-session");
        assert!(record["timestamp"].as_str().unwrap().contains("T"));

        let record2: serde_json::Value = serde_json::from_str(&lines[1]).unwrap();
        assert_eq!(record2["role"], "assistant");
        assert_eq!(record2["content"], "hello user");
    }

    #[test]
    fn records_tool_messages() {
        let dir = TempDir::new().unwrap();
        let mut transcript = make_transcript(&dir);

        let msg = Message::new(Role::Tool, "file contents here".to_string());
        transcript.record(&msg).unwrap();

        let file = fs::File::open(transcript.path()).unwrap();
        let line = std::io::BufReader::new(file)
            .lines()
            .next()
            .unwrap()
            .unwrap();
        let record: serde_json::Value = serde_json::from_str(&line).unwrap();
        assert_eq!(record["role"], "tool");
    }

    #[test]
    fn respects_agent_id() {
        let dir = TempDir::new().unwrap();
        let base = dir.path().join("agents");
        fs::create_dir_all(&base).unwrap();

        // Create two transcripts with different agent IDs
        let path_a = base.join("agent-a.jsonl");
        let file_a = fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path_a)
            .unwrap();
        let mut t_a = SidechainTranscript {
            session_id: "sess".to_string(),
            agent_id: "agent-a".to_string(),
            writer: BufWriter::new(file_a),
            path: path_a.clone(),
        };

        let path_b = base.join("agent-b.jsonl");
        let file_b = fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path_b)
            .unwrap();
        let mut t_b = SidechainTranscript {
            session_id: "sess".to_string(),
            agent_id: "agent-b".to_string(),
            writer: BufWriter::new(file_b),
            path: path_b.clone(),
        };

        t_a.record(&Message::new(Role::User, "msg-a".to_string()))
            .unwrap();
        t_b.record(&Message::new(Role::User, "msg-b".to_string()))
            .unwrap();

        // Verify separate files
        let lines_a: Vec<String> = std::io::BufReader::new(fs::File::open(&path_a).unwrap())
            .lines()
            .collect::<Result<Vec<_>, _>>()
            .unwrap();
        let lines_b: Vec<String> = std::io::BufReader::new(fs::File::open(&path_b).unwrap())
            .lines()
            .collect::<Result<Vec<_>, _>>()
            .unwrap();

        assert_eq!(lines_a.len(), 1);
        assert_eq!(lines_b.len(), 1);

        let rec_a: serde_json::Value = serde_json::from_str(&lines_a[0]).unwrap();
        let rec_b: serde_json::Value = serde_json::from_str(&lines_b[0]).unwrap();
        assert_eq!(rec_a["agent_id"], "agent-a");
        assert_eq!(rec_b["agent_id"], "agent-b");
    }
}
