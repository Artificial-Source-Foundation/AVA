# ava-tools

> Tool system for AVA -- trait, registry, built-in tools, custom tools, MCP bridge, middleware, and monitoring.

**Crate path:** `crates/ava-tools/`
**Primary modules:** `registry`, `core/`, `edit/`, `mcp_bridge`, `monitor`, `permission_middleware`

---

## Overview

The `ava-tools` crate defines the `Tool` trait that all tools implement, the `ToolRegistry` that manages tool lookup and execution, and the full set of built-in tools. It also provides:

- A middleware pipeline for cross-cutting concerns (permissions, sandboxing)
- An MCP bridge to expose Model Context Protocol tools as native tools
- A monitoring subsystem that detects stuck loops
- Custom TOML-based tools loaded from `~/.ava/tools/` and `.ava/tools/`
- A 9-strategy edit engine with fuzzy matching and recovery

---

## Tool Trait

**File:** `crates/ava-tools/src/registry.rs`, lines 21-36

Every tool implements this trait:

```rust
#[async_trait]
pub trait Tool: Send + Sync {
    fn name(&self) -> &str;
    fn description(&self) -> &str;
    fn parameters(&self) -> Value;       // JSON Schema
    async fn execute(&self, args: Value) -> Result<ToolResult>;

    // Optional streaming support (default wraps execute())
    async fn execute_streaming(&self, args: Value) -> Result<ToolOutput>;
}
```

- `name()` -- unique identifier used in LLM tool-call payloads (e.g., `"read"`, `"bash"`)
- `description()` -- human-readable text shown to the LLM in the tool list
- `parameters()` -- JSON Schema describing input parameters
- `execute()` -- async execution returning `ToolResult { call_id, content, is_error }`
- `execute_streaming()` -- optional streaming variant returning `ToolOutput::Streaming(Pin<Box<dyn Stream<Item=String>>>)` (only `bash` overrides this)

### How to implement a new tool

1. Create `crates/ava-tools/src/core/{tool_name}.rs`
2. Define a struct (optionally holding `Arc<dyn Platform>` or other dependencies)
3. Implement all 4 required `Tool` methods
4. Register in `crates/ava-tools/src/core/mod.rs` inside `register_core_tools()`
5. Add tests, run `cargo test -p ava-tools`

---

## ToolRegistry

**File:** `crates/ava-tools/src/registry.rs`, lines 67-201

Central registry of available tools with middleware pipeline and source tracking.

```rust
pub struct ToolRegistry {
    tools: HashMap<String, Box<dyn Tool>>,
    sources: HashMap<String, ToolSource>,
    middleware: Vec<Box<dyn Middleware>>,
}
```

### Key methods

| Method | Description |
|--------|-------------|
| `register(tool)` | Register a tool with `ToolSource::BuiltIn` |
| `register_with_source(tool, source)` | Register with explicit source |
| `unregister(name)` | Remove a tool by name |
| `remove_by_source(predicate)` | Remove all tools matching a source predicate (used for MCP reload) |
| `add_middleware(middleware)` | Add middleware to the pipeline |
| `execute(tool_call)` | Run middleware `before()`, execute tool, run middleware `after()` |
| `list_tools()` | Return sorted `Vec<ToolDefinition>` |
| `list_tools_with_source()` | Return sorted `Vec<(ToolDefinition, ToolSource)>` |
| `tool_count()` | Number of registered tools |

### Execution flow

1. All middleware `before()` hooks run in insertion order
2. Tool is looked up by name (returns `AvaError::ToolNotFound` with available tools list if missing)
3. `tool.execute(args)` is called
4. All middleware `after()` hooks run in insertion order, each can transform the result
5. Final `ToolResult` is returned

### ToolSource

**File:** `crates/ava-tools/src/registry.rs`, lines 49-65

Tracks where a tool came from for grouping in `/tools` command and selective reload:

```rust
pub enum ToolSource {
    BuiltIn,                    // Core tools (read, write, bash, etc.)
    MCP { server: String },     // MCP server tools
    Custom { path: String },    // TOML-defined tools
}
```

