# AVA Rust Competitive Architecture Analysis — 2026-03

## Executive Summary

Analysis of 12 AI coding assistant codebases focusing on Rust-applicable patterns. Key findings:

1. **Three Rust Competitors Exist**: Codex CLI, Zed, and Goose all use Rust for core agent logic — validating AVA's architecture direction
2. **thiserror Dominates Error Handling**: All Rust projects use `thiserror` for typed errors, `anyhow` for propagation — AVA should standardize on this pattern
3. **Extension-Based Tool Systems**: Goose and Zed use MCP-based extension systems — AVA should consider this for tool extensibility
4. **Context Compaction is Universal**: All tools implement sliding window + summarization — Codex CLI's approach is most sophisticated
5. **Two-Layer Client Pattern**: Codex CLI's ModelClient/ModelClientSession split is elegant for connection management
6. **Permission Systems Vary**: Goose has the most granular permission system with pre-execution inspection
7. **TUI Streaming**: Codex CLI's animation tick system (TARGET_FRAME_INTERVAL) for smooth streaming is worth adopting
8. **Workspace Granularity**: Zed's 245 crates vs AVA's ~20 — AVA has room to split further
9. **Async Patterns Converge**: All use tokio, channels for communication, Arc<Mutex<>> for shared state
10. **Tracing/Instrumentation**: All use `#[instrument]` macro for structured logging — AVA should adopt throughout

## Competitor Deep Dives

### Codex CLI (Rust — PRIMARY Reference)

**Architecture Overview**
- **Workspace**: 69 crates in `codex-rs/`
- **Key Crates**: codex-core, codex-tui, codex-api, protocol, exec, skills
- **Rust Version**: 2024 edition (cutting edge)
- **Lines of Code**: ~150K estimated

**A. Agent Loop Design** (`codex-rs/core/src/codex.rs`)

```rust
// Main orchestrator pattern
pub struct Codex {
    context_manager: Arc<Mutex<ContextManager>>,
    hooks: Hooks,
    turn_metadata_state: TurnMetadataState,
    agent_control_rx: async_channel::Receiver<AgentControl>,
}
```

- **Turn Structure**: Single-turn request/response with streaming
- **Stop Conditions**: TurnAborted error variant, agent_control channel for external cancellation
- **Retry Logic**: Retry-budget in client layer, exponential backoff via reqwest-retry
- **Tool Call Parsing**: Native function calling via OpenAI API, structured as `FunctionCall` type
- **System Prompt**: Multiple prompt files (`gpt_5_2_prompt.md`, `prompt.md`) with templating
- **History Management**: ContextManager with compaction threshold
- **Loop Detection**: Built into context manager via token counting

**B. Tool System Architecture** (`codex-rs/core/src/tools/`)

```rust
// Tool definition pattern
pub struct FunctionTool {
    pub name: String,
    pub description: String,
    pub parameters: serde_json::Value,
    pub handler: Box<dyn ToolHandler>,
}
```

- **Tool Definition**: Trait-based with JSON schema generation
- **Execution**: Async with timeout support
- **Sandboxing**: Multiple backends (Seccomp, Landlock, Windows sandbox)
- **Approval System**: Safety tags with per-tool configuration
- **Middleware**: Hooks system for pre/post execution

**C. Provider Abstraction** (`codex-rs/core/src/client.rs`)

Two-layer client pattern:
```rust
pub struct ModelClient {
    auth_manager: Arc<AuthManager>,
    conversation_id: Option<String>,
    provider: Option<String>,
    cached_websocket_session: Option<Arc<Mutex<WebsocketSession>>>,
}

pub struct ModelClientSession {
    client: Arc<ModelClient>,
    conversation_id: String,
}
```

- **Streaming**: WebSocket with prewarming, fallback to SSE
- **Native Tool Calling**: Via OpenAI `tools` parameter
- **Error Handling**: Rich error types with retry delays (`RetryBudget`)
- **Token Counting**: ContextManager estimates via tiktoken-equivalent

**D. Context Window Management** (`codex-rs/core/src/context_manager/`)

```rust
pub struct ContextManager {
    messages: Vec<Message>,
    token_count: usize,
    compaction_threshold: usize,
}
```

