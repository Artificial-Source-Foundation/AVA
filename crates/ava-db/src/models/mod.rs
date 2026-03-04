//! Database models for sessions and messages

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use sqlx::FromRow;

pub mod message;
pub mod session;

/// Represents a stored session
#[derive(Debug, Clone, FromRow, Serialize, Deserialize)]
pub struct SessionRecord {
    pub id: String,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub metadata: String, // JSON serialized
}

/// Represents a stored message
#[derive(Debug, Clone, FromRow, Serialize, Deserialize)]
pub struct MessageRecord {
    pub id: String,
    pub session_id: String,
    pub role: String,
    pub content: String,
    pub timestamp: DateTime<Utc>,
    pub tool_calls: Option<String>,   // JSON serialized
    pub tool_results: Option<String>, // JSON serialized
}

impl SessionRecord {
    /// Create a new session record
    pub fn new(id: impl Into<String>) -> Self {
        let now = Utc::now();
        Self {
            id: id.into(),
            created_at: now,
            updated_at: now,
            metadata: "{}".to_string(),
        }
    }

    /// Set metadata as JSON
    pub fn with_metadata(mut self, metadata: impl Serialize) -> ava_types::Result<Self> {
        self.metadata = serde_json::to_string(&metadata)
            .map_err(|e| ava_types::AvaError::SerializationError(e.to_string()))?;
        Ok(self)
    }

    /// Get metadata as typed struct
    pub fn parse_metadata<T: for<'de> Deserialize<'de>>(&self) -> ava_types::Result<T> {
        serde_json::from_str(&self.metadata)
            .map_err(|e| ava_types::AvaError::SerializationError(e.to_string()))
    }
}

impl MessageRecord {
    /// Create a new message record
    pub fn new(
        id: impl Into<String>,
        session_id: impl Into<String>,
        role: impl Into<String>,
        content: impl Into<String>,
    ) -> Self {
        Self {
            id: id.into(),
            session_id: session_id.into(),
            role: role.into(),
            content: content.into(),
            timestamp: Utc::now(),
            tool_calls: None,
            tool_results: None,
        }
    }

    /// Set tool calls as JSON
    pub fn with_tool_calls(mut self, tool_calls: impl Serialize) -> ava_types::Result<Self> {
        self.tool_calls = Some(
            serde_json::to_string(&tool_calls)
                .map_err(|e| ava_types::AvaError::SerializationError(e.to_string()))?,
        );
        Ok(self)
    }

    /// Set tool results as JSON
    pub fn with_tool_results(mut self, tool_results: impl Serialize) -> ava_types::Result<Self> {
        self.tool_results = Some(
            serde_json::to_string(&tool_results)
                .map_err(|e| ava_types::AvaError::SerializationError(e.to_string()))?,
        );
        Ok(self)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_session_record_creation() {
        let session = SessionRecord::new("test-id");
        assert_eq!(session.id, "test-id");
        assert_eq!(session.metadata, "{}");
    }

    #[test]
    fn test_session_with_metadata() {
        #[derive(Serialize, Deserialize, Debug, PartialEq)]
        struct Meta {
            project: String,
        }

        let session = SessionRecord::new("test-id")
            .with_metadata(Meta {
                project: "AVA".to_string(),
            })
            .unwrap();

        let meta: Meta = session.parse_metadata().unwrap();
        assert_eq!(meta.project, "AVA");
    }

    #[test]
    fn test_message_record_creation() {
        let msg = MessageRecord::new("msg-1", "session-1", "user", "Hello");
        assert_eq!(msg.id, "msg-1");
        assert_eq!(msg.session_id, "session-1");
        assert_eq!(msg.role, "user");
        assert_eq!(msg.content, "Hello");
    }
}
