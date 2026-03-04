# Epic 1: Foundation - Implementation Plan

**Goal:** Complete all 3 sprints (24-26) of the Foundation phase with full code review after each sprint.

**Timeline:** 6 weeks total (2 weeks per sprint)

---

## ✅ Sprint 24: Workspace & Types (COMPLETED)

**Status:** ✅ Done - Code reviewed and approved

**Delivered:**
- Workspace structure with 4 crates
- Core types (Tool, Message, Session, Context, AvaError)
- Platform abstraction (FileSystem, Shell, Platform traits)
- 17 tests passing, clippy clean

**Files:**
```
Cargo.toml (workspace)
crates/
├── ava-types/src/lib.rs
├── ava-platform/src/{lib.rs,fs.rs,shell.rs}
├── ava-config/src/lib.rs
└── ava-logger/src/lib.rs
```

---

## 🔄 Sprint 25: Infrastructure (NEXT)

**Goal:** Build database layer, shell execution, and file operations

### Story 1.4: Database Layer (6 hrs AI + 6 hrs human)

**Crate:** `crates/ava-db`

**What to build:**
```rust
// src/lib.rs
use sqlx::{sqlite::SqlitePool, Pool, Sqlite};
use ava_types::Result;

pub struct Database {
    pool: Pool<Sqlite>,
}

impl Database {
    pub async fn new(database_url: &str) -> Result<Self>;
    pub async fn migrate(&self) -> Result<()>;
}

// src/models/session.rs
#[derive(sqlx::FromRow)]
pub struct SessionRecord {
    pub id: String,
    pub created_at: chrono::DateTime<chrono::Utc>,
    pub updated_at: chrono::DateTime<chrono::Utc>,
    pub messages: Vec<u8>, // JSON serialized
}

pub async fn save_session(&self, session: &SessionRecord) -> Result<()>;
pub async fn load_session(&self, id: &str) -> Result<Option<SessionRecord>>;
pub async fn list_sessions(&self) -> Result<Vec<SessionRecord>>;

// src/models/message.rs
#[derive(sqlx::FromRow)]
pub struct MessageRecord {
    pub id: String,
    pub session_id: String,
    pub role: String,
    pub content: String,
    pub timestamp: chrono::DateTime<chrono::Utc>,
}

// migrations/001_initial.sql
CREATE TABLE sessions (
    id TEXT PRIMARY KEY,
    created_at TIMESTAMP NOT NULL,
    updated_at TIMESTAMP NOT NULL,
    messages BLOB NOT NULL
);

CREATE TABLE messages (
    id TEXT PRIMARY KEY,
    session_id TEXT NOT NULL,
    role TEXT NOT NULL,
    content TEXT NOT NULL,
    timestamp TIMESTAMP NOT NULL,
    FOREIGN KEY (session_id) REFERENCES sessions(id)
);

CREATE INDEX idx_messages_session_id ON messages(session_id);
CREATE INDEX idx_sessions_updated_at ON sessions(updated_at);
```

**Dependencies to add to workspace:**
```toml
sqlx = { version = "0.7", features = ["runtime-tokio", "sqlite", "migrate", "chrono"] }
```

**Acceptance Criteria:**
- [ ] Database connects and migrates on startup
- [ ] Can save/load sessions
- [ ] Can save/load messages
- [ ] All operations async with proper error handling
- [ ] Tests: `cargo test -p ava-db` passes
- [ ] No clippy warnings

---

### Story 1.5: Shell Execution (6 hrs AI + 6 hrs human)

**Crate:** `crates/ava-shell` (or extend `ava-platform/src/shell.rs`)