- **Tracking**: Token estimation with tiktoken
- **Compaction**: Hybrid (summarize old + truncate recent)
- **System Prompt**: Preserved during compaction
- **Tool Results**: Truncated to max length

**E. Code Quality Patterns**

Error handling (`codex-rs/core/src/error.rs`):
```rust
#[derive(Debug, Error)]
pub enum CodexErr {
    #[error("Turn was aborted: {0}")]
    TurnAborted(String),
    #[error("Context window exceeded: {0}")]
    ContextWindowExceeded(String),
    #[error(transparent)]
    Sandbox(#[from] SandboxErr),
    // ... 30+ variants
}
```

- **Errors**: thiserror for typed errors, anyhow for propagation
- **Async**: tokio with channels (async-channel, tokio::sync::mpsc)
- **Testing**: insta for snapshot testing in TUI
- **Modules**: Clear separation (codex/, client/, tools/, config/)
- **Performance**: Connection pooling via WebSocket prewarming

**F. TUI/CLI Architecture** (`codex-rs/tui/src/app.rs`)

```rust
pub struct App {
    chat_widget: ChatWidget,
    history_cell: HistoryCell,
    thread_event_rx: tokio::sync::mpsc::Receiver<ThreadEvent>,
}
```

- **Rendering**: Ratatui 0.29 with custom widgets
- **State**: Arc<Mutex<>> shared between UI and agent
- **Events**: Crossterm for keyboard/mouse
- **Streaming**: Animation tick (TARGET_FRAME_INTERVAL) for smooth token display
- **Threading**: Dedicated event channel with 32K capacity

**G. Unique Techniques**

1. **WebSocket Prewarming**: Connections cached and reused across turns
2. **Hierarchical Agents**: Message routing for multi-agent scenarios
3. **Skills System**: Reusable task templates
4. **Safety Tags**: Fine-grained permission system
5. **Apply Patch Tool**: Specialized diff application with conflict resolution

**Best Architectural Decision**: Two-layer client (ModelClient/ModelClientSession) with connection pooling and sticky routing headers.

**What to Steal**: WebSocket prewarming and the compaction algorithm with its hybrid summarization approach.

---

### Zed (Rust — Secondary Reference)

**Architecture Overview**
- **Workspace**: 245 crates (massive monorepo)
- **Key Crates**: zed (main), gpui (UI framework), agent (AI features)
- **Lines of Code**: ~2M estimated

**E. Code Quality Patterns (Exemplary)**

Error handling pattern (from grep analysis):
```rust
// 175+ files use thiserror
#[derive(Debug, Error)]
#[error("{0}")]
pub struct MyError(String);

// ErrorCode enum with conversion trait
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ErrorCode {
    RateLimited,
    ConnectionLost,
    // ...
}

pub trait ErrorCodeExt {
    fn error_code(&self) -> Option<ErrorCode>;
}
```

- **Errors**: thiserror everywhere, ErrorCode for categorization
- **Async**: smol executor alongside tokio, parking_lot for sync
- **Testing**: Extensive test coverage, TestAppContext for UI tests
- **Modules**: Hyper-modular (245 crates), clear boundaries
- **Performance**: Zero-copy via GPUI's element system

**A. Agent Loop** (`crates/agent/src/agent.rs`)

```rust
pub struct Session {
    thread: Entity<Thread>,
    acp_thread: Entity<AcpThread>,
    language_models: Arc<LanguageModels>,
}
```

- **Architecture**: Entity-based state management via GPUI
- **Protocol**: ACP (Agent Client Protocol) abstraction
- **Tools**: MCP-based with permission system
- **Streaming**: Via GPUI's reactive system

**F. UI Architecture (GPUI)**

```rust
// GPUI framework: Entity<T> + Context<T> pattern
pub trait Render {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement;
}

// Usage
impl Render for MyView {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        div().child("Hello")
    }
}
```

- **Framework**: GPUI (custom retained-mode UI)
- **State**: Entity<T> with read/update methods
- **Events**: Actions system for commands
- **Async**: cx.spawn() for foreground, cx.background_spawn() for background
- **Reactivity**: cx.notify() triggers re-render, cx.subscribe() for events

