//! FTS5 full-text search over session messages.

use ava_types::{Result, Session};
use rusqlite::params;

use crate::helpers::{db_error, parse_uuid};
use crate::SessionManager;

impl SessionManager {
    pub fn search(&self, query: &str) -> Result<Vec<Session>> {
        let conn = self.open_conn()?;
        let mut stmt = conn
            .prepare(
                "SELECT DISTINCT m.session_id
                 FROM messages_fts f
                 JOIN messages m ON m.rowid = f.rowid
                 WHERE messages_fts MATCH ?1
                 ORDER BY m.timestamp DESC",
            )
            .map_err(db_error)?;

        let ids = stmt
            .query_map(params![query], |row| row.get::<_, String>(0))
            .map_err(db_error)?
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(db_error)?;

        let mut sessions = Vec::new();
        for id in ids {
            if let Some(session) = self.get_with_conn(&conn, parse_uuid(&id)?)? {
                sessions.push(session);
            }
        }
        Ok(sessions)
    }
}
