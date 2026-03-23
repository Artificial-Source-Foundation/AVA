//! Persistent SQLite-backed audit log storage.
//!
//! Stores audit entries in `.ava/audit.db` so they survive across app restarts.
//! The in-memory `AuditLog` remains the fast path for session queries; this module
//! adds durable persistence for debugging and compliance.

use chrono::{DateTime, Utc};
use sqlx::sqlite::{
    SqliteConnectOptions, SqliteJournalMode, SqlitePool, SqlitePoolOptions, SqliteSynchronous,
};
use std::path::{Path, PathBuf};
use std::str::FromStr;

use crate::audit::{AuditDecision, AuditEntry};
use crate::tags::RiskLevel;

/// Persistent audit log backed by SQLite.
#[derive(Debug, Clone)]
pub struct AuditStore {
    pool: SqlitePool,
}

/// A row read back from the audit_log table.
#[derive(Debug, Clone)]
pub struct AuditRecord {
    pub id: i64,
    pub timestamp: DateTime<Utc>,
    pub session_id: Option<String>,
    pub tool_name: String,
    pub tool_source: Option<String>,
    pub decision: String,
    pub risk_level: Option<String>,
    pub summary: Option<String>,
    pub created_at: Option<String>,
}

impl AuditStore {
    /// Open (or create) the audit database at the given path.
    pub async fn open(db_path: impl AsRef<Path>) -> Result<Self, AuditStoreError> {
        let db_path = db_path.as_ref();

        // Ensure parent directory exists
        if let Some(parent) = db_path.parent() {
            tokio::fs::create_dir_all(parent)
                .await
                .map_err(|e| AuditStoreError::Io(e.to_string()))?;
        }

        let url = format!("sqlite:{}?mode=rwc", db_path.display());
        let options = SqliteConnectOptions::from_str(&url)
            .map_err(|e| AuditStoreError::Database(e.to_string()))?
            .journal_mode(SqliteJournalMode::Wal)
            .synchronous(SqliteSynchronous::Normal)
            .foreign_keys(true)
            .busy_timeout(std::time::Duration::from_millis(3000))
            .pragma("cache_size", "-64000");

        let pool = SqlitePoolOptions::new()
            .max_connections(2)
            .connect_with(options)
            .await
            .map_err(|e| AuditStoreError::Database(e.to_string()))?;

        let store = Self { pool };
        store.ensure_table().await?;
        Ok(store)
    }

    /// Open the default audit database at `~/.ava/audit.db`.
    pub async fn open_default() -> Result<Self, AuditStoreError> {
        let path = Self::default_path()
            .ok_or_else(|| AuditStoreError::Io("cannot determine home directory".into()))?;
        Self::open(&path).await
    }

    /// Returns the default database path: `~/.ava/audit.db`.
    pub fn default_path() -> Option<PathBuf> {
        dirs::home_dir().map(|h| h.join(".ava/audit.db"))
    }

    /// Create the audit_log table if it doesn't exist.
    async fn ensure_table(&self) -> Result<(), AuditStoreError> {
        sqlx::query(
            r#"
            CREATE TABLE IF NOT EXISTS audit_log (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT NOT NULL,
                session_id TEXT,
                tool_name TEXT NOT NULL,
                tool_source TEXT,
                decision TEXT NOT NULL,
                risk_level TEXT,
                summary TEXT,
                created_at TEXT DEFAULT (datetime('now'))
            )
            "#,
        )
        .execute(&self.pool)
        .await
        .map_err(|e| AuditStoreError::Database(e.to_string()))?;

        // Index for common queries
        sqlx::query("CREATE INDEX IF NOT EXISTS idx_audit_session ON audit_log(session_id)")
            .execute(&self.pool)
            .await
            .map_err(|e| AuditStoreError::Database(e.to_string()))?;

        sqlx::query("CREATE INDEX IF NOT EXISTS idx_audit_tool ON audit_log(tool_name)")
            .execute(&self.pool)
            .await
            .map_err(|e| AuditStoreError::Database(e.to_string()))?;

        Ok(())
    }