**G. Unique Techniques**

1. **GPUI Framework**: Custom UI framework with retained-mode rendering
2. **Entity System**: Handle-based state management
3. **Tree-sitter Integration**: Native parsing for 20+ languages
4. **Collaboration**: Multiplayer editing via CRDTs
5. **Extension System**: WASM-based extensions

**Best Architectural Decision**: Entity-based state management with automatic cleanup and reactive updates.

**What to Steal**: Module organization granularity and the Action-based command system.

---

### Goose (Rust — Competitor)

**Architecture Overview**
- **Workspace**: 8 crates
- **Key Crates**: goose (core), goose-cli, goose-server, goose-mcp
- **Lines of Code**: ~50K estimated

**A. Agent Loop Design** (`crates/goose/src/agents/agent.rs`)

```rust
pub struct Agent {
    provider: Arc<dyn Provider>,
    extension_manager: ExtensionManager,
    permission_manager: PermissionManager,
    token_counter: TokenCounter,
}
```

- **Turn Limit**: DEFAULT_MAX_TURNS = 1000
- **Loop Detection**: RepetitionInspector + ToolMonitor
- **Context Compaction**: check_if_compaction_needed() with token threshold
- **Extensions**: MCP-based tool system

**B. Tool System Architecture**

```rust
pub struct ExtensionManager {
    extensions: HashMap<String, Extension>,
    tools: Vec<Tool>,
}

pub struct PermissionManager {
    confirmations: HashMap<String, PermissionConfirmation>,
}
```

- **Registration**: MCP protocol via rmcp crate
- **Execution**: ToolExecution module with monitoring
- **Permissions**: Pre-execution inspection via PermissionInspector
- **Sandboxing**: Subprocess-based with security checks

**C. Provider Abstraction** (`crates/goose/src/providers/`)

47 provider implementations:
- Base trait: `Provider` in `base.rs`
- Implementations: anthropic, openai, ollama, bedrock, azure, etc.
- Retry: RetryManager with exponential backoff
- Tool Shim: Converts between provider tool formats

**G. Unique Techniques**

1. **Recipe System**: YAML-based task definitions
2. **Security Inspector**: Pre-execution safety checks
3. **Tool Monitoring**: Tracks tool usage patterns
4. **Dictation**: Voice input support
5. **OAuth Integration**: Built-in OAuth flows for providers

**Best Architectural Decision**: MCP-based extension system for tool pluggability.

**What to Steal**: PermissionInspector for pre-execution validation.

---

### Aider (Python)

**Architecture Overview**
- **Language**: Python
- **Lines of Code**: ~30K estimated
- **Pattern**: Strategy pattern with multiple coder implementations

**A. Agent Loop Design**

```python
class Coder:
    """Base class for all coding modes"""
    def run_loop(self):
        while True:
            user_input = self.get_input()
            self.send_message(user_input)
            self.process_response()
```

- **Coder Types**: EditBlockCoder, WholeFileCoder, UnifiedDiffCoder, etc. (38 variants)
- **Mode Selection**: Automatic based on file state
- **History**: Sliding window with prompt caching

**B. Tool System**

```python
class EditBlockCoder(Coder):
    """Uses SEARCH/REPLACE blocks"""
    def apply_edits(self, content):
        # Parse SEARCH/REPLACE blocks
        pass
```

- **Editing Modes**: editblock, wholefile, udiff, ask, architect
- **Prompt Separation**: Separate prompt classes per mode
- **Repo Map**: File dependency analysis

**G. Unique Techniques**

1. **Multiple Edit Modes**: Different strategies for different file types
2. **Repo Map**: Automatic codebase structure analysis
3. **Linting Integration**: Automatic lint error fixing
4. **Voice Input**: Whisper integration

**Rust Translation**: Edit strategies as traits, repo map as tree-sitter analysis.

---

### Cline (TypeScript)

**Architecture Overview**
- **VS Code Extension**
- **Lines of Code**: ~40K estimated

**A. Agent Loop**

