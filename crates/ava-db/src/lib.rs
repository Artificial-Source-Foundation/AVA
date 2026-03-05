//! AVA Database Layer
//!
//! Provides SQLite database operations for sessions, messages, and other persistent data.

use ava_types::Result;
use sqlx::sqlite::{SqlitePool, SqlitePoolOptions};
use std::path::Path;

pub mod models;

pub use models::message::MessageRepository;
pub use models::session::SessionRepository;
pub use models::{MessageRecord, SessionRecord};

/// Database connection manager
#[derive(Debug, Clone)]
pub struct Database {
    pool: SqlitePool,
}

impl Database {
    /// Create a new database connection pool
    pub async fn new(database_url: &str) -> Result<Self> {
        let pool = SqlitePoolOptions::new()
            .max_connections(5)
            .connect(database_url)
            .await
            .map_err(|e| ava_types::AvaError::DatabaseError(e.to_string()))?;

        Ok(Self { pool })
    }

    /// Create a new database at the given path
    pub async fn create_at(path: impl AsRef<Path>) -> Result<Self> {
        let path = path.as_ref();

        // Ensure parent directory exists
        if let Some(parent) = path.parent() {
            tokio::fs::create_dir_all(parent)
                .await
                .map_err(|e| ava_types::AvaError::IoError(e.to_string()))?;
        }

        let database_url = format!("sqlite:{}?mode=rwc", path.display());
        Self::new(&database_url).await
    }

    /// Run database migrations
    pub async fn run_migrations(&self) -> Result<()> {
        sqlx::migrate!("./src/migrations")
            .run(&self.pool)
            .await
            .map_err(|e| ava_types::AvaError::DatabaseError(e.to_string()))?;
        Ok(())
    }

    /// Backward compatible migration helper.
    pub async fn migrate(&self) -> Result<()> {
        self.run_migrations().await
    }

    /// Get a reference to the connection pool
    pub fn pool(&self) -> &SqlitePool {
        &self.pool
    }

    /// Close the database connection
    pub async fn close(&self) {
        self.pool.close().await;
    }
}

/// Initialize an in-memory database for testing
#[cfg(test)]
pub async fn create_test_db() -> Result<Database> {
    let db = Database::new("sqlite::memory:").await?;
    db.run_migrations().await?;
    Ok(db)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_database_creation() {
        let db = create_test_db().await.unwrap();
        assert!(!db.pool.is_closed());
    }

    #[tokio::test]
    async fn test_migrations_run() {
        let db = create_test_db().await.unwrap();
        // If migrations failed, we wouldn't get here
        let row: (i64,) = sqlx::query_as("SELECT COUNT(*) FROM sqlite_master WHERE type='table'")
            .fetch_one(db.pool())
            .await
            .unwrap();
        assert!(row.0 >= 2); // At least sessions and messages tables
    }
}