**What to build:**
```rust
// src/lib.rs
use ava_types::Result;
use std::process::Stdio;
use tokio::process::Command;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::sync::mpsc;
use std::time::Duration;

pub struct ShellExecutor;

pub struct ExecuteOptions {
    pub timeout: Option<Duration>,
    pub working_dir: Option<std::path::PathBuf>,
    pub env_vars: Vec<(String, String)>,
}

impl Default for ExecuteOptions {
    fn default() -> Self {
        Self {
            timeout: Some(Duration::from_secs(300)), // 5 min default
            working_dir: None,
            env_vars: Vec::new(),
        }
    }
}

pub struct ExecuteResult {
    pub stdout: String,
    pub stderr: String,
    pub exit_code: i32,
    pub duration: Duration,
}

impl ShellExecutor {
    pub fn new() -> Self {
        Self
    }
    
    // Execute command and return output
    pub async fn execute(
        &self,
        command: &str,
        options: ExecuteOptions,
    ) -> Result<ExecuteResult>;
    
    // Execute with streaming output
    pub async fn execute_streaming<F>(
        &self,
        command: &str,
        options: ExecuteOptions,
        on_output: F,
    ) -> Result<ExecuteResult>
    where
        F: FnMut(String) + Send + 'static;
    
    // Execute with timeout
    pub async fn execute_with_timeout(
        &self,
        command: &str,
        timeout: Duration,
    ) -> Result<ExecuteResult>;
}

// Error handling for shell operations
#[derive(Debug, thiserror::Error)]
pub enum ShellError {
    #[error("Command timed out after {0:?}")]
    Timeout(Duration),
    #[error("Command failed with exit code {0}")]
    ExitCode(i32),
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}
```

**Acceptance Criteria:**
- [ ] Can execute commands synchronously
- [ ] Can execute commands with streaming output
- [ ] Timeout works correctly
- [ ] Working directory can be set
- [ ] Environment variables can be passed
- [ ] Tests cover all execution modes
- [ ] No clippy warnings

---

### Story 1.6: File Operations (4 hrs AI + 4 hrs human)

**Crate:** `crates/ava-fs` (or extend `ava-platform/src/fs.rs`)

**What to build:**
```rust
// src/lib.rs
use ava_types::Result;
use std::path::{Path, PathBuf};
use tokio::fs;
use tokio::io::AsyncWriteExt;

pub struct FileOperations;

pub struct FileInfo {
    pub path: PathBuf,
    pub size: u64,
    pub modified: Option<chrono::DateTime<chrono::Utc>>,
    pub is_dir: bool,
}

pub struct FileWatcher {
    watcher: notify::RecommendedWatcher,
    rx: tokio::sync::mpsc::Receiver<notify::Event>,
}

impl FileOperations {
    pub fn new() -> Self {
        Self
    }
    
    // Read file as string
    pub async fn read_to_string<P: AsRef<Path>>(&self, path: P) -> Result<String>;
    
    // Read file as bytes
    pub async fn read<P: AsRef<Path>>(&self, path: P) -> Result<Vec<u8>>;
    
    // Write string to file
    pub async fn write<P: AsRef<Path>>(&self, path: P, content: &str) -> Result<()>;
    
    // Write bytes to file
    pub async fn write_bytes<P: AsRef<Path>>(&self, path: P, content: &[u8]) -> Result<()>;
    
    // Check if path exists
    pub async fn exists<P: AsRef<Path>>(&self, path: P) -> bool;
    
    // Check if path is directory
    pub async fn is_dir<P: AsRef<Path>>(&self, path: P) -> bool;
    
    // Get file metadata
    pub async fn metadata<P: AsRef<Path>>(&self, path: P) -> Result<FileInfo>;
    
    // List directory contents
    pub async fn read_dir<P: AsRef<Path>>(&self, path: P) -> Result<Vec<FileInfo>>;
    
    // Create directory recursively
    pub async fn create_dir_all<P: AsRef<Path>>(&self, path: P) -> Result<()>;
    
    // Remove file
    pub async fn remove_file<P: AsRef<Path>>(&self, path: P) -> Result<()>;
    
    // Remove directory recursively
    pub async fn remove_dir_all<P: AsRef<Path>>(&self, path: P) -> Result<()>;
    
    // Copy file
    pub async fn copy<P: AsRef<Path>, Q: AsRef<Path>>(
        &self,
        from: P,
        to: Q,
    ) -> Result<u64>;
    
    // Rename/move file
    pub async fn rename<P: AsRef<Path>, Q: AsRef<Path>>(
        &self,
        from: P,
        to: Q,
    ) -> Result<()>;
}

impl FileWatcher {
    // Create a new file watcher
    pub fn new() -> Result<(Self, tokio::sync::mpsc::Receiver<notify::Event>)>;
    
    // Watch a path
    pub fn watch<P: AsRef<Path>>(&mut self, path: P) -> Result<()>;
    
    // Unwatch a path
    pub fn unwatch<P: AsRef<Path>>(&mut self, path: P) -> Result<()>;
}
```

