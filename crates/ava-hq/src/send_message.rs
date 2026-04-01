//! SendMessageTool — allows HQ agents to send messages to each other.
//!
//! Implements the `Tool` trait so agents can use `send_message` as a regular
//! tool call. Messages are delivered via the file-based mailbox system.

use std::path::PathBuf;
use std::sync::Arc;

use async_trait::async_trait;
use ava_tools::registry::Tool;
use ava_types::{Result as AvaResult, ToolResult};
use serde_json::Value;
use tokio::sync::RwLock;

use crate::file_mailbox::{self, MailboxMessage};

/// Tool that sends a message to another HQ agent's inbox.
pub struct SendMessageTool {
    /// The name of the sending agent (set when the tool is created for a worker/lead).
    sender_name: String,
    /// Path to the mailbox directory for the current session.
    mailbox_dir: Arc<RwLock<PathBuf>>,
    /// Optional sender color for visual identification.
    sender_color: Option<String>,
}

impl SendMessageTool {
    /// Create a new `send_message` tool for a specific agent.
    pub fn new(sender_name: impl Into<String>, mailbox_dir: PathBuf) -> Self {
        Self {
            sender_name: sender_name.into(),
            mailbox_dir: Arc::new(RwLock::new(mailbox_dir)),
            sender_color: None,
        }
    }

    /// Attach a sender color for message identification.
    pub fn with_color(mut self, color: impl Into<String>) -> Self {
        self.sender_color = Some(color.into());
        self
    }
}

#[async_trait]
impl Tool for SendMessageTool {
    fn name(&self) -> &str {
        "send_message"
    }

    fn description(&self) -> &str {
        "Send a message to another HQ agent (lead or worker). Use this to coordinate work, \
         report status, request reviews, or communicate blockers."
    }

    fn parameters(&self) -> Value {
        serde_json::json!({
            "type": "object",
            "properties": {
                "to": {
                    "type": "string",
                    "description": "Name of the recipient agent (e.g., 'Backend Lead', 'QA Lead', 'Pedro')"
                },
                "message": {
                    "type": "string",
                    "description": "The message text to send"
                }
            },
            "required": ["to", "message"]
        })
    }

    async fn execute(&self, args: Value) -> AvaResult<ToolResult> {
        let to = args
            .get("to")
            .and_then(Value::as_str)
            .ok_or_else(|| ava_types::AvaError::ToolError("missing 'to' parameter".to_string()))?;

        let message_text = args.get("message").and_then(Value::as_str).ok_or_else(|| {
            ava_types::AvaError::ToolError("missing 'message' parameter".to_string())
        })?;

        let mut msg = MailboxMessage::new(&self.sender_name, message_text);
        if let Some(ref color) = self.sender_color {
            msg = msg.with_color(color.clone());
        }

        let dir = self.mailbox_dir.read().await;
        file_mailbox::send(&dir, to, msg)
            .map_err(|e| ava_types::AvaError::ToolError(format!("failed to send message: {e}")))?;

        Ok(ToolResult {
            call_id: String::new(),
            content: format!("Message sent to {to} from {}", self.sender_name),
            is_error: false,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[tokio::test]
    async fn send_message_tool_delivers() {
        let dir = TempDir::new().unwrap();
        let tool = SendMessageTool::new("Backend Lead", dir.path().to_path_buf());

        let result = tool
            .execute(serde_json::json!({
                "to": "QA Lead",
                "message": "Backend is ready for review"
            }))
            .await
            .unwrap();

        assert!(!result.is_error);
        assert!(result.content.contains("QA Lead"));

        // Verify delivery
        let received = file_mailbox::receive(dir.path(), "QA Lead").unwrap();
        assert_eq!(received.len(), 1);
        assert_eq!(received[0].from, "Backend Lead");
        assert_eq!(received[0].text, "Backend is ready for review");
    }

    #[tokio::test]
    async fn send_message_with_color() {
        let dir = TempDir::new().unwrap();
        let tool = SendMessageTool::new("Lead", dir.path().to_path_buf()).with_color("blue");

        tool.execute(serde_json::json!({
            "to": "Worker",
            "message": "task assigned"
        }))
        .await
        .unwrap();

        let received = file_mailbox::receive(dir.path(), "Worker").unwrap();
        assert_eq!(received[0].color, Some("blue".to_string()));
    }

    #[tokio::test]
    async fn missing_params_returns_error() {
        let dir = TempDir::new().unwrap();
        let tool = SendMessageTool::new("Lead", dir.path().to_path_buf());

        let result = tool.execute(serde_json::json!({"to": "Worker"})).await;
        assert!(result.is_err());

        let result = tool.execute(serde_json::json!({"message": "hello"})).await;
        assert!(result.is_err());
    }
}