---

## Built-in Tools

### Registration

**File:** `crates/ava-tools/src/core/mod.rs`, lines 33-42

`register_core_tools()` registers 8 tools that ship with every agent run:

```rust
pub fn register_core_tools(registry: &mut ToolRegistry, platform: Arc<dyn Platform>) {
    registry.register(read::ReadTool::new(platform.clone()));
    registry.register(write::WriteTool::new(platform.clone()));
    registry.register(edit::EditTool::new(platform.clone()));
    registry.register(bash::BashTool::new(platform.clone()));
    registry.register(glob::GlobTool::new());
    registry.register(grep::GrepTool::new());
    registry.register(apply_patch::ApplyPatchTool::new(platform.clone()));
    registry.register(web_fetch::WebFetchTool::new());
}
```

Additional tools are registered separately:
- `register_task_tool()` -- task tool with a `TaskSpawner`
- `register_todo_tools()` -- todo_write/todo_read with shared `TodoState`
- `register_question_tool()` -- question tool with a `QuestionBridge`
- `register_custom_tools()` -- TOML tools from filesystem directories

Note: Some tools (memory, session, codebase_search) have source files retained but are **not compiled** (crate dependencies removed). Their modules are commented out in `core/mod.rs`.

---

### read

**File:** `crates/ava-tools/src/core/read.rs`

Reads file content with line numbers (cat -n format).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `path` | string | yes | File path to read |
| `offset` | integer | no | Line number to start from (1-based, default 1) |
| `limit` | integer | no | Max lines to return (default 2000) |

- Lines are formatted as `{line_number}\t{content}` with 6-char right-aligned line numbers
- Truncates at `MAX_LINES_DEFAULT` (2000) with a notice message
- Maps `IoError` to `NotFound` or `PermissionDenied` based on error message content
- Uses `Platform::read_file()` for filesystem abstraction

### write

**File:** `crates/ava-tools/src/core/write.rs`

Creates or overwrites a file with the given content.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `path` | string | yes | File path to write |
| `content` | string | yes | Content to write |

- Creates parent directories automatically via `tokio::fs::create_dir_all`
- Returns byte count on success: `"Wrote {n} bytes to {path}"`

### edit

**File:** `crates/ava-tools/src/core/edit.rs`

Edits existing file content using a multi-strategy edit engine.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `path` | string | yes | File path to edit |
| `old_text` | string | yes | Text to find |
| `new_text` | string | yes | Replacement text |
| `replace_all` | boolean | no | Replace all occurrences (default false) |

- When `replace_all` is true, uses simple `String::replace()`
- Otherwise, delegates to `EditEngine` which tries 9 strategies in order (see Edit Engine section)
- Returns the strategy used and number of changed lines

### bash

**File:** `crates/ava-tools/src/core/bash.rs`

Executes shell commands with sandboxing, timeout, and output truncation.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `command` | string | yes | Shell command to run |
| `timeout_ms` | integer | no | Timeout in milliseconds (default 120000) |
| `cwd` | string | no | Working directory |

**Key behaviors:**
- **Dangerous pattern rejection** (line 14): blocks `rm -rf /`, `dd if=`, `mkfs`, fork bomb
- **Install-class routing** (line 198-218): commands matching `npm install`, `pip install`, `cargo add`, etc. are routed through the sandbox (`ava-sandbox`) with a restrictive `SandboxPolicy`
- **Output truncation**: max 100KB, truncates at char boundary with `[truncated]` notice
- **Streaming support**: `execute_streaming()` spawns a child process and streams stdout line-by-line (except for install-class commands)
- **Environment filtering** (line 224-241): sandboxed commands only get `PATH`, `HOME`, `USER`, `SHELL`, `LANG`, `LC_ALL`, `LC_CTYPE`, `TERM`, `CARGO_HOME`, `RUSTUP_HOME`

### glob

**File:** `crates/ava-tools/src/core/glob.rs`

Finds files matching a glob pattern, sorted by modification time (newest first).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `pattern` | string | yes | Glob pattern (e.g., `"**/*.rs"`) |
| `path` | string | no | Base directory (default `.`) |