**Dependencies to add to workspace:**
```toml
notify = { version = "6.0", features = ["tokio"] }
```

**Acceptance Criteria:**
- [ ] All file operations work async
- [ ] File watching detects changes
- [ ] Proper error handling for all operations
- [ ] Tests cover CRUD operations
- [ ] No clippy warnings

---

### Sprint 25 CODE REVIEW Checkpoint

After implementing Stories 1.4-1.6, invoke code-reviewer:

```
Task(
  description="Code review Sprint 25",
  prompt="Review Sprint 25 implementation:
1. Check all new files in:
   - crates/ava-db/
   - crates/ava-shell/ (or ava-platform/src/shell.rs)
   - crates/ava-fs/ (or ava-platform/src/fs.rs)
2. Verify:
   - cargo build --all-targets passes
   - cargo test --workspace passes
   - cargo clippy --workspace -- -D warnings passes
   - All database operations work
   - Shell execution handles timeouts
   - File operations handle errors properly
3. Check for:
   - Proper async/await usage
   - Error handling patterns
   - Test coverage
   - Documentation
4. Provide feedback and required fixes",
  subagent_type="code-reviewer"
)
```

---

## ⏳ Sprint 26: Core Foundation (FINAL)

**Goal:** Configuration system, logging, and error handling

### Story 1.7: Configuration System (4 hrs AI + 4 hrs human)

**Crate:** Extend `crates/ava-config`

**What to build:**
```rust
// src/lib.rs
use ava_types::Result;
use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use std::sync::Arc;
use tokio::sync::RwLock;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Config {
    pub llm: LlmConfig,
    pub editor: EditorConfig,
    pub ui: UiConfig,
    pub features: FeaturesConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LlmConfig {
    pub provider: String, // "openai", "anthropic", "openrouter"
    pub model: String,
    pub api_key: Option<String>,
    pub max_tokens: usize,
    pub temperature: f32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EditorConfig {
    pub default_editor: String,
    pub tab_size: usize,
    pub use_spaces: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UiConfig {
    pub theme: String,
    pub font_size: usize,
    pub show_line_numbers: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FeaturesConfig {
    pub enable_git: bool,
    pub enable_lsp: bool,
    pub enable_mcp: bool,
}

pub struct ConfigManager {
    config: Arc<RwLock<Config>>,
    config_path: PathBuf,
}

impl ConfigManager {
    // Load config from file or create default
    pub async fn load() -> Result<Self>;
    
    // Load from specific path
    pub async fn load_from(path: PathBuf) -> Result<Self>;
    
    // Save config to file
    pub async fn save(&self) -> Result<()>;
    
    // Get current config
    pub async fn get(&self) -> Config;
    
    // Update config
    pub async fn update<F>(&self, f: F) -> Result<()>
    where
        F: FnOnce(&mut Config);
    
    // Reload from disk
    pub async fn reload(&self) -> Result<()>;
    
    // Watch for changes
    pub async fn watch_for_changes(&self) -> Result<()>;
}

impl Default for Config {
    fn default() -> Self {
        Self {
            llm: LlmConfig {
                provider: "openai".to_string(),
                model: "gpt-4".to_string(),
                api_key: None,
                max_tokens: 4096,
                temperature: 0.7,
            },
            editor: EditorConfig {
                default_editor: "vscode".to_string(),
                tab_size: 4,
                use_spaces: true,
            },
            ui: UiConfig {
                theme: "dark".to_string(),
                font_size: 14,
                show_line_numbers: true,
            },
            features: FeaturesConfig {
                enable_git: true,
                enable_lsp: true,
                enable_mcp: true,
            },
        }
    }
}
```

**Config file locations:**
- Linux: `~/.config/ava/config.yaml`
- macOS: `~/Library/Application Support/ava/config.yaml`
- Windows: `%APPDATA%\ava\config.yaml`

