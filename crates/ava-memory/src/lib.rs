//! AVA Memory — persistent memory with SQLite and full-text search.
//!
//! This crate provides:
//! - Key-value memory storage with timestamps
//! - Full-text search via SQLite FTS5
//! - Thread-safe concurrent access

use rusqlite::{params, Connection, OptionalExtension, Result};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Memory {
    pub id: i64,
    pub key: String,
    pub value: String,
    pub created_at: String,
}

#[derive(Debug)]
pub struct MemorySystem {
    db_path: PathBuf,
}

impl MemorySystem {
    pub fn new(path: impl AsRef<Path>) -> Result<Self> {
        let system = Self {
            db_path: path.as_ref().to_path_buf(),
        };
        system.init_schema()?;
        Ok(system)
    }

    fn conn(&self) -> Result<Connection> {
        Connection::open(&self.db_path)
    }

    pub fn remember(&self, key: &str, value: &str) -> Result<Memory> {
        let conn = self.conn()?;
        conn.execute(
            "INSERT INTO memories (key, value) VALUES (?1, ?2)",
            params![key, value],
        )?;

        let id = conn.last_insert_rowid();
        conn.query_row(
            "SELECT id, key, value, created_at FROM memories WHERE id = ?1",
            params![id],
            row_to_memory,
        )
    }

    pub fn recall(&self, key: &str) -> Result<Option<Memory>> {
        let conn = self.conn()?;
        conn.query_row(
            "SELECT id, key, value, created_at FROM memories
                 WHERE key = ?1
                 ORDER BY created_at DESC, id DESC
                 LIMIT 1",
            params![key],
            row_to_memory,
        )
        .optional()
    }

    pub fn search(&self, query: &str) -> Result<Vec<Memory>> {
        let sanitized = sanitize_fts_query(query);
        if sanitized.is_empty() {
            return Ok(Vec::new());
        }

        let conn = self.conn()?;
        let mut stmt = conn.prepare(
            "SELECT m.id, m.key, m.value, m.created_at
             FROM memories_fts AS f
             JOIN memories AS m ON m.id = f.rowid
             WHERE memories_fts MATCH ?1
             ORDER BY m.created_at DESC, m.id DESC",
        )?;

        let rows = stmt.query_map(params![sanitized], row_to_memory)?;
        rows.collect()
    }

    pub fn get_recent(&self, limit: usize) -> Result<Vec<Memory>> {
        let conn = self.conn()?;
        let mut stmt = conn.prepare(
            "SELECT id, key, value, created_at FROM memories
             ORDER BY created_at DESC, id DESC
             LIMIT ?1",
        )?;

        let rows = stmt.query_map(params![limit as i64], row_to_memory)?;
        rows.collect()
    }

    fn init_schema(&self) -> Result<()> {
        let conn = self.conn()?;
        conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS memories (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                key TEXT NOT NULL,
                value TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
            );

            CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5(
                value,
                content='memories',
                content_rowid='id'
            );

            CREATE TRIGGER IF NOT EXISTS memories_ai AFTER INSERT ON memories BEGIN
                INSERT INTO memories_fts(rowid, value) VALUES (new.id, new.value);
            END;

            CREATE TRIGGER IF NOT EXISTS memories_ad AFTER DELETE ON memories BEGIN
                INSERT INTO memories_fts(memories_fts, rowid, value)
                VALUES ('delete', old.id, old.value);
            END;

            CREATE TRIGGER IF NOT EXISTS memories_au AFTER UPDATE ON memories BEGIN
                INSERT INTO memories_fts(memories_fts, rowid, value)
                VALUES ('delete', old.id, old.value);
                INSERT INTO memories_fts(rowid, value) VALUES (new.id, new.value);
            END;",
        )
    }
}

/// Sanitize a user query for FTS5 by wrapping each word in double quotes,
/// making all tokens literal and preventing FTS5 parse errors from special characters.
fn sanitize_fts_query(query: &str) -> String {
    query
        .split_whitespace()
        .map(|word| {
            // Strip characters that are special to FTS5 even inside quotes
            let clean: String = word.chars().filter(|c| *c != '"').collect();
            if clean.is_empty() {
                String::new()
            } else {
                format!("\"{clean}\"")
            }
        })
        .filter(|s| !s.is_empty())
        .collect::<Vec<_>>()
        .join(" ")
}

fn row_to_memory(row: &rusqlite::Row<'_>) -> Result<Memory> {
    Ok(Memory {
        id: row.get(0)?,
        key: row.get(1)?,
        value: row.get(2)?,
        created_at: row.get(3)?,
    })
}

const _: () = {
    fn _assert_send_sync<T: Send + Sync>() {}
    fn _check() {
        _assert_send_sync::<MemorySystem>();
    }
};

