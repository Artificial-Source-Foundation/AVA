# OpenAI Codex CLI -- Backend Architecture Analysis

> **Repository**: `github.com/openai/codex` (~63k stars)
> **Primary language**: Rust (backend), TypeScript (SDK wrapper)
> **License**: Apache-2.0
> **Analysis date**: 2026-03-03

---

## 1. Project Structure

The Codex CLI is a **native Rust binary** distributed via npm. The npm package (`@openai/codex`) is a thin Node.js wrapper (`codex-cli/bin/codex.js`) that resolves the correct platform-specific binary and spawns it as a child process.

```
codex/
├── codex-cli/              # npm package shell (bin/codex.js spawns native binary)
├── codex-rs/               # PRIMARY: Rust workspace (50+ crates)
│   ├── core/               # Business logic (~130 files, the brain)
│   ├── tui/                # Full-screen Ratatui TUI
│   ├── exec/               # Headless non-interactive CLI (codex exec)
│   ├── cli/                # CLI multitool (subcommand router)
│   ├── protocol/           # Wire types shared between core and consumers
│   ├── config/             # Config loading and schema
│   ├── app-server/         # JSON-RPC/WebSocket server for IDE extensions
│   ├── app-server-protocol/# v1/v2 API types for app-server
│   ├── linux-sandbox/      # Linux sandbox helper (bubblewrap + seccomp)
│   ├── execpolicy/         # .rules file parser and command policy engine
│   ├── apply-patch/        # Standalone patch application engine
│   ├── hooks/              # Lifecycle hook runner
│   ├── mcp-server/         # MCP server mode
│   ├── rmcp-client/        # MCP client using rmcp crate
│   ├── network-proxy/      # Managed HTTP/SOCKS proxy for network control
│   ├── file-search/        # File search with BM25
│   ├── skills/             # Skill system (auto-invoked context modules)
│   ├── shell-command/      # Shell command parsing/execution
│   ├── shell-escalation/   # Execve intercept for subcommand approval
│   ├── secrets/            # Secret detection
│   ├── state/              # SQLite state persistence
│   ├── codex-api/          # OpenAI Responses API client (SSE + WebSocket)
│   ├── codex-client/       # High-level Codex API client
│   ├── backend-client/     # Backend service client
│   ├── login/              # OAuth/API key auth
│   ├── ollama/             # Ollama provider adapter
│   ├── lmstudio/           # LM Studio provider adapter
│   ├── responses-api-proxy/# Local Responses API proxy for third-party providers
│   ├── feedback/           # User feedback collection
│   ├── otel/               # OpenTelemetry instrumentation
│   ├── process-hardening/  # Process security hardening
│   ├── windows-sandbox-rs/ # Windows sandbox implementation
│   └── utils/              # Shared utility crates (14 sub-crates)
├── sdk/typescript/         # TypeScript SDK (wraps codex exec)
├── shell-tool-mcp/         # MCP server that exposes a shell tool
├── docs/                   # Documentation
└── patches/                # Vendored dependency patches
```

### Key architectural decisions

- **Native Rust binary**: Not a Node.js CLI. The npm package is purely a launcher.
- **Workspace monorepo**: 50+ Rust crates in a single Cargo workspace.
- **Separation of concerns**: `core/` (business logic) vs `tui/` (presentation) vs `exec/` (headless mode) vs `app-server/` (IDE integration).
- **Submission Queue / Event Queue pattern**: Core uses an SQ/EQ async channel architecture -- callers submit `Op` variants, core emits `EventMsg` variants.

---

## 2. Tools

The tool system is built around a `ToolRegistryBuilder` that constructs a `ToolRouter` with both tool specifications (JSON Schema sent to the model) and handler implementations.

### Core Tools