- Max 1000 results
- Returns one file path per line

### grep

**File:** `crates/ava-tools/src/core/grep.rs`

Searches file contents by regex using the `grep` crate ecosystem (`grep-regex`, `grep-searcher`, `ignore`).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `pattern` | string | yes | Regex pattern |
| `path` | string | no | Search directory (default `.`) |
| `include` | string | no | Filename glob filter (e.g., `"*.rs"`) |

- Uses `WalkBuilder` with `.gitignore` support (respects git_ignore, git_global, git_exclude)
- Shows hidden files
- Max 500 matches
- Output format: `{file}:{line_number}:{line_content}`

### multiedit

**File:** `crates/ava-tools/src/core/multiedit.rs`

Applies multiple edits across one or more files atomically.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `edits` | array | yes | Array of `{path, old_text, new_text}` objects |

- Groups edits by file path (preserving order within each file)
- **Two-pass approach**: validation pass checks all edits can be applied, then apply pass writes files
- If any edit fails validation, **none** are applied (atomic)
- Returns: `"Applied {n} edits across {m} files"`

### apply_patch

**File:** `crates/ava-tools/src/core/apply_patch.rs`

Applies unified diff patches to one or more files.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `patch` | string | yes | Unified diff string |
| `strip` | integer | no | Leading path components to strip (default 1) |

- Parses `--- a/path` / `+++ b/path` headers and `@@ -old,count +new,count @@` hunk headers
- **Fuzzy application**: tries exact position first, then offsets up to +/-3 lines in each direction
- Creates new files if they don't exist
- Reports applied and rejected hunks separately

### test_runner

**File:** `crates/ava-tools/src/core/test_runner.rs`

**Note:** Source retained but not registered in `register_core_tools()`.

Runs project tests with auto-detection of test framework.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `command` | string | no | Custom test command (overrides auto-detection) |
| `filter` | string | no | Test name filter pattern |
| `timeout` | integer | no | Timeout in seconds (default 60) |

- Auto-detects: `cargo test` (Cargo.toml), `npm test` (package.json), `pytest` (pyproject.toml/pytest.ini), `go test ./...` (go.mod)
- Max output 50KB with split truncation (keeps head and tail)
- Returns JSON: `{"passed": bool, "exit_code": int, "output": string}`

### lint

**File:** `crates/ava-tools/src/core/lint.rs`

**Note:** Source retained but not registered in `register_core_tools()`.

Runs project linter with auto-detection.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `command` | string | no | Custom lint command |
| `fix` | boolean | no | Apply auto-fixes if supported |
| `path` | string | no | Scope lint to this path |

- Auto-detects: `cargo clippy` (Rust), `npx eslint .` (JS/TS), `ruff check .` (Python)
- Parses ESLint and Rust/clippy diagnostic output to count warnings and errors
- Returns JSON: `{"warnings": int, "errors": int, "output": string, "fixed": bool}`

### diagnostics

**File:** `crates/ava-tools/src/core/diagnostics.rs`

**Note:** Source retained but not registered in `register_core_tools()`.

Gets compiler/type-checker diagnostics for the project.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `path` | string | no | File path to check (optional, checks whole project) |

- Auto-detects: `cargo check --message-format=json` (Rust), `npx tsc --noEmit` (TS), `python -m py_compile` (Python)
- Parses cargo JSON messages and TypeScript diagnostic output
- Returns JSON: `{"diagnostics": [{file, line, severity, message}]}`

### remember

**File:** `crates/ava-tools/src/core/memory.rs`, lines 10-62

**Note:** Source retained but not compiled (crate dependency `ava-memory` removed from build).

Stores a key-value pair in persistent memory (SQLite via `ava-memory`).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `key` | string | yes | Memory key identifier |
| `value` | string | yes | Value to remember |

### recall

**File:** `crates/ava-tools/src/core/memory.rs`, lines 64-116

**Note:** Source retained but not compiled.

Recalls a value from persistent memory by key.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `key` | string | yes | Memory key to recall |

### memory_search

