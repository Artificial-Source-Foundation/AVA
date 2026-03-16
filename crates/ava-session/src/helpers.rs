use ava_types::{AvaError, Result, Role};
use chrono::{DateTime, Utc};
use uuid::Uuid;

pub const SCHEMA_SQL: &str = "CREATE TABLE IF NOT EXISTS sessions (
    id TEXT PRIMARY KEY,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    metadata TEXT NOT NULL,
    parent_id TEXT,
    token_usage TEXT NOT NULL DEFAULT '{}'
);

CREATE TABLE IF NOT EXISTS messages (
    id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL,
    role TEXT NOT NULL,
    content TEXT NOT NULL,
    timestamp TEXT NOT NULL,
    tool_calls TEXT NOT NULL,
    tool_results TEXT NOT NULL,
    tool_call_id TEXT,
    images TEXT NOT NULL DEFAULT '[]',
    FOREIGN KEY(session_id) REFERENCES sessions(id)
);

CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts USING fts5(
    content,
    content='messages',
    content_rowid='rowid'
);

CREATE TRIGGER IF NOT EXISTS messages_ai AFTER INSERT ON messages BEGIN
    INSERT INTO messages_fts(rowid, content) VALUES (new.rowid, new.content);
END;

CREATE TRIGGER IF NOT EXISTS messages_ad AFTER DELETE ON messages BEGIN
    INSERT INTO messages_fts(messages_fts, rowid, content)
    VALUES ('delete', old.rowid, old.content);
END;

CREATE TRIGGER IF NOT EXISTS messages_au AFTER UPDATE ON messages BEGIN
    INSERT INTO messages_fts(messages_fts, rowid, content)
    VALUES ('delete', old.rowid, old.content);
    INSERT INTO messages_fts(rowid, content) VALUES (new.rowid, new.content);
END;";

/// Migration SQL for adding new columns to existing databases.
/// Each statement is idempotent — ALTER TABLE will fail silently if the column already exists.
pub const MIGRATION_SQL: &[&str] = &[
    "ALTER TABLE messages ADD COLUMN tool_call_id TEXT",
    "ALTER TABLE messages ADD COLUMN images TEXT NOT NULL DEFAULT '[]'",
    "ALTER TABLE sessions ADD COLUMN token_usage TEXT NOT NULL DEFAULT '{}'",
];

pub fn role_to_str(role: &Role) -> &'static str {
    match role {
        Role::System => "system",
        Role::User => "user",
        Role::Assistant => "assistant",
        Role::Tool => "tool",
    }
}

pub fn str_to_role(value: &str) -> Result<Role> {
    match value {
        "system" => Ok(Role::System),
        "user" => Ok(Role::User),
        "assistant" => Ok(Role::Assistant),
        "tool" => Ok(Role::Tool),
        _ => Err(AvaError::ValidationError(format!("unknown role: {value}"))),
    }
}

pub fn parse_uuid(value: &str) -> Result<Uuid> {
    Uuid::parse_str(value).map_err(|error| AvaError::ValidationError(error.to_string()))
}

pub fn parse_datetime(value: &str) -> Result<DateTime<Utc>> {
    DateTime::parse_from_rfc3339(value)
        .map(|date| date.with_timezone(&Utc))
        .map_err(|error| AvaError::ValidationError(error.to_string()))
}

pub fn db_error(error: rusqlite::Error) -> AvaError {
    AvaError::DatabaseError(error.to_string())
}

pub fn to_conversion_error(error: AvaError) -> rusqlite::Error {
    rusqlite::Error::FromSqlConversionFailure(
        0,
        rusqlite::types::Type::Text,
        Box::new(std::io::Error::new(
            std::io::ErrorKind::InvalidData,
            error.to_string(),
        )),
    )
}
