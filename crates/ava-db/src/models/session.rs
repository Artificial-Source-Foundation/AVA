//! Session model and repository operations.

use sqlx::{FromRow, SqlitePool};

#[derive(Debug, Clone, PartialEq, Eq, FromRow)]
pub struct SessionRecord {
    pub id: String,
    pub title: String,
    pub created_at: String,
    pub updated_at: String,
    pub metadata_json: Option<String>,
}

#[derive(Debug, Clone)]
pub struct SessionRepository {
    pool: SqlitePool,
}

impl SessionRepository {
    pub fn new(pool: SqlitePool) -> Self {
        Self { pool }
    }

    pub async fn create(&self, record: &SessionRecord) -> Result<(), sqlx::Error> {
        sqlx::query(
            "INSERT INTO sessions (id, title, created_at, updated_at, metadata_json) VALUES (?1, ?2, ?3, ?4, ?5)",
        )
        .bind(&record.id)
        .bind(&record.title)
        .bind(&record.created_at)
        .bind(&record.updated_at)
        .bind(&record.metadata_json)
        .execute(&self.pool)
        .await?;

        Ok(())
    }

    pub async fn get_by_id(&self, id: &str) -> Result<Option<SessionRecord>, sqlx::Error> {
        sqlx::query_as::<_, SessionRecord>(
            "SELECT id, title, created_at, updated_at, metadata_json FROM sessions WHERE id = ?1",
        )
        .bind(id)
        .fetch_optional(&self.pool)
        .await
    }

    pub async fn list_recent(&self, limit: i64) -> Result<Vec<SessionRecord>, sqlx::Error> {
        sqlx::query_as::<_, SessionRecord>(
            "SELECT id, title, created_at, updated_at, metadata_json FROM sessions ORDER BY updated_at DESC LIMIT ?1",
        )
        .bind(limit)
        .fetch_all(&self.pool)
        .await
    }

    pub async fn update_title(&self, id: &str, title: &str) -> Result<u64, sqlx::Error> {
        let result = sqlx::query(
            "UPDATE sessions SET title = ?1, updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = ?2",
        )
        .bind(title)
        .bind(id)
        .execute(&self.pool)
        .await?;

        Ok(result.rows_affected())
    }

    pub async fn delete(&self, id: &str) -> Result<u64, sqlx::Error> {
        let result = sqlx::query("DELETE FROM sessions WHERE id = ?1")
            .bind(id)
            .execute(&self.pool)
            .await?;

        Ok(result.rows_affected())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::create_test_db;

    #[tokio::test]
    async fn test_create_get_update_and_delete_session() {
        let db = create_test_db().await.expect("test db should initialize");
        let repo = SessionRepository::new(db.pool().clone());
        let session = SessionRecord {
            id: "test-session-1".to_string(),
            title: "Initial title".to_string(),
            created_at: "2026-01-01T00:00:00Z".to_string(),
            updated_at: "2026-01-01T00:00:00Z".to_string(),
            metadata_json: None,
        };

        repo.create(&session)
            .await
            .expect("session should be inserted");

        let loaded = repo
            .get_by_id("test-session-1")
            .await
            .expect("load should succeed")
            .expect("session should exist");

        assert_eq!(loaded.id, session.id);

        let updated = repo
            .update_title("test-session-1", "Updated title")
            .await
            .expect("update should succeed");
        assert_eq!(updated, 1);

        let loaded = repo
            .get_by_id("test-session-1")
            .await
            .expect("load should succeed")
            .expect("session should still exist");
        assert_eq!(loaded.title, "Updated title");

        let deleted = repo
            .delete("test-session-1")
            .await
            .expect("delete should succeed");
        assert_eq!(deleted, 1);

        let missing = repo
            .get_by_id("test-session-1")
            .await
            .expect("load should succeed");
        assert!(missing.is_none());
    }

    #[tokio::test]
    async fn test_list_recent_sessions() {
        let db = create_test_db().await.expect("test db should initialize");
        let repo = SessionRepository::new(db.pool().clone());

        for i in 0..5 {
            repo.create(&SessionRecord {
                id: format!("session-{i}"),
                title: format!("Session {i}"),
                created_at: format!("2026-01-01T00:00:0{i}Z"),
                updated_at: format!("2026-01-01T00:00:0{i}Z"),
                metadata_json: None,
            })
            .await
            .expect("session should be inserted");
        }

        let sessions = repo
            .list_recent(3)
            .await
            .expect("list should succeed");
        assert_eq!(sessions.len(), 3);
        assert_eq!(sessions[0].id, "session-4");
    }
}