```typescript
class Agent {
  async runTask(userContent: string) {
    while (this.shouldContinue()) {
      const response = await this.provider.createMessage(messages);
      await this.processResponse(response);
    }
  }
}
```

**B. Tool System**

- ToolExecutorCoordinator for tool dispatch
- Hook system: precompact-executor, hook-executor
- ACP protocol support

**G. Unique Techniques**

1. **Hook System**: Extensible pre/post execution hooks
2. **QuickWin**: Optimization for simple tasks
3. **Subagent Support**: Recursive task delegation

---

### Continue (TypeScript)

**Architecture**: VS Code extension with modular architecture

**Key Patterns**:
- Config-based tool loading
- Multi-provider support via adapters
- Context providers for different file types

**Unique Techniques**:
1. **Context Providers**: Modular context injection
2. **Embeddings Integration**: Built-in codebase indexing

---

### Gemini CLI (TypeScript)

**Architecture**: Google's official CLI

**Key Patterns**:
- Google AI SDK integration
- Streaming via Gemini API
- Multi-turn conversations with context management

**Unique Techniques**:
1. **Grounding**: Google Search integration for facts
2. **Code Execution**: Sandboxed Python execution

---

### OpenCode (TypeScript)

**Architecture**: Extension-based monorepo

**Packages**:
- extensions/: Core extension system
- opencode/: Core runtime
- desktop/: Electron app

**Unique Techniques**:
1. **Extension System**: Similar to VS Code's extension model
2. **Multi-interface**: CLI + Desktop + Web

---

### OpenHands (Python)

**Architecture**: Agent framework with multi-agent support

**Key Patterns**:
- Agent abstraction with different implementations (CodeActAgent, PlannerAgent)
- Runtime abstraction (Docker, Local, E2B)
- Event-based communication

**Unique Techniques**:
1. **Multi-Agent**: Planner + Executor agent split
2. **Runtime Abstraction**: Docker, Local, E2B backends
3. **Microagents**: Specialized sub-agents

---

### Pi Mono / Claude Code (TypeScript)

**Architecture**: Claude's official CLI

**Key Patterns**:
- Tool-based architecture with approval system
- Bash tool with sandboxing
- Edit tool for file modifications
- Conversation history with summarization

**Unique Techniques**:
1. **Compact Tool**: Context window management via tool call
2. **Thinking Tokens**: Chain-of-thought visibility
3. **Sub-agents**: Delegate tool for task distribution

---

### Plandex (Go)

**Architecture**: Go-based CLI with client/server split

**Key Patterns**:
- Server manages state, CLI is thin client
- Plan-based execution (hence the name)
- Git-like command structure

**Unique Techniques**:
1. **Plans**: Structured task plans with steps
2. **Client/Server**: State managed on server, CLI is stateless

---

### SWE Agent (Python)

**Architecture**: Academic research project

**Key Patterns**:
- Environment abstraction (docker-based)
- Trajectory replay for debugging
- Specialized for GitHub issues

**Unique Techniques**:
1. **Trajectory Reproduction**: Exact replay of agent actions
2. **Evaluation Framework**: Built-in benchmark support

---

## Pattern Comparison Matrix

### Agent Loop Patterns

| Pattern | Codex CLI | Zed | Goose | Aider | Cline | Claude Code | AVA | Recommendation |
|---------|-----------|-----|-------|-------|-------|-------------|-----|----------------|
| **Turn Structure** | Streaming req/resp | Streaming req/resp | Streaming req/resp | Blocking req/resp | Streaming req/resp | Streaming req/resp | Streaming req/resp | Keep AVA's streaming |
| **Stop Conditions** | Error variants + channel | Entity state | Token limit | User interrupt | Token limit | Tool-based | Middleware + error | Adopt Codex's error variants |
| **Retry Logic** | Retry-budget | Basic retry | RetryManager | Basic retry | Exponential | Exponential | Basic retry | Adopt Codex's retry-budget |
| **Tool Parsing** | Native function calling | MCP | MCP | Text parsing | Native + text | Native + text | JSON parsing | Migrate to native function calling |
| **History Management** | ContextManager | Thread entity | Token counter | Sliding window | Sliding window | Compact tool | Sliding window | Adopt ContextManager pattern |
| **Loop Detection** | Token counting | Built-in | RepetitionInspector | Manual | Manual | Manual | Middleware | Adopt RepetitionInspector |

