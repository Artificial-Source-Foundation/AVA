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

pub use models::message::MessageRepository;
pub use models::session::SessionRepository;
pub use models::{MessageRecord, SessionRecord};

// `003_hq.sql` shipped first without the historical-compatibility comments below,
// then later had comments added in-place. SQLx hashes the full file contents, so
// older databases now fail startup with `migration 3 was previously applied but
// has been modified` even though the actual schema is unchanged.
const MIGRATION_003_OLD_CHECKSUM_HEX: &str =
    "f1d521bc859e398b5d2db2f5d8ead7b55ae8d155e8740e185401c96c49f45084c2a90f1d37e8642ba448e8edc577e6c1";
const MIGRATION_003_NEW_CHECKSUM_HEX: &str =
    "0b3ab7a8cb6d6087985c7c8b8fc47c4f0029c899701af79e9c76ea8a3081de8c8131056de281cbf06bf0725d3c0b5440";
const MIGRATION_004_OLD_CHECKSUM_HEX: &str =
    "03ce9e82bfd05f3fbc36898c65dc6d80e3c47b8e15c861d2a58e3ea3d338d75423ed051c457a7b3ea693b091aff568b5";
const MIGRATION_004_NEW_CHECKSUM_HEX: &str =
    "5b901922ecafe25160b0d4313fb237b100478e7ca0c78232ee4fb9b970b2cbbf53a2fe4900d49e9fe0b0acca9c3a1b92";

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
        self.repair_known_migration_checksum_drift().await?;

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

    async fn repair_known_migration_checksum_drift(&self) -> Result<()> {
        let has_migration_table: i64 = sqlx::query_scalar(
            "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = '_sqlx_migrations'",
        )
        .fetch_one(&self.pool)
        .await
        .map_err(|e| ava_types::AvaError::DatabaseError(e.to_string()))?;

        if has_migration_table == 0 {
            return Ok(());
        }

        for (version, old_hex, new_hex) in [
            (
                3_i64,
                MIGRATION_003_OLD_CHECKSUM_HEX,
                MIGRATION_003_NEW_CHECKSUM_HEX,
            ),
            (
                4_i64,
                MIGRATION_004_OLD_CHECKSUM_HEX,
                MIGRATION_004_NEW_CHECKSUM_HEX,
            ),
        ] {
            let old_checksum = decode_hex_checksum(old_hex)?;
            let new_checksum = decode_hex_checksum(new_hex)?;

            let result = sqlx::query(
                "UPDATE _sqlx_migrations SET checksum = ? WHERE version = ? AND checksum = ?",
            )
            .bind(new_checksum)
            .bind(version)
            .bind(old_checksum)
            .execute(&self.pool)
            .await
            .map_err(|e| ava_types::AvaError::DatabaseError(e.to_string()))?;

            if result.rows_affected() > 0 {
                tracing::info!(
                    version,
                    rows_affected = result.rows_affected(),
                    "repaired legacy SQLx checksum drift"
                );
            }
        }

        Ok(())
    }
}

fn is_in_memory_sqlite(database_url: &str) -> bool {
    let normalized = database_url.to_ascii_lowercase();
    normalized.starts_with("sqlite::memory:") || normalized.contains("mode=memory")
}

fn decode_hex_checksum(hex: &str) -> Result<Vec<u8>> {
    if !hex.len().is_multiple_of(2) {
        return Err(ava_types::AvaError::DatabaseError(format!(
            "invalid checksum hex length: {}",
            hex.len()
        )));
    }

    let mut bytes = Vec::with_capacity(hex.len() / 2);
    for i in (0..hex.len()).step_by(2) {
        let byte = u8::from_str_radix(&hex[i..i + 2], 16).map_err(|e| {
            ava_types::AvaError::DatabaseError(format!("invalid checksum hex: {e}"))
        })?;
        bytes.push(byte);
    }

    Ok(bytes)
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

    #[tokio::test]
    async fn repairs_legacy_checksum_drift_for_migration_003() {
        let db = create_test_db().await.unwrap();

        let old_checksum = decode_hex_checksum(MIGRATION_003_OLD_CHECKSUM_HEX).unwrap();
        sqlx::query("UPDATE _sqlx_migrations SET checksum = ? WHERE version = 3")
            .bind(old_checksum)
            .execute(db.pool())
            .await
            .unwrap();

        db.run_migrations().await.unwrap();

        let repaired: Vec<u8> =
            sqlx::query_scalar("SELECT checksum FROM _sqlx_migrations WHERE version = 3")
                .fetch_one(db.pool())
                .await
                .unwrap();

        assert_eq!(
            repaired,
            decode_hex_checksum(MIGRATION_003_NEW_CHECKSUM_HEX).unwrap()
        );
    }

    #[tokio::test]
    async fn repairs_legacy_checksum_drift_for_migration_004() {
        let db = create_test_db().await.unwrap();

        let old_checksum = decode_hex_checksum(MIGRATION_004_OLD_CHECKSUM_HEX).unwrap();
        sqlx::query("UPDATE _sqlx_migrations SET checksum = ? WHERE version = 4")
            .bind(old_checksum)
            .execute(db.pool())
            .await
            .unwrap();

        db.run_migrations().await.unwrap();

        let repaired: Vec<u8> =
            sqlx::query_scalar("SELECT checksum FROM _sqlx_migrations WHERE version = 4")
                .fetch_one(db.pool())
                .await
                .unwrap();

        assert_eq!(
            repaired,
            decode_hex_checksum(MIGRATION_004_NEW_CHECKSUM_HEX).unwrap()
        );
    }
}
