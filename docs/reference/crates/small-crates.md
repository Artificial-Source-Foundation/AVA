# Small Crates Reference

Four smaller crates that provide logging, validation, LSP client, and CLI agent integration.

---

## ava-logger

Structured logging with a background file writer and metrics tracking.

### Key Types

**Logger** -- creates a background tokio task that writes log entries to `{log_dir}/ava.log` via an mpsc channel. Also integrates with the `tracing` crate for standard Rust logging.

```rust
pub struct Logger {
    log_tx: mpsc::Sender<LogEntry>,
    metrics: Arc<RwLock<Metrics>>,
}
```

**LogLevel**: Trace, Debug, Info, Warn, Error (maps to `tracing::Level`).

**LogEntry**: timestamp, level, message, optional metadata (JSON).

**Metrics**: tracks `llm_requests`, `llm_tokens_sent`, `llm_tokens_received`, `tool_calls`, `session_duration_secs`.

| Method | Description |
|--------|-------------|
| `Logger::init()` | Initializes tracing subscriber from `RUST_LOG` env |
| `Logger::new(log_dir)` | Spawns background file writer |
| `log(level, message)` | Sends to file writer + tracing |
| `log_with_metadata(level, message, metadata)` | With JSON metadata |
| `log_tool_call(tool, duration)` | Increments `tool_calls` metric |
| `log_llm_request(tokens, cost)` | Increments LLM metrics |

**File**: `crates/ava-logger/src/lib.rs` (lines 1-267)

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

**File**: `crates/ava-validator/src/validators.rs` (lines 1-176)

**ValidationPipeline** -- chains validators, stops at first failure:
```rust
let pipeline = ValidationPipeline::new()
    .with_validator(SyntaxValidator)
    .with_validator(CompilationValidator);
let result = pipeline.validate(content);
```

**validate_with_retry** -- runs pipeline with bounded retries and a `FixGenerator`:
```rust
pub fn validate_with_retry(
    pipeline: &ValidationPipeline,
    content: &str,
    fixer: &dyn FixGenerator,
    max_attempts: usize,
) -> RetryOutcome
```

`FixGenerator` trait provides `generate_fix(content, failure, attempt) -> Option<String>` for automated fix attempts.

**File**: `crates/ava-validator/src/pipeline.rs` (lines 1-113)

---

## ava-lsp

LSP (Language Server Protocol) client for code intelligence.

### Key Types

**LspClient** -- builds JSON-RPC 2.0 requests and parses responses for LSP methods:

| Method | LSP Method |
|--------|-----------|
| `initialize_request()` | `initialize` |
| `goto_definition_request()` | `textDocument/definition` |
| `hover_request()` | `textDocument/hover` |
| `references_request()` | `textDocument/references` |
| `pull_diagnostics_request()` | `textDocument/diagnostic` |

Each request method returns a JSON string. Companion `parse_*_response()` methods deserialize the response envelope, extracting the result or returning an `LspError` for JSON-RPC errors.

Transport-aware convenience methods (`*_via_transport()`) write the request frame and read the response frame in a single call, using `write_frame` / `read_frame` for Content-Length framing.

`handle_notification()` processes `textDocument/publishDiagnostics` notifications, broadcasting diagnostics via a `broadcast::Sender<Vec<Diagnostic>>`.

**File**: `crates/ava-lsp/src/client.rs` (lines 1-288)

**Transport** (`src/transport.rs`):
- `encode_message(payload)` -- adds `Content-Length` header
- `decode_message(frame)` -- parses header, validates body length
- `write_frame` / `read_frame` -- async Content-Length framed I/O

**LspError**: Io, Serde, Protocol variants.

**File**: `crates/ava-lsp/src/transport.rs` (lines 1-123)

---

## ava-cli-providers

Integration with external CLI-based AI agents (Claude Code, Gemini CLI, Codex, OpenCode, Aider).

### Architecture

