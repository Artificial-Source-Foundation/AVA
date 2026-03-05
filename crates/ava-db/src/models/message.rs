//! Message model and repository operations.

use sqlx::{FromRow, SqlitePool};

#[derive(Debug, Clone, PartialEq, Eq, FromRow)]
pub struct MessageRecord {
    pub id: String,
    pub session_id: String,
    pub role: String,
    pub content: String,
    pub tool_calls_json: Option<String>,
    pub created_at: String,
}

#[derive(Debug, Clone)]
pub struct MessageRepository {
    pool: SqlitePool,
}

impl MessageRepository {
    pub fn new(pool: SqlitePool) -> Self {
        Self { pool }
    }

    pub async fn insert(&self, record: &MessageRecord) -> Result<(), sqlx::Error> {
        sqlx::query(
            "INSERT INTO messages (id, session_id, role, content, tool_calls_json, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
        )
        .bind(&record.id)
        .bind(&record.session_id)
        .bind(&record.role)
        .bind(&record.content)
        .bind(&record.tool_calls_json)
        .bind(&record.created_at)
        .execute(&self.pool)
        .await?;

        Ok(())
    }

    pub async fn get_by_id(&self, id: &str) -> Result<Option<MessageRecord>, sqlx::Error> {
        sqlx::query_as::<_, MessageRecord>(
            "SELECT id, session_id, role, content, tool_calls_json, created_at FROM messages WHERE id = ?1",
        )
        .bind(id)
        .fetch_optional(&self.pool)
        .await
    }

    pub async fn list_by_session(&self, session_id: &str) -> Result<Vec<MessageRecord>, sqlx::Error> {
        sqlx::query_as::<_, MessageRecord>(
            "SELECT id, session_id, role, content, tool_calls_json, created_at FROM messages WHERE session_id = ?1 ORDER BY created_at ASC",
        )
        .bind(session_id)
        .fetch_all(&self.pool)
        .await
    }

    pub async fn delete_by_session(&self, session_id: &str) -> Result<u64, sqlx::Error> {
        let result = sqlx::query("DELETE FROM messages WHERE session_id = ?1")
            .bind(session_id)
            .execute(&self.pool)
            .await?;

        Ok(result.rows_affected())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::create_test_db;
    use crate::models::session::{SessionRecord, SessionRepository};

    #[tokio::test]
    async fn test_insert_get_and_list_by_session() {
        let db = create_test_db().await.expect("test db should initialize");
        let sessions = SessionRepository::new(db.pool().clone());
        let messages = MessageRepository::new(db.pool().clone());

        let session = SessionRecord {
            id: "test-session".to_string(),
            title: "test".to_string(),
            created_at: "2026-01-01T00:00:00Z".to_string(),
            updated_at: "2026-01-01T00:00:00Z".to_string(),
            metadata_json: None,
        };
        sessions
            .create(&session)
            .await
            .expect("session should be inserted");

        let first = MessageRecord {
            id: "m-1".to_string(),
            session_id: session.id.clone(),
            role: "user".to_string(),
            content: "hello".to_string(),
            tool_calls_json: None,
            created_at: "2026-01-01T00:00:00Z".to_string(),
        };
        messages
            .insert(&first)
            .await
            .expect("message should be inserted");

        let loaded = messages
            .get_by_id("m-1")
            .await
            .expect("query should succeed")
            .expect("message should exist");
        assert_eq!(loaded.content, "hello");

        let listed = messages
            .list_by_session("test-session")
            .await
            .expect("list should succeed");
        assert_eq!(listed.len(), 1);
    }

    #[tokio::test]
    async fn test_delete_messages_by_session() {
        let db = create_test_db().await.expect("test db should initialize");
        let sessions = SessionRepository::new(db.pool().clone());
        let messages = MessageRepository::new(db.pool().clone());

        sessions
            .create(&SessionRecord {
                id: "delete-session".to_string(),
                title: "delete test".to_string(),
                created_at: "2026-01-01T00:00:00Z".to_string(),
                updated_at: "2026-01-01T00:00:00Z".to_string(),
                metadata_json: None,
            })
            .await
            .expect("session should be inserted");

        messages
            .insert(&MessageRecord {
                id: "msg-1".to_string(),
                session_id: "delete-session".to_string(),
                role: "user".to_string(),
                content: "Hello".to_string(),
                tool_calls_json: None,
                created_at: "2026-01-01T00:00:00Z".to_string(),
            })
            .await
            .expect("message should be inserted");

        let deleted = messages
            .delete_by_session("delete-session")
            .await
            .expect("delete should succeed");

        assert_eq!(deleted, 1);

        let remaining = messages
            .list_by_session("delete-session")
            .await
            .expect("list should succeed");
        assert!(remaining.is_empty());
    }
}