**File:** `crates/ava-tools/src/core/memory.rs`, lines 118-184

**Note:** Source retained but not compiled.

Searches persistent memory using full-text search.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `query` | string | yes | Search query |
| `limit` | integer | no | Max results (default 10) |

### session_search

**File:** `crates/ava-tools/src/core/session_search.rs`

**Note:** Source retained but not compiled.

Searches past sessions by content using full-text search (via `ava-session`).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `query` | string | yes | Search query |
| `limit` | integer | no | Max results (default 5) |

### session_list

**File:** `crates/ava-tools/src/core/session_ops.rs`, lines 10-73

**Note:** Source retained but not compiled.

Lists recent sessions with message counts and timestamps.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `limit` | integer | no | Max sessions to list (default 10) |

### session_load

**File:** `crates/ava-tools/src/core/session_ops.rs`, lines 75-151

**Note:** Source retained but not compiled.

Loads a past session by UUID, returning messages with role and truncated content (200 chars).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `id` | string | yes | Session UUID |

### codebase_search

**File:** `crates/ava-tools/src/core/codebase_search.rs`

**Note:** Source retained but not compiled.

Searches the codebase index for files matching a query (BM25 ranked, via `ava-codebase`).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `query` | string | yes | Search query |
| `limit` | integer | no | Max results (default 10) |

- Returns `"not yet available"` if index is still building
- Results include path, score, and snippet

### git (git_read)

**File:** `crates/ava-tools/src/core/git_read.rs`

Runs read-only git commands. The `git` prefix is added automatically.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `command` | string | yes | Git subcommand and arguments (e.g., `"diff --staged"`) |

- Validates against `is_safe_git_command()` from `ava-permissions::classifier::rules`
- Allowed subcommands: `status`, `log`, `diff`, `branch`, `show`, `tag`, `remote`, `stash list`, `shortlog`, `describe`, `rev-parse`, `ls-files`, `blame`
- Rejects: `push`, `commit`, `reset`, `checkout`, etc.

### web_fetch

**File:** `crates/ava-tools/src/core/web_fetch.rs`

Fetches a URL and returns its content. Extracts text from HTML, pretty-prints JSON.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `url` | string | yes | URL to fetch |
| `max_length` | integer | no | Max characters to return (default 50000) |

- **SSRF prevention**: blocks `localhost`, `127.x.x.x`, `::1`, `0.0.0.0`, non-HTTP schemes (`file://`, `ftp://`, etc.)
- 30-second timeout, max 5 redirects
- HTML processing: strips `<script>`, `<style>`, all tags; decodes HTML entities; collapses blank lines
- User agent: `ava/2.1`

### task

**File:** `crates/ava-tools/src/core/task.rs`

Spawns a sub-agent to work on a task autonomously.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `prompt` | string | yes | Task description for the sub-agent |

- Sub-agent gets core tools (read, write, edit, bash, glob, grep, apply_patch) but NOT task, todo_write, todo_read, or question -- prevents infinite recursion
- Uses `TaskSpawner` trait (defined in ava-tools, implemented in ava-agent)
- Returns `TaskResult { text, session_id, messages }`

### todo_write

**File:** `crates/ava-tools/src/core/todo.rs`, lines 14-159

Creates or replaces the entire agent todo list. Full-replace semantics.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `todos` | array | yes | Array of `{content, status, priority}` objects |

- Status values: `pending`, `in_progress`, `completed`, `cancelled`
- Priority values: `high`, `medium`, `low`
- Shared state via `TodoState` so the TUI can display progress

### todo_read

**File:** `crates/ava-tools/src/core/todo.rs`, lines 161-219

Reads the current todo list. No parameters.

### question

**File:** `crates/ava-tools/src/core/question.rs`

Asks the user a question and waits for their answer via the TUI.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `question` | string | yes | The question to ask |
| `options` | array of strings | no | Optional selectable choices |

- Uses `QuestionBridge` (mpsc channel) to communicate with the TUI
- TUI receives `QuestionRequest`, shows modal, sends answer back via oneshot channel
- Returns `"User's answer: {answer}"` or `"The user declined to answer the question."` for empty answers