### Tool System Patterns

| Pattern | Codex CLI | Zed | Goose | Aider | Cline | Claude Code | AVA | Recommendation |
|---------|-----------|-----|-------|-------|-------|-------------|-----|----------------|
| **Definition** | Trait + JSON schema | MCP | MCP | Python class | Interface | JSON schema | Tool trait | Keep trait, add JSON schema gen |
| **Execution** | Async with timeout | Async | Async with monitoring | Sync | Async | Async | Async | Add ToolMonitor |
| **Sandboxing** | Seccomp/Landlock | Process | Subprocess | None | None | Bash sandbox | bwrap/sandbox-exec | Add Landlock option |
| **Approval** | Safety tags | Per-tool | PermissionInspector | None | Per-tool | Per-tool | Permission system | Adopt PermissionInspector |
| **Middleware** | Hooks system | Actions | ExtensionManager | None | Hooks | None | Middleware stack | Keep middleware, add hooks |

### Provider Abstraction Patterns

| Pattern | Codex CLI | Zed | Goose | Aider | Cline | Claude Code | AVA | Recommendation |
|---------|-----------|-----|-------|-------|-------|-------------|-----|----------------|
| **Abstraction** | Two-layer client | Provider trait | Provider trait | LiteLLM | Adapter | Adapter | LLMProvider trait | Adopt two-layer pattern |
| **Streaming** | WebSocket + SSE | GPUI reactive | SSE | SSE | SSE | SSE | SSE | Add WebSocket support |
| **Native Tools** | Yes | Yes | Via toolshim | No | Yes | Yes | Yes | Keep native tools |
| **Error Handling** | Rich types | ErrorCode | anyhow | Exceptions | Errors | Errors | Custom types | Standardize on thiserror |
| **Token Counting** | ContextManager | Provider API | TokenCounter | tiktoken | Estimation | Estimation | Estimation | Add TokenCounter |
| **Connection Pool** | WebSocket cache | None | None | N/A | N/A | N/A | None | Add connection pooling |

### Context Management Patterns

| Pattern | Codex CLI | Zed | Goose | Aider | Cline | Claude Code | AVA | Recommendation |
|---------|-----------|-----|-------|-------|-------|-------------|-----|----------------|
| **Tracking** | Token estimation | Token estimation | Token counting | tiktoken | Estimation | Estimation | Estimation | Add accurate token counting |
| **Compaction** | Hybrid (sum+trunc) | Summarize | Hybrid | Truncate | Truncate | Compact tool | Truncate | Adopt hybrid compaction |
| **System Prompt** | Preserved | Preserved | Preserved | Preserved | Preserved | Preserved | Preserved | Keep preservation |
| **Tool Results** | Truncated | Truncated | Truncated | Full | Truncated | Truncated | Truncated | Keep truncation |
| **Threshold** | Configurable | Configurable | DEFAULT_COMPACTION_THRESHOLD | 80% | 80% | 90% | 80% | Make configurable |

### TUI/CLI Patterns

| Pattern | Codex CLI | Zed | Goose | AVA | Recommendation |
|---------|-----------|-----|-------|-----|----------------|
| **Framework** | Ratatui | GPUI (custom) | Rustyline | Ratatui | Keep Ratatui |
| **State** | Arc<Mutex<>> | Entity<T> | Arc<Mutex<>> | Arc<Mutex<>> | Standardize on Arc<Mutex<>> |
| **Events** | Crossterm | Custom | Crossterm | Crossterm | Keep crossterm |
| **Streaming** | Animation tick | GPUI reactive | Line-by-line | Token-by-token | Add animation tick |
| **Rendering** | Immediate mode | Retained mode | Immediate mode | Immediate mode | Keep immediate mode |

---

## Top 20 Steal-Worthy Patterns

### 1. **Two-Layer Client Pattern** (from Codex CLI)
- **What**: ModelClient (session-scoped) + ModelClientSession (per-turn)
- **Why**: Enables connection pooling, sticky routing, and clean separation of concerns
- **Where**: `codex-rs/core/src/client.rs:40-200`
- **AVA Target**: `crates/ava-llm/src/providers/` - refactor current single-layer clients