#[cfg(test)]
mod tests {
    use super::{Memory, MemorySystem};
    use tempfile::{tempdir, TempDir};

    fn make_db_path(test_name: &str) -> (TempDir, std::path::PathBuf) {
        let dir = tempdir().expect("temp dir should be created");
        let path = dir.path().join(format!("{test_name}.sqlite3"));
        (dir, path)
    }

    fn remember_values(system: &MemorySystem, pairs: &[(&str, &str)]) -> Vec<Memory> {
        pairs
            .iter()
            .map(|(key, value)| {
                system
                    .remember(key, value)
                    .expect("memory should be stored")
            })
            .collect()
    }

    #[test]
    fn remember_and_recall_by_key() {
        let (_dir, db_path) = make_db_path("remember_recall");
        let system = MemorySystem::new(&db_path).expect("memory system should initialize");

        let stored = system
            .remember("project", "ava")
            .expect("remember should store value");
        let recalled = system
            .recall("project")
            .expect("recall should succeed")
            .expect("memory should exist");

        assert_eq!(stored.id, recalled.id);
        assert_eq!(recalled.key, "project");
        assert_eq!(recalled.value, "ava");
        assert!(!recalled.created_at.is_empty());
    }

    #[test]
    fn search_queries_stored_values_with_full_text() {
        let (_dir, db_path) = make_db_path("full_text_search");
        let system = MemorySystem::new(&db_path).expect("memory system should initialize");

        remember_values(
            &system,
            &[
                ("task", "implement persistent memory for ava"),
                ("note", "write tests for memory recall"),
                ("todo", "ship sqlite storage"),
            ],
        );

        let matches = system.search("persistent").expect("search should execute");

        assert_eq!(matches.len(), 1);
        assert_eq!(matches[0].key, "task");
        assert!(matches[0].value.contains("persistent memory"));
    }

    #[test]
    fn get_recent_returns_descending_created_at() {
        let (_dir, db_path) = make_db_path("recent_desc_order");
        let system = MemorySystem::new(&db_path).expect("memory system should initialize");

        remember_values(
            &system,
            &[("entry", "first"), ("entry", "second"), ("entry", "third")],
        );

        let recent = system.get_recent(3).expect("get_recent should succeed");

        assert_eq!(recent.len(), 3);
        assert_eq!(recent[0].value, "third");
        assert_eq!(recent[1].value, "second");
        assert_eq!(recent[2].value, "first");
    }

    #[test]
    fn data_persists_across_new_instances_with_same_db_path() {
        let (_dir, db_path) = make_db_path("persistence_across_instances");

        {
            let system = MemorySystem::new(&db_path).expect("memory system should initialize");
            system
                .remember("session", "first run")
                .expect("remember should store value");
        }

        let reopened = MemorySystem::new(&db_path).expect("memory system should reopen");
        let recalled = reopened
            .recall("session")
            .expect("recall should succeed")
            .expect("memory should persist");

        assert_eq!(recalled.value, "first run");
    }

    #[test]
    fn search_with_special_chars_does_not_crash() {
        let (_dir, db_path) = make_db_path("special_chars_search");
        let system = MemorySystem::new(&db_path).expect("memory system should initialize");

        remember_values(
            &system,
            &[("note", "foo's bar baz implementation")],
        );

        // These contain FTS5 special chars that would crash without sanitization
        let result = system.search("foo's bar & <baz>");
        assert!(result.is_ok(), "search should not crash: {:?}", result.err());

        // Also test unbalanced quotes and FTS5 operators
        let result = system.search("\"unbalanced");
        assert!(result.is_ok(), "unbalanced quote should not crash");

        let result = system.search("AND OR NOT");
        assert!(result.is_ok(), "FTS5 operators should not crash");

        // Empty/whitespace-only query should return empty
        let result = system.search("   ");
        assert!(result.is_ok());
        assert!(result.unwrap().is_empty());
    }

    #[test]
    fn concurrent_access_from_two_threads() {
        let (_dir, db_path) = make_db_path("concurrent_access");
        let system = std::sync::Arc::new(
            MemorySystem::new(&db_path).expect("memory system should initialize"),
        );

        let s1 = system.clone();
        let t1 = std::thread::spawn(move || {
            s1.remember("thread1", "value1")
                .expect("thread 1 should write");
        });

        let s2 = system.clone();
        let t2 = std::thread::spawn(move || {
            s2.remember("thread2", "value2")
                .expect("thread 2 should write");
        });

        t1.join().expect("thread 1 should join");
        t2.join().expect("thread 2 should join");

        let r1 = system.recall("thread1").unwrap().expect("thread1 value");
        let r2 = system.recall("thread2").unwrap().expect("thread2 value");
        assert_eq!(r1.value, "value1");
        assert_eq!(r2.value, "value2");
    }
}
