# AVA Rust Migration: Architecture & Boundary Analysis

> **Scope**: 12–18 month migration roadmap for moving AVA's TypeScript core to Rust
> **Status**: Planning / RFC
> **Last updated**: 2026-03-03

---

## Table of Contents

1. [Current Architecture](#1-current-architecture)
2. [Rust/Tauri Integration Points](#2-rusttauri-integration-points)
3. [Proposed Rust Crate Structure](#3-proposed-rust-crate-structure)
4. [Migration Phases](#4-migration-phases)
5. [Risks & Mitigation](#5-risks--mitigation)

---

## 1. Current Architecture

### 1.1 High-Level Topology

```
┌──────────────────────────────────────────────────────────────────┐
│                        Tauri Shell (Rust)                        │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌───────────┐ │
│  │  PTY Mgr   │  │   OAuth    │  │  FS Scope  │  │  Plugins  │ │
│  │ (portable- │  │ (reqwest,  │  │ (Tauri     │  │  State    │ │
│  │  pty 0.8)  │  │  device    │  │  allow_    │  │ (JSON     │ │
│  │            │  │  flow)     │  │  scope)    │  │  CRUD)    │ │
│  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘  └─────┬─────┘ │
│        │ invoke()       │ invoke()      │ invoke()      │       │
├────────┼────────────────┼───────────────┼───────────────┼───────┤
│        ▼                ▼               ▼               ▼       │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │              SolidJS Frontend (WebView)                   │   │
│  │  ┌──────────┐  ┌──────────┐  ┌───────────┐  ┌────────┐  │   │
│  │  │useAgent  │  │useChat   │  │core-bridge│  │pty-    │  │   │
│  │  │  .ts     │  │  .ts     │  │  .ts      │  │bridge  │  │   │
│  │  └────┬─────┘  └────┬─────┘  └─────┬─────┘  └───┬────┘  │   │
│  │       │              │              │            │        │   │
│  │       ▼              ▼              ▼            ▼        │   │
│  │  ┌──────────────────────────────────────────────────┐    │   │
│  │  │               @ava/core-v2 (TypeScript)          │    │   │
│  │  │  agent/ tools/ llm/ mcp/ session/ commander/     │    │   │
│  │  │  context/ permissions/ codebase/ lsp/ bus/ ...   │    │   │
│  │  └──────────────────┬───────────────────────────────┘    │   │
│  │                     │ getPlatform()                       │   │
│  │                     ▼                                     │   │
│  │  ┌──────────────────────────────────────────────────┐    │   │
│  │  │           platform-tauri (TypeScript)             │    │   │
│  │  │  fs.ts  shell.ts  database.ts  credentials.ts    │    │   │
│  │  │  → calls @tauri-apps/plugin-* → invoke() → Rust  │    │   │
│  │  └──────────────────────────────────────────────────┘    │   │
│  └──────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

### 1.2 Where the Logic Lives

| Layer | Language | Role | Lines (est.) |
|-------|----------|------|-------------|
| `src-tauri/src/` | Rust | Thin shell: PTY, OAuth, env vars, log files, plugin state, FS scope | ~800 |
| `packages/core/src/` | TypeScript | **ALL business logic**: agent loop, 25+ tools, 13 LLM providers, MCP, commander, sessions, permissions, codebase analysis, context tracking, etc. | ~25,000 |
| `packages/platform-tauri/src/` | TypeScript | Platform bridge: wraps Tauri plugin APIs into `IPlatformProvider` interfaces | ~1,200 |
| `packages/platform-node/src/` | TypeScript | Node.js platform impl for CLI | ~1,000 |
| `src/` | TypeScript/SolidJS | Frontend: UI components, hooks, services | ~15,000 |

**Key insight**: The Rust backend currently does almost nothing — it's a thin Tauri command layer. All AI agent logic, tool execution, LLM streaming, MCP orchestration, and session management runs in the webview's JavaScript context.

### 1.3 Core Module Inventory (32 modules)

These modules in `packages/core/src/` must all eventually migrate to Rust:

| Module | Complexity | External I/O | Migration Priority |
|--------|-----------|-------------|-------------------|
| `agent/` | **Critical** — main loop, evaluator, planner, recovery, subagent, modes | LLM streaming, tool execution | Phase 3 |
| `tools/` | **Critical** — 25+ tools (bash, read, write, edit, glob, grep, browser, websearch, etc.) | FS, shell, network, PTY | Phase 2–3 |
| `llm/` | **High** — client interface, provider registry, 13 providers | HTTP streaming (SSE/WebSocket) | Phase 3 |
| `commander/` | **High** — hierarchical delegation, DAG parallel execution, file conflict detection | Spawns sub-agents | Phase 3 |
| `mcp/` | **High** — MCP client manager (stdio, SSE, HTTP transports) | Process spawning, HTTP | Phase 3 |
| `session/` | **Medium** — LRU cache, persistence, checkpoints, fork | Database | Phase 2 |
| `context/` | **Medium** — token tracking, compaction strategies | LLM tokenizer | Phase 2 |
| `permissions/` | **Medium** — audit, auto-approve, validators, inspectors, rules | FS (rules files) | Phase 2 |
| `codebase/` | **Medium** — dependency graph, indexer, repomap, tree-sitter | FS, tree-sitter | Phase 2 |
| `bus/` | **Low** — pub/sub message bus | None (in-memory) | Phase 1 |
| `config/` | **Low** — settings, validation | FS | Phase 1 |
| `models/` | **Low** — model registry, pricing | None (static data) | Phase 1 |
| `lsp/` | **Medium** — diagnostics, call hierarchy | LSP protocol | Phase 3 |
| `extensions/` | **Medium** — extension manager, manifest, tool middleware | FS, dynamic loading | Phase 3 |
| `diff/`, `git/` | **Low–Medium** | Shell (git CLI) | Phase 2 |
| `auth/` | **Medium** — OAuth + PKCE | HTTP, credential store | Phase 2 |
| Others (`hooks/`, `instructions/`, `logger/`, `policy/`, `question/`, `scheduler/`, `skills/`, `slash-commands/`, `validator/`, `focus-chain/`, `custom-commands/`, `integrations/`) | **Low–Medium** | Varies | Phase 2–3 |

### 1.4 The Platform Abstraction Layer — The Migration Boundary

The single most important file for migration is `packages/core/src/platform.ts`. It defines:

```
IPlatformProvider
├── fs: IFileSystem        (14 methods)
├── shell: IShell          (2 methods)
├── credentials: ICredentialStore (4 methods)
├── database: IDatabase    (4 methods)
└── pty?: IPTY             (2 methods)
```

This abstraction was designed for multi-platform support (Tauri vs Node.js) but it is **exactly** the seam where Rust can progressively replace TypeScript. The `getPlatform()` / `setPlatform()` singleton pattern means the entire core codebase is already decoupled from platform specifics.

**Migration strategy**: Implement `IPlatformProvider` methods as Tauri commands backed by Rust, then point `platform-tauri` TypeScript wrappers at those commands. Over time, move the *callers* (core modules) to Rust too, eliminating the bridge entirely.

---

## 2. Rust/Tauri Integration Points

### 2.1 Current IPC Mechanism

```
Frontend JS  ──invoke("command_name", {args})──►  Rust #[tauri::command]
             ◄──Result<T, String>──────────────   (serde serialized)

Rust         ──app.emit("event_name", payload)──►  Frontend JS
             (via tauri::Emitter)                   listen("event_name")
```

**17 registered commands** (as of current state):
- `greet` — demo/health check
- `oauth_listen`, `oauth_copilot_device_start`, `oauth_copilot_device_poll` — auth
- `get_env_var` — env reading (with allowlist)
- `allow_project_path` — expand FS scope
- `append_log`, `cleanup_old_logs` — log file management
- `get_cwd` — working directory
- `get_plugins_state`, `set_plugins_state`, `install_plugin`, `uninstall_plugin`, `set_plugin_enabled` — plugin state
- `pty_spawn`, `pty_write`, `pty_resize`, `pty_kill` — PTY management

### 2.2 Existing Rust Capabilities

| Capability | Implementation | Quality |
|-----------|---------------|---------|
| PTY management | `pty.rs` — PtyManager with HashMap<String, session>, portable-pty | **Production-ready** |
| OAuth | `commands/oauth.rs` — HTTP listener + GitHub Copilot device flow | **Production-ready** |
| Plugin state | `commands/plugin_state.rs` — JSON file CRUD | **Production-ready** |
| Env vars | `commands/env.rs` — allowlisted env reading | **Production-ready** |
| FS scope | `commands/fs_scope.rs` — Tauri scope expansion | **Production-ready** |
| Dev logging | `commands/dev_log.rs` — append/cleanup log files | **Production-ready** |
| Tool implementations | `tools/mod.rs` — **PLACEHOLDER** (comments only) | **Not started** |
| Database ops | `db/mod.rs` — **PLACEHOLDER** (comments only) | **Not started** |

### 2.3 Communication Patterns for Migration

The migration will use three IPC patterns, introduced progressively:

#### Pattern A: Command Invoke (Current)
```
JS → invoke("rust_fn", {args}) → Rust → Result<T>
```
**Use for**: Request/response operations (file read, database query, etc.)
**Limitation**: Synchronous from JS perspective (await), no streaming.

#### Pattern B: Event Streaming (Partially used)
```
Rust → app.emit("event_name", payload) → JS listen()
```
**Use for**: PTY output, LLM token streaming, agent progress updates.
**Advantage**: Rust-initiated, allows long-running operations to push data to frontend.

#### Pattern C: Channel Streaming (New — Tauri 2.x)
```
JS → invoke("stream_fn", {args, onEvent: channel}) → Rust
Rust → channel.send(chunk) → JS callback (multiple times)
       channel.send(done) → JS callback (final)
```
**Use for**: LLM response streaming, tool output streaming, agent loop events.
**Advantage**: Bidirectional, backpressure-aware, tied to specific request.

#### Pattern D: Sidecar Process (Future consideration)
```
Rust spawns sidecar ──stdio──► MCP server / LSP server
```
**Use for**: MCP server hosting, LSP integration, isolated tool sandboxing.

### 2.4 Serialization Boundary

All data crossing the JS↔Rust boundary must be `serde::Serialize + serde::Deserialize`. This means:

- Define Rust structs mirroring TypeScript interfaces
- Use `#[serde(rename_all = "camelCase")]` to match JS naming conventions
- Use `Option<T>` for optional fields
- Use `#[serde(tag = "type")]` for discriminated unions (TypeScript union types)
- Large binary data (file contents) should use `Vec<u8>` / `Uint8Array`

**Example — mirroring `ExecResult`**:
```rust
#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ExecResult {
    pub stdout: String,
    pub stderr: String,
    pub exit_code: i32,
}
```

---

## 3. Proposed Rust Crate Structure

### 3.1 Workspace Layout

```
src-tauri/
├── Cargo.toml              (workspace root)
├── crates/
│   ├── ava-types/          # Shared types, error types, serde models
│   ├── ava-platform/       # Platform trait definitions (Rust equivalent of platform.ts)
│   ├── ava-fs/             # Filesystem operations, glob, path security
│   ├── ava-shell/          # Shell execution, PTY management
│   ├── ava-db/             # SQLite operations, migrations, session persistence
│   ├── ava-auth/           # OAuth, credential store, API key management
│   ├── ava-tools/          # Tool registry + implementations
│   ├── ava-llm/            # LLM client, provider registry, streaming
│   ├── ava-mcp/            # MCP client (stdio, SSE, HTTP transports)
│   ├── ava-agent/          # Agent loop, evaluator, planner, recovery
│   ├── ava-commander/      # Hierarchical delegation, worker management
│   ├── ava-context/        # Token tracking, compaction
│   ├── ava-codebase/       # Repo analysis, tree-sitter, symbols
│   ├── ava-permissions/    # Security, audit, rules
│   ├── ava-session/        # Session management, checkpoints
│   ├── ava-config/         # Configuration, validation
│   ├── ava-lsp/            # LSP client integration
│   └── ava-sandbox/        # Process isolation, resource limits
└── src/
    ├── lib.rs              # Tauri app setup, command registration
    ├── bridge.rs           # JS↔Rust bridge layer (invoke handlers)
    └── commands/           # Tauri command handlers (thin wrappers)
```

### 3.2 Crate Dependency Graph

```
                    ava-types (foundation — no deps)
                        │
              ┌─────────┼──────────┐
              ▼         ▼          ▼
         ava-config  ava-platform  ava-db
              │         │          │
              ▼         ▼          ▼
         ava-auth    ava-fs    ava-session
              │      ava-shell     │
              │         │          │
              ▼         ▼          ▼
         ava-permissions  ava-context
              │              │
              ▼              ▼
         ava-tools ◄── ava-codebase
              │         ava-lsp
              │
              ▼
         ava-llm ──► ava-mcp
              │
              ▼
         ava-agent
              │
              ▼
         ava-commander
              │
              ▼
         ava-sandbox (wraps agent/tools for isolation)
```

### 3.3 Crate Public APIs

#### `ava-types`
The foundation crate. All other crates depend on this.

```rust
// ava-types/src/lib.rs

/// Core error type used across all crates
#[derive(Debug, thiserror::Error, Serialize)]
pub enum AvaError {
    #[error("IO error: {0}")]
    Io(String),
    #[error("Permission denied: {0}")]
    PermissionDenied(String),
    #[error("Tool error: {tool} - {message}")]
    Tool { tool: String, message: String },
    #[error("LLM error: {provider} - {message}")]
    Llm { provider: String, message: String },
    #[error("Session error: {0}")]
    Session(String),
    #[error("Config error: {0}")]
    Config(String),
    #[error("Timeout after {duration_ms}ms")]
    Timeout { duration_ms: u64 },
    // ...
}

pub type AvaResult<T> = Result<T, AvaError>;

/// Tool call representation
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolCall {
    pub id: String,
    pub name: String,
    pub arguments: serde_json::Value,
}

/// Tool result
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolResult {
    pub tool_call_id: String,
    pub content: String,
    pub is_error: bool,
    pub metadata: Option<serde_json::Value>,
}

/// LLM message types
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "role", rename_all = "camelCase")]
pub enum Message {
    System { content: String },
    User { content: Vec<ContentBlock> },
    Assistant { content: Vec<ContentBlock>, tool_calls: Vec<ToolCall> },
    Tool { tool_call_id: String, content: String, is_error: bool },
}

/// Content block (text, image, tool_use, tool_result)
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum ContentBlock {
    Text { text: String },
    Image { media_type: String, data: String },
    ToolUse { id: String, name: String, input: serde_json::Value },
    ToolResult { tool_use_id: String, content: String, is_error: bool },
}

/// Model definition
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ModelInfo {
    pub id: String,
    pub provider: String,
    pub context_window: u32,
    pub max_output: u32,
    pub input_price_per_mtok: f64,
    pub output_price_per_mtok: f64,
    pub supports_vision: bool,
    pub supports_tools: bool,
    pub supports_streaming: bool,
}

/// Session state
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Session {
    pub id: String,
    pub project_path: String,
    pub model_id: String,
    pub messages: Vec<Message>,
    pub created_at: i64,
    pub updated_at: i64,
    pub total_input_tokens: u64,
    pub total_output_tokens: u64,
    pub total_cost_usd: f64,
}

/// Agent turn metadata
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TurnMetadata {
    pub turn_number: u32,
    pub input_tokens: u64,
    pub output_tokens: u64,
    pub cost_usd: f64,
    pub duration_ms: u64,
    pub tool_calls: Vec<String>,
    pub model_id: String,
}
```

#### `ava-platform`
Rust equivalent of `platform.ts` — trait definitions for platform operations.

```rust
// ava-platform/src/lib.rs
use ava_types::AvaResult;

#[async_trait]
pub trait FileSystem: Send + Sync {
    async fn read_file(&self, path: &Path) -> AvaResult<String>;
    async fn read_binary(&self, path: &Path, limit: Option<usize>) -> AvaResult<Vec<u8>>;
    async fn write_file(&self, path: &Path, content: &str) -> AvaResult<()>;
    async fn write_binary(&self, path: &Path, content: &[u8]) -> AvaResult<()>;
    async fn read_dir(&self, path: &Path) -> AvaResult<Vec<String>>;
    async fn read_dir_with_types(&self, path: &Path) -> AvaResult<Vec<DirEntry>>;
    async fn stat(&self, path: &Path) -> AvaResult<FileStat>;
    async fn exists(&self, path: &Path) -> AvaResult<bool>;
    async fn is_file(&self, path: &Path) -> AvaResult<bool>;
    async fn is_directory(&self, path: &Path) -> AvaResult<bool>;
    async fn mkdir(&self, path: &Path) -> AvaResult<()>;
    async fn remove(&self, path: &Path) -> AvaResult<()>;
    async fn glob(&self, pattern: &str, cwd: &Path) -> AvaResult<Vec<PathBuf>>;
    async fn realpath(&self, path: &Path) -> AvaResult<PathBuf>;
}

#[async_trait]
pub trait Shell: Send + Sync {
    async fn exec(&self, command: &str, options: ExecOptions) -> AvaResult<ExecResult>;
    fn spawn(&self, command: &str, args: &[&str], options: SpawnOptions)
        -> AvaResult<Box<dyn ChildProcess>>;
}

#[async_trait]
pub trait Pty: Send + Sync {
    fn is_supported(&self) -> bool;
    fn spawn(&self, command: &str, args: &[&str], options: PtyOptions)
        -> AvaResult<Box<dyn PtyProcess>>;
}

#[async_trait]
pub trait CredentialStore: Send + Sync {
    async fn get(&self, key: &str) -> AvaResult<Option<String>>;
    async fn set(&self, key: &str, value: &str) -> AvaResult<()>;
    async fn delete(&self, key: &str) -> AvaResult<()>;
    async fn has(&self, key: &str) -> AvaResult<bool>;
}

#[async_trait]
pub trait Database: Send + Sync {
    async fn query<T: DeserializeOwned>(&self, sql: &str, params: &[SqlParam]) -> AvaResult<Vec<T>>;
    async fn execute(&self, sql: &str, params: &[SqlParam]) -> AvaResult<()>;
    async fn migrate(&self, migrations: &[Migration]) -> AvaResult<()>;
    async fn close(&self) -> AvaResult<()>;
}

/// Aggregated platform provider
pub struct Platform {
    pub fs: Arc<dyn FileSystem>,
    pub shell: Arc<dyn Shell>,
    pub credentials: Arc<dyn CredentialStore>,
    pub database: Arc<dyn Database>,
    pub pty: Option<Arc<dyn Pty>>,
}
```

#### `ava-fs`
Concrete filesystem implementation with security.

```rust
// ava-fs/src/lib.rs
pub struct SecureFs {
    /// Allowed root paths (project directories)
    allowed_roots: Vec<PathBuf>,
    /// Denied patterns (e.g., .env, credentials.json)
    denied_patterns: Vec<glob::Pattern>,
}

impl SecureFs {
    pub fn new(allowed_roots: Vec<PathBuf>) -> Self;
    pub fn allow_root(&mut self, path: PathBuf);

    /// Validates path is within allowed roots and not denied
    fn validate_path(&self, path: &Path) -> AvaResult<PathBuf>;
}

impl FileSystem for SecureFs {
    // All methods validate paths before operating
}

// Glob implementation using `globwalk` or `ignore` crate
pub struct GlobWalker;
impl GlobWalker {
    pub fn walk(pattern: &str, cwd: &Path, gitignore: bool) -> AvaResult<Vec<PathBuf>>;
}
```

#### `ava-shell`
Shell execution with process management.

```rust
// ava-shell/src/lib.rs
pub struct ShellExecutor {
    default_shell: String,  // e.g., "/bin/bash"
    default_cwd: PathBuf,
    env_allowlist: HashSet<String>,
}

impl Shell for ShellExecutor {
    async fn exec(&self, command: &str, options: ExecOptions) -> AvaResult<ExecResult>;
    fn spawn(&self, command: &str, args: &[&str], options: SpawnOptions)
        -> AvaResult<Box<dyn ChildProcess>>;
}

/// Enhanced PTY manager (evolution of current pty.rs)
pub struct PtyManager {
    sessions: DashMap<String, PtySession>,
}

impl Pty for PtyManager {
    fn is_supported(&self) -> bool;
    fn spawn(&self, command: &str, args: &[&str], options: PtyOptions)
        -> AvaResult<Box<dyn PtyProcess>>;
}
```

#### `ava-db`
SQLite with migrations and typed queries.

```rust
// ava-db/src/lib.rs
pub struct SqliteDatabase {
    pool: sqlx::SqlitePool,  // or rusqlite::Connection
}

impl Database for SqliteDatabase {
    async fn query<T: DeserializeOwned>(&self, sql: &str, params: &[SqlParam]) -> AvaResult<Vec<T>>;
    async fn execute(&self, sql: &str, params: &[SqlParam]) -> AvaResult<()>;
    async fn migrate(&self, migrations: &[Migration]) -> AvaResult<()>;
    async fn close(&self) -> AvaResult<()>;
}

// Typed repositories for domain objects
pub struct SessionRepository { db: Arc<SqliteDatabase> }
impl SessionRepository {
    pub async fn create(&self, session: &Session) -> AvaResult<()>;
    pub async fn get(&self, id: &str) -> AvaResult<Option<Session>>;
    pub async fn update(&self, session: &Session) -> AvaResult<()>;
    pub async fn list(&self, project_path: &str) -> AvaResult<Vec<Session>>;
    pub async fn delete(&self, id: &str) -> AvaResult<()>;
    pub async fn create_checkpoint(&self, session_id: &str) -> AvaResult<String>;
    pub async fn restore_checkpoint(&self, checkpoint_id: &str) -> AvaResult<Session>;
}
```

#### `ava-tools`
Tool registry and implementations.

```rust
// ava-tools/src/lib.rs

/// Tool definition
pub struct ToolDef {
    pub name: &'static str,
    pub description: &'static str,
    pub parameters: serde_json::Value,  // JSON Schema
    pub requires_approval: bool,
}

/// Tool registry
pub struct ToolRegistry {
    tools: HashMap<String, Arc<dyn Tool>>,
}

#[async_trait]
pub trait Tool: Send + Sync {
    fn definition(&self) -> &ToolDef;
    async fn execute(&self, args: serde_json::Value, ctx: &ToolContext) -> AvaResult<ToolResult>;
}

/// Context available to all tools during execution
pub struct ToolContext {
    pub project_path: PathBuf,
    pub platform: Arc<Platform>,
    pub permissions: Arc<dyn PermissionChecker>,
    pub session_id: String,
}

impl ToolRegistry {
    pub fn new() -> Self;
    pub fn register(&mut self, tool: Arc<dyn Tool>);
    pub fn get(&self, name: &str) -> Option<&Arc<dyn Tool>>;
    pub fn list(&self) -> Vec<&ToolDef>;
    pub async fn execute(&self, name: &str, args: serde_json::Value, ctx: &ToolContext)
        -> AvaResult<ToolResult>;
}

// Individual tools: ReadFileTool, WriteFileTool, EditTool, GlobTool,
// GrepTool, BashTool, CreateFileTool, DeleteFileTool, LsTool, etc.
```

#### `ava-llm`
LLM client with provider abstraction and streaming.

```rust
// ava-llm/src/lib.rs

#[async_trait]
pub trait LlmProvider: Send + Sync {
    fn name(&self) -> &str;
    fn models(&self) -> &[ModelInfo];

    async fn complete(&self, request: CompletionRequest) -> AvaResult<CompletionResponse>;

    /// Streaming completion — returns a Stream of chunks
    fn complete_stream(&self, request: CompletionRequest)
        -> Pin<Box<dyn Stream<Item = AvaResult<StreamChunk>> + Send>>;
}

pub struct CompletionRequest {
    pub model: String,
    pub messages: Vec<Message>,
    pub tools: Vec<ToolDef>,
    pub max_tokens: Option<u32>,
    pub temperature: Option<f32>,
    pub stop_sequences: Vec<String>,
}

pub enum StreamChunk {
    Text(String),
    ToolCall { id: String, name: String, arguments_delta: String },
    Usage { input_tokens: u64, output_tokens: u64 },
    Done,
}

/// Provider registry with lazy initialization
pub struct ProviderRegistry {
    providers: HashMap<String, Arc<dyn LlmProvider>>,
}

impl ProviderRegistry {
    pub fn register(&mut self, provider: Arc<dyn LlmProvider>);
    pub fn get(&self, name: &str) -> Option<&Arc<dyn LlmProvider>>;
    pub fn resolve_model(&self, model_id: &str) -> Option<(&Arc<dyn LlmProvider>, &ModelInfo)>;
}

// Provider implementations:
// AnthropicProvider, OpenAiProvider, GoogleProvider, OpenRouterProvider,
// GroqProvider, MistralProvider, DeepSeekProvider, XaiProvider,
// CohereProvider, TogetherProvider, OllamaProvider, etc.
```

#### `ava-agent`
The core agent loop.

```rust
// ava-agent/src/lib.rs

pub struct AgentExecutor {
    llm: Arc<ProviderRegistry>,
    tools: Arc<ToolRegistry>,
    session: Arc<SessionManager>,
    context: Arc<ContextTracker>,
    permissions: Arc<PermissionManager>,
    bus: Arc<EventBus>,
}

impl AgentExecutor {
    pub async fn run(&self, goal: &str, config: AgentConfig) -> AvaResult<AgentResult>;

    /// Main turn loop
    async fn turn_loop(&self, state: &mut AgentState) -> AvaResult<()> {
        // 1. Build messages with context tracking
        // 2. Call LLM (streaming)
        // 3. Parse tool calls
        // 4. Execute tools (with permission checks)
        // 5. Check termination (attempt_completion)
        // 6. Persist session + checkpoint
        // 7. Emit progress events
    }
}

/// Events emitted during agent execution
#[derive(Debug, Clone, Serialize)]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum AgentEvent {
    TurnStart { turn: u32 },
    LlmChunk { text: String },
    ToolStart { name: String, args: serde_json::Value },
    ToolResult { name: String, result: ToolResult },
    TurnEnd { metadata: TurnMetadata },
    Completion { summary: String },
    Error { message: String },
    ApprovalRequired { tool: String, args: serde_json::Value, request_id: String },
}
```

#### `ava-mcp`
MCP client for tool/resource discovery.

```rust
// ava-mcp/src/lib.rs

pub struct McpClientManager {
    clients: DashMap<String, McpClient>,
}

pub struct McpClient {
    transport: Box<dyn McpTransport>,
    capabilities: ServerCapabilities,
    tools: Vec<McpToolDef>,
    resources: Vec<McpResource>,
}

#[async_trait]
pub trait McpTransport: Send + Sync {
    async fn send(&self, request: JsonRpcRequest) -> AvaResult<JsonRpcResponse>;
    fn notifications(&self) -> Pin<Box<dyn Stream<Item = JsonRpcNotification> + Send>>;
    async fn close(&self) -> AvaResult<()>;
}

// Transport implementations:
pub struct StdioTransport { /* spawned process */ }
pub struct SseTransport { /* HTTP SSE connection */ }
pub struct StreamableHttpTransport { /* HTTP with streaming */ }
```

#### `ava-commander`
Hierarchical agent delegation.

```rust
// ava-commander/src/lib.rs

pub struct Commander {
    agent_factory: Arc<dyn AgentFactory>,
    scheduler: DagScheduler,
}

#[async_trait]
pub trait AgentFactory: Send + Sync {
    fn create_worker(&self, definition: &WorkerDefinition) -> AgentExecutor;
}

pub struct WorkerDefinition {
    pub name: String,           // e.g., "coder", "tester", "reviewer"
    pub description: String,
    pub system_prompt: String,
    pub tools: Vec<String>,     // allowed tool names
    pub model_override: Option<String>,
}

/// DAG-based parallel task scheduler
pub struct DagScheduler;
impl DagScheduler {
    pub async fn execute(&self, tasks: Vec<DagTask>) -> AvaResult<Vec<TaskResult>>;
    fn detect_file_conflicts(&self, tasks: &[DagTask]) -> Vec<Conflict>;
}
```

#### `ava-sandbox`
Process isolation for tool execution.

```rust
// ava-sandbox/src/lib.rs

pub struct Sandbox {
    /// Filesystem jail (chroot-like)
    allowed_paths: Vec<PathBuf>,
    /// Network restrictions
    network_policy: NetworkPolicy,
    /// Resource limits
    limits: ResourceLimits,
}

pub struct ResourceLimits {
    pub max_memory_bytes: u64,
    pub max_cpu_seconds: u64,
    pub max_file_size_bytes: u64,
    pub max_open_files: u32,
    pub max_processes: u32,
}

pub enum NetworkPolicy {
    Allow,
    Deny,
    AllowList(Vec<String>),  // allowed domains
}

impl Sandbox {
    pub fn wrap_tool(&self, tool: Arc<dyn Tool>) -> Arc<dyn Tool>;
    pub fn wrap_shell(&self, shell: Arc<dyn Shell>) -> Arc<dyn Shell>;
}
```

### 3.4 Inter-Crate Communication

| Pattern | Use Case | Mechanism |
|---------|----------|-----------|
| **Shared traits** | Platform abstraction | `ava-platform` traits, `Arc<dyn Trait>` |
| **Event bus** | Agent progress → UI | `tokio::broadcast::channel<AgentEvent>` |
| **Channels** | Tool output streaming | `tokio::mpsc::channel` |
| **Shared state** | Session cache, config | `Arc<RwLock<T>>` or `DashMap` |
| **Dependency injection** | Agent ← Tools ← Platform | Constructor injection, builder pattern |

**Example — wiring it all together**:

```rust
// In src/lib.rs (Tauri app setup)
fn setup_core(app: &tauri::App) -> AvaResult<Arc<AvaCore>> {
    let config = ava_config::load()?;

    // Platform layer
    let fs = Arc::new(ava_fs::SecureFs::new(vec![]));
    let shell = Arc::new(ava_shell::ShellExecutor::new());
    let db = Arc::new(ava_db::SqliteDatabase::open(&config.db_path).await?);
    let pty = Arc::new(ava_shell::PtyManager::new());
    let creds = Arc::new(ava_auth::KeyringCredentialStore::new());
    let platform = Arc::new(Platform { fs, shell, credentials: creds, database: db, pty: Some(pty) });

    // Core services
    let tools = Arc::new(ava_tools::ToolRegistry::default_tools(platform.clone()));
    let llm = Arc::new(ava_llm::ProviderRegistry::default_providers(&config));
    let session = Arc::new(ava_session::SessionManager::new(platform.database.clone()));
    let context = Arc::new(ava_context::ContextTracker::new());
    let permissions = Arc::new(ava_permissions::PermissionManager::new(&config));
    let bus = Arc::new(ava_types::EventBus::new());

    // Agent
    let agent = Arc::new(ava_agent::AgentExecutor::new(
        llm, tools, session, context, permissions, bus.clone()
    ));

    Ok(Arc::new(AvaCore { agent, bus, platform, config }))
}
```

---

## 4. Migration Phases

### Phase 1: Foundation (Months 1–3)

**Goal**: Establish the Rust crate workspace, shared types, and platform traits. No TypeScript code is removed — Rust runs alongside.

#### Deliverables

| Task | Crate | Details |
|------|-------|---------|
| Create workspace | root | Convert `src-tauri/Cargo.toml` to workspace with `crates/` directory |
| Shared types | `ava-types` | Port all TypeScript interfaces from `platform.ts` and `agent/types.ts` to Rust structs |
| Platform traits | `ava-platform` | Define Rust trait equivalents of `IFileSystem`, `IShell`, `IPTY`, `ICredentialStore`, `IDatabase` |
| Config crate | `ava-config` | Port `config/` module — settings loading, validation |
| Event bus | `ava-types` | Implement `tokio::broadcast`-based event bus for `AgentEvent` |
| Migrate PTY | `ava-shell` | Move existing `pty.rs` into `ava-shell` crate (already production-ready) |
| Database crate | `ava-db` | Implement SQLite operations using `rusqlite` or `sqlx`, port existing migrations |
| Model registry | `ava-types` | Port static model definitions and pricing from `models/` |

#### Validation Criteria
- All existing Tauri commands still work (no regression)
- New crates compile and pass unit tests
- TypeScript core continues to function identically
- Shared types can round-trip through serde (TS → JSON → Rust → JSON → TS)

#### Key Decision: SQLite Library
| Option | Pros | Cons |
|--------|------|------|
| `rusqlite` | Mature, sync API, full SQLite control | Sync (needs `spawn_blocking`) |
| `sqlx` (sqlite) | Async-native, compile-time query checking | Heavier dependency, less SQLite control |
| `tauri-plugin-sql` (keep) | Already integrated | JS-only API, can't use from Rust directly |

**Recommendation**: Use `rusqlite` with `tokio::task::spawn_blocking` for async wrapping. It gives us full control over SQLite pragmas, WAL mode, and custom functions.

### Phase 2: Bridge (Months 4–7)

**Goal**: Implement Rust versions of platform operations and core tools. TypeScript `platform-tauri` calls Rust via `invoke()` instead of Tauri plugins directly. This is the **progressive replacement** phase.

#### Deliverables

| Task | Crate | Details |
|------|-------|---------|
| Filesystem impl | `ava-fs` | Implement `FileSystem` trait: read/write, glob (using `ignore` crate), stat, path security |
| Shell impl | `ava-shell` | Implement `Shell` trait: exec with timeout, spawn with streaming, env management |
| Credential store | `ava-auth` | Implement `CredentialStore` trait using OS keyring (`keyring` crate) |
| Auth flows | `ava-auth` | Port OAuth + PKCE, GitHub Copilot device flow (already partially in Rust) |
| File tools (Rust) | `ava-tools` | Port: `read_file`, `create_file`, `write_file`, `delete_file`, `edit`, `multiedit`, `apply_patch` |
| Search tools (Rust) | `ava-tools` | Port: `glob`, `grep` (using `ignore` + `grep` crates), `ls`, `codesearch` |
| Bash tool (Rust) | `ava-tools` | Port: `bash` tool using `ava-shell` |
| Permission system | `ava-permissions` | Port: command validator, security inspector, auto-approve, rules |
| Session management | `ava-session` | Port: SessionManager with LRU cache, persistence, checkpoints |
| Bridge commands | `bridge.rs` | Expose all new Rust tools as Tauri commands; update `platform-tauri` TS to call them |
| Context tracking | `ava-context` | Port: token counting, compaction strategies |

#### Bridge Architecture

During this phase, the call chain looks like:

```
Frontend JS
  → useAgent.ts
    → @ava/core-v2 (TypeScript agent loop — STILL IN TS)
      → getPlatform().fs.readFile()
        → platform-tauri/fs.ts
          → invoke("rust_read_file", {path})     ← NEW: calls Rust
            → ava-fs::SecureFs::read_file()       ← Rust implementation
```

**The TypeScript agent loop is still orchestrating**, but individual operations are progressively executing in Rust. This means:
- Each tool can be migrated independently
- Fallback to TypeScript is easy (just revert `platform-tauri` to call Tauri plugins)
- Performance-critical tools (glob, grep, codesearch) benefit immediately from Rust speed

#### Performance Targets

| Operation | Current (TS) | Target (Rust) | Expected Speedup |
|-----------|-------------|--------------|------------------|
| Glob (large repo) | ~2–5s | ~100–500ms | 5–20x |
| Grep (large repo) | ~3–8s | ~200–800ms | 5–15x |
| File read (large) | ~100ms | ~10ms | 10x |
| Codesearch | ~5–10s | ~500ms–2s | 5–10x |
| Session load | ~200ms | ~50ms | 4x |

### Phase 3: Core Migration (Months 8–13)

**Goal**: Migrate the agent loop, LLM client, MCP, and commander to Rust. This is the heavyweight phase — the brain of AVA moves to Rust.

#### Deliverables

| Task | Crate | Details |
|------|-------|---------|
| LLM streaming client | `ava-llm` | HTTP client with SSE/streaming for all 13 providers. Use `reqwest` + `eventsource-client` |
| Anthropic provider | `ava-llm` | Port Anthropic Messages API (highest priority — primary provider) |
| OpenAI-compatible providers | `ava-llm` | Port OpenAI, Groq, Together, DeepSeek, XAI, Mistral, Ollama (shared base) |
| Google provider | `ava-llm` | Port Gemini API (different format) |
| OpenRouter gateway | `ava-llm` | Port OpenRouter (meta-provider) |
| MCP client | `ava-mcp` | Port MCP client: stdio transport (process spawn), SSE, HTTP |
| Agent loop | `ava-agent` | Port AgentExecutor: turn loop, tool parsing, error recovery, subagent spawning |
| Agent modes | `ava-agent` | Port plan mode, evaluator, planner |
| Commander | `ava-commander` | Port hierarchical delegation, DAG scheduler, worker definitions |
| Codebase analysis | `ava-codebase` | Port tree-sitter integration, symbol extraction, repomap, dependency graph |
| LSP client | `ava-lsp` | Port LSP integration (diagnostics, call hierarchy) |
| Diff/Git | `ava-tools` | Port diff tracking, git snapshots, rollback |
| Browser tool | `ava-tools` | Port Puppeteer-based browser automation (or replace with Rust headless browser) |
| Web tools | `ava-tools` | Port websearch, webfetch |

#### Agent Loop Migration Strategy

The agent loop is the most critical migration. Strategy:

1. **First**: Implement Rust agent loop that mirrors TypeScript exactly
2. **Second**: Expose it as a single Tauri command: `invoke("agent_run", {goal, config})`
3. **Third**: Stream events back via Tauri channels: `AgentEvent` enum
4. **Fourth**: Frontend `useAgent.ts` switches from calling TS core to calling Rust command
5. **Fifth**: Remove TypeScript core dependency from frontend

```
BEFORE (Phase 2):
  Frontend → TS AgentExecutor → TS Tools → Rust Platform

AFTER (Phase 3):
  Frontend → invoke("agent_run") → Rust AgentExecutor → Rust Tools → Rust Platform
                                  ↓ (streaming)
                           Tauri Channel → Frontend
```

#### LLM Provider Migration Order

Prioritized by user base and complexity:

1. **Anthropic** (primary, unique API format) — Month 8
2. **OpenAI** (base for many others) — Month 8
3. **OpenRouter** (gateway, unlocks all models) — Month 9
4. **Google Gemini** (unique format) — Month 9
5. **Ollama** (local, simple) — Month 9
6. **Remaining** (Groq, Mistral, DeepSeek, XAI, Cohere, Together, GLM, Kimi) — Months 10–11

### Phase 4: Full Rust (Months 14–18)

**Goal**: Remove TypeScript core entirely. The frontend communicates exclusively with Rust via Tauri commands/channels. The CLI uses the same Rust core via a thin Node.js binding or is rewritten as a native Rust CLI.

#### Deliverables

| Task | Details |
|------|---------|
| Remove `@ava/core-v2` | Delete `packages/core/` — all logic now in Rust crates |
| Remove `platform-tauri` | Delete `packages/platform-tauri/` — no longer needed |
| Rewrite CLI | Either: (a) Rust CLI binary using `ava-agent` directly, or (b) Keep Node.js CLI with `napi-rs` bindings to Rust crates |
| Extension system (Rust) | Port extension manager to Rust — WASM-based extensions for safety |
| Sandbox | Implement `ava-sandbox` for process isolation |
| Frontend simplification | `useAgent.ts` becomes a thin Tauri command/channel wrapper |
| Node.js platform removal | Delete `packages/platform-node/` if CLI is pure Rust |
| Performance optimization | Profile and optimize hot paths (LLM streaming, tool execution) |
| Memory optimization | Reduce memory footprint (Rust's ownership model helps) |

#### CLI Decision

| Option | Pros | Cons |
|--------|------|------|
| **Pure Rust CLI** (clap) | Single binary, fast startup, no Node.js dep | Must reimplement ACP protocol in Rust |
| **Node.js + napi-rs** | Keep existing CLI code, gradual migration | Still requires Node.js runtime |
| **Rust CLI + ACP in Rust** | Best long-term, fully native | Largest upfront investment |

**Recommendation**: Pure Rust CLI with `clap`. By Phase 4, all core logic is in Rust anyway — the CLI is just a thin wrapper around `ava-agent::AgentExecutor`.

#### Final Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Tauri App (Rust)                          │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                    AvaCore (Rust)                         │   │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐ │   │
│  │  │ava-agent │  │ava-llm   │  │ava-tools │  │ava-mcp  │ │   │
│  │  │(loop,    │  │(13       │  │(25+      │  │(stdio,  │ │   │
│  │  │ planner) │  │providers)│  │ tools)   │  │SSE,HTTP)│ │   │
│  │  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬────┘ │   │
│  │       │              │             │             │       │   │
│  │  ┌────┴──────────────┴─────────────┴─────────────┴────┐ │   │
│  │  │            ava-platform (traits)                    │ │   │
│  │  │  ava-fs  ava-shell  ava-db  ava-auth  ava-sandbox  │ │   │
│  │  └────────────────────────────────────────────────────┘ │   │
│  └──────────────────────────────────────────────────────────┘   │
│       │ Tauri Commands + Channels                                │
│       ▼                                                          │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │              SolidJS Frontend (WebView)                    │   │
│  │  Thin UI layer — all logic delegated to Rust via IPC      │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│                     AVA CLI (Rust binary)                         │
│  Uses ava-agent, ava-llm, ava-tools directly (no IPC overhead)  │
│  Speaks ACP protocol for editor integration                      │
└─────────────────────────────────────────────────────────────────┘
```

---

## 5. Risks & Mitigation

### 5.1 Risk Matrix

| Risk | Severity | Probability | Impact | Mitigation |
|------|----------|-------------|--------|------------|
| **LLM streaming parity** | High | Medium | Agent UX degrades if streaming breaks | Extensive integration tests; keep TS fallback until Rust streaming is proven |
| **Serde boundary bugs** | Medium | High | Silent data corruption across JS↔Rust | Property-based tests (proptest) for all serde types; round-trip validation |
| **Tool behavior drift** | High | Medium | Migrated tools behave differently than TS versions | Snapshot testing: run TS and Rust tools on same inputs, diff outputs |
| **Performance regression** | Medium | Low | Rust should be faster, but async overhead or poor patterns could hurt | Benchmark suite from Phase 1; CI regression checks |
| **MCP compatibility** | Medium | Medium | Rust MCP client may not handle edge cases | Use official MCP SDK test suite; test against real MCP servers |
| **Team velocity** | High | Medium | Rust learning curve slows development | Pair programming; invest in Rust training; keep TS working until Rust is proven |
| **Feature freeze** | High | High | Can't ship new features while migrating core | **Phase 2 bridge pattern** — new features can be added in either language |
| **Tree-sitter integration** | Medium | Low | Rust tree-sitter bindings are mature | `tree-sitter` crate is the canonical implementation (TS version wraps it) |
| **Browser tool** | Medium | Medium | No Rust equivalent of Puppeteer | Use `chromiumoxide` or `headless-chrome` crate; or keep as subprocess |
| **Extension compatibility** | Medium | High | Existing JS extensions won't work with Rust core | WASM-based extension runtime; provide migration SDK |
| **Database migration** | Low | Low | Switching from tauri-plugin-sql to native rusqlite | Same SQLite format; migration is transparent |
| **Backward compatibility** | High | Medium | Users' sessions must survive the migration | Versioned session format; migration scripts; backup before upgrade |

### 5.2 Testing Strategy

#### Unit Tests (Per Crate)

Every crate maintains its own test suite:

```rust
#[cfg(test)]
mod tests {
    // ava-fs: test path validation, glob patterns, read/write
    // ava-shell: test command execution, timeout, env isolation
    // ava-db: test migrations, CRUD, concurrent access
    // ava-tools: test each tool's execute() with mock platform
    // ava-llm: test request building, response parsing, error handling
    // ava-agent: test turn loop with mock LLM + mock tools
}
```

#### Integration Tests

```
tests/
├── integration/
│   ├── tool_parity/       # Run TS and Rust tools, compare outputs
│   ├── llm_streaming/     # Test real LLM streaming (with recorded fixtures)
│   ├── mcp_protocol/      # Test MCP client against reference server
│   ├── session_compat/    # Load TS-created sessions in Rust
│   └── e2e/               # Full agent loop with real filesystem
```

#### Snapshot Testing (Critical for Migration)

```rust
/// Compare Rust tool output against recorded TypeScript output
#[test]
fn tool_parity_read_file() {
    let ts_output = load_fixture("read_file/output.json");
    let rust_output = rust_read_file_tool.execute(load_fixture("read_file/input.json"));
    assert_json_eq!(ts_output, rust_output);
}
```

#### Property-Based Testing

```rust
use proptest::prelude::*;

proptest! {
    /// Any ExecResult can round-trip through serde
    #[test]
    fn exec_result_roundtrip(
        stdout in ".*",
        stderr in ".*",
        exit_code in 0i32..256
    ) {
        let original = ExecResult { stdout, stderr, exit_code };
        let json = serde_json::to_string(&original).unwrap();
        let decoded: ExecResult = serde_json::from_str(&json).unwrap();
        assert_eq!(original, decoded);
    }
}
```

#### CI Pipeline Addition

```yaml
# .github/workflows/rust.yml
- name: Rust tests
  run: cargo test --workspace
- name: Rust clippy
  run: cargo clippy --workspace -- -D warnings
- name: Rust format
  run: cargo fmt --check
- name: Tool parity tests
  run: cargo test --test tool_parity
- name: Benchmarks (regression check)
  run: cargo bench --workspace -- --save-baseline ci
```

### 5.3 Backward Compatibility

#### Session Format

- Sessions are stored in SQLite with a versioned schema
- Each migration phase increments `SCHEMA_VERSION`
- Rust reads the same SQLite database with the same schema
- **Guarantee**: Sessions created in TS can be loaded by Rust and vice versa during the bridge phase

#### Configuration

- Config files (JSON/TOML) maintain backward compatibility
- New Rust config loader validates against the same schema
- Unknown fields are preserved (forward compatibility)

#### Extension API

- Phase 2–3: Extensions continue to run in JS (loaded by frontend)
- Phase 4: Extensions migrate to WASM or are provided a compatibility shim
- **Migration SDK**: Tooling to convert JS extensions to WASM

### 5.4 Rollback Strategy

Each phase has a rollback plan:

| Phase | Rollback | Cost |
|-------|----------|------|
| Phase 1 | Remove Rust crates; no TS was changed | Zero — purely additive |
| Phase 2 | Revert `platform-tauri` to call Tauri plugins directly | Low — one package revert |
| Phase 3 | Keep TS agent loop as fallback; feature-flag Rust agent | Medium — maintain both for a release |
| Phase 4 | Cannot rollback easily — TS core is deleted | High — this is the point of no return |

**Recommendation**: Phase 4 (point of no return) should only proceed after:
1. Full test suite passes on Rust implementation
2. Beta testing with real users for at least 1 month
3. Performance benchmarks show parity or improvement
4. All 13 LLM providers verified with real API calls
5. MCP compatibility confirmed with popular MCP servers

---

## Appendix A: Existing Rust Code Inventory

| File | Lines | Status | Migration Target |
|------|-------|--------|-----------------|
| `src-tauri/src/lib.rs` | 44 | Production | Becomes workspace orchestrator |
| `src-tauri/src/pty.rs` | ~200 | Production | → `ava-shell::PtyManager` |
| `src-tauri/src/commands/oauth.rs` | ~150 | Production | → `ava-auth` |
| `src-tauri/src/commands/plugin_state.rs` | ~100 | Production | → `ava-config` or dedicated crate |
| `src-tauri/src/commands/env.rs` | ~30 | Production | → `ava-config` |
| `src-tauri/src/commands/fs_scope.rs` | ~20 | Production | → `ava-fs` |
| `src-tauri/src/commands/dev_log.rs` | ~50 | Production | → `ava-config` |
| `src-tauri/src/tools/mod.rs` | 6 | **Placeholder** | → `ava-tools` |
| `src-tauri/src/db/mod.rs` | 2 | **Placeholder** | → `ava-db` |

## Appendix B: Recommended Rust Crate Dependencies

| Purpose | Crate | Notes |
|---------|-------|-------|
| Async runtime | `tokio` (already used) | Full features |
| HTTP client | `reqwest` (already used) | + `eventsource-client` for SSE |
| Serialization | `serde` + `serde_json` (already used) | |
| Error handling | `thiserror` | Derive Error for library crates |
| Error context | `anyhow` | For application-level error handling |
| SQLite | `rusqlite` + `r2d2` | Connection pooling |
| File globbing | `ignore` | Respects .gitignore, fast |
| Regex search | `grep-regex` + `grep-searcher` | ripgrep internals |
| Tree-sitter | `tree-sitter` + language grammars | Canonical Rust impl |
| PTY | `portable-pty` (already used) | |
| UUID | `uuid` (already used) | |
| Concurrent maps | `dashmap` | Lock-free concurrent HashMap |
| CLI (Phase 4) | `clap` | Derive-based arg parsing |
| Property testing | `proptest` | Fuzzing serde types |
| Benchmarking | `criterion` | Statistical benchmarks |
| Tracing | `tracing` + `tracing-subscriber` | Structured logging |
| Keyring | `keyring` | OS credential storage |
| Headless browser | `chromiumoxide` | Puppeteer alternative |
| MCP protocol | `rmcp` or custom | JSON-RPC 2.0 over stdio/HTTP |
| WASM runtime | `wasmtime` | Extension sandboxing (Phase 4) |

## Appendix C: TypeScript `platform.ts` Interface Summary

For reference, the complete interface surface that Rust must implement:

| Interface | Methods | Rust Trait |
|-----------|---------|------------|
| `IFileSystem` | `readFile`, `readBinary`, `writeFile`, `writeBinary`, `readDir`, `readDirWithTypes`, `stat`, `exists`, `isFile`, `isDirectory`, `mkdir`, `remove`, `glob`, `realpath` (14 methods) | `ava_platform::FileSystem` |
| `IShell` | `exec`, `spawn` (2 methods) | `ava_platform::Shell` |
| `IPTY` | `isSupported`, `spawn` (2 methods) | `ava_platform::Pty` |
| `ICredentialStore` | `get`, `set`, `delete`, `has` (4 methods) | `ava_platform::CredentialStore` |
| `IDatabase` | `query`, `execute`, `migrate`, `close` (4 methods) | `ava_platform::Database` |
| **Total** | **26 methods** | **5 traits** |
