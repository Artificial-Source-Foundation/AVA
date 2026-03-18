# ava-memory

> Persistent memory storage with SQLite and FTS5 full-text search.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `MemorySystem` | Main entry point for memory operations |
| `MemorySystem::new(path)` | Initialize with schema creation |
| `MemorySystem::remember(key, value)` | Store key-value pair |
| `MemorySystem::recall(key)` | Retrieve most recent value by key |
| `MemorySystem::search(query)` | Full-text search via FTS5 |
| `MemorySystem::get_recent(limit)` | List recent memories |
| `MemorySystem::observe_learned_pattern(...)` | Record learning observation |
| `MemorySystem::set_learned_status(id, status)` | Update LearnedMemoryStatus |
| `MemorySystem::list_learned(status, limit)` | List learned patterns with optional filter |
| `MemorySystem::search_confirmed_learned(query, limit)` | Search confirmed learnings |
| `Memory` | Struct: id, key, value, created_at |
| `LearnedMemory` | Struct: id, key, value, source_excerpt, observed_count, confidence, status, timestamps |
| `LearnedMemoryStatus` | Enum: Pending, Confirmed, Rejected |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | MemorySystem impl, FTS query sanitization, row mapping, schema init |
| `learned.rs` | LearnedMemoryStatus enum, LearnedMemory struct, upsert/list/search functions |

## Dependencies

Uses: (none - only external crates: rusqlite, tracing)
Used by: ava-tui, ava-agent

## Key Patterns

- Uses `rusqlite` (sync, not async) with bundled SQLite
- WAL mode, foreign keys enabled, 5s busy timeout
- FTS5 virtual table for full-text search with auto-sync triggers
- Query sanitization wraps words in quotes to prevent FTS5 parse errors
- Learned patterns promoted to Confirmed after 2+ observations with confidence >= 0.7
- Rejected patterns are preserved and won't re-activate
- Compile-time Send+Sync assertions via `const _: () = {...}` pattern
- Schema versioning via `PRAGMA user_version`
