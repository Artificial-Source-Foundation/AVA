# Database Schema

> SQLite via tauri-plugin-sql

---

## Tables

```sql
CREATE TABLE sessions (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE messages (
    id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL REFERENCES sessions(id),
    role TEXT NOT NULL CHECK (role IN ('user', 'assistant', 'system', 'tool')),
    content TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE files (
    id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL REFERENCES sessions(id),
    path TEXT NOT NULL,
    operation TEXT NOT NULL CHECK (operation IN ('create', 'read', 'write', 'delete', 'edit')),
    diff TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
```

## Notes

- Sessions can be forked (creates new session with copied messages)
- File-based session persistence also available for CLI (no SQLite dependency)
- Session resume searches by ID prefix