| Tool | Handler File | Description | Unique Approach |
|------|-------------|-------------|-----------------|
| `shell` | `tools/handlers/shell.rs` | Executes shell commands via `execvp()` | Array-of-strings command format; `is_known_safe_command()` determines if mutating |
| `shell_command` | `tools/handlers/shell.rs` | Shell script execution in user's default shell | Single string command; supports login shell toggle |
| `exec_command` | `tools/handlers/unified_exec.rs` | PTY-based command execution | Returns session ID for ongoing interaction; supports `write_stdin` follow-up |
| `write_stdin` | `tools/handlers/unified_exec.rs` | Write to running exec session | Allows interactive PTY I/O within a persistent session |
| `apply_patch` | `tools/handlers/apply_patch.rs` | Apply unified diffs to files | Custom Lark grammar parser (`tool_apply_patch.lark`); supports both freeform and JSON modes |
| `read_file` | `tools/handlers/read_file.rs` | Read file with offset/limit | Indentation-aware mode: reads blocks by indent level around an anchor line |
| `grep_files` | `tools/handlers/grep_files.rs` | Search file contents | Shells out to bundled `rg` (ripgrep); 30s timeout, max 2000 results |
| `list_dir` | `tools/handlers/list_dir.rs` | List directory contents | Tree-style with configurable depth (default 2), offset/limit pagination |
| `view_image` | `tools/handlers/view_image.rs` | View local image file | Checks `InputModality::Image` capability; base64-encodes for model |
| `update_plan` | `tools/handlers/plan.rs` | Update task plan with steps | Steps have `pending/in_progress/completed` status; at most one `in_progress` |
| `request_user_input` | `tools/handlers/request_user_input.rs` | Ask user 1-3 questions | Availability gated by collaboration mode |
| `search_tool_bm25` | `tools/handlers/search_tool_bm25.rs` | BM25 search across MCP tools | Builds an in-memory BM25 index over all available MCP tool descriptions |
| `web_search` | Built-in Responses API tool | Web search | Delegated to OpenAI's server-side web search; `Cached` or `Live` modes |

### Collaboration/Multi-Agent Tools

| Tool | Description |
|------|-------------|
| `spawn_agent` | Spawn a sub-agent with its own thread, role-specific config, and tool set |
| `send_input` | Send input to a running sub-agent |
| `resume_agent` | Resume a paused sub-agent |
| `wait` | Wait for sub-agent(s) to complete (10s-3600s timeout) |
| `close_agent` | Terminate a sub-agent |

### MCP Resource Tools

| Tool | Description |
|------|-------------|
| `list_mcp_resources` | List resources from MCP servers |
| `list_mcp_resource_templates` | List resource templates |
| `read_mcp_resource` | Read a specific MCP resource |

### Artifact Tools (Feature-Gated)

| Tool | Description |
|------|-------------|
| `presentation_artifact` | Create/modify presentation slides |
| `spreadsheet_artifact` | Create/modify spreadsheet data |

### JS REPL Tools (Feature-Gated)

| Tool | Description |
|------|-------------|
| `js_repl` | Execute JavaScript in a persistent Node.js REPL |
| `js_repl_reset` | Reset the JS REPL state |

### Dynamic and MCP Tools

All MCP server tools are dynamically registered via `McpHandler`. Dynamic tools (user-defined JSON Schema tools) use `DynamicToolHandler`. The tool router checks if a function call name matches an MCP tool pattern (`<server>__<tool>`) and dispatches accordingly.

### Tool Architecture

```rust
// core/src/tools/registry.rs
#[async_trait]
pub trait ToolHandler: Send + Sync {
    fn kind(&self) -> ToolKind;              // Function or MCP
    fn matches_kind(&self, payload: &ToolPayload) -> bool;
    async fn is_mutating(&self, invocation: &ToolInvocation) -> bool;
    async fn handle(&self, invocation: ToolInvocation) -> Result<ToolOutput, FunctionCallError>;
}
```

Key design: `is_mutating()` returns true for potentially side-effecting calls. Non-mutating tools can execute immediately; mutating tools wait on a `tool_call_gate` (readiness flag) for approval.

---

## 3. Agent Loop

The agent loop lives in `core/src/codex.rs` (the file is ~370KB, making it the largest file in the codebase). The core pattern is:

### Architecture: SQ/EQ Channel Pair