### 2. **ContextManager with Hybrid Compaction** (from Codex CLI)
- **What**: Token tracking + hybrid compaction (summarize old, truncate recent)
- **Why**: Maximizes context retention while staying within limits
- **Where**: `codex-rs/core/src/context_manager/`
- **AVA Target**: `crates/ava-context/src/` - replace simple truncation

### 3. **Error Enum with Rich Variants** (from Codex CLI)
- **What**: 30+ typed error variants using thiserror
- **Why**: Enables precise error handling and user-friendly messages
- **Where**: `codex-rs/core/src/error.rs:1-200`
- **AVA Target**: `crates/ava-types/src/error.rs` - standardize error types

### 4. **WebSocket Prewarming** (from Codex CLI)
- **What**: Cache WebSocket connections for reuse across turns
- **Why**: Eliminates connection latency on subsequent requests
- **Where**: `codex-rs/core/src/client.rs:180-250`
- **AVA Target**: `crates/ava-llm/src/providers/openai.rs`

### 5. **Retry-Budget Pattern** (from Codex CLI)
- **What**: Track retry attempts with budget limits per operation
- **Why**: Prevents infinite retry loops, enables circuit breaker patterns
- **Where**: `codex-rs/core/src/client.rs:250-320`
- **AVA Target**: `crates/ava-llm/src/client.rs`

### 6. **Animation Tick for Streaming** (from Codex CLI)
- **What**: TARGET_FRAME_INTERVAL for smooth token animation
- **Why**: Smooths out bursty token streams, better perceived performance
- **Where**: `codex-rs/tui/src/app.rs:200-250`
- **AVA Target**: `crates/ava-tui/src/components/chat.rs`

### 7. **PermissionInspector Pattern** (from Goose)
- **What**: Pre-execution tool validation with configurable policies
- **Why**: Catches dangerous operations before execution
- **Where**: `goose/crates/goose/src/permission/permission_inspector.rs`
- **AVA Target**: `crates/ava-permissions/src/`

### 8. **ToolMonitor with Repetition Detection** (from Goose)
- **What**: Track tool usage patterns, detect stuck loops
- **Why**: Automatic detection of agent getting stuck
- **Where**: `goose/crates/goose/src/agents/tool_monitor.rs`
- **AVA Target**: `crates/ava-agent/src/middleware/reliability.rs`

### 9. **MCP-Based Extension System** (from Goose/Zed)
- **What**: Tools as external MCP servers, not compiled in
- **Why**: True extensibility without recompilation
- **Where**: `goose/crates/goose/src/agents/extension_manager.rs`
- **AVA Target**: New crate `crates/ava-extensions/`

### 10. **Entity-Based State Management** (from Zed)
- **What**: Handle-based state with automatic lifecycle management
- **Why**: Clean ownership, automatic cleanup, reactive updates
- **Where**: `zed/crates/gpui/src/entity.rs`
- **AVA Target**: Consider for future TUI refactoring

### 11. **Action-Based Command System** (from Zed)
- **What**: All UI commands as Action types, dispatchable anywhere
- **Why**: Decouples keyboard shortcuts from command implementation
- **Where**: `zed/crates/gpui/src/action.rs`
- **AVA Target**: `crates/ava-tui/src/actions.rs`

### 12. **Multiple Coder Strategy Pattern** (from Aider)
- **What**: Different editing strategies for different file types
- **Why**: Optimizes edit format for context
- **Where**: `aider/aider/coders/`
- **AVA Target**: `crates/ava-tools/src/core/edit.rs` - add strategies

### 13. **Repo Map Analysis** (from Aider)
- **What**: Automatic codebase structure extraction
- **Why**: Better context selection for large codebases
- **Where**: `aider/aider/repomap.py`
- **AVA Target**: `crates/ava-codebase/src/indexer.rs`

### 14. **Hook System** (from Codex CLI/Cline)
- **What**: Pre/post execution hooks for tools
- **Why**: Extensibility without modifying core
- **Where**: `codex-rs/core/src/hooks.rs`
- **AVA Target**: `crates/ava-tools/src/middleware.rs`

