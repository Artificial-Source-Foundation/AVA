# Small Crates Reference

Four smaller crates support the current Rust-first runtime without carrying the
full agent stack on their own.

---

## ava-db

Shared SQLite connection-pool crate used by persistence-oriented crates.

### Purpose

- centralizes `SqlitePool` setup and migration-friendly connection wiring
- keeps low-level database bootstrap logic out of higher-level crates

**File**: `crates/ava-db/src/lib.rs`

---

## ava-validator

Pluggable content validation with pipeline composition and retry support.

### Key Types

**ValidationResult**:

```rust
pub struct ValidationResult {
    pub valid: bool,
    pub error: Option<String>,
    pub details: Vec<String>,
}
```

**Validator trait** (`Send + Sync`):

```rust
pub trait Validator: Send + Sync {
    fn name(&self) -> &'static str;
    fn validate(&self, content: &str) -> ValidationResult;
}
```

**Built-in validators**:

| Validator | Checks |
|-----------|--------|
| `SyntaxValidator` | Merge conflict markers (`<<<<<<<`, `=======`, `>>>>>>>`), unbalanced delimiters (`()`, `{}`, `[]`) |
| `CompilationValidator` | `compile_error!` macro, unresolved merge markers (`<<<`, `>>>`) |

**Files**:

- `crates/ava-validator/src/validators.rs`
- `crates/ava-validator/src/pipeline.rs`

---

## ava-types

Shared domain types reused across the runtime.

### Purpose

- defines message, session, tool, todo, and token-usage types
- hosts `AvaError` and shared result aliases used across crates
- provides common DTO-style types so crates can stay decoupled

**File**: `crates/ava-types/src/lib.rs`

---

## ava-cli-providers

Integration with external CLI-based AI agents (Claude Code, Gemini CLI, Codex,
OpenCode, Aider).

### Architecture

Discovers installed CLI agents, wraps them as `LLMProvider` instances
(prefixed `cli:`), and executes them with tier-appropriate settings.

### Key Types

**CLIAgentConfig** -- describes how to invoke a CLI agent:

```rust
pub struct CLIAgentConfig {
    pub name: String,
    pub binary: String,
    pub prompt_flag: PromptMode,
    pub non_interactive_flags: Vec<String>,
    pub yolo_flags: Vec<String>,
    pub output_format_flag: Option<String>,
    pub supports_stream_json: bool,
    pub supports_tool_scoping: bool,
}
```

**Built-in configs** include `claude-code`, `gemini-cli`, `codex`, `opencode`,
and `aider`.

**Files**:

- `crates/ava-cli-providers/src/config.rs`
- `crates/ava-cli-providers/src/configs.rs`
- `crates/ava-cli-providers/src/provider.rs`
- `crates/ava-cli-providers/src/runner/`
