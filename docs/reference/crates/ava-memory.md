# ava-memory

Persistent key-value memory with full-text search, backed by SQLite and FTS5.

## How It Works

`MemorySystem` uses the same path-based connection pattern as `ava-session`: each operation opens a fresh SQLite connection, making the type `Send + Sync` safe without interior mutability. The database is initialized with:

- **`memories` table**: `id` (autoincrement PK), `key` (TEXT), `value` (TEXT), `created_at` (ISO 8601 timestamp, defaulting to UTC now)
- **`memories_fts` virtual table**: FTS5 index on `value`, kept in sync via INSERT/DELETE/UPDATE triggers

**File**: `crates/ava-memory/src/lib.rs` (lines 96-127, schema)

## Key Types

### MemorySystem

```rust
pub struct MemorySystem {
    db_path: PathBuf,
}
```

| Method | Description |
|--------|-------------|
| `new(path)` | Creates DB and initializes schema |
| `remember(key, value)` | Stores a memory, returns the created `Memory` |
| `recall(key)` | Returns the most recent memory for a key |
| `search(query)` | Full-text search over values (FTS5 MATCH) |
| `get_recent(limit)` | Returns N most recent memories, newest first |

### Memory

```rust
pub struct Memory {
    pub id: i64,
    pub key: String,
    pub value: String,
    pub created_at: String,
}
```

### FTS5 Query Sanitization

User queries are sanitized before being passed to FTS5: each word is wrapped in double quotes to make all tokens literal, preventing parse errors from special characters like `'`, `&`, `<`, `>`, and FTS5 operators (AND, OR, NOT).

```rust
fn sanitize_fts_query(query: &str) -> String {
    query.split_whitespace()
        .map(|word| {
            let clean: String = word.chars().filter(|c| *c != '"').collect();
            if clean.is_empty() { String::new() } else { format!("\"{clean}\"") }
        })
        .filter(|s| !s.is_empty())
        .collect::<Vec<_>>()
        .join(" ")
}
```

**File**: `crates/ava-memory/src/lib.rs` (lines 132-147)

## Source Files

| File | Lines | Purpose |
|------|------:|---------|
| `src/lib.rs` | 320 | MemorySystem, Memory, sanitization, tests |
