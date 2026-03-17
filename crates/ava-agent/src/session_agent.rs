//! Spawn agent with session continuity (BG2-33, inspired by Zed).
//!
//! Sub-agents with persistent session IDs that support follow-up messages
//! without re-sending the full context.

use std::collections::HashMap;
use std::sync::Arc;

use ava_types::{Message, Role};
use tokio::sync::RwLock;
use uuid::Uuid;

/// A tracked sub-agent session.
#[derive(Debug, Clone)]
pub struct AgentSession {
    pub session_id: Uuid,
    /// Accumulated messages from this sub-agent.
    pub messages: Vec<Message>,
    /// The goal/task this session was created for.
    pub goal: String,
    /// Whether this session is still active.
    pub active: bool,
    pub created_at: chrono::DateTime<chrono::Utc>,
    pub last_active: chrono::DateTime<chrono::Utc>,
}

/// Registry of active sub-agent sessions for follow-up without full context replay.
pub struct SessionRegistry {
    sessions: Arc<RwLock<HashMap<Uuid, AgentSession>>>,
}

impl Default for SessionRegistry {
    fn default() -> Self {
        Self::new()
    }
}

impl SessionRegistry {
    pub fn new() -> Self {
        Self {
            sessions: Arc::new(RwLock::new(HashMap::new())),
        }
    }

    /// Create a new sub-agent session.
    pub async fn create(&self, goal: &str) -> Uuid {
        let session_id = Uuid::new_v4();
        let now = chrono::Utc::now();
        let session = AgentSession {
            session_id,
            messages: Vec::new(),
            goal: goal.to_string(),
            active: true,
            created_at: now,
            last_active: now,
        };
        self.sessions.write().await.insert(session_id, session);
        session_id
    }

    /// Add messages to an existing session.
    pub async fn append_messages(&self, session_id: Uuid, messages: &[Message]) {
        if let Some(session) = self.sessions.write().await.get_mut(&session_id) {
            session.messages.extend_from_slice(messages);
            session.last_active = chrono::Utc::now();
        }
    }

    /// Get the history for a session (for follow-up messages).
    pub async fn get_history(&self, session_id: Uuid) -> Option<Vec<Message>> {
        self.sessions
            .read()
            .await
            .get(&session_id)
            .map(|s| s.messages.clone())
    }

    /// Mark a session as completed.
    pub async fn complete(&self, session_id: Uuid) {
        if let Some(session) = self.sessions.write().await.get_mut(&session_id) {
            session.active = false;
        }
    }

    /// List active sessions.
    pub async fn list_active(&self) -> Vec<AgentSession> {
        self.sessions
            .read()
            .await
            .values()
            .filter(|s| s.active)
            .cloned()
            .collect()
    }

    /// Send a follow-up message to an existing session.
    /// Returns the session's history with the new message appended.
    pub async fn follow_up(&self, session_id: Uuid, message: &str) -> Option<Vec<Message>> {
        let mut sessions = self.sessions.write().await;
        let session = sessions.get_mut(&session_id)?;

        let msg = Message::new(Role::User, message);
        session.messages.push(msg);
        session.last_active = chrono::Utc::now();
        session.active = true;

        Some(session.messages.clone())
    }

    /// Clean up old inactive sessions (older than max_age).
    pub async fn cleanup(&self, max_age: chrono::Duration) {
        let cutoff = chrono::Utc::now() - max_age;
        self.sessions
            .write()
            .await
            .retain(|_, s| s.active || s.last_active > cutoff);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn create_and_get_session() {
        let registry = SessionRegistry::new();
        let id = registry.create("fix the bug").await;

        let history = registry.get_history(id).await.unwrap();
        assert!(history.is_empty());
    }

    #[tokio::test]
    async fn append_and_retrieve_messages() {
        let registry = SessionRegistry::new();
        let id = registry.create("task").await;

        let msgs = vec![
            Message::new(Role::User, "hello"),
            Message::new(Role::Assistant, "hi"),
        ];
        registry.append_messages(id, &msgs).await;

        let history = registry.get_history(id).await.unwrap();
        assert_eq!(history.len(), 2);
        assert_eq!(history[0].content, "hello");
    }

    #[tokio::test]
    async fn follow_up_appends_to_history() {
        let registry = SessionRegistry::new();
        let id = registry.create("task").await;

        registry
            .append_messages(
                id,
                &[
                    Message::new(Role::User, "fix bug"),
                    Message::new(Role::Assistant, "done"),
                ],
            )
            .await;

        registry.complete(id).await;

        let history = registry.follow_up(id, "also add tests").await.unwrap();
        assert_eq!(history.len(), 3);
        assert_eq!(history[2].content, "also add tests");
    }

    #[tokio::test]
    async fn list_active_filters_completed() {
        let registry = SessionRegistry::new();
        let id1 = registry.create("task1").await;
        let id2 = registry.create("task2").await;

        registry.complete(id1).await;

        let active = registry.list_active().await;
        assert_eq!(active.len(), 1);
        assert_eq!(active[0].session_id, id2);
    }

    #[tokio::test]
    async fn cleanup_removes_old_inactive() {
        let registry = SessionRegistry::new();
        let id = registry.create("old task").await;
        registry.complete(id).await;

        // Cleanup with zero max age should remove completed sessions
        registry.cleanup(chrono::Duration::zero()).await;

        assert!(registry.get_history(id).await.is_none());
    }

    #[tokio::test]
    async fn nonexistent_session_returns_none() {
        let registry = SessionRegistry::new();
        assert!(registry.get_history(Uuid::new_v4()).await.is_none());
        assert!(registry.follow_up(Uuid::new_v4(), "test").await.is_none());
    }
}