    /// Insert an audit entry into the persistent store.
    pub async fn insert(
        &self,
        entry: &AuditEntry,
        session_id: Option<&str>,
        tool_source: Option<&str>,
    ) -> Result<(), AuditStoreError> {
        let timestamp = entry.timestamp.to_rfc3339();
        let decision = decision_to_str(&entry.decision);
        let risk_level = risk_level_to_str(&entry.risk_level);

        sqlx::query(
            r#"
            INSERT INTO audit_log (timestamp, session_id, tool_name, tool_source, decision, risk_level, summary)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            "#,
        )
        .bind(&timestamp)
        .bind(session_id)
        .bind(&entry.tool_name)
        .bind(tool_source)
        .bind(decision)
        .bind(risk_level)
        .bind(&entry.arguments_summary)
        .execute(&self.pool)
        .await
        .map_err(|e| AuditStoreError::Database(e.to_string()))?;

        Ok(())
    }

    /// Return the last `limit` entries, most recent first.
    pub async fn recent(&self, limit: usize) -> Result<Vec<AuditRecord>, AuditStoreError> {
        let rows = sqlx::query_as::<_, AuditRow>(
            "SELECT id, timestamp, session_id, tool_name, tool_source, decision, risk_level, summary, created_at \
             FROM audit_log ORDER BY id DESC LIMIT ?",
        )
        .bind(limit as i64)
        .fetch_all(&self.pool)
        .await
        .map_err(|e| AuditStoreError::Database(e.to_string()))?;

        Ok(rows.into_iter().map(Into::into).collect())
    }

    /// Return all entries for a given session, oldest first.
    pub async fn for_session(&self, session_id: &str) -> Result<Vec<AuditRecord>, AuditStoreError> {
        let rows = sqlx::query_as::<_, AuditRow>(
            "SELECT id, timestamp, session_id, tool_name, tool_source, decision, risk_level, summary, created_at \
             FROM audit_log WHERE session_id = ? ORDER BY id ASC",
        )
        .bind(session_id)
        .fetch_all(&self.pool)
        .await
        .map_err(|e| AuditStoreError::Database(e.to_string()))?;

        Ok(rows.into_iter().map(Into::into).collect())
    }

    /// Return all entries for a given tool name, oldest first.
    pub async fn for_tool(&self, tool_name: &str) -> Result<Vec<AuditRecord>, AuditStoreError> {
        let rows = sqlx::query_as::<_, AuditRow>(
            "SELECT id, timestamp, session_id, tool_name, tool_source, decision, risk_level, summary, created_at \
             FROM audit_log WHERE tool_name = ? ORDER BY id ASC",
        )
        .bind(tool_name)
        .fetch_all(&self.pool)
        .await
        .map_err(|e| AuditStoreError::Database(e.to_string()))?;

        Ok(rows.into_iter().map(Into::into).collect())
    }

    /// Close the database connection pool.
    pub async fn close(&self) {
        self.pool.close().await;
    }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// sqlx row type for `FromRow` derivation.
#[derive(Debug, sqlx::FromRow)]
struct AuditRow {
    id: i64,
    timestamp: String,
    session_id: Option<String>,
    tool_name: String,
    tool_source: Option<String>,
    decision: String,
    risk_level: Option<String>,
    summary: Option<String>,
    created_at: Option<String>,
}

impl From<AuditRow> for AuditRecord {
    fn from(row: AuditRow) -> Self {
        let timestamp = DateTime::parse_from_rfc3339(&row.timestamp)
            .map(|dt| dt.with_timezone(&Utc))
            .unwrap_or_else(|_| Utc::now());

        Self {
            id: row.id,
            timestamp,
            session_id: row.session_id,
            tool_name: row.tool_name,
            tool_source: row.tool_source,
            decision: row.decision,
            risk_level: row.risk_level,
            summary: row.summary,
            created_at: row.created_at,
        }
    }
}

fn decision_to_str(d: &AuditDecision) -> &'static str {
    match d {
        AuditDecision::AutoApproved => "auto_approved",
        AuditDecision::UserApproved => "user_approved",
        AuditDecision::UserDenied => "user_denied",
        AuditDecision::Blocked => "blocked",
        AuditDecision::SessionApproved => "session_approved",
    }
}

fn risk_level_to_str(r: &RiskLevel) -> &'static str {
    match r {
        RiskLevel::Safe => "safe",
        RiskLevel::Low => "low",
        RiskLevel::Medium => "medium",
        RiskLevel::High => "high",
        RiskLevel::Critical => "critical",
    }
}

