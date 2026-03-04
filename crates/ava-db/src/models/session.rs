//! Session repository operations

use ava_types::Result;
use sqlx::{Executor, Sqlite};

use crate::models::SessionRecord;

/// Repository for session operations
pub struct SessionRepository;

impl SessionRepository {
    /// Save a session to the database
    pub async fn save<'e, E>(executor: E, session: &SessionRecord) -> Result<()>
    where
        E: Executor<'e, Database = Sqlite>,
    {
        sqlx::query(
            r#"
            INSERT INTO sessions (id, created_at, updated_at, metadata)
            VALUES (?1, ?2, ?3, ?4)
            ON CONFLICT(id) DO UPDATE SET
                updated_at = excluded.updated_at,
                metadata = excluded.metadata
            "#,
        )
        .bind(&session.id)
        .bind(session.created_at)
        .bind(session.updated_at)
        .bind(&session.metadata)
        .execute(executor)
        .await
        .map_err(|e| ava_types::AvaError::DatabaseError(e.to_string()))?;

        Ok(())
    }

    /// Load a session by ID
    pub async fn load<'e, E>(executor: E, id: &str) -> Result<Option<SessionRecord>>
    where
        E: Executor<'e, Database = Sqlite>,
    {
        let session = sqlx::query_as::<_, SessionRecord>(
            "SELECT id, created_at, updated_at, metadata FROM sessions WHERE id = ?1",
        )
        .bind(id)
        .fetch_optional(executor)
        .await
        .map_err(|e| ava_types::AvaError::DatabaseError(e.to_string()))?;

        Ok(session)
    }

    /// List all sessions ordered by updated_at desc
    pub async fn list<'e, E>(executor: E, limit: i64) -> Result<Vec<SessionRecord>>
    where
        E: Executor<'e, Database = Sqlite>,
    {
        let sessions = sqlx::query_as::<_, SessionRecord>(
            "SELECT id, created_at, updated_at, metadata FROM sessions ORDER BY updated_at DESC LIMIT ?1",
        )
        .bind(limit)
        .fetch_all(executor)
        .await
        .map_err(|e| ava_types::AvaError::DatabaseError(e.to_string()))?;

        Ok(sessions)
    }

    /// Delete a session by ID
    pub async fn delete<'e, E>(executor: E, id: &str) -> Result<bool>
    where
        E: Executor<'e, Database = Sqlite>,
    {
        let result = sqlx::query("DELETE FROM sessions WHERE id = ?1")
            .bind(id)
            .execute(executor)
            .await
            .map_err(|e| ava_types::AvaError::DatabaseError(e.to_string()))?;

        Ok(result.rows_affected() > 0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::create_test_db;

    #[tokio::test]
    async fn test_save_and_load_session() {
        let db = create_test_db().await.unwrap();
        let session = SessionRecord::new("test-session-1");

        SessionRepository::save(db.pool(), &session).await.unwrap();

        let loaded = SessionRepository::load(db.pool(), "test-session-1")
            .await
            .unwrap()
            .expect("Session should exist");

        assert_eq!(loaded.id, session.id);
    }

    #[tokio::test]
    async fn test_list_sessions() {
        let db = create_test_db().await.unwrap();

        for i in 0..5 {
            let session = SessionRecord::new(format!("session-{}", i));
            SessionRepository::save(db.pool(), &session).await.unwrap();
        }

        let sessions = SessionRepository::list(db.pool(), 3).await.unwrap();
        assert_eq!(sessions.len(), 3);
    }

    #[tokio::test]
    async fn test_delete_session() {
        let db = create_test_db().await.unwrap();
        let session = SessionRecord::new("delete-test");

        SessionRepository::save(db.pool(), &session).await.unwrap();
        let deleted = SessionRepository::delete(db.pool(), "delete-test")
            .await
            .unwrap();
        assert!(deleted);

        let loaded = SessionRepository::load(db.pool(), "delete-test")
            .await
            .unwrap();
        assert!(loaded.is_none());
    }
}
