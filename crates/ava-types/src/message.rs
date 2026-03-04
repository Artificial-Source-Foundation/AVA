//! Message and role types

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::tool::{ToolCall, ToolResult};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Message {
    pub id: Uuid,
    pub role: Role,
    pub content: String,
    pub timestamp: DateTime<Utc>,
    pub tool_calls: Vec<ToolCall>,
    pub tool_results: Vec<ToolResult>,
}

impl Message {
    pub fn new(role: Role, content: impl Into<String>) -> Self {
        Self {
            id: Uuid::new_v4(),
            role,
            content: content.into(),
            timestamp: Utc::now(),
            tool_calls: Vec::new(),
            tool_results: Vec::new(),
        }
    }

    pub fn with_tool_calls(mut self, tool_calls: Vec<ToolCall>) -> Self {
        self.tool_calls = tool_calls;
        self
    }

    pub fn with_tool_results(mut self, tool_results: Vec<ToolResult>) -> Self {
        self.tool_results = tool_results;
        self
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum Role {
    System,
    User,
    Assistant,
    Tool,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_message_creation() {
        let message = Message::new(Role::User, "Test message");
        assert_eq!(message.role, Role::User);
        assert_eq!(message.content, "Test message");
        assert!(message.tool_calls.is_empty());
        assert!(message.tool_results.is_empty());
    }

    #[test]
    fn test_message_with_tool_calls() {
        let tool_call = ToolCall {
            id: "call_1".to_string(),
            name: "read_file".to_string(),
            arguments: serde_json::json!({"path": "/tmp/test"}),
        };
        let message = Message::new(Role::Assistant, "I'll read that file")
            .with_tool_calls(vec![tool_call.clone()]);
        assert_eq!(message.tool_calls.len(), 1);
        assert_eq!(message.tool_calls[0], tool_call);
    }

    #[test]
    fn test_role_serialization() {
        let roles = vec![Role::System, Role::User, Role::Assistant, Role::Tool];
        for role in roles {
            let json = serde_json::to_string(&role).unwrap();
            let deserialized: Role = serde_json::from_str(&json).unwrap();
            assert_eq!(role, deserialized);
        }
    }
}
