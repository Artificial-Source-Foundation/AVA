//! AVA Database Layer
//!
//! Provides SQLite database operations for sessions, messages, and other persistent data.

use ava_types::Result;
use sqlx::sqlite::{
    SqliteConnectOptions, SqliteJournalMode, SqlitePool, SqlitePoolOptions, SqliteSynchronous,
};
use std::path::Path;
use std::str::FromStr;

pub mod models;

pub use models::hq::HqRepository;
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
        let in_memory = is_in_memory_sqlite(database_url);
        let options = SqliteConnectOptions::from_str(database_url)
            .map_err(|e| ava_types::AvaError::DatabaseError(e.to_string()))?
            .journal_mode(SqliteJournalMode::Wal)
            .synchronous(SqliteSynchronous::Normal)
            .foreign_keys(true)
            .busy_timeout(std::time::Duration::from_millis(5000))
            .pragma("cache_size", "-64000");

        if in_memory {
            tracing::debug!(
                database_url,
                "forcing single SQLite connection for in-memory database"
            );
        }

        let pool = SqlitePoolOptions::new()
            .max_connections(if in_memory { 1 } else { 5 })
            .connect_with(options)
            .await
            .map_err(|e| ava_types::AvaError::DatabaseError(e.to_string()))?;

        Ok(Self { pool })
    }

    /// Create a new database at the given path
    pub async fn create_at(path: impl AsRef<Path>) -> Result<Self> {
        let path = path.as_ref();
        tracing::debug!("DB initialized at {}", path.display());

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
            .map_err(|e| {
                tracing::error!("DB migration failed: {e}");
                ava_types::AvaError::DatabaseError(e.to_string())
            })?;
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

fn is_in_memory_sqlite(database_url: &str) -> bool {
    let normalized = database_url.to_ascii_lowercase();
    normalized.starts_with("sqlite::memory:") || normalized.contains("mode=memory")
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

    #[tokio::test]
    async fn in_memory_pool_reuses_single_schema_across_concurrent_queries() {
        let db = create_test_db().await.unwrap();

        let handles: Vec<_> = (0..4)
            .map(|_| {
                let pool = db.pool().clone();
                tokio::spawn(async move {
                    let row: (i64,) =
                        sqlx::query_as("SELECT COUNT(*) FROM sqlite_master WHERE type='table'")
                            .fetch_one(&pool)
                            .await
                            .unwrap();
                    row.0
                })
            })
            .collect();

        for handle in handles {
            let table_count = handle.await.unwrap();
            assert!(
                table_count >= 2,
                "expected migrated tables, got {table_count}"
            );
        }
    }
}
