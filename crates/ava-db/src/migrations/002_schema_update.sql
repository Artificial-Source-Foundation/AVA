PRAGMA foreign_keys = OFF;

ALTER TABLE sessions RENAME TO sessions_old;
ALTER TABLE messages RENAME TO messages_old;

CREATE TABLE sessions (
    id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    created_at TEXT NOT NULL,
    updated_at TEXT NOT NULL,
    metadata_json TEXT
);

INSERT INTO sessions (id, title, created_at, updated_at, metadata_json)
SELECT
    id,
    id,
    CAST(created_at AS TEXT),
    CAST(updated_at AS TEXT),
    metadata
FROM sessions_old;

CREATE TABLE messages (
    id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL,
    role TEXT NOT NULL,
    content TEXT NOT NULL,
    tool_calls_json TEXT,
    created_at TEXT NOT NULL,
    FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
);

INSERT INTO messages (id, session_id, role, content, tool_calls_json, created_at)
SELECT
    id,
    session_id,
    role,
    content,
    tool_calls,
    CAST(timestamp AS TEXT)
FROM messages_old;

DROP TABLE messages_old;
DROP TABLE sessions_old;

CREATE INDEX idx_messages_session_id ON messages(session_id);
CREATE INDEX idx_messages_created_at ON messages(created_at);
CREATE INDEX idx_sessions_updated_at ON sessions(updated_at);

PRAGMA foreign_keys = ON;