```rust
// core/src/codex.rs
pub struct Codex {
    tx_sub: Sender<Submission>,    // Submit operations
    rx_event: Receiver<Event>,     // Receive events
    agent_status: watch::Receiver<AgentStatus>,
    session: Arc<Session>,
}
```

Users submit `Op` variants (user input, interrupts, approvals) and receive `EventMsg` variants (turn started, item completed, agent message deltas, approvals needed, errors).

### Turn Lifecycle

```
User submits Op::UserTurn { items, ... }
  |
  v
run_turn() called by RegularTask
  |
  +-- run_pre_sampling_compact()  -- auto-compact if near context limit
  +-- record_context_updates()    -- inject environment/settings diffs
  +-- build Prompt { input, base_instructions, tools, ... }
  |
  +-- LOOP:
  |     stream_response() via ModelClient
  |       |-- SSE or WebSocket streaming from Responses API
  |       |-- Process events: text deltas, tool calls, reasoning
  |       |
  |       +-- On tool_call items:
  |       |     Dispatch via ToolCallRuntime (parallel execution)
  |       |     Each tool call -> ToolRouter.dispatch() -> ToolHandler.handle()
  |       |     Results fed back as FunctionCallOutput items
  |       |
  |       +-- On response.completed:
  |       |     Check if more tool results pending -> continue loop
  |       |     No pending tools -> turn complete
  |       |
  |       +-- On error:
  |             Retry with backoff (up to stream_max_retries, default 5)
  |             On context_window_exceeded -> auto-compact and retry
  |
  +-- emit TurnCompleted event
```

### Streaming

The client supports two transport modes:

1. **SSE (Server-Sent Events)**: Traditional HTTP streaming via `/v1/responses`
2. **WebSocket**: Persistent connection with `response.create` / `response.append` for sticky routing

WebSocket is the preferred path when enabled. It supports:
- **Prewarm**: A `response.create` with `generate=false` to pre-establish routing
- **Sticky routing**: `x-codex-turn-state` header for server affinity within a turn
- **Append mode**: Subsequent requests within a turn send only new items

```rust
// core/src/client.rs
pub struct ModelClientSession {
    client: ModelClient,
    websocket_session: WebsocketSession,
    turn_state: OnceLock<String>,   // sticky routing token
    last_full_request: Option<...>, // for append detection
}
```

### Retry and Error Handling

- Stream retries: exponential backoff, up to `stream_max_retries` (default 5, max 100)
- Request retries: up to `request_max_retries` (default 4, max 100)
- Stream idle timeout: `stream_idle_timeout_ms` (default 300,000ms / 5 minutes)
- Context window exceeded: triggers auto-compaction, then retries
- Interrupted: `CancellationToken` propagated through all async paths

### Parallel Tool Execution

Tool calls from a single model response are dispatched in parallel via `ToolCallRuntime`:

```rust
// core/src/tools/parallel.rs
pub(crate) struct ToolCallRuntime {
    router: Arc<ToolRouter>,
    session: Arc<Session>,
    turn_context: Arc<TurnContext>,
    tracker: SharedTurnDiffTracker,
    parallel_execution: Arc<RwLock<()>>,  // serializes non-parallel tools
}
```

Tools that declare `supports_parallel_tool_calls = true` run concurrently. Others acquire an exclusive lock. Each dispatched tool is spawned as a Tokio task with `AbortOnDropHandle` and respects the cancellation token.

---

## 4. LLM Providers

### Wire Protocol

Codex uses **exclusively the OpenAI Responses API** (`/v1/responses`). The Chat Completions API support was removed:

```rust
// core/src/model_provider_info.rs
pub enum WireApi {
    Responses,  // Only option; "chat" returns an error
}
```

### Provider Definition

Providers are defined as `ModelProviderInfo` structs, either built-in or user-configured in `~/.codex/config.toml`:

```rust
pub struct ModelProviderInfo {
    pub name: String,
    pub base_url: Option<String>,
    pub env_key: Option<String>,          // e.g., OPENAI_API_KEY
    pub wire_api: WireApi,                // Always Responses
    pub query_params: Option<HashMap<String, String>>,
    pub http_headers: Option<HashMap<String, String>>,
    pub env_http_headers: Option<HashMap<String, String>>,
    pub request_max_retries: Option<u64>,
    pub stream_max_retries: Option<u64>,
    pub stream_idle_timeout_ms: Option<u64>,
}
```

### Local Provider Support

- **Ollama**: Dedicated `codex-rs/ollama/` crate with discovery/setup
- **LM Studio**: Dedicated `codex-rs/lmstudio/` crate
- **`responses-api-proxy`**: A local proxy that translates the Responses API to compatible format for third-party providers

All third-party providers must speak the Responses API wire protocol. There is no Chat Completions abstraction layer.

### Transport Layer

The `codex-api` crate handles:
- SSE streaming via `eventsource-stream`
- WebSocket streaming via `tokio-tungstenite`
- Request compression (zstd)
- Auth header management (Bearer token, OAuth)
- Conversation-scoped headers for routing

---

## 5. Context / Token Management

### ContextManager

```rust
// core/src/context_manager/history.rs
pub(crate) struct ContextManager {
    items: Vec<ResponseItem>,              // oldest -> newest
    token_info: Option<TokenUsageInfo>,
    reference_context_item: Option<TurnContextItem>,
}
```