### 15. **Safety Tags** (from Codex CLI)
- **What**: Fine-grained permission levels per tool
- **Why**: More granular than binary allow/deny
- **Where**: `codex-rs/core/src/safety.rs`
- **AVA Target**: `crates/ava-permissions/src/tags.rs`

### 16. **TokenCounter Utility** (from Goose)
- **What**: Accurate token estimation per provider
- **Why**: Better context window management
- **Where**: `goose/crates/goose/src/token_counter.rs`
- **AVA Target**: `crates/ava-llm/src/tokenizer.rs`

### 17. **Skill Templates** (from Codex CLI)
- **What**: Reusable task templates with parameters
- **Why**: Common tasks without rewriting prompts
- **Where**: `codex-rs/core/src/skills/`
- **AVA Target**: New crate `crates/ava-skills/`

### 18. **Session Persistence with Rollout** (from Codex CLI)
- **What**: Structured session recording for replay/analysis
- **Why**: Debugging, evaluation, audit trails
- **Where**: `codex-rs/core/src/rollout.rs`
- **AVA Target**: `crates/ava-session/src/recorder.rs`

### 19. **Provider Tool Shim** (from Goose)
- **What**: Convert between different provider tool formats
- **Why**: Single tool definition, multiple provider outputs
- **Where**: `goose/crates/goose/src/providers/toolshim.rs`
- **AVA Target**: `crates/ava-llm/src/tool_format.rs`

### 20. **Structured Logging with Tracing** (from All Rust Projects)
- **What**: #[instrument] macro on all async functions
- **Why**: Automatic span creation, structured logs, debugging
- **Where**: Throughout all Rust codebases
- **AVA Target**: Add to all crates in `crates/`

---

## Updated Comparison Matrix

### Feature Matrix (Updated)

| Feature | Codex CLI | Zed | Goose | Aider | Cline | Claude Code | AVA Current | AVA Target |
|---------|-----------|-----|-------|-------|-------|-------------|-------------|------------|
| **Native Tool Calling** | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ |
| **Streaming Responses** | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ |
| **Context Compaction** | ✅ Hybrid | ✅ | ✅ Hybrid | ✅ | ✅ | ✅ | ⚠️ Truncate | ✅ Hybrid |
| **Connection Pooling** | ✅ | ❌ | ❌ | N/A | ❌ | ❌ | ❌ | ✅ |
| **Fine-Grained Permissions** | ✅ Tags | ✅ | ✅ Inspector | ❌ | ✅ | ✅ | ⚠️ Basic | ✅ Inspector |
| **Extension System** | ❌ | ✅ MCP | ✅ MCP | ❌ | ❌ | ❌ | ❌ | ✅ MCP |
| **Repetition Detection** | ✅ | ✅ | ✅ | ❌ | ❌ | ❌ | ⚠️ Basic | ✅ |
| **Multi-Agent** | ✅ | ✅ | ✅ | ❌ | ✅ | ✅ | ✅ | ✅ |
| **Sandboxing** | ✅ Seccomp | ❌ | ⚠️ Basic | ❌ | ❌ | ⚠️ Basic | ✅ bwrap | ✅ + Landlock |
| **Session Recording** | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ✅ | ✅ |
| **WebSocket Streaming** | ✅ | ❌ | ❌ | N/A | ❌ | ❌ | ❌ | ✅ |
| **Voice Mode** | ✅ | ❌ | ✅ | ✅ | ❌ | ❌ | ❌ | ⚠️ Future |

Legend: ✅ = Yes, ❌ = No, ⚠️ = Partial, N/A = Not Applicable

---

## Recommended Sprints

Based on this analysis, here are 10 recommended sprints in priority order:

### Sprint 1: Error Handling Standardization
**Priority**: High
**Scope**: Standardize on `thiserror` across all crates
**Tasks**:
- Create `crates/ava-types/src/error.rs` with shared error types
- Refactor existing errors to use typed variants
- Add context-specific error types (AgentError, ToolError, ProviderError)
- Add user-friendly Display implementations
**Effort**: 3 days
**Impact**: High — better error handling, debugging, UX

