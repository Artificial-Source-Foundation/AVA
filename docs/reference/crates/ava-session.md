# ava-session

Session persistence layer using SQLite with FTS5 full-text search. Provides CRUD operations, session forking, and text search over message content.

## How It Works

`SessionManager` stores data in a single SQLite database file. Each operation opens a fresh connection (path-based pattern, making the manager `Send + Sync`). The schema contains three structures:

1. **`sessions` table** -- `id` (TEXT PK), `created_at`, `updated_at`, `metadata` (JSON), `parent_id` (nullable, for forked sessions)
2. **`messages` table** -- `id`, `session_id` (FK), `role`, `content`, `timestamp`, `tool_calls` (JSON), `tool_results` (JSON)
3. **`messages_fts` virtual table** -- FTS5 index on `content`, kept in sync via INSERT/DELETE/UPDATE triggers

**File**: `crates/ava-session/src/helpers.rs` (lines 1-88, contains `SCHEMA_SQL`)

## Key Types

### SessionManager

```rust
pub struct SessionManager {
    db_path: PathBuf,
}
```

| Method | Description |
|--------|-------------|
| `new(path)` | Opens/creates DB, runs schema migration |
| `create()` | Returns a new `Session` (in-memory only, not persisted) |
| `save(session)` | Upserts session + replaces all messages in a transaction |
| `get(id)` | Loads session + messages by UUID |
| `list_recent(limit)` | Returns most recently updated sessions |
| `fork(session)` | Creates a new session copying messages/metadata, sets `parent_id` |
| `search(query)` | FTS5 MATCH query over message content |
| `get_children(parent_id)` | Lists sub-agent sessions by parent |
| `delete(id)` | Removes session and all its messages |

**File**: `crates/ava-session/src/lib.rs` (lines 74-328)

### Title Generation

`generate_title(first_message)` creates session titles from the first user message:
- Slash commands keep the command name (e.g., "/help")
- Long messages truncate at word boundaries with "..." suffix
- Maximum 50 characters
- Empty input yields "Untitled session"

**File**: `crates/ava-session/src/lib.rs` (lines 25-68)

## Transaction Safety

The `save()` method uses a SQLite transaction: it upserts the session row, deletes all existing messages for that session, then re-inserts them. This ensures atomicity -- partial saves cannot leave orphaned messages.

## Source Files

| File | Lines | Purpose |
|------|------:|---------|
| `src/lib.rs` | 378 | SessionManager, generate_title, tests |
| `src/helpers.rs` | 88 | SQL schema, role/UUID/datetime helpers |
