# AVA Architecture Analysis

> Comprehensive analysis of the AVA codebase for Rust migration planning.
> Generated: March 2026 | Codebase: packages/core (~55,000 LoC), packages/core-v2 (~7,300 LoC), frontend (~55,900 LoC)

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Architecture Overview](#architecture-overview)
3. [Platform Abstraction Layer](#platform-abstraction-layer)
4. [Module-by-Module Analysis](#module-by-module-analysis)
5. [Inter-Module Dependency Map](#inter-module-dependency-map)
6. [Critical Path Analysis](#critical-path-analysis)
7. [core-v2: The Extension-First Rewrite](#core-v2-the-extension-first-rewrite)
8. [Frontend Analysis](#frontend-analysis)
9. [Rust Migration Recommendations](#rust-migration-recommendations)
10. [Migration Priority Matrix](#migration-priority-matrix)
11. [Risk Assessment](#risk-assessment)

---

## 1. Executive Summary

AVA is a multi-agent AI coding assistant built as a Tauri 2.0 desktop application with a TypeScript core monorepo. The codebase consists of three main packages:

| Package | Source Lines | Files | Purpose |
|---------|-------------|-------|---------|
| `packages/core` | ~55,000 | ~200+ | Full-featured business logic (v1) |
| `packages/core-v2` | ~7,300 | ~28 | Simplified extension-first core |
| `src/` (frontend) | ~55,900 | ~150+ | SolidJS desktop UI |
| `packages/platform-node` | ~1,500 | 8 | Node.js platform implementation |
| `packages/platform-tauri` | ~2,000 | 12 | Tauri platform implementation |

**Key architectural insight**: The codebase already has a clean **Platform Abstraction Layer** (`IPlatformProvider`) that decouples all OS-level operations (filesystem, shell, PTY, credentials, database) from business logic. This is the natural Rust boundary — Tauri already provides a `platform-tauri` implementation that wraps Rust commands.

**Strategic finding**: `core-v2` represents an intentional architectural simplification (~7,300 lines vs ~55,000) with an extension-first design. It should be the **foundation for Rust migration**, not `core` v1. The v2 agent loop is 984 lines vs 1,083, with complexity offloaded to extensions.

---

## 2. Architecture Overview

### Package Dependency Graph

```
┌──────────────────────────────────────────────┐
│                  Frontend (src/)              │
│        SolidJS + TailwindCSS + Stores        │
│           ~55,900 lines, ~150 files          │
└──────────────┬───────────────────────────────┘
               │ Tauri IPC (invoke/events)
               ▼
┌──────────────────────────────────────────────┐
│              Tauri Rust Backend               │
│         (src-tauri/ — commands, IPC)         │
└──────┬───────────────────────────────┬───────┘
       │                               │
       ▼                               ▼
┌─────────────────┐         ┌─────────────────┐
│  platform-tauri  │         │  platform-node   │
│   (IPC bridge)   │         │  (direct Node)   │
└─────────┬───────┘         └─────────┬───────┘
          │ implements                 │ implements
          ▼                           ▼
┌──────────────────────────────────────────────┐
│         IPlatformProvider Interface           │
│   IFileSystem | IShell | IPTY | IDatabase    │
│              ICredentialStore                 │
└──────────────────────┬───────────────────────┘
                       │ consumed by
                       ▼
┌──────────────────────────────────────────────┐
│           Core Business Logic                │
│   core (~55K LoC) / core-v2 (~7.3K LoC)    │
│                                              │
│  Agent Loop ─→ LLM Client ─→ Tool Registry  │
│  Session Mgmt ─→ Context Mgmt ─→ Validator  │
│  Commander ─→ MCP Client ─→ Permissions     │
│  Policy Engine ─→ MessageBus ─→ Hooks       │
└──────────────────────────────────────────────┘
```

### 32 Core Modules (v1)

```
packages/core/src/
├── agent/          # 5,958 lines, 22 files — Main execution loop
├── tools/          # 12,139 lines, 43 files — 24 tool implementations
├── permissions/    # 3,924 lines, 13 files — Permission management
├── codebase/       # 3,425 lines, 11 files — Code intelligence
├── commander/      # 2,941 lines, 13 files — Multi-agent orchestration
├── llm/            # 2,806 lines, 19 files — LLM provider clients
├── validator/      # 2,220 lines, 9 files  — Validation pipeline
├── context/        # 2,198 lines, 12 files — Token tracking/compaction
├── config/         # 2,109 lines, 9 files  — Settings management
├── session/        # 2,024 lines, 6 files  — Session management
├── mcp/            # 1,498 lines, 6 files  — MCP client
├── lsp/            # 1,219 lines, 4 files  — LSP integration
├── hooks/          # 1,147 lines, 4 files  — Lifecycle hooks
├── policy/         # 1,071 lines, 5 files  — Policy engine
├── extensions/     # ~800 lines, 8 files   — Extension manager
├── bus/            # 524 lines, 3 files    — Message bus
├── models/         # ~700 lines, 4 files   — Model registry
├── diff/           # ~700 lines, 6 files   — Diff tracking
├── git/            # ~600 lines, 6 files   — Git integration
├── scheduler/      # ~500 lines, 4 files   — Background tasks
├── skills/         # ~400 lines, 4 files   — Skill loading
├── focus-chain/    # ~400 lines, 5 files   — Focus chain management
├── custom-commands/# ~500 lines, 9 files   — Custom commands
├── auth/           # OAuth + PKCE flows
├── question/       # LLM-to-user questions
├── instructions/   # Project instructions
├── memory/         # (not found — referenced in docs)
├── types/          # Shared type definitions
└── platform.ts     # 226 lines — Platform abstraction
```

---

## 3. Platform Abstraction Layer

**This is the most important architectural boundary for Rust migration.**

### Interface Definition (`platform.ts`)

```typescript
interface IPlatformProvider {
  readonly fs: IFileSystem       // File operations (read, write, stat, glob, etc.)
  readonly shell: IShell         // Command execution (exec, spawn)
  readonly credentials: ICredentialStore  // Secure credential storage
  readonly database: IDatabase   // SQLite operations (query, execute, migrate)
  readonly pty?: IPTY           // Pseudo-terminal (optional)
}
```

### IFileSystem (15 methods)
- `readFile`, `readBinary`, `writeFile`, `writeBinary` — Core I/O
- `readDir`, `readDirWithTypes` — Directory listing
- `stat`, `exists`, `isFile`, `isDirectory` — File metadata
- `mkdir`, `remove` — File operations
- `glob` — Pattern matching
- `realpath` — Path resolution

### IShell (2 methods)
- `exec(command, options?) → { stdout, stderr, exitCode }` — Blocking execution
- `spawn(command, args, options?) → ChildProcess` — Streaming execution with pid, stdin/stdout/stderr streams

### IPTY (2 methods)
- `isSupported() → boolean`
- `spawn(command, args, options?) → PTYProcess` — With onData/onExit callbacks, write, resize, kill

### IDatabase (4 methods)
- `query<T>(sql, params?) → T[]` — Read queries
- `execute(sql, params?)` — Write operations
- `migrate(migrations)` — Schema migrations
- `close()`

### Current Implementations

| Implementation | Location | Runtime |
|---------------|----------|---------|
| `platform-node` | `packages/platform-node/src/` | Node.js (fs, child_process, node-pty) |
| `platform-tauri` | `packages/platform-tauri/src/` | Tauri IPC → Rust commands |

**Migration implication**: A `platform-rust` can replace both. The Tauri implementation already calls into Rust — the migration path is to move the business logic boundary further into Rust, keeping only UI orchestration in TypeScript.

---

## 4. Module-by-Module Analysis

### 4.1 Agent Loop (`agent/`)

| Metric | Value |
|--------|-------|
| Lines | 5,958 (v1), 984 (v2 loop only) |
| Files | 22 (v1), 17 (v2) |
| Key class | `AgentExecutor` |
| Dependencies | LLM Client, Tool Registry, Hooks, Validator, Session, Context, MessageBus |

**Architecture (v1 — `loop.ts`, 1,083 lines)**:
- Turn-based loop: LLM stream → parse tool calls → execute → check termination
- Doom loop detection via consecutive identical tool call hashing
- Recovery system (retry on transient errors)
- Validation pipeline integration (post-tool)
- Session checkpoint/rollback
- Hooks system (pre/post tool execution)
- Mode support (plan mode, minimal mode)

**Architecture (v2 — `loop.ts`, 984 lines)**:
- Simplified: no inline validation, no hooks, no recovery
- Extension-first: middleware/events replace inline logic
- Steer() mechanism for mid-run user interrupts (AbortController)
- Follow-up queue for injecting messages between turns
- Parallel OR sequential tool execution (configurable)
- Context compaction using extension strategies (truncate/summarize)
- Tool name repair (fuzzy matching for hallucinated tool names)
- Structured output support via synthetic tool
- Step limit tracking (maxSteps)
- Tool result truncation (50KB per result, 200KB total)

**Performance criticality**: **HIGH** — This is the hot loop. Every LLM interaction, tool execution, and context management decision flows through here. Token counting, streaming, and concurrent tool execution are latency-sensitive.

**Safety criticality**: **HIGH** — Doom loop detection prevents runaway costs. Permission checks gate every tool execution.

**Rust migration priority**: **PHASE 2** — After platform layer. The loop itself is orchestration logic that benefits from Rust's performance for parallel tool execution and memory management during streaming.

---

### 4.2 Tool System (`tools/`)

| Metric | Value |
|--------|-------|
| Lines | 12,139 (v1), ~1,500 (v2) |
| Files | 43 (v1), 31 (v2) |
| Registered tools | 24 (v1) |
| Key interface | `Tool<TParams>` |

**Tool Interface**:
```typescript
interface Tool<TParams> {
  definition: { name, description, parameters (JSON Schema) }
  validate?(params, ctx) → string | undefined
  execute(params, ctx) → Promise<ToolResult>
}
```

**Tool Registry** (`registry.ts`, 540 lines v1):
- Global `Map<string, AnyTool>` registry
- `executeTool()` orchestrates: validate → permission check → pre-hook → execute → post-hook → auto-commit
- Tool context carries: sessionId, workingDirectory, signal, metadata callback
- Permission integration via MessageBus for approval flow

**Major Tools by Complexity**:

| Tool | Lines | Performance Critical | Safety Critical |
|------|-------|---------------------|-----------------|
| `bash` | 806 | HIGH (PTY/sandbox) | HIGH (arbitrary exec) |
| `edit` | ~500 | MEDIUM (diff computation) | MEDIUM |
| `apply-patch` | ~400 | MEDIUM (unified diff parsing) | MEDIUM |
| `browser` | ~350 | LOW (Puppeteer wrapper) | LOW |
| `read` | ~200 | HIGH (large file streaming) | LOW |
| `write/create` | ~150 | MEDIUM | MEDIUM (file modification) |
| `glob/grep` | ~200 | HIGH (filesystem traversal) | LOW |
| `bash` sandbox | ~200 | MEDIUM | HIGH (Docker isolation) |

**Bash Tool Deep Dive** (806 lines):
- PTY support with timeout handling
- Docker sandbox mode (container isolation)
- Output truncation (50KB soft limit)
- Command validation integration
- Kill process group support
- Environment variable injection
- Working directory management

**Rust migration priority**: **PHASE 1-2** — Platform-level tools (bash, read, write, glob, grep) should move to Rust immediately via the platform layer. Tool orchestration (registry, hooks) moves in Phase 2.

---

### 4.3 LLM Client (`llm/`)

| Metric | Value |
|--------|-------|
| Lines | 2,806 |
| Files | 19 |
| Providers | 13 |
| Key interface | `LLMClient.stream()` |

**LLMClient Interface**:
```typescript
interface LLMClient {
  stream(messages, config, signal) → AsyncGenerator<StreamDelta>
}
```

**Provider Implementations**: Anthropic, OpenAI, Google, DeepSeek, GLM, Kimi, Mistral, Groq, Together, Fireworks, OpenRouter, Copilot, Custom/OpenAI-compatible.

**Authentication Flow**:
1. OAuth token (GitHub Copilot, via credential store)
2. Direct API key (provider-specific, from settings)
3. OpenRouter as gateway fallback

**Performance criticality**: **HIGH** — Streaming latency directly impacts user experience. Network I/O and JSON parsing are hot paths.

**Safety criticality**: **MEDIUM** — API key management, token counting for cost control.

**Rust migration priority**: **PHASE 3** — HTTP streaming is well-served by Rust (reqwest + tokio), but the provider diversity (13 implementations) makes this a large migration surface. TypeScript provider implementations work well; migrate only when performance becomes a bottleneck.

---

### 4.4 Context Management (`context/`)

| Metric | Value |
|--------|-------|
| Lines | 2,198 |
| Files | 12 |
| Key classes | `ContextTracker`, `Compactor` |

**ContextTracker** (291 lines):
- Token counting via `gpt-tokenizer`
- Threshold-based compaction trigger
- Per-message token tracking

**Compactor** (270 lines):
- Pluggable strategies: sliding-window, summarize, hierarchical, visibility
- Strategy chain execution
- Target token count calculation

**Performance criticality**: **HIGH** — Token counting runs on every message. Compaction involves re-processing the entire conversation history.

**Rust migration priority**: **PHASE 2** — Token counting is CPU-intensive and would benefit significantly from Rust. The `tiktoken` Rust crate is much faster than `gpt-tokenizer`.

---

### 4.5 Session Management (`session/`)

| Metric | Value |
|--------|-------|
| Lines | 2,024 |
| Files | 6 |
| Key class | `SessionManager` |

**SessionManager** (772 lines):
- LRU cache of active sessions (configurable max)
- File-based storage (JSON)
- Checkpoint/rollback support
- Fork (branching) support
- Dirty tracking with auto-save
- Event system for session lifecycle
- Doom-loop detection metrics per session
- Resume from file with validation

**Performance criticality**: **MEDIUM** — Session serialization/deserialization can be slow for large sessions with many messages.

**Safety criticality**: **HIGH** — Session data integrity, checkpoint reliability.

**Rust migration priority**: **PHASE 2** — Move to SQLite-backed storage in Rust for better performance and ACID guarantees.

---

### 4.6 Commander / Multi-Agent (`commander/`)

| Metric | Value |
|--------|-------|
| Lines | 2,941 |
| Files | 13 |
| Key class | `WorkerExecutor` |

**WorkerExecutor** (396 lines):
- Creates isolated `AgentExecutor` per worker
- Tool filtering (blocks `delegate_*` to prevent recursion)
- Worker definitions with role-specific system prompts
- Result extraction from worker output

**Router**: Auto-routing with keyword analysis
**Parallel Scheduler**: DAG-based scheduling with file conflict detection
**Workers**: Coder, Tester, Reviewer, Researcher, Debugger

**Performance criticality**: **HIGH** — Parallel worker execution is the primary scaling mechanism.

**Rust migration priority**: **PHASE 3** — Complex orchestration logic. Benefits from Rust's concurrency primitives (tokio tasks, channels) but requires careful migration of the agent loop first.

---

### 4.7 Permission System (`permissions/` + `policy/`)

| Metric | Value |
|--------|-------|
| Lines | 3,924 (permissions) + 1,071 (policy) = 4,995 |
| Files | 18 |
| Key classes | `PermissionManager`, `PolicyEngine` |

**PermissionManager** (386 lines):
- Rule-based permission checking with glob patterns
- Session-scoped allow/deny decisions
- Risk assessment for commands

**PolicyEngine** (324 lines):
- Priority-sorted rule evaluation (first match wins)
- Wildcard tool name patterns (`*`, `mcp__*`, `delegate_*`)
- Regex args matching on stable JSON
- Approval mode scoping (default, yolo, plan, auto_edit)
- Compound bash command recursive validation (splits `&&`, `||`, `|`, `;`)
- Safety checker integration (post-rule override)
- Non-interactive mode (ASK_USER → DENY)

**Performance criticality**: **MEDIUM** — Every tool call passes through permission checking, but rules are simple pattern matches.

**Safety criticality**: **CRITICAL** — This is the safety boundary. Any bypass could result in unauthorized file modifications or command execution.

**Rust migration priority**: **PHASE 1** — Safety-critical code benefits most from Rust's type safety and memory safety. The policy engine should be among the first components migrated.

---

### 4.8 MCP Client (`mcp/`)

| Metric | Value |
|--------|-------|
| Lines | 1,498 |
| Files | 6 |
| Key class | `MCPClientManager` |

**MCPClientManager** (412 lines):
- Manages connections to multiple MCP servers
- Transports: stdio, SSE, HTTP (via official `@modelcontextprotocol/sdk`)
- Tool discovery and namespacing (`mcp__serverName__toolName`)
- OAuth flow support for authenticated servers

**Performance criticality**: **LOW** — MCP operations are infrequent and I/O-bound.

**Safety criticality**: **MEDIUM** — External server connections need sandboxing.

**Rust migration priority**: **PHASE 4** — Low priority. The MCP SDK handles transport complexity. Migrate only if the Rust MCP ecosystem matures.

---

### 4.9 Codebase Intelligence (`codebase/`)

| Metric | Value |
|--------|-------|
| Lines | 3,425 |
| Files | 11 |
| Key classes | `FileIndexer`, `DependencyGraph` |

**FileIndexer** (351 lines):
- Platform fs.glob() for file discovery
- Language detection from extensions
- Token estimation (size/4)
- Incremental updates via content hash (djb2)

**Ranking** (396 lines):
- PageRank algorithm for file importance (dependency graph in-degree)
- Composite relevance scoring: PageRank (0.3) + keywords (0.5) + recency (0.2)
- Keyword extraction with stop words + tech terms + camelCase parsing

**Tree-sitter** (3 files):
- Symbol extraction for AST analysis
- Currently only bash language grammar

**Performance criticality**: **HIGH** — Full project indexing and PageRank computation on large codebases.

**Rust migration priority**: **PHASE 1** — Tree-sitter is already Rust-native. File indexing and PageRank benefit enormously from Rust performance. This is a natural fit.

---

### 4.10 Validator (`validator/`)

| Metric | Value |
|--------|-------|
| Lines | 2,220 |
| Files | 9 |
| Key class | `ValidationPipeline` |

**ValidationPipeline** (362 lines):
- Sequential validator execution with fail-fast
- Timeout per validator
- Validators: syntax, typescript, lint, test, self-review, build
- Abort signal support
- Summary statistics

**Performance criticality**: **MEDIUM** — Runs after tool execution; validators shell out to external tools (tsc, eslint).

**Rust migration priority**: **PHASE 4** — Validators primarily shell out to external tools. The pipeline orchestration is simple. Low migration value.

---

### 4.11 Configuration (`config/`)

| Metric | Value |
|--------|-------|
| Lines | 2,109 |
| Files | 9 |
| Key class | `SettingsManager` |

**SettingsManager** (352 lines):
- Zod schema validation (full + partial schemas per category)
- Categories: provider, agent, permissions, context, ui, git, sandbox
- File-based persistence with deep merge on load
- Reactive event system
- Singleton pattern

**Performance criticality**: **LOW** — Settings are read infrequently.

**Rust migration priority**: **PHASE 4** — Pure business logic with no performance concerns. Works fine in TypeScript.

---

### 4.12 Supporting Modules

| Module | Lines | Description | Rust Priority |
|--------|-------|-------------|---------------|
| `hooks/` | 1,147 | Pre/post tool execution hooks | PHASE 3 |
| `bus/` | 524 | Pub/sub message bus for tool confirmations | PHASE 2 |
| `diff/` | ~700 | Unified diff tracking, pending edit management | PHASE 3 |
| `git/` | ~600 | Auto-commit, snapshot, revert | PHASE 3 |
| `models/` | ~700 | Model registry (16 models), pricing, capabilities | PHASE 4 (data) |
| `scheduler/` | ~500 | Background task scheduling (setInterval-based) | PHASE 3 |
| `extensions/` | ~800 | Extension manager (~/.ava/extensions/) | PHASE 4 |
| `skills/` | ~400 | SKILL.md loader with YAML frontmatter | PHASE 4 |
| `focus-chain/` | ~400 | Focus chain for UI navigation | STAYS TS |
| `custom-commands/` | ~500 | Custom command discovery + templating | PHASE 4 |
| `lsp/` | 1,219 | Language Server Protocol integration | PHASE 2 |
| `auth/` | N/A | OAuth + PKCE flows | PHASE 3 |

---

## 5. Inter-Module Dependency Map

### Critical Dependency Chains

```
Agent Loop
  ├── LLM Client → Provider Registry → Auth (OAuth/API keys)
  ├── Tool Registry
  │     ├── Permission Manager → Policy Engine → MessageBus (approval flow)
  │     ├── Hooks Executor (pre/post)
  │     ├── Git Auto-Commit (post file modification)
  │     └── Platform Provider (fs, shell, PTY for tool implementations)
  ├── Context Tracker → Compactor → Compaction Strategies
  ├── Session Manager → File Storage
  ├── Validator Pipeline (post-turn)
  └── MessageBus (events, metadata streaming)

Commander
  ├── Agent Loop (creates isolated AgentExecutor per worker)
  ├── Router (keyword-based auto-routing)
  └── Parallel Scheduler (DAG, conflict detection)

MCP Client
  ├── Platform Shell (stdio transport)
  └── Tool Registry (registers discovered tools as mcp__*)

Codebase
  ├── Platform FS (glob, readFile, stat)
  ├── Tree-sitter (symbol extraction)
  └── Dependency Graph → PageRank → Relevance Scoring
```

### Singleton Dependencies (Global State)

The codebase uses extensive singleton patterns via `getXxxManager()`:

| Singleton | Module | Consumers |
|-----------|--------|-----------|
| `getPlatform()` | platform.ts | All platform operations |
| `getSettingsManager()` | config/manager.ts | Agent, tools, git, permissions |
| `getPolicyEngine()` | policy/engine.ts | Tool registry, permissions |
| `getDefaultTracker()` | diff/tracker.ts | Tools (edit, write, apply-patch) |
| `getScheduler()` | scheduler/scheduler.ts | Background tasks |
| `getExtensionManager()` | extensions/manager.ts | Extension lifecycle |
| Tool Registry (Map) | tools/registry.ts | Agent loop, MCP, commander |

**Migration concern**: These singletons create implicit coupling. A Rust migration should replace them with explicit dependency injection.

---

## 6. Critical Path Analysis

### Hot Path: User Message → AI Response

```
1. User sends message (frontend)
2. Frontend store → core-bridge → AgentExecutor.run()
3. Build system prompt + context
4. ContextTracker checks token count
   └── If over threshold → Compactor runs strategies
5. LLM Client streams response
   └── Provider-specific HTTP client (fetch/axios)
   └── Parse streaming chunks → emit deltas
6. Parse tool calls from response
7. For each tool call:
   a. PolicyEngine.check() → allow/deny/ask_user
   b. If ask_user → MessageBus → UI → wait for response
   c. Hooks.pre() → execute tool → Hooks.post()
   d. Platform operations (fs, shell, PTY)
   e. Git auto-commit if file-modifying
8. Doom loop detection
9. Build tool results → add to history
10. Loop to step 4 (next turn)
11. On completion → Session.checkpoint()
```

### Performance Bottlenecks (Candidates for Rust)

| Bottleneck | Current Tech | Rust Benefit | Priority |
|-----------|-------------|-------------|----------|
| Token counting | gpt-tokenizer (JS) | tiktoken-rs: 10-50x faster | HIGH |
| File globbing | Platform fs.glob | walkdir + globset: 5-10x faster | HIGH |
| Tree-sitter parsing | JS bindings | Native Rust tree-sitter: 3-5x faster | HIGH |
| LLM streaming | fetch/axios | reqwest + tokio: better backpressure | MEDIUM |
| Session serialization | JSON.parse/stringify | serde: 5-20x faster for large payloads | MEDIUM |
| PageRank computation | Pure JS | ndarray/petgraph: 10x+ on large graphs | MEDIUM |
| Diff computation | JS unified diff | similar-rs: 5-10x faster | LOW |
| Regex matching (grep) | JS RegExp | ripgrep engine: 5-50x faster | HIGH |

---

## 7. core-v2: The Extension-First Rewrite

### Design Philosophy

core-v2 is a **deliberate simplification** from ~55,000 to ~7,300 lines. It represents the architectural direction for AVA:

| Aspect | core v1 | core-v2 |
|--------|---------|---------|
| Agent loop | 1,083 lines, inline everything | 984 lines, extensions via middleware |
| Validation | Inline pipeline | Extension validators |
| Hooks | Dedicated module | Extension hooks |
| Recovery | Inline retry/recovery | Extension event subscribers |
| Context strategies | Module with strategies | Extension-registered strategies |
| Agent modes | Built-in plan/minimal | Extension-registered modes |
| Tool middleware | None | Extension middleware chain |

### core-v2 Module Structure

```
core-v2/src/
├── agent/          # 984 lines — Simplified loop
│   ├── loop.ts           # AgentExecutor with steer/follow-up
│   ├── efficient-results.ts  # Token-efficient tool result compression
│   ├── output-files.ts   # Overflow output saving
│   ├── repair.ts         # Tool name repair (fuzzy matching)
│   ├── structured-output.ts  # JSON schema structured output
│   └── types.ts          # AgentConfig, AgentResult, etc.
├── bus/            # Message bus (pub/sub + request/response)
├── config/         # SettingsManager (simplified)
├── extensions/     # Extension API, loader, manager, types
│   ├── api.ts            # Registries: modes, strategies, middlewares, validators, hooks
│   ├── loader.ts         # Load from directory / built-in
│   ├── manager.ts        # Lifecycle management
│   └── types.ts          # Extension, ExtensionAPI, middleware types
├── llm/            # Client factory, types, normalize
├── logger/         # Structured logging
├── platform.ts     # Same IPlatformProvider (147 lines)
├── session/        # SessionManager (385 lines, simplified)
└── tools/          # Simplified registry + 7 core tools
    ├── bash.ts, edit.ts, glob.ts, grep.ts, pty.ts, read.ts, write.ts
    ├── registry.ts       # Tool registration + execution
    ├── define.ts         # defineTool() helper
    ├── sanitize.ts       # Output sanitization
    ├── utils.ts          # Shared utilities
    └── validation.ts     # Parameter validation
```

### Extension System Architecture

core-v2's extension system provides:

| Registry | Purpose | Examples |
|----------|---------|---------|
| Agent Modes | Filter tools, modify system prompt | Plan mode, minimal mode |
| Context Strategies | Compaction algorithms | Truncate, summarize |
| Tool Middlewares | Intercept tool execution | Approval, logging, caching |
| Validators | Post-turn validation | Syntax check, lint |
| Slash Commands | User-facing commands | /plan, /undo, /compact |
| Hooks | Named lifecycle callbacks | history:process |
| Events | Pub/sub notifications | llm:usage, agent:completing |

### Implications for Rust Migration

core-v2's architecture is **ideal for incremental Rust migration**:

1. **Extensions can be Rust or TypeScript** — The extension API is transport-agnostic
2. **The platform layer is already abstracted** — Same `IPlatformProvider` as v1
3. **The agent loop is simpler** — 984 lines with clear boundary to extensions
4. **Tool middleware** replaces inline permission/hook logic — cleaner Rust/TS boundary

**Recommendation**: Use core-v2 as the migration starting point. Merge any v1 features still needed into the v2 architecture before beginning Rust migration.

---

## 8. Frontend Analysis

### Frontend Architecture (55,900 lines)

The frontend is a SolidJS application with:

| Layer | Lines | Purpose |
|-------|-------|---------|
| Components | ~15,000 | UI components (chat, dialogs, panels, sidebar) |
| Stores | ~5,000 | State management (session, layout, plugins, settings) |
| Hooks | ~3,000 | Reactive logic (useAgent, useChat, useModelStatus) |
| Services | ~15,000 | Backend integration (44 files) |
| Lib/Config | ~5,000 | Utilities, syntax highlighting |

### Largest Frontend Files (Complexity Indicators)

| File | Lines | Purpose |
|------|-------|---------|
| `stores/session.ts` | 1,367 | Session state management |
| `components/chat/MessageInput.tsx` | 1,054 | Chat input with autocomplete |
| `hooks/useAgent.ts` | 1,016 | Agent interaction hook |
| `services/providers/model-fetcher.ts` | 964 | Model catalog fetching |
| `services/database.ts` | 872 | SQLite via Tauri plugin |
| `components/settings/tabs/PluginsTab.tsx` | 864 | Plugin management UI |

### Frontend-to-Core Bridge

Key bridge services:
- `core-bridge.ts` — Primary bridge to core AgentExecutor
- `env-bridge.ts` — Environment variable polyfills
- `pty-bridge.ts` — PTY session management
- `tool-approval-bridge.ts` — Permission dialog flow
- `desktop-session-storage.ts` — Session persistence via Tauri

**Rust migration impact**: The frontend stays TypeScript/SolidJS. The Tauri IPC boundary is the natural API surface. As core logic moves to Rust, the bridge services shrink — they just call `invoke()` on Rust commands.

---

## 9. Rust Migration Recommendations

### What MUST Move to Rust

| Component | Reason | Effort |
|-----------|--------|--------|
| Platform layer (fs, shell, PTY) | OS-level operations, already partially in Rust via Tauri | LOW — extend existing |
| Token counting | 10-50x performance improvement with tiktoken-rs | LOW |
| File indexing + globbing | 5-10x faster, critical for large codebases | MEDIUM |
| Tree-sitter integration | Already Rust-native, JS bindings add overhead | MEDIUM |
| Policy engine | Safety-critical, benefits from Rust type safety | MEDIUM |
| Session storage | ACID guarantees with SQLite (rusqlite) | MEDIUM |
| Grep (content search) | ripgrep engine: 5-50x faster | MEDIUM |

### What SHOULD Move to Rust (Phase 2-3)

| Component | Reason | Effort |
|-----------|--------|--------|
| Agent loop core | Performance for parallel tool execution, streaming | HIGH |
| Tool registry | Centralized execution with Rust safety | HIGH |
| Context compaction | CPU-intensive for large conversations | MEDIUM |
| LLM streaming | Better backpressure with tokio | HIGH |
| Commander/parallel | Rust concurrency primitives (tokio tasks) | HIGH |
| Message bus | Lock-free channels vs JS event emitter | MEDIUM |

### What STAYS TypeScript

| Component | Reason |
|-----------|--------|
| Frontend (SolidJS) | UI framework, no Rust benefit |
| LLM provider implementations | HTTP client diversity, rapid iteration needed |
| Extension loader/manager | Plugin ecosystem needs JS flexibility |
| Config/settings | Pure business logic, no performance concerns |
| Validator orchestration | Shells out to external tools |
| Custom commands/skills | User-facing scripting, needs JS |
| Model registry | Static data, no performance concerns |

### Hybrid Components (Rust core + TS wrapper)

| Component | Rust Part | TS Part |
|-----------|-----------|---------|
| Agent loop | Turn execution, tool dispatch, doom loop | Event streaming, UI integration |
| MCP client | Transport (stdio, HTTP) | Protocol handling (SDK) |
| Git integration | Git operations (libgit2) | Auto-commit logic |
| Diff | Diff computation (similar-rs) | UI rendering, tracking |

---

## 10. Migration Priority Matrix

### Phase 1: Foundation (Months 1-3)

**Goal**: Establish Rust core infrastructure and migrate performance-critical operations.

| Task | Lines to Migrate | Dependencies | Risk |
|------|-----------------|-------------|------|
| Extend Tauri platform layer | ~500 new | None | LOW |
| Token counting (tiktoken-rs) | ~300 | Platform | LOW |
| File globbing (walkdir + globset) | ~200 | Platform | LOW |
| Content search (ripgrep engine) | ~200 | Platform | LOW |
| Policy engine | ~400 | None | MEDIUM |
| Tree-sitter native | ~200 | Platform | LOW |

**Deliverable**: `ava-core` Rust crate with platform operations and safety engine.

### Phase 2: Core Engine (Months 4-8)

**Goal**: Move the agent execution engine to Rust.

| Task | Lines to Migrate | Dependencies | Risk |
|------|-----------------|-------------|------|
| Tool registry + execution | ~600 | Phase 1 platform | MEDIUM |
| Session management (SQLite) | ~500 | Phase 1 platform | MEDIUM |
| Context tracker + compaction | ~600 | Token counting | MEDIUM |
| Agent loop (simplified from v2) | ~1000 | Tool registry, context | HIGH |
| Message bus (Rust channels) | ~300 | None | LOW |
| LSP integration | ~400 | Platform | MEDIUM |

**Deliverable**: Agent can run entirely in Rust with Tauri IPC for UI events.

### Phase 3: Advanced Features (Months 9-14)

**Goal**: Migrate orchestration and multi-agent capabilities.

| Task | Lines to Migrate | Dependencies | Risk |
|------|-----------------|-------------|------|
| LLM client (reqwest + tokio) | ~800 | Agent loop | HIGH |
| Commander/parallel execution | ~500 | Agent loop | HIGH |
| Git integration (libgit2) | ~400 | Platform | MEDIUM |
| Auth (OAuth/PKCE) | ~300 | LLM client | MEDIUM |
| Hooks system | ~300 | Tool registry | LOW |
| Diff engine (similar-rs) | ~300 | Platform | LOW |

**Deliverable**: Full multi-agent execution in Rust.

### Phase 4: Polish & Optimization (Months 15-18)

**Goal**: Migrate remaining business logic, optimize, stabilize.

| Task | Lines to Migrate | Dependencies | Risk |
|------|-----------------|-------------|------|
| Extension system (Rust + WASM) | ~500 | Core engine | HIGH |
| MCP client (Rust transport) | ~400 | Platform | MEDIUM |
| Config/settings (serde) | ~350 | None | LOW |
| Validator pipeline | ~360 | Platform | LOW |
| Remaining tools | ~1000 | Tool registry | MEDIUM |
| Performance optimization | N/A | All | LOW |

**Deliverable**: Production-ready Rust core with TypeScript UI.

---

## 11. Risk Assessment

### Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| Async streaming complexity in Rust | HIGH | HIGH | Use tokio streams, study similar projects (Zed, Helix) |
| Extension system Rust/WASM boundary | HIGH | MEDIUM | Keep extensions in JS initially, migrate to WASM later |
| LLM provider diversity (13 providers) | MEDIUM | HIGH | Abstract HTTP client, migrate providers incrementally |
| Session format migration | LOW | HIGH | Versioned migration system, backwards compatibility |
| Tree-sitter WASM vs native | LOW | LOW | Native Rust tree-sitter is well-established |

### Architectural Risks

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| core-v2 not feature-complete vs v1 | HIGH | HIGH | Audit v1 features, merge needed ones into v2 architecture |
| Singleton pattern migration | MEDIUM | MEDIUM | Replace with dependency injection in Rust |
| Tauri IPC overhead | LOW | MEDIUM | Batch IPC calls, use Tauri events for streaming |
| Parallel tool execution correctness | MEDIUM | HIGH | Rust's ownership model helps; thorough testing |
| MCP protocol stability | LOW | LOW | Official SDK handles protocol evolution |

### Process Risks

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| Feature development paused during migration | HIGH | HIGH | Incremental migration, keep TS working alongside Rust |
| Rust learning curve for team | MEDIUM | MEDIUM | Start with platform layer (familiar concepts) |
| Testing regression | MEDIUM | HIGH | Port test suite, add integration tests at IPC boundary |
| Build complexity (Rust + TS + Tauri) | MEDIUM | MEDIUM | Cargo workspace, Turborepo integration |

---

## Appendix A: Module Size Reference

### packages/core/src/ — Full Module Sizes (Source Only, Excluding Tests)

| Module | Lines | Files | Key Files |
|--------|-------|-------|-----------|
| tools/ | 12,139 | 43 | bash.ts (806), registry.ts (540), edit.ts (~500) |
| agent/ | 5,958 | 22 | loop.ts (1,083), types.ts (435) |
| permissions/ | 3,924 | 13 | manager.ts (386), auto-approve logic |
| codebase/ | 3,425 | 11 | indexer.ts (351), ranking.ts (396) |
| commander/ | 2,941 | 13 | executor.ts (396), types.ts (233) |
| llm/ | 2,806 | 19 | client.ts (265), anthropic.ts (315) |
| validator/ | 2,220 | 9 | pipeline.ts (362) |
| context/ | 2,198 | 12 | tracker.ts (291), compactor.ts (270) |
| config/ | 2,109 | 9 | manager.ts (352), schema.ts |
| session/ | 2,024 | 6 | manager.ts (772) |
| mcp/ | 1,498 | 6 | client.ts (412) |
| lsp/ | 1,219 | 4 | diagnostics, call-hierarchy |
| hooks/ | 1,147 | 4 | executor.ts |
| policy/ | 1,071 | 5 | engine.ts (324) |
| extensions/ | ~800 | 8 | manager.ts (437) |
| models/ | ~700 | 4 | registry.ts (522) |
| diff/ | ~700 | 6 | tracker.ts (363) |
| git/ | ~600 | 6 | auto-commit.ts (168) |
| bus/ | 524 | 3 | message-bus.ts |
| scheduler/ | ~500 | 4 | scheduler.ts (263) |
| custom-commands/ | ~500 | 9 | parser, template, discovery |
| skills/ | ~400 | 4 | loader.ts (213) |
| focus-chain/ | ~400 | 5 | manager.ts, parser.ts |

### packages/core-v2/src/ — Top Files

| File | Lines | Purpose |
|------|-------|---------|
| agent/loop.ts | 984 | Simplified agent executor |
| session/manager.ts | 385 | Session management |
| extensions/api.ts | 317 | Extension registries |
| extensions/types.ts | 255 | Extension type definitions |
| tools/utils.ts | 240 | Tool utilities |
| tools/edit-replacers.ts | 218 | Edit replacement strategies |
| agent/efficient-results.ts | 204 | Token-efficient tool results |
| extensions/manager.ts | 176 | Extension lifecycle |
| agent/types.ts | 174 | Agent types |

---

## Appendix B: Key Type Definitions

### IPlatformProvider (Rust Migration Target)

```typescript
interface IPlatformProvider {
  readonly fs: IFileSystem          // 15 methods
  readonly shell: IShell            // 2 methods (exec, spawn)
  readonly credentials: ICredentialStore  // 4 methods
  readonly database: IDatabase      // 4 methods
  readonly pty?: IPTY              // 2 methods
}
```

### Tool<TParams> (Registry Pattern)

```typescript
interface Tool<TParams> {
  definition: {
    name: string
    description: string
    parameters: JSONSchema
  }
  validate?(params: TParams, ctx: ToolContext): string | undefined
  execute(params: TParams, ctx: ToolContext): Promise<ToolResult>
}

interface ToolContext {
  sessionId: string
  workingDirectory: string
  signal: AbortSignal
  provider?: string
  model?: string
  onEvent?: AgentEventCallback
  onProgress?: (data: { chunk: string }) => void
  delegationDepth?: number
}

interface ToolResult {
  success: boolean
  output: string
  error?: string
}
```

### AgentConfig (v2)

```typescript
interface AgentConfig {
  id?: string
  provider?: LLMProvider
  model?: string
  systemPrompt?: string
  maxTurns: number              // default: 50
  maxTimeMinutes: number        // default: 30
  maxSteps?: number             // optional step limit
  maxRetries?: number           // default: 3
  compactionThreshold?: number  // default: 0.8
  compactionStrategy?: string | string[]
  thinking?: boolean
  parallelToolExecution?: boolean  // default: true
  toolChoiceStrategy?: 'auto' | 'required' | 'required-first'
  toolMode?: string             // agent mode name
  allowedTools?: string[]       // for subagents
  delegationDepth?: number
  steeringDeliveryMode?: 'one-at-a-time' | 'all'
  responseFormat?: { type: 'json_object'; schema: Record<string, unknown> }
}
```

---

*This document serves as the technical foundation for the AVA Rust migration roadmap. It should be updated as the codebase evolves and as migration phases are completed.*
