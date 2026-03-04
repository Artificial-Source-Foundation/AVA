use rusqlite::{params, Connection, OptionalExtension, Result};
use std::path::Path;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Memory {
    pub id: i64,
    pub key: String,
    pub value: String,
    pub created_at: String,
}

#[derive(Debug)]
pub struct MemorySystem {
    conn: Connection,
}

impl MemorySystem {
    pub fn new(path: impl AsRef<Path>) -> Result<Self> {
        let conn = Connection::open(path)?;
        let system = Self { conn };
        system.init_schema()?;
        Ok(system)
    }

    pub fn remember(&self, key: &str, value: &str) -> Result<Memory> {
        self.conn.execute(
            "INSERT INTO memories (key, value) VALUES (?1, ?2)",
            params![key, value],
        )?;

        let id = self.conn.last_insert_rowid();
        self.conn.query_row(
            "SELECT id, key, value, created_at FROM memories WHERE id = ?1",
            params![id],
            row_to_memory,
        )
    }

    pub fn recall(&self, key: &str) -> Result<Option<Memory>> {
        self.conn
            .query_row(
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
        let mut stmt = self.conn.prepare(
            "SELECT m.id, m.key, m.value, m.created_at
             FROM memories_fts AS f
             JOIN memories AS m ON m.id = f.rowid
             WHERE memories_fts MATCH ?1
             ORDER BY m.created_at DESC, m.id DESC",
        )?;

        let rows = stmt.query_map(params![query], row_to_memory)?;
        rows.collect()
    }

    pub fn get_recent(&self, limit: usize) -> Result<Vec<Memory>> {
        let mut stmt = self.conn.prepare(
            "SELECT id, key, value, created_at FROM memories
             ORDER BY created_at DESC, id DESC
             LIMIT ?1",
        )?;

        let rows = stmt.query_map(params![limit as i64], row_to_memory)?;
        rows.collect()
    }

    fn init_schema(&self) -> Result<()> {
        self.conn.execute_batch(
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

fn row_to_memory(row: &rusqlite::Row<'_>) -> Result<Memory> {
    Ok(Memory {
        id: row.get(0)?,
        key: row.get(1)?,
        value: row.get(2)?,
        created_at: row.get(3)?,
    })
}

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
}
