# AVA Architecture Overview

AVA is an AI coding assistant CLI built in Rust. It provides an interactive TUI
(terminal user interface) powered by Ratatui, an agentic execution loop that
calls LLM providers and executes tools, and a desktop app shell via Tauri. All
new agent and CLI features are implemented in Rust; the TypeScript layer exists
only for the Tauri desktop webview.

## High-Level Architecture

```
                          User
                           |
                    +------v------+
                    |   ava-tui   |  Ratatui + Crossterm + Tokio
                    |  (binary)   |  Handles input, rendering, keybinds
                    +------+------+
                           |
                    +------v------+
                    | AgentStack  |  crates/ava-agent/src/stack.rs
                    |             |  Composes all subsystems
                    +--+---+---+--+
                       |   |   |
          +------------+   |   +-------------+
          |                |                 |
   +------v------+  +-----v------+   +------v------+
   |  AgentLoop   |  |  ModelRouter|   | ToolRegistry|
   |  agent_loop/ |  |  ava-llm   |   |  ava-tools  |
   +------+------+  +-----+------+   +------+------+
          |                |                 |
          |         +------v------+   +------v------+
          |         |  Providers  |   |  19 built-in|
          |         |  anthropic  |   |  + MCP      |
          |         |  openai     |   |  + custom   |
          |         |  gemini     |   +-------------+
          |         |  ollama     |
          |         |  openrouter |
          |         |  copilot    |
          |         +-------------+
          |
   +------v-----------+
   | Supporting Crates |
   | ava-session       |  SQLite session persistence + FTS5
   | ava-memory        |  Key-value memory store
   | ava-context       |  Context window management + condensation
   | ava-permissions   |  Safety tags, risk classification, policies
   | ava-sandbox       |  OS-level command sandboxing (bwrap/sandbox-exec)
   | ava-codebase      |  Code indexing (BM25 + PageRank)
   | ava-config        |  Config files, credentials, model catalog
   | ava-mcp           |  MCP server client + transport
   | ava-extensions    |  Hook system, native/WASM extension loading
   | ava-praxis     |  Multi-agent orchestration (Praxis)
   +-------------------+
```

## The 22 Crates

### Core Runtime

| Crate | Purpose | Key File |
|---|---|---|
| `ava-agent` | Agent execution loop, stuck detection, system prompt, sub-agent spawning | `src/stack.rs`, `src/agent_loop/mod.rs` |
| `ava-llm` | LLM provider trait, 7 providers, connection pool, retry, circuit breaker | `src/provider.rs`, `src/providers/` |
| `ava-tools` | Tool trait, registry, middleware, 19 built-in tools, custom TOML tools | `src/registry.rs`, `src/core/` |
| `ava-praxis` | Multi-agent workflows (plan-code-review, etc.) | `src/workflow.rs` |
| `ava-cli-providers` | CLI provider resolution (cli:* prefix for external providers) | `src/provider.rs` |

### Data & Persistence

| Crate | Purpose | Key File |
|---|---|---|
| `ava-session` | Session CRUD, SQLite storage, FTS5 full-text search | `src/lib.rs`, `src/helpers.rs` |
| `ava-memory` | Persistent key-value memory (SQLite) | `src/lib.rs` |
| `ava-db` | SQLite connection pool | `src/lib.rs` |
| `ava-config` | Config file loading, credentials, model catalog, agents.toml | `src/lib.rs`, `src/model_catalog/` |

### Infrastructure

| Crate | Purpose | Key File |
|---|---|---|
| `ava-platform` | File system and shell abstractions | `src/lib.rs` |
| `ava-sandbox` | OS-level sandboxing (Linux bwrap, macOS sandbox-exec) | `src/lib.rs` |
| `ava-permissions` | Safety tags, risk levels, command classification, permission policies | `src/inspector.rs`, `src/classifier/` |
| `ava-context` | Context window management, hybrid condensation | `src/manager.rs` |
| `ava-codebase` | Async project indexer, BM25 + PageRank scoring | `src/lib.rs` |

### Auth & Protocol