---

## Edit Engine

**File:** `crates/ava-tools/src/edit/mod.rs`

The `EditEngine` tries 9 strategies in order until one succeeds:

| # | Strategy | File | Description |
|---|----------|------|-------------|
| 1 | `ExactMatchStrategy` | `edit/strategies/mod.rs:17` | Direct `String::replacen()` |
| 2 | `FlexibleMatchStrategy` | `edit/strategies/mod.rs:37` | Whitespace-normalized line-by-line comparison |
| 3 | `BlockAnchorStrategy` | `edit/strategies/advanced.rs:8` | Replace between before/after anchor strings |
| 4 | `RegexMatchStrategy` | `edit/strategies/advanced.rs:47` | Regex-based replacement |
| 5 | `FuzzyMatchStrategy` | `edit/fuzzy_match.rs:34` | Weighted edit-distance fuzzy match (substitution cost 2, indel cost 1, max distance 8) |
| 6 | `LineNumberStrategy` | `edit/strategies/advanced.rs:73` | Replace at specific line number |
| 7 | `TokenBoundaryStrategy` | `edit/strategies/advanced.rs:107` | Whole-word boundary matching via `\b` regex |
| 8 | `IndentationAwareStrategy` | `edit/strategies/advanced.rs:131` | Matches trimmed content, preserves original indentation |
| 9 | `MultiOccurrenceStrategy` | `edit/strategies/advanced.rs:168` | Targets the Nth occurrence of the match |

### EditRequest

**File:** `crates/ava-tools/src/edit/request.rs`

```rust
pub struct EditRequest {
    pub content: String,           // Full file content
    pub old_text: String,          // Text to find
    pub new_text: String,          // Replacement text
    pub before_anchor: Option<String>,  // For BlockAnchorStrategy
    pub after_anchor: Option<String>,
    pub line_number: Option<usize>,     // For LineNumberStrategy
    pub regex_pattern: Option<String>,  // For RegexMatchStrategy
    pub occurrence: Option<usize>,      // For MultiOccurrenceStrategy
}
```

### RecoveryPipeline

**File:** `crates/ava-tools/src/edit/recovery.rs`

A tiered recovery pipeline that tries strategies in order (exact -> flexible -> regex -> fuzzy) and tracks which tier succeeded. Supports optional `SelfCorrector` trait for pre-processing failed requests.

### FuzzyMatchStrategy / StreamingMatcher

**File:** `crates/ava-tools/src/edit/fuzzy_match.rs`

Uses weighted Levenshtein distance to find near-matches:
- Substitution cost: 2
- Insertion/deletion cost: 1
- Max allowed distance: `min(max_distance, needle_len/3 + 2)`
- Candidate window: 60%-140% of needle length
- `match_stream()` can search across a stream of text chunks

---

## Custom Tools (TOML)

**File:** `crates/ava-tools/src/core/custom_tool.rs`

Custom tools are defined in `.toml` files in `~/.ava/tools/` (global) and `.ava/tools/` (project-local).

### TOML format

```toml
name = "hello"
description = "A simple greeting tool"

[[params]]
name = "name"
type = "string"
required = true
description = "Name to greet"

[execution]
type = "shell"          # or "script"
command = "echo 'Hello, {{name}}!'"
timeout_secs = 5
```

### Execution types

- **Shell** (`type = "shell"`): runs via `sh -c "{command}"` with `{{param}}` template substitution
- **Script** (`type = "script"`): runs via `{interpreter} -c "{script}"` (e.g., `python3`)

### Registration flow

1. `register_custom_tools(registry, dirs)` scans each directory for `.toml` files
2. Each file is parsed into `CustomToolDef` via `toml::from_str()`
3. A `CustomTool` wrapper is created implementing the `Tool` trait
4. Registered with `ToolSource::Custom { path }` for grouping

### Template substitution

`{{param_name}}` placeholders in commands/scripts are replaced with argument values from the JSON `args` object. String values are used directly; other types are JSON-stringified.

---

## MCP Bridge

**File:** `crates/ava-tools/src/mcp_bridge.rs`