**Acceptance Criteria:**
- [ ] Config loads from file
- [ ] Config creates default if missing
- [ ] Config saves properly
- [ ] Hot reload works
- [ ] Tests: `cargo test -p ava-config` passes
- [ ] No clippy warnings

---

### Story 1.8: Logging & Telemetry (4 hrs AI + 4 hrs human)

**Crate:** Extend `crates/ava-logger`

**What to build:**
```rust
// src/lib.rs
use ava_types::Result;
use tracing::{info, error, warn, debug, trace};
use tracing_subscriber::{layer::SubscriberExt, util::SubscriberInitExt};
use std::path::PathBuf;
use std::sync::Arc;
use tokio::sync::mpsc;

pub struct Logger {
    log_tx: mpsc::Sender<LogEntry>,
}

#[derive(Debug, Clone)]
pub struct LogEntry {
    pub timestamp: chrono::DateTime<chrono::Utc>,
    pub level: LogLevel,
    pub message: String,
    pub metadata: Option<serde_json::Value>,
}

#[derive(Debug, Clone, Copy)]
pub enum LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
}

#[derive(Debug, Clone)]
pub struct Metrics {
    pub llm_requests: u64,
    pub llm_tokens_sent: u64,
    pub llm_tokens_received: u64,
    pub tool_calls: u64,
    pub session_duration: std::time::Duration,
}

impl Logger {
    // Initialize logging system
    pub async fn init(log_dir: PathBuf) -> Result<Self>;
    
    // Log a message
    pub async fn log(&self, level: LogLevel, message: &str);
    
    // Log with structured metadata
    pub async fn log_with_metadata(
        &self,
        level: LogLevel,
        message: &str,
        metadata: serde_json::Value,
    );
    
    // Log tool call
    pub async fn log_tool_call(&self, tool: &str, duration: std::time::Duration);
    
    // Log LLM request
    pub async fn log_llm_request(&self, tokens: usize, cost: f64);
    
    // Get metrics
    pub async fn get_metrics(&self) -> Metrics;
    
    // Flush logs to disk
    pub async fn flush(&self) -> Result<()>;
}

// Convenience macros
#[macro_export]
macro_rules! log_info {
    ($logger:expr, $msg:expr) => {
        $logger.log($crate::LogLevel::Info, $msg).await
    };
}

#[macro_export]
macro_rules! log_error {
    ($logger:expr, $msg:expr) => {
        $logger.log($crate::LogLevel::Error, $msg).await
    };
}
```

**Acceptance Criteria:**
- [ ] Logging initializes successfully
- [ ] Logs write to rotating files
- [ ] Structured JSON logging available
- [ ] Metrics collection works
- [ ] Tests: `cargo test -p ava-logger` passes
- [ ] No clippy warnings

---

### Story 1.9: Error Handling (4 hrs AI + 4 hrs human)

**Crate:** Extend `crates/ava-types/src/error.rs` (split from lib.rs)

**What to build:**
```rust
// src/error.rs
use thiserror::Error;
use serde::{Serialize, Deserialize};

#[derive(Error, Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum AvaError {
    #[error("Tool execution failed: {0}")]
    ToolError(String),
    
    #[error("IO error: {0}")]
    IoError(String),
    
    #[error("Serialization error: {0}")]
    SerializationError(String),
    
    #[error("Platform error: {0}")]
    PlatformError(String),
    
    #[error("Configuration error: {0}")]
    ConfigError(String),
    
    #[error("Validation error: {0}")]
    ValidationError(String),
    
    #[error("Database error: {0}")]
    DatabaseError(String),
    
    #[error("LLM error: {0}")]
    LlmError(String),
    
    #[error("Shell error: {0}")]
    ShellError(String),
    
    #[error("Timeout error: {0}")]
    TimeoutError(String),
    
    #[error("Not found: {0}")]
    NotFound(String),
    
    #[error("Permission denied: {0}")]
    PermissionDenied(String),
}

impl AvaError {
    // Get error category for metrics/logging
    pub fn category(&self) -> ErrorCategory {
        match self {
            AvaError::ToolError(_) => ErrorCategory::Tool,
            AvaError::IoError(_) | AvaError::PlatformError(_) | AvaError::ShellError(_) => {
                ErrorCategory::System
            }
            AvaError::SerializationError(_) => ErrorCategory::Data,
            AvaError::ConfigError(_) => ErrorCategory::Config,
            AvaError::ValidationError(_) => ErrorCategory::Validation,
            AvaError::DatabaseError(_) => ErrorCategory::Database,
            AvaError::LlmError(_) => ErrorCategory::Llm,
            AvaError::TimeoutError(_) => ErrorCategory::Timeout,
            AvaError::NotFound(_) => ErrorCategory::NotFound,
            AvaError::PermissionDenied(_) => ErrorCategory::Permission,
        }
    }
    
    // Check if error is retryable
    pub fn is_retryable(&self) -> bool {
        matches!(
            self,
            AvaError::TimeoutError(_) | AvaError::LlmError(_) | AvaError::DatabaseError(_)
        )
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ErrorCategory {
    Tool,
    System,
    Data,
    Config,
    Validation,
    Database,
    Llm,
    Timeout,
    NotFound,
    Permission,
}

// Result type alias
pub type Result<T> = std::result::Result<T, AvaError>;
```