| Crate | Purpose | Key File |
|---|---|---|
| `ava-auth` | OAuth flows, Copilot token exchange, PKCE | `src/lib.rs`, `src/copilot.rs` |
| `ava-mcp` | MCP client, stdio/HTTP transports, server management | `src/config.rs`, `src/manager.rs` |
| `ava-extensions` | Hook registration, extension descriptors, native/WASM loaders | `src/lib.rs` |

### User Interface

| Crate | Purpose | Key File |
|---|---|---|
| `ava-tui` | TUI binary (Ratatui), headless mode, widgets, state management | `src/app/mod.rs`, `src/widgets/` |

### Shared

| Crate | Purpose | Key File |
|---|---|---|
| `ava-types` | Shared types: Message, Session, Tool, TokenUsage, StreamChunk, AvaError | `src/lib.rs` |
| `ava-logger` | Structured logging setup | `src/lib.rs` |
| `ava-validator` | Validation utilities | `src/lib.rs` |
| `ava-lsp` | LSP client integration | `src/lib.rs` |

## Data Flow

A user interaction follows this path:

1. **User input** -- The TUI (`ava-tui`) captures keystrokes via Crossterm.
   The composer widget collects text. On submit, `App::submit_goal()` is called
   (`crates/ava-tui/src/app/event_handler.rs:271`).

2. **History collection** -- The TUI extracts User/Assistant messages from its
   UI message list and passes them as `history: Vec<Message>` to the agent.

3. **AgentStack::run()** (`crates/ava-agent/src/stack.rs:385`) -- This is the
   unified entry point. It:
   - Resolves the provider and model (with fallback support)
   - Enriches the goal with relevant memories
   - Builds a context manager with hybrid condensation
   - Assembles the tool registry (built-in + custom TOML + MCP)
   - Loads project instructions for the system prompt suffix
   - Creates an `AgentLoop` and starts streaming

4. **AgentLoop::run_streaming()** (`crates/ava-agent/src/agent_loop/mod.rs:293`)
   -- The core execution loop:
   - Injects system prompt, conversation history, then the user's goal
   - Calls the LLM provider with tool definitions
   - Streams back `StreamChunk` objects containing text, thinking, tool calls, usage
   - Executes tools: read-only tools run concurrently, write tools run sequentially
   - Runs stuck detection after each turn
   - Emits `AgentEvent` variants to the TUI

5. **LLM Provider** (`crates/ava-llm/src/providers/`) -- Each provider
   implements `LLMProvider` trait methods: `generate`, `generate_stream`,
   `generate_stream_with_tools`, `generate_stream_with_thinking`. The provider
   maps AVA messages to the provider's API format, makes HTTP requests via the
   connection pool, and parses responses into `StreamChunk` objects.

6. **Tool execution** (`crates/ava-tools/`) -- The `ToolRegistry` dispatches
   tool calls to the matching `Tool` implementation. Middleware runs before/after
   each execution. Results flow back as `ToolResult` objects.

7. **Event handling** -- The TUI receives `AgentEvent` variants via an unbounded
   channel and updates its state:
   - `Token(text)` -- appended to streaming message
   - `ToolCall(call)` -- displayed as tool invocation
   - `ToolResult(result)` -- displayed as tool output
   - `TokenUsage{..}` -- accumulated into cost display
   - `Complete(session)` -- agent run finished

8. **Session persistence** -- On completion, the session (messages + metadata)
   is saved to SQLite via `SessionManager::save()`.

## Key Design Decisions

**Why Rust**: Performance (340x faster cold start than comparable TS tools,
31x less memory), single static binary (~15MB), no runtime dependencies, and
type safety for a complex multi-crate system.

**Why Ratatui**: Native terminal rendering without a browser. Minimal
dependencies, fast startup, works over SSH, integrates naturally with developer
workflows. Crossterm provides cross-platform terminal handling.

**Why SQLite**: Embedded database with zero setup. FTS5 provides full-text
search over sessions. Single file for all persistence (`~/.ava/data.db`).
No external database server to manage.

**Why a crate-per-concern**: Each crate has a focused responsibility and can be
tested independently. The `ava-agent` crate composes them all via `AgentStack`,
but individual crates like `ava-permissions` or `ava-session` have no
dependencies on the agent runtime.
