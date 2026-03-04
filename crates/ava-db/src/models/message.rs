//! Message repository operations

use ava_types::Result;
use sqlx::{Executor, Sqlite};

use crate::models::MessageRecord;

/// Repository for message operations
pub struct MessageRepository;

impl MessageRepository {
    /// Save a message to the database
    pub async fn save<'e, E>(executor: E, message: &MessageRecord) -> Result<()>
    where
        E: Executor<'e, Database = Sqlite>,
    {
        sqlx::query(
            r#"
            INSERT INTO messages (id, session_id, role, content, timestamp, tool_calls, tool_results)
            VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
            ON CONFLICT(id) DO UPDATE SET
                content = excluded.content,
                tool_calls = excluded.tool_calls,
                tool_results = excluded.tool_results
            "#,
        )
        .bind(&message.id)
        .bind(&message.session_id)
        .bind(&message.role)
        .bind(&message.content)
        .bind(message.timestamp)
        .bind(&message.tool_calls)
        .bind(&message.tool_results)
        .execute(executor)
        .await
        .map_err(|e| ava_types::AvaError::DatabaseError(e.to_string()))?;

        Ok(())
    }

    /// Load messages for a session
    pub async fn load_by_session<'e, E>(executor: E, session_id: &str) -> Result<Vec<MessageRecord>>
    where
        E: Executor<'e, Database = Sqlite>,
    {
        let messages = sqlx::query_as::<_, MessageRecord>(
            r#"
            SELECT id, session_id, role, content, timestamp, tool_calls, tool_results
            FROM messages
            WHERE session_id = ?1
            ORDER BY timestamp ASC
            "#,
        )
        .bind(session_id)
        .fetch_all(executor)
        .await
        .map_err(|e| ava_types::AvaError::DatabaseError(e.to_string()))?;

        Ok(messages)
    }

    /// Delete messages for a session
    pub async fn delete_by_session<'e, E>(executor: E, session_id: &str) -> Result<u64>
    where
        E: Executor<'e, Database = Sqlite>,
    {
        let result = sqlx::query("DELETE FROM messages WHERE session_id = ?1")
            .bind(session_id)
            .execute(executor)
            .await
            .map_err(|e| ava_types::AvaError::DatabaseError(e.to_string()))?;

        Ok(result.rows_affected())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::create_test_db;
    use crate::models::session::SessionRepository;
    use crate::models::SessionRecord;

    #[tokio::test]
    async fn test_save_and_load_messages() {
        let db = create_test_db().await.unwrap();

        // Create a session first
        let session = SessionRecord::new("test-session");
        SessionRepository::save(db.pool(), &session).await.unwrap();

        // Add messages
        for i in 0..3 {
            let msg = MessageRecord::new(
                format!("msg-{}", i),
                "test-session",
                if i % 2 == 0 { "user" } else { "assistant" },
                format!("Message {}", i),
            );
            MessageRepository::save(db.pool(), &msg).await.unwrap();
        }

        let messages = MessageRepository::load_by_session(db.pool(), "test-session")
            .await
            .unwrap();

        assert_eq!(messages.len(), 3);
    }

    #[tokio::test]
    async fn test_delete_messages_by_session() {
        let db = create_test_db().await.unwrap();

        let session = SessionRecord::new("delete-session");
        SessionRepository::save(db.pool(), &session).await.unwrap();

        let msg = MessageRecord::new("msg-1", "delete-session", "user", "Hello");
        MessageRepository::save(db.pool(), &msg).await.unwrap();

        let deleted = MessageRepository::delete_by_session(db.pool(), "delete-session")
            .await
            .unwrap();

        assert_eq!(deleted, 1);

        let messages = MessageRepository::load_by_session(db.pool(), "delete-session")
            .await
            .unwrap();
        assert!(messages.is_empty());
    }
}