Bridges MCP (Model Context Protocol) tools into the `ToolRegistry`.

```rust
pub trait MCPToolCaller: Send + Sync {
    async fn call_tool(&self, name: &str, arguments: Value) -> Result<ToolResult>;
}

pub struct MCPBridgeTool {
    definition: ToolDefinition,
    caller: Arc<dyn MCPToolCaller>,
}
```

- `MCPBridgeTool` wraps a `ToolDefinition` (name, description, parameters) and an `MCPToolCaller`
- Implements `Tool` trait, delegating `execute()` to `caller.call_tool()`
- Registered in the `ToolRegistry` like any other tool -- the agent sees no difference
- The concrete `MCPToolCaller` implementation lives in `ava-mcp`

---

## Middleware

**File:** `crates/ava-tools/src/registry.rs`, lines 39-47

```rust
#[async_trait]
pub trait Middleware: Send + Sync {
    async fn before(&self, tool_call: &ToolCall) -> Result<()>;
    async fn after(&self, tool_call: &ToolCall, result: &ToolResult) -> Result<ToolResult>;
}
```

- `before()` runs before tool execution -- can reject by returning `Err`
- `after()` runs after execution -- can transform or replace the result
- Middleware runs in insertion order (not priority-sorted at the registry level)

### PermissionMiddleware

**File:** `crates/ava-tools/src/permission_middleware.rs`

Bridges `ava-permissions` into the tool middleware pipeline.

```rust
pub struct PermissionMiddleware {
    inspector: Arc<dyn PermissionInspector>,
    context: Arc<RwLock<InspectionContext>>,
}
```

- `before()`: calls `inspector.inspect()` and maps `Action::Allow` to `Ok(())`, `Action::Deny`/`Action::Ask` to `Err(AvaError::PermissionDenied)`
- `after()`: passes result through unchanged

---

## Tool Monitoring

**File:** `crates/ava-tools/src/monitor.rs`

Tracks tool usage patterns and detects stuck loops.

### ToolExecution

```rust
pub struct ToolExecution {
    pub tool_name: String,
    pub arguments_hash: u64,
    pub success: bool,
    pub duration: Duration,
    pub timestamp: Instant,
}
```

### ToolMonitor

Maintains a history of executions and detects three repetition patterns:

| Pattern | Detection | Threshold |
|---------|-----------|-----------|
| `ExactRepeat` | Same tool + same args hash | 3 consecutive calls |
| `AlternatingLoop` | A-B-A-B-A-B pattern | 6 consecutive calls (3 cycles) |
| `ToolLoop` | Same tool regardless of args | 5 consecutive calls |

Detection priority: ExactRepeat > AlternatingLoop > ToolLoop (checked in that order).

### ToolStats

Aggregate statistics:
- `total_calls`, `unique_tools`, `total_errors`, `total_duration_ms`
- `tool_breakdown: Vec<(name, calls, errors)>` sorted by usage count

### Additional methods

- `most_used()` -- tools sorted by call count descending
- `error_rate(tool_name)` -- 0.0-1.0 error rate for a specific tool
- `recent_error_rate(last_n)` -- error rate over last N calls across all tools
- `hash_arguments(args)` -- deterministic u64 hash of a `serde_json::Value`

---

## Browser Module

**File:** `crates/ava-tools/src/browser.rs`

A browser automation abstraction (not exposed as a registered tool):

- `BrowserAction` enum: `Navigate`, `Click`, `Type`, `Extract`, `Screenshot`
- `BrowserDriver` trait: implement per browser backend
- `BrowserEngine`: dispatches actions to the driver from JSON payloads

---

## Git Module

**File:** `crates/ava-tools/src/git/mod.rs`

Low-level git command execution (separate from `git_read` tool):

- `GitAction` enum: `Commit`, `Branch`, `Checkout`, `Status`, `Diff`, `Log`, `Pr`
- `GitTool::dispatch()` maps actions to `(program, args)` -- uses `gh` for PR operations
- `GitTool::run()` executes async and returns `ToolResult` with stdout/stderr/exit_code
