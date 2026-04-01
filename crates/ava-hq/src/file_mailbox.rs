//! File-based mailbox — persistent inter-agent messaging via JSON files.
//!
//! Each agent gets an inbox file at `.ava/hq/{session_id}/inboxes/{agent_name}.json`.
//! Messages are atomically appended using write-to-temp-then-rename to prevent
//! corruption from concurrent writes.

use std::fs;
use std::path::{Path, PathBuf};

use fs2::FileExt;
use serde::{Deserialize, Serialize};
use tracing::debug;

/// A message between HQ agents (leads, workers, director).
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct MailboxMessage {
    /// Sender agent name.
    pub from: String,
    /// Message text.
    pub text: String,
    /// ISO 8601 timestamp.
    pub timestamp: String,
    /// Whether this message has been read by the recipient.
    pub read: bool,
    /// Optional sender color for UI display.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub color: Option<String>,
}

impl MailboxMessage {
    /// Create a new unread message with the current timestamp.
    pub fn new(from: impl Into<String>, text: impl Into<String>) -> Self {
        Self {
            from: from.into(),
            text: text.into(),
            timestamp: chrono::Utc::now().to_rfc3339(),
            read: false,
            color: None,
        }
    }

    /// Attach a color to this message for sender identification.
    pub fn with_color(mut self, color: impl Into<String>) -> Self {
        self.color = Some(color.into());
        self
    }
}

/// Inbox file wrapper containing a list of messages.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
struct InboxFile {
    messages: Vec<MailboxMessage>,
}

/// Send a message to the named agent's inbox.
///
/// Uses advisory file locking (fs2) to serialize concurrent read-modify-write
/// operations, then atomic write (temp + rename) to prevent corruption.
pub fn send(mailbox_dir: &Path, to: &str, message: MailboxMessage) -> std::io::Result<()> {
    fs::create_dir_all(mailbox_dir)?;

    let inbox_path = inbox_path(mailbox_dir, to);
    let lock_path = lock_path(mailbox_dir, to);

    // Acquire exclusive lock for the read-modify-write cycle
    let lock_file = fs::OpenOptions::new()
        .create(true)
        .write(true)
        .truncate(false)
        .open(&lock_path)?;
    lock_file.lock_exclusive()?;

    let result = (|| {
        let mut inbox = read_inbox_file(&inbox_path)?;
        inbox.messages.push(message);
        write_inbox_atomic(mailbox_dir, &inbox_path, &inbox)?;
        debug!(to, messages = inbox.messages.len(), "mailbox: message sent");
        Ok(())
    })();

    lock_file.unlock()?;
    result
}

/// Receive all unread messages for the named agent, marking them as read.
///
/// Returns the previously-unread messages. After this call, all messages
/// in the inbox are marked as read.
pub fn receive(mailbox_dir: &Path, agent_name: &str) -> std::io::Result<Vec<MailboxMessage>> {
    let inbox_path = inbox_path(mailbox_dir, agent_name);

    if !inbox_path.exists() {
        return Ok(Vec::new());
    }

    let lock_path = lock_path(mailbox_dir, agent_name);
    let lock_file = fs::OpenOptions::new()
        .create(true)
        .write(true)
        .truncate(false)
        .open(&lock_path)?;
    lock_file.lock_exclusive()?;

    let result = (|| {
        let mut inbox = read_inbox_file(&inbox_path)?;
        let unread: Vec<MailboxMessage> =
            inbox.messages.iter().filter(|m| !m.read).cloned().collect();

        if unread.is_empty() {
            return Ok(Vec::new());
        }

        // Mark all as read
        for msg in &mut inbox.messages {
            msg.read = true;
        }

        write_inbox_atomic(mailbox_dir, &inbox_path, &inbox)?;

        debug!(
            agent_name,
            unread = unread.len(),
            "mailbox: messages received"
        );
        Ok(unread)
    })();

    lock_file.unlock()?;
    result
}

/// Check if the named agent has unread messages without consuming them.
pub fn has_unread(mailbox_dir: &Path, agent_name: &str) -> std::io::Result<bool> {
    let inbox_path = inbox_path(mailbox_dir, agent_name);

    if !inbox_path.exists() {
        return Ok(false);
    }

    let inbox = read_inbox_file(&inbox_path)?;
    Ok(inbox.messages.iter().any(|m| !m.read))
}

/// Build the lock file path for a given agent's inbox.
fn lock_path(mailbox_dir: &Path, agent_name: &str) -> PathBuf {
    let safe_name: String = agent_name
        .chars()
        .map(|c| {
            if c.is_alphanumeric() || c == '-' || c == '_' {
                c
            } else {
                '_'
            }
        })
        .collect();
    mailbox_dir.join(format!(".{safe_name}.lock"))
}

/// Build the inbox file path for a given agent.
fn inbox_path(mailbox_dir: &Path, agent_name: &str) -> PathBuf {
    // Sanitize agent name for filesystem safety
    let safe_name: String = agent_name
        .chars()
        .map(|c| {
            if c.is_alphanumeric() || c == '-' || c == '_' {
                c
            } else {
                '_'
            }
        })
        .collect();
    mailbox_dir.join(format!("{safe_name}.json"))
}

