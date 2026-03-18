# ava-db

> SQLite database layer for sessions and messages (legacy compatibility).

## Public API

| Type/Function | Description |
|--------------|-------------|
| `Database` | Connection manager with connection pool |
| `Database::new(url)` | Create pool from connection string |
| `Database::create_at(path)` | Create database file and pool |
| `Database::run_migrations()` | Execute sqlx migrations |
| `Database::migrate()` | Backward-compatible migration helper |
| `Database::pool()` | Get reference to SqlitePool |
| `Database::close()` | Close all connections |
| `MessageRepository` | CRUD operations for message records |
| `SessionRepository` | CRUD operations for session records |
| `MessageRecord` | Struct: id, session_id, role, content, tool_calls_json, created_at |
| `SessionRecord` | Struct: id, title, created_at, updated_at, metadata_json |
| `create_test_db()` | In-memory database for tests |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Database struct, connection management, migrations, test helpers |
| `models/mod.rs` | Re-exports MessageRecord and SessionRecord |
| `models/message.rs` | MessageRecord struct, MessageRepository with insert/get/list/delete |
| `models/session.rs` | SessionRecord struct, SessionRepository with create/get/list/update/delete |
| `migrations/` | Embedded sqlx migrations (referenced but not in repo) |

## Dependencies

Uses: ava-types
Used by: ava-tui

## Key Patterns

- Uses `sqlx` async SQLite with WAL mode and connection pooling (max 5)
- Errors converted via `map_err()` to `AvaError::DatabaseError`
- **DEPRECATED**: This crate is legacy; active persistence is in `ava-session`
- No direct trait implementations beyond std traits (Debug, Clone, PartialEq, Eq)
- Repository pattern: separate structs for entity queries
