//! Session management types

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

use crate::message::Message;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Session {
    pub id: Uuid,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub messages: Vec<Message>,
    pub metadata: serde_json::Value,
}

impl Session {
    pub fn new() -> Self {
        let now = Utc::now();
        Self {
            id: Uuid::new_v4(),
            created_at: now,
            updated_at: now,
            messages: Vec::new(),
            metadata: serde_json::json!({}),
        }
    }

    pub fn with_metadata(mut self, metadata: serde_json::Value) -> Self {
        self.metadata = metadata;
        self
    }

    pub fn add_message(&mut self, message: Message) {
        self.messages.push(message);
        self.updated_at = Utc::now();
    }
}

impl Default for Session {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::message::Role;

    #[test]
    fn test_session_creation() {
        let session = Session::new();
        assert!(!session.id.to_string().is_empty());
        assert!(session.messages.is_empty());
        assert_eq!(session.metadata, serde_json::json!({}));
    }

    #[test]
    fn test_session_with_metadata() {
        let metadata = serde_json::json!({
            "project": "AVA",
            "version": "0.1.0"
        });
        let session = Session::new().with_metadata(metadata.clone());
        assert_eq!(session.metadata, metadata);
    }

    #[test]
    fn test_session_add_message() {
        let mut session = Session::new();
        let message = Message::new(Role::User, "Hello");
        let original_updated_at = session.updated_at;

        session.add_message(message);

        assert_eq!(session.messages.len(), 1);
        assert!(session.updated_at >= original_updated_at);
    }
}