**Update lib.rs:**
```rust
// src/lib.rs
pub mod error;
pub mod types;

pub use error::{AvaError, ErrorCategory, Result};
pub use types::*;
```

**Acceptance Criteria:**
- [ ] All error variants defined
- [ ] Error categories work correctly
- [ ] Retryable detection works
- [ ] All crates use AvaError
- [ ] Tests cover error conversions
- [ ] No clippy warnings

---

### Sprint 26 CODE REVIEW Checkpoint

After implementing Stories 1.7-1.9, invoke code-reviewer:

```
Task(
  description="Code review Sprint 26",
  prompt="Review Sprint 26 (Final) implementation:
1. Check all modified files:
   - crates/ava-config/src/lib.rs
   - crates/ava-logger/src/lib.rs
   - crates/ava-types/src/{lib.rs,error.rs,types.rs}
2. Verify:
   - cargo build --all-targets passes
   - cargo test --workspace passes
   - cargo clippy --workspace -- -D warnings passes
   - Config loads/saves/reloads
   - Logging writes to files
   - All errors use AvaError
3. Check for:
   - File size limits (max 300 lines)
   - Proper module structure
   - Documentation completeness
4. Confirm Epic 1 is complete",
  subagent_type="code-reviewer"
)
```

---

## Epic 1 Completion Checklist

**After all 3 sprints and code reviews:**

- [ ] ✅ Sprint 24: Workspace & Types (COMPLETED)
- [ ] ✅ Sprint 24 Code Review (PASSED)
- [ ] ⏳ Sprint 25: Infrastructure (DATABASE, SHELL, FILE OPS)
- [ ] ⏳ Sprint 25 Code Review (TODO)
- [ ] ⏳ Sprint 26: Core Foundation (CONFIG, LOGGING, ERRORS)
- [ ] ⏳ Sprint 26 Code Review (TODO)
- [ ] ⏳ Final Epic 1 Integration Test
- [ ] ⏳ Documentation Complete

**Success Metrics:**
- All 4 crates compile without warnings
- 50+ tests passing
- Clippy clean
- No file > 300 lines
- All functionality works together

---

## Implementation Order

1. **Start Sprint 25** → Implement Stories 1.4, 1.5, 1.6
2. **Sprint 25 Code Review** → Fix any issues
3. **Start Sprint 26** → Implement Stories 1.7, 1.8, 1.9
4. **Sprint 26 Code Review** → Fix any issues
5. **Epic 1 Complete** → Ready for Epic 2

---

## Dependencies Summary

**Current workspace dependencies:**
```toml
tokio = { version = "1.0", features = ["full"] }
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
thiserror = "1.0"
async-trait = "0.1"
uuid = { version = "1.0", features = ["v4", "serde"] }
chrono = { version = "0.4", features = ["serde"] }
tracing = "0.1"
```

**New dependencies to add:**
```toml
# Sprint 25
sqlx = { version = "0.7", features = ["runtime-tokio", "sqlite", "migrate", "chrono"] }
notify = { version = "6.0", features = ["tokio"] }

# Sprint 26
tracing-subscriber = { version = "0.3", features = ["env-filter", "json"] }
dirs = "5.0"  # For config directories
```
