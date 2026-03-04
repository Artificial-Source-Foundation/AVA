# Sprint 25: Infrastructure

**Epic:** Foundation (Epic 1)  
**Duration:** 2 weeks  
**Goal:** Database, shell execution, file operations

## Stories

### Story 1.4: Database Layer
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
SQLite database layer in `crates/ava-db/src/lib.rs`

**Features:**
- Connection pooling (sqlx)
- Session persistence
- Migrations
- FTS5 search

**API:**
```rust
pub struct Database {
    pool: SqlitePool,
}

impl Database {
    pub async fn save_session(&self, session: &Session) -> Result<()>;
    pub async fn load_session(&self, id: Uuid) -> Result<Session>;
    pub async fn search_sessions(&self, query: &str) -> Result<Vec<Session>>;
}
```

**Acceptance Criteria:**
- [ ] CRUD operations work
- [ ] Migrations run automatically
- [ ] Connection pooling configured
- [ ] Tests pass

---

### Story 1.5: Shell Execution
**Points:** 12 (AI: 6 hrs, Human: 6 hrs)

**What to build:**
Shell execution in `crates/ava-shell/src/lib.rs`

**Features:**
- Async command execution
- Timeout support
- Streaming output
- PTY support

**API:**
```rust
pub struct ShellExecutor {
    timeout: Duration,
}

impl ShellExecutor {
    pub async fn execute(&self, command: &str) -> Result<Output>;
    pub async fn execute_streaming(&self, command: &str) -> impl Stream<Item = Output>;
}
```

**Acceptance Criteria:**
- [ ] Commands execute
- [ ] Timeout works
- [ ] Streaming output works
- [ ] Tests pass

---

### Story 1.6: File Operations
**Points:** 8 (AI: 4 hrs, Human: 4 hrs)

**What to build:**
Async file operations in `crates/ava-fs/src/lib.rs`

**Features:**
- Async file I/O
- File watching (notify crate)
- Atomic writes

**API:**
```rust
pub struct FileSystem;

impl FileSystem {
    pub async fn read(&self, path: &Path) -> Result<String>;
    pub async fn write(&self, path: &Path, content: &str) -> Result<()>;
    pub async fn watch(&self, path: &Path) -> impl Stream<Item = Event>;
}
```

**Acceptance Criteria:**
- [ ] File ops work
- [ ] Watcher triggers on changes
- [ ] Tests pass

---

## Sprint Goal

**Success Criteria:**
- [x] Database layer working
- [x] Shell execution working
- [x] File operations working
- [x] All tests passing

## Implementation Status (2026-03-04)

- Added `ava-db` with migrations, repositories, and persistence tests
- Implemented async shell execution with timeout and streaming in platform layer
- Implemented async filesystem operations and metadata helpers
- Verified with:
  - `cargo build --all-targets`
  - `cargo test --workspace`
  - `cargo clippy --workspace -- -D warnings`

**Next:** Sprint 26 - Core Foundation