The `ContextManager` maintains an ordered list of all `ResponseItem`s in the conversation. It provides:
- `record_items()`: Append new items with truncation policy applied
- `for_prompt()`: Normalize and prepare history for model consumption (strip images if model doesn't support them, remove ghost snapshots)
- `estimate_token_count()`: Coarse byte-based heuristic (4 bytes per token)

### Truncation Policy

```rust
pub enum TruncationPolicy {
    Bytes(usize),
    Tokens(usize),
}
```

Tool output truncation preserves a prefix and suffix on UTF-8 boundaries. The `formatted_truncate_text()` function prepends "Total output lines: N" when truncation occurs, so the model knows content was elided.

### Auto-Compaction

When the context nears the model's limit, Codex runs **auto-compaction**:

1. **Pre-turn compaction**: Before starting a new turn, check if estimated tokens exceed `auto_compact_token_limit`
2. **Mid-turn compaction**: If the API returns `context_window_exceeded`, compact and retry
3. **Compaction process**: Send the full history to the model with a summarization prompt (`compact/prompt.md`), receive a summary, replace history with `[summary, recent_user_messages]`

Two compaction backends:
- **Inline (local)**: Uses the same model to generate the summary
- **Remote**: Server-side compaction for OpenAI providers (`should_use_remote_compact_task()`)

```rust
// core/src/compact.rs
pub const SUMMARIZATION_PROMPT: &str = include_str!("../templates/compact/prompt.md");
pub const SUMMARY_PREFIX: &str = include_str!("../templates/compact/summary_prefix.md");
const COMPACT_USER_MESSAGE_MAX_TOKENS: usize = 20_000;
```

Compaction preserves recent user messages (last N) so the model retains the most recent intent.

### Context Updates

Between turns, the system injects `TurnContextItem` diffs that capture changes to:
- Working directory
- Environment context
- Model settings
- Collaboration mode
- User instructions

This avoids reinjecting the entire system state on every turn -- only deltas are sent.

---

## 6. Sandbox Execution

Sandboxing is a core differentiator. Codex uses **OS-level sandboxing** to confine tool execution:

### Sandbox Policies

```rust
// protocol/src/protocol.rs
pub enum SandboxPolicy {
    DangerFullAccess,                    // No restrictions
    ReadOnly { access: ReadOnlyAccess }, // Read-only filesystem
    ExternalSandbox { network_access },  // Already in a container
    // (WorkspaceWrite variant in app-server v2)
}
```

### Platform-Specific Implementations

| Platform | Technology | Crate |
|----------|-----------|-------|
| macOS | **Seatbelt** (`/usr/bin/sandbox-exec`) | `core/src/seatbelt.rs` |
| Linux | **Bubblewrap** (bwrap) + **seccomp** + **Landlock** | `codex-rs/linux-sandbox/` |
| Windows | Custom sandbox | `codex-rs/windows-sandbox-rs/` |

### macOS Seatbelt

Codex generates a Scheme-based sandbox profile dynamically:
- `seatbelt_base_policy.sbpl`: Base restrictions
- `seatbelt_network_policy.sbpl`: Network denial rules
- `seatbelt_platform_defaults.sbpl`: Platform-specific read paths

Commands are wrapped: `sandbox-exec -p "<generated_profile>" <command>`. The profile allows read access to specified paths and denies everything else. Write access is confined to writable roots.

```rust
pub(crate) const MACOS_PATH_TO_SEATBELT_EXECUTABLE: &str = "/usr/bin/sandbox-exec";
// Only uses the system sandbox-exec to prevent PATH injection
```

### Linux Sandbox

The `codex-linux-sandbox` binary applies:
1. **Bubblewrap (bwrap)**: Filesystem namespace isolation via bind mounts
2. **Seccomp**: System call filtering (blocks network syscalls when needed)
3. **Landlock**: Linux Security Module for filesystem access control
4. **`no_new_privs`**: Prevents privilege escalation

```rust
// linux-sandbox/src/lib.rs
// On Linux, codex-linux-sandbox applies:
// - in-process restrictions (no_new_privs + seccomp), and
// - bubblewrap for filesystem isolation.
```

### Network Proxy

When network control is required but not fully blocked, Codex runs a managed HTTP/SOCKS proxy (`codex-rs/network-proxy/`) that:
- Routes all traffic through the proxy
- Allows/denies by host based on user approval
- Emits audit metadata for each connection
- Sets `HTTP_PROXY`/`HTTPS_PROXY` environment variables in sandboxed commands

### Sandbox Orchestration Flow

```
Tool call received
  |
  +-- ExecPolicyManager evaluates command against .rules files
  |     |
  |     +-- Decision::Allow -> Skip approval
  |     +-- Decision::Deny -> Reject
  |     +-- Decision::Ask -> Needs approval
  |
  +-- ToolOrchestrator.run()
        |
        +-- Determine ExecApprovalRequirement
        +-- If NeedsApproval -> send ExecApprovalRequestEvent -> wait for ReviewDecision
        +-- Select SandboxAttempt (with/without sandbox)
        +-- Transform CommandSpec -> ExecRequest with platform sandbox wrapper
        +-- Execute command
        +-- If sandbox failure -> retry without sandbox (with user approval)
```

---

## 7. Approval Modes

### AskForApproval Enum

```rust
pub enum AskForApproval {
    UnlessTrusted,    // Only "known safe" read-only commands auto-approved
    OnFailure,        // Auto-approve in sandbox; escalate on failure (deprecated)
    OnRequest,        // Model decides when to ask (default)
    Reject(RejectConfig), // Fine-grained rejection (sandbox_approval, rules, mcp_elicitations)
    Never,            // Never ask; failures returned to model directly
}
```

### How They Map to User-Facing Modes

| CLI Flag | AskForApproval | Behavior |
|----------|---------------|----------|
| `--sandbox read-only` | `OnRequest` | Commands run in read-only sandbox; model requests escalation when needed |
| `--sandbox workspace-write` | `OnRequest` | Writes allowed in workspace; escalation for other paths |
| `--sandbox danger-full-access` | `Never` | No sandbox, no approval |
| (default) | `OnRequest` | Sandbox + model-driven approval requests |

### Approval Flow

1. Model emits a tool call with optional `sandbox_permissions` and `justification` fields
2. `ExecPolicyManager` checks against `.rules` files (Starlark-based policy DSL)
3. If approval needed, core emits `ExecApprovalRequestEvent` with:
   - Command and cwd
   - Justification text
   - Proposed `ExecPolicyAmendment` (prefix rule for future auto-approval)
   - Available decisions list
4. TUI/IDE shows the approval prompt
5. User responds with `ReviewDecision`:
   - `Approved` (one-time)
   - `ApprovedForSession` (cached for this session)
   - `AlwaysAllow` (writes a .rules file for permanent approval)
   - `Rejected`

### ExecPolicy (.rules Files)

The `execpolicy` crate parses `.rules` files using a Starlark-based DSL:

```
# ~/.codex/rules/default.rules
prefix_rule(["git", "pull"], decision="allow")
prefix_rule(["npm", "test"], decision="allow")
```

When the user approves a command with "Always Allow", Codex appends a `prefix_rule` to the rules file. Banned prefix suggestions (like bare `python`, `bash`, `sudo`) are rejected to prevent overly broad rules.

---

## 8. Session Management

### Rollout Files

Sessions are persisted as **rollout files** -- JSONL files containing all events:

```
~/.codex/sessions/YYYY/MM/DD/<thread-id>.jsonl
~/.codex/archived_sessions/...
```

File: `core/src/rollout/recorder.rs`

The `RolloutRecorder` streams events to disk as they occur. Sessions can be:
- **Resumed**: Load history from rollout file, continue conversation
- **Forked**: Branch from an existing session at a point in time
- **Archived**: Moved to `archived_sessions/`

### Session Index

A session index (`core/src/rollout/session_index.rs`) maps thread IDs to file paths and optional human-readable names, enabling `codex --resume` and thread listing.

### SQLite State DB

When the `Sqlite` feature is enabled, `core/src/state_db.rs` persists:
- Dynamic tools registered at thread start
- Session metadata
- Turn state

### Thread Manager

`core/src/thread_manager.rs` manages concurrent threads (for multi-agent collaboration), tracking thread lifecycle, status, and parent-child relationships.

---

## 9. Permissions / Safety

### Command Classification

Two classification functions gate command execution:

```rust
// is_safe_command(): Returns true for known read-only commands
// These bypass approval even in UnlessTrusted mode
// Examples: ls, cat, git status, grep, find, head, tail

// command_might_be_dangerous(): Returns true for potentially destructive commands
// These may trigger additional warnings
```

### Patch Safety

`core/src/safety.rs` -- `assess_patch_safety()` checks:
1. Is the patch empty? -> Reject
2. Is the approval policy `UnlessTrusted`? -> Always ask
3. Are all writes within writable paths? -> Auto-approve in sandbox
4. Does the platform support sandboxing? -> Auto-approve with sandbox
5. Otherwise -> Ask user

### Network Policy

Network access is controlled at two levels:
1. **Sandbox level**: Seccomp/Seatbelt blocks all network syscalls
2. **Proxy level**: Managed proxy allows/denies by host with user approval

Network approval events include:
- Host and protocol (HTTP, HTTPS, SOCKS5)
- Proposed `NetworkPolicyAmendment` (allow/deny this host permanently)

### Process Hardening

The `process-hardening` crate applies additional security:
- On macOS: Disables core dumps, applies process-level restrictions
- Prevents sandbox escape via hardlinks (even patches within writable roots run in sandbox because hardlinks could point outside)

### Secret Detection

The `secrets` crate scans for inadvertent credential exposure.

---

## 10. TUI (Terminal User Interface)

Built with **Ratatui** (Rust TUI framework) + **crossterm** for terminal manipulation.

### Structure

```
tui/src/
├── app.rs                 # Main application state and event loop
├── app_event.rs           # App-level events
├── bottom_pane/           # Input area, approval overlay, command popup
│   ├── approval_overlay.rs
│   ├── chat_composer.rs
│   ├── command_popup.rs
│   ├── footer.rs
│   └── ...
├── markdown/              # Markdown rendering with syntect highlighting
├── main_content/          # Chat history display
├── side_panel/            # Session list, agent status
└── wrapping.rs            # Text wrapping utilities
```

### Key Features

- Full-screen alternate screen mode
- Markdown rendering with syntax highlighting (via `syntect`)
- Inline approval prompts (accept/reject/always allow)
- Plan visualization (step status tracking)
- File diff display
- Clipboard integration (`arboard`)
- Shell-passthrough for interactive commands
- Session list and resume
- Custom prompt editor
- Realtime audio conversation support (voice mode)

The TUI communicates with `codex-core` via the same `Submission`/`Event` channel pair.

---

## 11. Unique Features

### 1. OS-Level Sandboxing as a First-Class Primitive

Unlike other AI coding tools that rely on Docker or trust-based approaches, Codex uses the OS's own sandbox mechanisms (Seatbelt, bubblewrap+seccomp, Landlock). This means:
- No Docker required
- Zero-overhead for read-only operations
- Granular filesystem path control per-command
- Network control without container networking

### 2. Responses API WebSocket with Sticky Routing

Codex uses WebSocket connections to the Responses API with a sticky routing token (`x-codex-turn-state`), enabling:
- Connection reuse within a turn
- `response.append` for incremental context (only send new items)
- Prewarm to establish routing before the first real request

### 3. ExecPolicy: Starlark-Based Command Rules

The `.rules` file system uses a Starlark-based DSL for persistent command approval rules. When a user approves a command with "Always Allow", the rule is automatically appended. This creates a growing whitelist that reduces approval friction over time.

### 4. Shell Escalation Intercept

The `shell-escalation` crate can intercept `execve` calls within a sandboxed process, allowing Codex to prompt for approval of subcommands spawned by an approved parent command.

### 5. Managed Network Proxy

Rather than binary network on/off, the managed proxy provides per-host, per-protocol network control with audit logging. Commands can have network access to specific hosts while blocking everything else.

### 6. Multi-Agent Collaboration

The `spawn_agent` / `send_input` / `wait` / `close_agent` tool set enables the model to spawn sub-agents with:
- Independent conversation threads
- Role-specific tool filtering
- Depth limits (`agent_max_depth`)
- Status polling and input injection

### 7. Ghost Snapshots

`core/src/tasks/ghost_snapshot.rs` -- Periodic invisible snapshots of workspace state are injected into context to help the model track changes it didn't initiate (e.g., user editing files between turns).

### 8. Memories System

`core/src/memories/` -- A multi-phase memory system that:
- Phase 1: Extracts raw memories from completed rollouts
- Phase 2: Consolidates memories via LLM summarization
- Injects relevant memories as context citations
- Tracks memory usage and auto-prunes unused memories

### 9. Unified Exec (PTY Sessions)

The `exec_command` + `write_stdin` tool pair allows the model to maintain persistent PTY sessions:
- Start a long-running process
- Write to stdin
- Poll for output with configurable yield time
- Multiple concurrent sessions with unique IDs

### 10. Skills System

`core/src/skills/` + `codex-rs/skills/` -- Auto-invoked knowledge modules that:
- Load based on project type, file globs, or explicit mention
- Inject context-specific instructions
- Can depend on MCP servers or environment variables
- Support remote skill loading

### 11. App Server (IDE Integration)

The `app-server` crate exposes a JSON-RPC/WebSocket API for IDE extensions (VS Code), enabling:
- Thread creation/management
- Config read/write
- Model selection
- Approval handling
- v2 API with cursor pagination and experimental features

---

## Summary Comparison

| Aspect | Codex CLI | Typical TS-based AI CLI |
|--------|-----------|------------------------|
| Language | Rust (native binary) | TypeScript/Node.js |
| LLM API | Responses API only (SSE + WebSocket) | Chat Completions (REST) |
| Sandbox | OS-level (Seatbelt/bwrap/seccomp) | Docker or none |
| Distribution | npm wrapper around native binary | npm package |
| TUI | Ratatui (full-screen terminal) | React Ink or readline |
| Session persistence | JSONL rollout files + SQLite | None or minimal |
| Multi-agent | Sub-agent spawning with depth limits | Subagent tool calls |
| Config | TOML with layered loading | JSON/YAML |
| Policy | Starlark-based .rules files | Hardcoded lists |
| Network control | Managed HTTP/SOCKS proxy | Binary on/off |
