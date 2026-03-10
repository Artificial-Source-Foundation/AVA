# ava-db

SQLite database layer using sqlx. Provides connection pooling and repository patterns for sessions and messages.

## How It Works

### Database (`src/lib.rs`)

```rust
pub struct Database {
    pool: SqlitePool,
}
```

`Database::create_at(path)` creates a SQLite connection pool at the given path and runs migrations via `run_migrations()`. Uses sqlx's built-in migration system.

### Repositories (`src/models/`)

#### SessionRepository (`src/models/session.rs`)

`SessionRecord` is a sqlx `FromRow` struct mapping the sessions table. `SessionRepository` provides:

- `insert(session)` -- creates a new session row
- `get(id)` -- retrieves by UUID
- `list_recent(limit)` -- ordered by `updated_at DESC`
- `update(session)` -- updates metadata and timestamps
- `delete(id)` -- removes a session

#### MessageRepository (`src/models/message.rs`)

`MessageRecord` is a sqlx `FromRow` struct. `MessageRepository` provides:

- `insert(message)` -- creates a message row
- `get(id)` -- retrieves by UUID
- `list_by_session(session_id)` -- all messages for a session, ordered by timestamp
- `delete(id)` -- removes a message

## Relationship to ava-session

`ava-db` uses sqlx and provides a repository pattern. `ava-session` uses rusqlite directly with a path-based connection pattern (no pool). Both can persist session data; the agent stack currently uses `ava-session` for its `Send + Sync` simplicity.

## Source Files

| File | Lines | Purpose |
|------|------:|---------|
| `src/lib.rs` | -- | Database, SqlitePool, migrations |
| `src/models/session.rs` | -- | SessionRecord, SessionRepository |
| `src/models/message.rs` | -- | MessageRecord, MessageRepository |