/// Errors from the audit store.
#[derive(Debug, thiserror::Error)]
pub enum AuditStoreError {
    #[error("audit store I/O error: {0}")]
    Io(String),
    #[error("audit store database error: {0}")]
    Database(String),
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tags::SafetyTag;

    async fn test_store() -> AuditStore {
        // In-memory database for tests
        let options = SqliteConnectOptions::from_str("sqlite::memory:")
            .unwrap()
            .journal_mode(SqliteJournalMode::Wal);
        let pool = SqlitePoolOptions::new()
            .max_connections(1)
            .connect_with(options)
            .await
            .unwrap();
        let store = AuditStore { pool };
        store.ensure_table().await.unwrap();
        store
    }

    fn make_entry(tool: &str, decision: AuditDecision) -> AuditEntry {
        AuditEntry {
            timestamp: Utc::now(),
            tool_name: tool.to_string(),
            arguments_summary: format!("{tool} args"),
            risk_level: RiskLevel::Low,
            tags: vec![SafetyTag::ExecuteCommand],
            decision,
        }
    }

    #[tokio::test]
    async fn insert_and_recent() {
        let store = test_store().await;
        let e1 = make_entry("bash", AuditDecision::AutoApproved);
        let e2 = make_entry("write", AuditDecision::UserApproved);

        store
            .insert(&e1, Some("sess-1"), Some("builtin"))
            .await
            .unwrap();
        store
            .insert(&e2, Some("sess-1"), Some("builtin"))
            .await
            .unwrap();

        let recent = store.recent(10).await.unwrap();
        assert_eq!(recent.len(), 2);
        // Most recent first
        assert_eq!(recent[0].tool_name, "write");
        assert_eq!(recent[1].tool_name, "bash");
    }

    #[tokio::test]
    async fn for_session_filters() {
        let store = test_store().await;
        let e1 = make_entry("bash", AuditDecision::AutoApproved);
        let e2 = make_entry("read", AuditDecision::AutoApproved);
        let e3 = make_entry("write", AuditDecision::UserDenied);

        store.insert(&e1, Some("sess-a"), None).await.unwrap();
        store.insert(&e2, Some("sess-b"), None).await.unwrap();
        store.insert(&e3, Some("sess-a"), None).await.unwrap();

        let results = store.for_session("sess-a").await.unwrap();
        assert_eq!(results.len(), 2);
        assert_eq!(results[0].tool_name, "bash");
        assert_eq!(results[1].tool_name, "write");
    }

    #[tokio::test]
    async fn for_tool_filters() {
        let store = test_store().await;
        let e1 = make_entry("bash", AuditDecision::AutoApproved);
        let e2 = make_entry("bash", AuditDecision::Blocked);
        let e3 = make_entry("write", AuditDecision::UserApproved);

        store.insert(&e1, None, None).await.unwrap();
        store.insert(&e2, None, None).await.unwrap();
        store.insert(&e3, None, None).await.unwrap();

        let results = store.for_tool("bash").await.unwrap();
        assert_eq!(results.len(), 2);
        assert_eq!(results[0].decision, "auto_approved");
        assert_eq!(results[1].decision, "blocked");
    }

    #[tokio::test]
    async fn recent_respects_limit() {
        let store = test_store().await;
        for i in 0..10 {
            let e = make_entry(&format!("tool_{i}"), AuditDecision::AutoApproved);
            store.insert(&e, None, None).await.unwrap();
        }

        let recent = store.recent(3).await.unwrap();
        assert_eq!(recent.len(), 3);
        assert_eq!(recent[0].tool_name, "tool_9");
    }

    #[tokio::test]
    async fn empty_store() {
        let store = test_store().await;
        let recent = store.recent(10).await.unwrap();
        assert!(recent.is_empty());
        let session = store.for_session("nonexistent").await.unwrap();
        assert!(session.is_empty());
        let tool = store.for_tool("nonexistent").await.unwrap();
        assert!(tool.is_empty());
    }

    #[tokio::test]
    async fn no_session_id() {
        let store = test_store().await;
        let e = make_entry("bash", AuditDecision::AutoApproved);
        store.insert(&e, None, None).await.unwrap();

        let recent = store.recent(1).await.unwrap();
        assert!(recent[0].session_id.is_none());
    }
}
