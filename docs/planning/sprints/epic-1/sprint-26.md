# Sprint 26: Core Foundation

**Epic:** Foundation (Epic 1)  
**Duration:** 2 weeks  
**Goal:** Configuration, logging, error handling

## Stories

### Story 1.7: Configuration System
**Points:** 8 (AI: 4 hrs, Human: 4 hrs)

**What to build:**
Configuration in `crates/ava-config/src/lib.rs`

**Features:**
- YAML/JSON support
- Environment variables
- Hot reload
- Validation

**API:**
```rust
pub struct Config {
    pub model: ModelConfig,
    pub permissions: PermissionConfig,
    pub extensions: Vec<ExtensionConfig>,
}

impl Config {
    pub fn load() -> Result<Self>;
    pub fn save(&self) -> Result<()>;
}
```

**Acceptance Criteria:**
- [ ] Config loads from disk
- [ ] Environment variables work
- [ ] Hot reload works
- [ ] Tests pass

---

### Story 1.8: Logging & Telemetry
**Points:** 8 (AI: 4 hrs, Human: 4 hrs)

**What to build:**
Logging in `crates/ava-logger/src/lib.rs`

**Features:**
- Structured logging (tracing)
- Metrics collection
- Tool call logging
- LLM request logging

**API:**
```rust
pub fn init_logging() -> Result<()>;
pub fn log_tool_call(tool: &str, duration: Duration);
pub fn log_llm_request(tokens: usize, cost: f64);
```

**Acceptance Criteria:**
- [ ] Logs write to file
- [ ] Metrics collected
- [ ] Tests pass

---

### Story 1.9: Error Handling
**Points:** 8 (AI: 4 hrs, Human: 4 hrs)

**What to build:**
Centralized error handling in `crates/ava-types/src/error.rs`

**Features:**
- thiserror for ergonomics
- Structured errors
- Error chaining

**API:**
```rust
#[derive(Error, Debug)]
pub enum AvaError {
    #[error("Tool failed: {0}")]
    ToolError(String),
    #[error("LLM error: {0}")]
    LLMError(String),
    #[error("IO error: {0}")]
    IoError(#[from] std::io::Error),
}
```

**Acceptance Criteria:**
- [ ] All crates use common error type
- [ ] Error messages are helpful
- [ ] Tests pass

---

## Epic 1 Complete!

**Success Criteria:**
- [x] Foundation crates ready
- [x] All tests passing
- [x] Workspace builds cleanly

## Implementation Status (2026-03-04)

- Completed configuration manager updates in `ava-config`
- Completed structured logger and telemetry foundation in `ava-logger`
- Completed consolidated error model and category helpers in `ava-types`
- Verified with:
  - `cargo build --all-targets`
  - `cargo test --workspace`
  - `cargo clippy --workspace -- -D warnings`

**Next:** Epic 2 - Essential Tools (Sprint 27)