Discovers installed CLI agents, wraps them as `LLMProvider` instances (prefixed `cli:`), and executes them with tier-appropriate settings.

### Key Types

**CLIAgentConfig** -- describes how to invoke a CLI agent:
```rust
pub struct CLIAgentConfig {
    pub name: String,           // "claude-code", "codex", etc.
    pub binary: String,         // "claude", "codex", etc.
    pub prompt_flag: PromptMode, // Flag("-p") or Subcommand("exec")
    pub non_interactive_flags: Vec<String>,
    pub yolo_flags: Vec<String>,
    pub output_format_flag: Option<String>,
    pub supports_stream_json: bool,
    pub supports_tool_scoping: bool,
    pub tier_tool_scopes: Option<HashMap<String, Vec<String>>>,
    // ... cwd_flag, model_flag, session_flag, version_command
}
```

**File**: `crates/ava-cli-providers/src/config.rs` (lines 1-86)

**Built-in configs** (`src/configs.rs`): 5 agents pre-configured:

| Agent | Binary | Prompt Mode | Stream JSON | Tool Scoping |
|-------|--------|-------------|:-----------:|:------------:|
| claude-code | `claude` | Flag(`-p`) | yes | yes |
| gemini-cli | `gemini` | Flag(`-p`) | no | no |
| codex | `codex` | Subcommand(`exec`) | yes | no |
| opencode | `opencode` | Subcommand(`run`) | no | no |
| aider | `aider` | Flag(`--message`) | no | no |

**CLIAgentLLMProvider** (`src/provider.rs`) -- wraps a `CLIAgentRunner` as an `LLMProvider`. Implements `generate()` (synchronous run) and `generate_stream()` (streaming via mpsc channel). Token estimation: `len / 4`. Cost estimation: `0.0` (external agent, no direct API cost).

**CLIAgentRunner** (`src/runner/`):
- `is_available()` / `version()` -- checks if binary is installed
- `run(options)` -- executes and returns structured `CLIAgentResult`
- `stream(options, tx)` -- executes with streaming events via channel
- `cancel()` -- uses `CancellationToken` to kill the subprocess
- `build_args(options)` -- constructs CLI arguments from config + options
- `parse_event(line)` -- deserializes stream-json lines into `CLIAgentEvent`

**Discovery** (`src/discovery.rs`): `discover_agents()` runs availability checks for all built-in configs in parallel. `create_providers(agents, yolo)` wraps discovered agents as `Arc<dyn LLMProvider>` with `cli:` prefix.

**Bridge** (`src/bridge.rs`): `execute_with_cli_agent()` runs a task with role-based settings:

| Role | Timeout | Yolo | Prompt Style |
|------|--------:|:----:|-------------|
| Engineer | 600s | yes | "Implement... Write clean, tested code. Commit when done." |
| Reviewer | 300s | no | "Review... Run lint and tests to verify." |
| Subagent | 120s | no | "Research... report your findings." |

Tool scoping per role (when supported): Engineer gets Edit/Write/Bash/Read/Glob/Grep, Reviewer gets Read/Bash/Glob/Grep, Subagent gets Read/Glob/Grep.

### Source Files

| File | Lines | Purpose |
|------|------:|---------|
| `src/lib.rs` | 20 | Re-exports |
| `src/config.rs` | 153 | CLIAgentConfig, PromptMode, CLIAgentEvent |
| `src/configs.rs` | 183 | 5 built-in agent configs |
| `src/provider.rs` | 283 | CLIAgentLLMProvider (LLMProvider impl) |
| `src/bridge.rs` | 170 | Role-based execution, tier prompts |
| `src/discovery.rs` | 200 | Parallel agent discovery, provider creation |
| `src/runner/mod.rs` | 239 | CLIAgentRunner, RunOptions |
| `src/runner/args.rs` | 69 | Argument construction |
| `src/runner/execution.rs` | 189 | Process spawning, streaming, timeout/cancel |
