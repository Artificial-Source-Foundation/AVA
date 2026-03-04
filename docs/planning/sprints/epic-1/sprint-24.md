# Sprint 24: Workspace & Types

**Epic:** Foundation (Epic 1)  
**Duration:** 2 weeks  
**Goal:** Core Rust infrastructure - workspace, types, platform traits

## Stories

### Story 1.1: Workspace Setup
**Points:** 4 (AI: 2 hrs, Human: 2 hrs)

**What to build:**
Create the Rust workspace structure at repo root:

```bash
mkdir -p crates/{ava-types,ava-platform,ava-config,ava-logger}
```

**Files:**

`Cargo.toml` (workspace root):
```toml
[workspace]
members = [
    "crates/ava-types",
    "crates/ava-platform",
    "crates/ava-config",
    "crates/ava-logger",
]
resolver = "2"

[workspace.package]
version = "0.1.0"
edition = "2021"
authors = ["AVA Team"]
license = "MIT"

[workspace.dependencies]
tokio = { version = "1.0", features = ["full"] }
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
thiserror = "1.0"
async-trait = "0.1"
uuid = { version = "1.0", features = ["v4", "serde"] }
chrono = { version = "0.4", features = ["serde"] }
tracing = "0.1"
```

`crates/ava-types/Cargo.toml`:
```toml
[package]
name = "ava-types"
version.workspace = true
edition.workspace = true

[dependencies]
serde.workspace = true
uuid.workspace = true
chrono.workspace = true
thiserror.workspace = true
```

Similar for other crates.

**Acceptance Criteria:**
- [ ] `cargo build --all-targets` passes
- [ ] All 4 crates compile
- [ ] No warnings

---

### Story 1.2: Core Types
**Points:** 8 (AI: 4 hrs, Human: 4 hrs)

**What to build:**
Define core types in `crates/ava-types/src/lib.rs`

See full implementation in reference docs.

**Key Types:**
- `Tool`, `ToolCall`, `ToolResult`
- `Message`, `Role`
- `Session`
- `Context`
- `AvaError`

**Acceptance Criteria:**
- [ ] All types compile
- [ ] Tests pass: `cargo test -p ava-types`
- [ ] No clippy warnings

---

### Story 1.3: Platform Abstraction
**Points:** 8 (AI: 4 hrs, Human: 4 hrs)

**What to build:**
Platform traits in `crates/ava-platform/src/lib.rs`

**Traits:**
- `FileSystem`: read_file, write_file, exists, is_directory
- `Shell`: execute, execute_streaming
- `Platform`: combines fs + shell

**Implementations:**
- `LocalFileSystem`
- `LocalShell`

**Acceptance Criteria:**
- [ ] Traits compile
- [ ] Local implementations work
- [ ] Tests pass
- [ ] Can read/write files and execute commands

---

## Sprint Goal

**Success Criteria:**
- [x] Workspace builds
- [x] Core types defined
- [x] Platform abstraction working
- [x] All tests passing

## Implementation Status (2026-03-04)

- Completed in Rust workspace with crates: `ava-types`, `ava-platform`, `ava-config`, `ava-logger`
- Added core type system, platform traits, and initial test coverage
- Verified with:
  - `cargo build --all-targets`
  - `cargo test --workspace`
  - `cargo clippy --workspace -- -D warnings`

**Next:** Sprint 25 - Infrastructure