### Sprint 2: Context Management Refactor
**Priority**: High
**Scope**: Replace simple truncation with hybrid compaction
**Tasks**:
- Study Codex CLI's ContextManager implementation
- Implement hybrid compaction (summarize old + truncate recent)
- Add configurable compaction threshold
- Add token counting per message
**Effort**: 5 days
**Impact**: High — better context retention

### Sprint 3: Two-Layer Client Architecture
**Priority**: High
**Scope**: Refactor LLM providers to use session/turn split
**Tasks**:
- Create ProviderClient (session-scoped)
- Create ProviderTurn (per-turn)
- Implement connection pooling for WebSocket providers
- Add WebSocket prewarming for OpenAI
**Effort**: 5 days
**Impact**: High — better performance, cleaner architecture

### Sprint 4: Tool System Enhancement
**Priority**: Medium-High
**Scope**: Add tool monitoring and repetition detection
**Tasks**:
- Create ToolMonitor struct
- Implement RepetitionInspector
- Add tool usage tracking
- Integrate with existing reliability middleware
**Effort**: 4 days
**Impact**: Medium — better loop detection

### Sprint 5: Permission System Upgrade
**Priority**: Medium-High
**Scope**: Adopt PermissionInspector pattern
**Tasks**:
- Create PermissionInspector trait
- Implement pre-execution validation
- Add fine-grained safety tags
- Add configurable permission policies
**Effort**: 4 days
**Impact**: Medium — better security

### Sprint 6: TUI Streaming Improvements
**Priority**: Medium
**Scope**: Add animation tick for smooth streaming
**Tasks**:
- Add TARGET_FRAME_INTERVAL constant
- Buffer tokens and render on tick
- Smooth out bursty streams
- Benchmark perceived performance
**Effort**: 2 days
**Impact**: Medium — better UX

### Sprint 7: MCP Extension System
**Priority**: Medium
**Scope**: Add MCP-based tool extensions
**Tasks**:
- Create `crates/ava-extensions/`
- Integrate rmcp crate
- Implement ExtensionManager
- Add MCP client support
**Effort**: 10 days
**Impact**: High — true extensibility

### Sprint 8: Token Counter Implementation
**Priority**: Medium
**Scope**: Add accurate token counting per provider
**Tasks**:
- Study tiktoken/tiktoken-rs
- Implement TokenCounter per provider
- Add token estimation for context management
- Cache token counts per message
**Effort**: 3 days
**Impact**: Medium — better context management

### Sprint 9: Structured Logging with Tracing
**Priority**: Low-Medium
**Scope**: Add #[instrument] to all async functions
**Tasks**:
- Add tracing dependency to all crates
- Add #[instrument] to public APIs
- Configure tracing-subscriber
- Add span contexts for debugging
**Effort**: 3 days
**Impact**: Low-Medium — better debugging

### Sprint 10: Skill Template System
**Priority**: Low
**Scope**: Add reusable task templates
**Tasks**:
- Create `crates/ava-skills/`
- Design skill template format (YAML/TOML)
- Implement skill loader
- Add parameter substitution
**Effort**: 5 days
**Impact**: Low — developer productivity

---

## Summary

This analysis reveals that **AVA's architecture is well-positioned** — the Rust-first approach is validated by three major competitors (Codex CLI, Zed, Goose) using the same language and patterns. Key areas for improvement:

1. **Context Management**: Codex CLI's hybrid compaction is superior to AVA's simple truncation
2. **Provider Architecture**: Two-layer client pattern enables connection pooling and cleaner separation
3. **Error Handling**: Standardize on `thiserror` throughout for typed, rich errors
4. **Tool System**: MCP-based extensions provide true pluggability
5. **Permissions**: Goose's PermissionInspector offers better pre-execution validation
6. **TUI**: Animation tick for streaming improves perceived performance

The top 3 priorities should be:
1. Error handling standardization (foundation)
2. Context management refactor (user experience)
3. Two-layer client architecture (performance)

These changes will position AVA competitively with Codex CLI while maintaining its architectural advantages in multi-agent orchestration and session management.