/// Read an inbox file, returning an empty inbox if it doesn't exist.
fn read_inbox_file(path: &Path) -> std::io::Result<InboxFile> {
    if !path.exists() {
        return Ok(InboxFile::default());
    }
    let content = fs::read_to_string(path)?;
    serde_json::from_str(&content).map_err(|e| {
        std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            format!("corrupt inbox file {}: {e}", path.display()),
        )
    })
}

/// Write inbox to a temp file and rename atomically.
fn write_inbox_atomic(parent_dir: &Path, target: &Path, inbox: &InboxFile) -> std::io::Result<()> {
    let content = serde_json::to_string_pretty(inbox)
        .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e.to_string()))?;

    // Write to temp file in the same directory (ensures same filesystem for rename)
    let temp_path = parent_dir.join(format!(".tmp_{}", uuid::Uuid::new_v4()));
    fs::write(&temp_path, content)?;

    // Atomic rename
    fs::rename(&temp_path, target).or_else(|_| {
        // Fallback: if rename fails (cross-device), copy + remove
        let data = fs::read(&temp_path)?;
        fs::write(target, data)?;
        fs::remove_file(&temp_path)
    })
}

/// Build the mailbox directory path for a session.
pub fn mailbox_dir_for_session(session_id: &str) -> PathBuf {
    PathBuf::from(".ava")
        .join("hq")
        .join(session_id)
        .join("inboxes")
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    fn make_dir() -> TempDir {
        TempDir::new().unwrap()
    }

    #[test]
    fn send_receive_roundtrip() {
        let dir = make_dir();
        let mailbox = dir.path();

        let msg = MailboxMessage::new("Backend Lead", "task completed");
        send(mailbox, "QA Lead", msg).unwrap();

        let received = receive(mailbox, "QA Lead").unwrap();
        assert_eq!(received.len(), 1);
        assert_eq!(received[0].from, "Backend Lead");
        assert_eq!(received[0].text, "task completed");
    }

    #[test]
    fn multiple_messages_preserved() {
        let dir = make_dir();
        let mailbox = dir.path();

        send(mailbox, "worker-1", MailboxMessage::new("lead", "first")).unwrap();
        send(mailbox, "worker-1", MailboxMessage::new("lead", "second")).unwrap();
        send(
            mailbox,
            "worker-1",
            MailboxMessage::new("director", "third"),
        )
        .unwrap();

        let received = receive(mailbox, "worker-1").unwrap();
        assert_eq!(received.len(), 3);
        assert_eq!(received[0].text, "first");
        assert_eq!(received[1].text, "second");
        assert_eq!(received[2].text, "third");
    }

    #[test]
    fn unread_detection() {
        let dir = make_dir();
        let mailbox = dir.path();

        assert!(!has_unread(mailbox, "agent-a").unwrap());

        send(mailbox, "agent-a", MailboxMessage::new("b", "hello")).unwrap();
        assert!(has_unread(mailbox, "agent-a").unwrap());

        // Reading should mark as read
        let _ = receive(mailbox, "agent-a").unwrap();
        assert!(!has_unread(mailbox, "agent-a").unwrap());
    }

    #[test]
    fn receive_returns_empty_for_unknown_agent() {
        let dir = make_dir();
        let received = receive(dir.path(), "nonexistent").unwrap();
        assert!(received.is_empty());
    }

    #[test]
    fn already_read_messages_not_returned_again() {
        let dir = make_dir();
        let mailbox = dir.path();

        send(mailbox, "agent", MailboxMessage::new("a", "msg1")).unwrap();
        let first = receive(mailbox, "agent").unwrap();
        assert_eq!(first.len(), 1);

        // Second receive should return empty (already read)
        let second = receive(mailbox, "agent").unwrap();
        assert!(second.is_empty());

        // New message should show up
        send(mailbox, "agent", MailboxMessage::new("b", "msg2")).unwrap();
        let third = receive(mailbox, "agent").unwrap();
        assert_eq!(third.len(), 1);
        assert_eq!(third[0].text, "msg2");
    }

    #[test]
    fn message_color_preserved() {
        let dir = make_dir();
        let mailbox = dir.path();

        let msg = MailboxMessage::new("lead", "styled message").with_color("blue");
        send(mailbox, "worker", msg).unwrap();

        let received = receive(mailbox, "worker").unwrap();
        assert_eq!(received[0].color, Some("blue".to_string()));
    }

    #[test]
    fn file_locking_concurrent_writes() {
        let dir = make_dir();
        let mailbox_path = dir.path().to_path_buf();

        // Simulate concurrent sends from multiple threads
        let handles: Vec<_> = (0..10)
            .map(|i| {
                let path = mailbox_path.clone();
                std::thread::spawn(move || {
                    let msg = MailboxMessage::new(format!("sender-{i}"), format!("msg-{i}"));
                    send(&path, "shared-inbox", msg).unwrap();
                })
            })
            .collect();

        for h in handles {
            h.join().unwrap();
        }

        // All 10 messages should be present
        let received = receive(&mailbox_path, "shared-inbox").unwrap();
        assert_eq!(received.len(), 10);
    }

    #[test]
    fn sanitized_agent_names() {
        let dir = make_dir();
        let mailbox = dir.path();

        // Agent name with spaces and special chars
        send(
            mailbox,
            "Backend Lead (QA)",
            MailboxMessage::new("director", "test"),
        )
        .unwrap();

        let received = receive(mailbox, "Backend Lead (QA)").unwrap();
        assert_eq!(received.len(), 1);
    }
}
