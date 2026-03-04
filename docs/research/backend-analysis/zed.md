# Zed Editor -- AI/Agent Backend Analysis

> Zed is a Rust-based code editor (~76k GitHub stars). This document analyzes its AI agent backend,
> covering the agent loop, tool system, LLM providers, context management, edit prediction (Zeta),
> MCP integration, and novel features like the multibuffer review diff and inline assistant.

---

## Table of Contents

1. [AI Architecture Overview](#1-ai-architecture-overview)
2. [Tools](#2-tools)
3. [Agent Loop](#3-agent-loop)
4. [LLM Providers](#4-llm-providers)
5. [Context Management](#5-context-management)
6. [Zeta Edit Prediction](#6-zeta-edit-prediction)
7. [Tool Execution](#7-tool-execution)
8. [MCP Integration](#8-mcp-integration)
9. [Unique Features](#9-unique-features)

---

## 1. AI Architecture Overview

### Crate Map

Zed's AI system is distributed across ~25 crates. The key ones:

| Crate | Path | Purpose |
|-------|------|---------|
| `agent` | `crates/agent/` | Core agent loop, thread, tools, templates, permissions |
| `agent_ui` | `crates/agent_ui/` | Agent panel, diff pane, inline assistant, model selector |
| `agent_settings` | `crates/agent_settings/` | Agent profiles, tool permissions, model parameters |
| `language_model` | `crates/language_model/` | Trait abstractions: `LanguageModel`, `LanguageModelProvider`, registry |
| `language_models` | `crates/language_models/` | Concrete provider implementations (14 providers) |
| `context_server` | `crates/context_server/` | MCP client (JSON-RPC, stdio/HTTP transport) |
| `edit_prediction` | `crates/edit_prediction/` | Zeta/Mercury/Ollama edit prediction engine |
| `edit_prediction_context` | `crates/edit_prediction_context/` | Related file/excerpt assembly for predictions |
| `zeta_prompt` | `crates/zeta_prompt/` | Prompt formatting for the Zeta prediction model |
| `streaming_diff` | `crates/streaming_diff/` | Character-level streaming diff algorithm |
| `prompt_store` | `crates/prompt_store/` | System prompt building, rules files, project context |
| `acp_tools` | `crates/acp_tools/` | ACP (Agent Client Protocol) connection inspector |
| `web_search` | `crates/web_search/` | Web search provider abstraction |
| `anthropic` | `crates/anthropic/` | Anthropic API client |
| `open_ai` | `crates/open_ai/` | OpenAI API client |
| `google_ai` | `crates/google_ai/` | Google AI API client |
| `ollama` | `crates/ollama/` | Ollama local model client |
| `copilot` / `copilot_chat` | `crates/copilot*/` | GitHub Copilot integration |

### High-Level Architecture

```
User Input (Agent Panel / Inline Assistant)
    |
    v
Thread (crates/agent/src/thread.rs) -- the agentic loop
    |
    +---> LanguageModel trait (crates/language_model/) -- provider abstraction
    |         |
    |         +---> 14 concrete providers (crates/language_models/src/provider/)
    |
    +---> Tools (crates/agent/src/tools/) -- 18 built-in tools
    |         |
    |         +---> MCP tools via ContextServerRegistry
    |
    +---> System Prompt (crates/agent/src/templates/system_prompt.hbs)
    |         |
    |         +---> ProjectContext (crates/prompt_store/) -- rules, worktrees, OS info
    |
    +---> EditAgent (crates/agent/src/edit_agent.rs) -- sub-agent for file edits
              |
              +---> StreamingDiff (crates/streaming_diff/) -- real-time buffer updates

Edit Prediction (separate pipeline, not the agent):
    User types in editor
    |
    v
    EditPredictionStore (crates/edit_prediction/)
    |
    +---> Zeta (Zed's own model) / Mercury / Copilot / Ollama
    |
    +---> Prediction rendered as ghost text in editor
```

### Key Design Decisions

1. **Thread-based, not agent-based.** The core unit is a `Thread` (conversation), not a standalone agent. The agent loop lives inside `Thread::run_turn_internal`.

2. **Two editing pipelines.** The agent panel uses `EditFileTool` (delegates to `EditAgent` for LLM-generated edits), while the inline assistant uses `BufferCodegen` (streaming rewrite with tool calls for `rewrite_section` / `failure_message`).

3. **GPUI entity system.** Everything is GPUI entities (`Entity<Thread>`, `Entity<Project>`), with `Context<T>` for mutations and `AsyncApp` for async work.

4. **Handlebars templates.** System prompts are compiled from `.hbs` templates via `rust_embed`, not string concatenation.

5. **ACP (Agent Client Protocol).** Zed has its own protocol (`agent_client_protocol`) for agent-frontend communication, distinct from MCP.

---

## 2. Tools

### Built-in Tools (18)

Registered via a compile-time `tools!` macro in `crates/agent/src/tools.rs` that validates name uniqueness at compile time:

| Tool | File | Purpose |
|------|------|---------|
| `read_file` | `read_file_tool.rs` | Read file contents (supports line ranges, images, auto-outline for large files) |
| `edit_file` | `edit_file_tool.rs` | Create/edit/overwrite files (delegates to `EditAgent`) |
| `streaming_edit_file` | `streaming_edit_file_tool.rs` | Feature-flagged streaming edit (direct character-level diffs) |
| `terminal` | `terminal_tool.rs` | Execute shell commands with PTY, timeout support |
| `grep` | `grep_tool.rs` | Regex search across project (paginated, 20 matches/page) |
| `find_path` | `find_path_tool.rs` | Glob-based file path search (paginated, 50 matches/page) |
| `list_directory` | `list_directory_tool.rs` | List directory contents |
| `create_directory` | `create_directory_tool.rs` | Create new directories |
| `copy_path` | `copy_path_tool.rs` | Copy files/directories |
| `move_path` | `move_path_tool.rs` | Move/rename files |
| `delete_path` | `delete_path_tool.rs` | Delete files/directories |
| `save_file` | `save_file_tool.rs` | Save file to disk |
| `restore_file_from_disk` | `restore_file_from_disk_tool.rs` | Revert file to on-disk version |
| `diagnostics` | `diagnostics_tool.rs` | Get LSP errors/warnings (file-level or project-wide summary) |
| `fetch` | `fetch_tool.rs` | Fetch URL, convert HTML to markdown |
| `web_search` | `web_search_tool.rs` | Web search via configurable providers |
| `spawn_agent` | `spawn_agent_tool.rs` | Spawn subagent threads (parallel tasks, follow-ups) |
| `open` | `open_tool.rs` | Open file/URL with OS default application |
| `now` | `now_tool.rs` | Get current date/time |

### MCP Tools (Dynamic)

MCP tools are discovered and registered dynamically via `ContextServerRegistry` (`crates/agent/src/tools/context_server_registry.rs`). Each MCP server's tools are namespaced as `mcp:<server_id>:<tool_name>` and stored in `BTreeMap<SharedString, Arc<dyn AnyAgentTool>>`.

### Tool Permission System

The permission system (`crates/agent/src/tool_permissions.rs` -- 77k lines) is highly granular:

- **Permission modes:** `ByTool`, `ByCategory`, `AllowAll`
- **Tool kinds:** `Read`, `Write`, `Execute`, `Fetch`, `Agent`
- **Pattern-based always-allow/deny:** Extracts regex patterns from terminal commands, file paths, and URLs
- **Shell compatibility checks:** "Always allow" for terminal only shown if the user's shell supports POSIX chaining (prevents bypass via `cargo build && rm -rf /`)
- **Symlink authorization:** Separate permission flow for symlink targets

---

## 3. Agent Loop

The agent loop lives in `crates/agent/src/thread.rs` (152k -- the largest file in the agent crate).

### Core Types

```rust
// crates/agent/src/thread.rs

pub struct Thread {
    id: acp::SessionId,
    messages: Vec<Message>,
    pending_message: Option<AgentMessage>,
    model: Option<Arc<dyn LanguageModel>>,
    running_turn: Option<RunningTurn>,
    tools: BTreeMap<SharedString, Arc<dyn AnyAgentTool>>,
    project_context: Entity<ProjectContext>,
    templates: Arc<Templates>,
    thinking_enabled: bool,
    thinking_effort: Option<String>,
    prompt_id: PromptId,
    // ...
}

pub enum Message {
    User(UserMessage),
    Agent(AgentMessage),
    Resume,                    // "Continue where you left off"
}

pub struct AgentMessage {
    content: Vec<AgentMessageContent>,
    tool_results: IndexMap<LanguageModelToolUseId, LanguageModelToolResult>,
    reasoning_details: Option<serde_json::Value>,
}
```

### Turn Lifecycle

```
Thread::send(user_message)
    |
    v
Thread::run_turn(cx)
    |-- Flush pending message
    |-- Cancel previous turn
    |-- Collect enabled tools for profile + model
    |-- Spawn run_turn_internal task
    |
    v
Thread::run_turn_internal (async loop)
    |
    loop {
        1. Build completion request (build_completion_request)
        2. Call model.stream_completion(request)
        3. Process event stream:
           |-- Batch events (collect all immediately available)
           |-- For each event:
           |   |-- Text -> append to pending message, emit ThreadEvent::AgentText
           |   |-- Thinking -> append thinking block, emit ThreadEvent::AgentThinking
           |   |-- ToolUse -> spawn tool execution task, emit ThreadEvent::ToolCall
           |   |-- UsageUpdate -> record token usage telemetry
           |   |-- Stop -> handle end reason
           |
        4. Wait for all tool results (FuturesUnordered)
        5. If tool results exist -> continue loop (another turn)
        6. If stop reason is EndTurn or MaxTokens -> break
        7. If error -> retry with strategy (exponential backoff or fixed delay)
    }
```

### Retry Strategy

```rust
// crates/agent/src/thread.rs

const MAX_RETRY_ATTEMPTS: u8 = 4;
const BASE_RETRY_DELAY: Duration = Duration::from_secs(5);

enum RetryStrategy {
    ExponentialBackoff { initial_delay: Duration, max_attempts: u8 },
    Fixed { delay: Duration, max_attempts: u8 },
}
```

Retries are applied per-error type:
- **429 Too Many Requests / 503 Service Unavailable:** Exponential backoff, 4 attempts
- **500 Internal Server Error:** Fixed delay, 3 attempts
- **401/403/413 (auth, permission, payload too large):** No retry

### Subagent System

The `spawn_agent` tool creates child threads with `MAX_SUBAGENT_DEPTH = 1`. Each subagent:
- Gets a new `Thread` entity with `SubagentContext { parent_thread_id, depth }`
- Does NOT see the parent's conversation history
- Can be followed up via `session_id` parameter
- Returns only its final message to the parent

---

## 4. LLM Providers

### Provider Trait

```rust
// crates/language_model/src/language_model.rs

pub trait LanguageModel: Send + Sync {
    fn id(&self) -> LanguageModelId;
    fn name(&self) -> LanguageModelName;
    fn provider_id(&self) -> LanguageModelProviderId;
    fn provider_name(&self) -> LanguageModelProviderName;

    fn supports_thinking(&self) -> bool;
    fn supports_images(&self) -> bool;
    fn supports_tools(&self) -> bool;
    fn supports_tool_choice(&self, choice: LanguageModelToolChoice) -> bool;
    fn supports_streaming_tools(&self) -> bool;
    fn supports_fast_mode(&self) -> bool;

    fn max_token_count(&self) -> u64;
    fn max_output_tokens(&self) -> Option<u64>;

    fn stream_completion(
        &self,
        request: LanguageModelRequest,
        cx: &AsyncApp,
    ) -> BoxFuture<'static, Result<
        BoxStream<'static, Result<LanguageModelCompletionEvent, LanguageModelCompletionError>>
    >>;

    fn count_tokens(&self, request: LanguageModelRequest, cx: &App) -> BoxFuture<'static, Result<u64>>;

    fn cache_configuration(&self) -> Option<LanguageModelCacheConfiguration>;
}

pub trait LanguageModelProvider: 'static {
    fn id(&self) -> LanguageModelProviderId;
    fn name(&self) -> LanguageModelProviderName;
    fn default_model(&self, cx: &App) -> Option<Arc<dyn LanguageModel>>;
    fn default_fast_model(&self, cx: &App) -> Option<Arc<dyn LanguageModel>>;
    fn provided_models(&self, cx: &App) -> Vec<Arc<dyn LanguageModel>>;
    fn recommended_models(&self, cx: &App) -> Vec<Arc<dyn LanguageModel>>;
    fn is_authenticated(&self, cx: &App) -> bool;
    fn authenticate(&self, cx: &mut App) -> Task<Result<(), AuthenticateError>>;
    fn configuration_view(&self, ...) -> AnyView;
    fn reset_credentials(&self, cx: &mut App) -> Task<Result<()>>;
}
```

### Registered Providers (14)

All providers are in `crates/language_models/src/provider/`:

| Provider | File | Notes |
|----------|------|-------|
| Anthropic | `anthropic.rs` | Native Anthropic API client in `crates/anthropic/` |
| OpenAI | `open_ai.rs` | Native client in `crates/open_ai/` |
| Google AI | `google.rs` | Native client in `crates/google_ai/` |
| xAI | `x_ai.rs` | Native client in `crates/x_ai/` |
| OpenRouter | `open_router.rs` | Native client in `crates/open_router/` |
| Ollama | `ollama.rs` | Local models via `crates/ollama/` |
| LM Studio | `lmstudio.rs` | Local model server |
| Copilot Chat | `copilot_chat.rs` | GitHub Copilot Chat |
| DeepSeek | `deepseek.rs` | DeepSeek API |
| Mistral | `mistral.rs` | Mistral API via `crates/mistral/` |
| Bedrock | `bedrock.rs` | AWS Bedrock via `crates/bedrock/` |
| Vercel | `vercel.rs` | Vercel AI SDK |
| Vercel AI Gateway | `vercel_ai_gateway.rs` | Vercel AI Gateway |
| OpenAI Compatible | `open_ai_compatible.rs` | Generic OpenAI-compatible endpoint |
| Zed Cloud | `cloud.rs` | Zed's own proxy (routes to upstream providers) |

### Completion Event Stream

```rust
// crates/language_model/src/language_model.rs

pub enum LanguageModelCompletionEvent {
    Queued { position: usize },
    Started,
    Stop(StopReason),
    Text(String),
    Thinking { text: String, signature: Option<String> },
    RedactedThinking { data: String },
    ToolUse(LanguageModelToolUse),
    ToolUseJsonParseError { id, tool_name, raw_input, json_parse_error },
    StartMessage { message_id: String },
    ReasoningDetails(serde_json::Value),
    UsageUpdate(TokenUsage),
}
```

### Provider Registry

The `LanguageModelRegistry` (`crates/language_model/src/registry.rs`) is a GPUI global entity that:
- Stores providers in a `BTreeMap<LanguageModelProviderId, Arc<dyn LanguageModelProvider>>`
- Manages separate model slots: `default_model`, `default_fast_model`, `inline_assistant_model`, `commit_message_model`, `thread_summary_model`
- Tracks installed extension LLM providers and hides built-in providers when extensions replace them
- Supports inline alternatives (multiple models for the inline assistant)

---

## 5. Context Management

### System Prompt Construction

The system prompt is built from a Handlebars template (`crates/agent/src/templates/system_prompt.hbs`) with the following data model:

```rust
// crates/agent/src/templates.rs

pub struct SystemPromptTemplate<'a> {
    pub project: &'a ProjectContext,      // worktrees, rules, OS, shell
    pub available_tools: Vec<SharedString>, // tool names for conditional sections
    pub model_name: Option<String>,
}
```

The `ProjectContext` (`crates/prompt_store/src/prompts.rs`) includes:

```rust
pub struct ProjectContext {
    pub worktrees: Vec<WorktreeContext>,   // root dirs with abs paths
    pub has_rules: bool,                   // any .rules/.cursorrules/CLAUDE.md etc.
    pub user_rules: Vec<UserRulesContext>, // user-defined rules
    pub has_user_rules: bool,
    pub os: String,                        // e.g., "linux"
    pub arch: String,                      // e.g., "x86_64"
    pub shell: String,                     // e.g., "bash"
}
```

### Rules File Discovery

Zed searches for rules files with these names (in order):

```rust
pub const RULES_FILE_NAMES: &[&str] = &[
    ".rules",
    ".cursorrules",
    ".windsurfrules",
    ".clinerules",
    ".github/copilot-instructions.md",
    "CLAUDE.md",
    "AGENT.md",
    "AGENTS.md",
    "GEMINI.md",
];
```

This is notable -- Zed reads `.cursorrules`, `.windsurfrules`, `CLAUDE.md` and more. It does not limit itself to its own format.

### User Mention Context

When a user includes `@` mentions in their message, the context is structured into XML tags:

```xml
<context>
The following items were attached by the user. They are up-to-date and don't need to be re-read.

<files>
```rust src/main.rs
fn main() { ... }
```
</files>

<directories>
src/
  main.rs
  lib.rs
</directories>

<symbols>
```rust src/lib.rs:42-55
pub fn parse() { ... }
```
</symbols>

<selections>
```rust src/lib.rs:10-15
selected code
```
</selections>

<diffs>
Branch diff against main:
```diff
+added line
-removed line
```
</diffs>

<fetched_urls>
Fetch: https://example.com
... fetched content ...
</fetched_urls>

<rules>
user-specified rules
</rules>

<diagnostics>
error diagnostics
</diagnostics>
</context>
```

### Prompt Caching

The last message in the request is marked with `cache: true`:

```rust
if let Some(last_message) = messages.last_mut() {
    last_message.cache = true;
}
```

This enables Anthropic's prompt caching for the conversation history.

### Agent Profiles

Agent profiles (`crates/agent_settings/src/agent_profile.rs`) allow users to configure:
- Which tools are enabled/disabled per profile
- Custom system prompt additions
- Default model selection
- Tool permission overrides

---

## 6. Zeta Edit Prediction

Zeta is Zed's proprietary edit prediction system. It is completely separate from the agent loop --
it predicts the next edit a user will make based on their cursor position and recent edits.

### Architecture

```
crates/edit_prediction/           -- Main prediction engine
crates/edit_prediction_context/   -- Related file/excerpt extraction
crates/edit_prediction_types/     -- Shared types
crates/edit_prediction_ui/        -- UI rendering (ghost text)
crates/edit_prediction_cli/       -- CLI for eval/testing
crates/zeta_prompt/               -- Prompt formatting
```

### Prediction Providers

| Provider | File | Description |
|----------|------|-------------|
| Zeta (Zed Cloud) | `zeta.rs` | Zed's proprietary model, accessed via cloud API |
| Mercury | `mercury.rs` | InceptionLabs API (`api.inceptionlabs.ai`) |
| Copilot | via `copilot` crate | GitHub Copilot suggestions |
| Ollama | `ollama.rs` | Local models for edit prediction |
| OpenAI-compatible | `open_ai_response.rs` | Generic OpenAI-compatible endpoints |
| SweepAI | `sweep_ai.rs` | SweepAI prediction provider |

### Zeta Prompt Format

The Zeta model uses a specialized prompt format (`crates/zeta_prompt/`) that includes:
- Current file content with a cursor marker (`CURSOR_MARKER`)
- An editable region marked with start/end markers
- Related files for cross-file context
- Recent edit events (what the user just typed/deleted)
- Excerpt ranges for context windowing

```rust
pub struct ZetaPromptInput {
    pub events: Vec<...>,               // recent user edits
    pub related_files: Vec<...>,        // files related to the cursor file
    pub cursor_path: Arc<Path>,
    pub cursor_offset_in_excerpt: usize,
    pub cursor_excerpt: String,
    pub excerpt_start_row: Option<u32>,
    pub excerpt_ranges: ExcerptRanges,
    pub experiment: Option<String>,
    pub in_open_source_repo: bool,
    pub can_collect_data: bool,
}
```

### Prediction Pipeline

1. **Trigger:** User pauses typing (debounced)
2. **Context assembly:** `edit_prediction_context` gathers the cursor excerpt, related files via LSP definitions, and recent edit events
3. **Prompt formatting:** `zeta_prompt` formats the input into the model-specific prompt
4. **Model call:** Sent to Zed's cloud API (`cloud_llm_client::predict_edits_v3`)
5. **Response parsing:** `compute_edits` in `zeta.rs` parses the model output into buffer anchor ranges
6. **Edit interpolation:** `prediction.rs` interpolates edits against the current buffer state (handles concurrent user edits)
7. **Rendering:** `edit_prediction_ui` renders as ghost text

### License Detection

The `license_detection.rs` module watches for open-source licenses in the project to set `in_open_source_repo` and `can_collect_data` flags, affecting data collection policies.

### Key Constants

```rust
// Mercury provider
const MAX_REWRITE_TOKENS: usize = 150;
const MAX_CONTEXT_TOKENS: usize = 350;
```

---

## 7. Tool Execution

### Tool Trait

```rust
// crates/agent/src/thread.rs

pub trait AgentTool: 'static + Sized {
    type Input: Deserialize + Serialize + JsonSchema;
    type Output: Deserialize + Serialize + Into<LanguageModelToolResultContent>;

    const NAME: &'static str;

    fn description() -> SharedString;      // derived from JsonSchema
    fn kind() -> acp::ToolKind;            // Read, Write, Execute, Fetch, Agent
    fn initial_title(&self, input: Result<Self::Input, serde_json::Value>, cx: &mut App) -> SharedString;
    fn input_schema(format: LanguageModelToolSchemaFormat) -> Schema;
    fn supports_input_streaming() -> bool; // default: false
    fn supports_provider(provider: &LanguageModelProviderId) -> bool; // default: true

    fn run(
        self: Arc<Self>,
        input: ToolInput<Self::Input>,
        event_stream: ToolCallEventStream,
        cx: &mut App,
    ) -> Task<Result<Self::Output, Self::Output>>;

    fn replay(&self, input: Self::Input, output: Self::Output, ...) -> Result<()>;
}
```

### Key Design Patterns

1. **`Result<Output, Output>` for tool errors.** Tool errors are NOT `anyhow::Error` -- they return the same `Output` type. This ensures error messages are structured and readable by the LLM when sent back as tool results.

2. **Type-erased tools.** The `Erased<Arc<T>>` wrapper implements `AnyAgentTool`, allowing heterogeneous tool storage in `BTreeMap<SharedString, Arc<dyn AnyAgentTool>>`.

3. **Input streaming.** Some tools (like `streaming_edit_file`) support receiving partial input before it is fully parsed. The `ToolInput<T>` wrapper provides either immediate or streamed access.

4. **Compile-time validation.** The `tools!` macro validates at compile time that no two tools share the same `NAME`.

5. **Provider filtering.** Tools can declare incompatibility with specific providers (e.g., `web_search` only works with Zed Cloud).

### EditAgent (Sub-Agent for File Editing)

The `edit_file` tool does NOT directly edit buffers. It delegates to `EditAgent` (`crates/agent/src/edit_agent.rs`), which:

1. Renders a prompt from the edit description using Handlebars templates
2. Makes a separate LLM call with the file content and edit instructions
3. Parses the model output using `EditParser` (supports XML and diff-fenced formats)
4. Applies edits to the buffer using `StreamingDiff` for character-level streaming
5. Handles reindentation via the `Reindenter`

```rust
pub struct EditAgent {
    model: Arc<dyn LanguageModel>,
    action_log: Entity<ActionLog>,
    project: Entity<Project>,
    templates: Arc<Templates>,
    edit_format: EditFormat,        // XML or DiffFenced
    thinking_allowed: bool,
    update_agent_location: bool,    // show cursor position in UI
}
```

The edit format varies by model family:
- Some models use XML-based search/replace blocks
- Others use diff-fenced format (unified diff style)

### Tool Event Stream

Tools communicate status updates back to the UI via `ToolCallEventStream`:

```rust
pub struct ToolCallEventStream { ... }

// Tools can emit:
// - Title updates
// - Location updates (file path + line range)
// - Diff previews
// - Progress information
```

---

## 8. MCP Integration

### Transport Layer

```rust
// crates/context_server/src/transport.rs

#[async_trait]
pub trait Transport: Send + Sync {
    async fn send(&self, message: String) -> Result<()>;
    fn receive(&self) -> Pin<Box<dyn Stream<Item = String> + Send>>;
    fn receive_err(&self) -> Pin<Box<dyn Stream<Item = String> + Send>>;
}
```

Two transports:
- **Stdio:** `StdioTransport` -- spawns a child process, communicates via stdin/stdout
- **HTTP:** `HttpTransport` -- HTTP/HTTPS streaming (Server-Sent Events)

### Client

The MCP client (`crates/context_server/src/client.rs`) implements full JSON-RPC 2.0:

```rust
pub(crate) struct Client {
    server_id: ContextServerId,
    next_id: AtomicI32,
    outbound_tx: channel::Sender<String>,
    response_handlers: Arc<Mutex<Option<HashMap<RequestId, ResponseHandler>>>>,
    executor: BackgroundExecutor,
    transport: Arc<dyn Transport>,
    request_timeout: Option<Duration>,  // default: 60s
}
```

### Protocol Support

The MCP protocol version is `2025-03-26` (`crates/context_server/src/types.rs`):

```rust
pub const LATEST_PROTOCOL_VERSION: &str = "2025-03-26";
```

Supported MCP operations:

| Operation | Method |
|-----------|--------|
| Initialize | `initialize` |
| List tools | `tools/list` |
| Call tool | `tools/call` |
| List resources | `resources/list` |
| Read resource | `resources/read` |
| Subscribe resource | `resources/subscribe` |
| Unsubscribe resource | `resources/unsubscribe` |
| List prompts | `prompts/list` |
| Get prompt | `prompts/get` |
| Complete | `completion/complete` |
| List roots | `roots/list` |
| List resource templates | `resources/templates/list` |
| Ping | `ping` |

### MCP Server Hosting

Zed also hosts an MCP server (`crates/context_server/src/listener.rs`) over Unix domain sockets:

```rust
pub struct McpServer {
    socket_path: PathBuf,
    tools: Rc<RefCell<HashMap<&'static str, RegisteredTool>>>,
    handlers: Rc<RefCell<HashMap<&'static str, RequestHandler>>>,
    _server_task: Task<()>,
}
```

This allows external MCP clients to connect to Zed and use its tools.

### Context Server Registry

The `ContextServerRegistry` (`crates/agent/src/tools/context_server_registry.rs`) bridges MCP servers with the agent's tool system:

- Watches `ContextServerStore` for server status changes
- When a server starts, loads its tools via `tools/list` and wraps them as `Arc<dyn AnyAgentTool>`
- Tool names are namespaced: `mcp:<server_id>:<tool_name>`
- Automatically reloads tools when servers emit `ToolsChanged` notifications
- Also loads MCP prompts via `prompts/list`

---

## 9. Unique Features

### 9.1 Agent Diff Pane (Multibuffer Review)

`crates/agent_ui/src/agent_diff.rs` -- `AgentDiffPane`

When the agent makes file changes, they are collected into a **multibuffer diff view** that shows all changes across multiple files in a single editor:

```rust
pub struct AgentDiffPane {
    multibuffer: Entity<MultiBuffer>,     // all changed files combined
    editor: Entity<Editor>,               // standard editor for navigation
    thread: Entity<AcpThread>,            // the agent thread that made changes
    // ...
}
```

Key behaviors:
- **Keep/Reject per hunk:** Users can accept or reject individual diff hunks, not just entire files
- **Keep All/Reject All:** Batch operations across all files
- **Live updates:** The diff pane updates in real-time as the agent makes changes
- **Navigation:** Standard Go to Hunk / Go to Previous Hunk keybindings work

### 9.2 Inline Assistant

`crates/agent_ui/src/inline_assistant.rs` + `buffer_codegen.rs`

The inline assistant is a separate AI pipeline from the agent panel:

- User selects code and triggers inline assist (Ctrl+Enter or similar)
- A `PromptEditor` overlay appears inline in the editor
- Uses `BufferCodegen` which calls the LLM with tool-use:
  - `rewrite_section` tool: replaces the selected code
  - `failure_message` tool: explains why the edit cannot be made
- Supports **alternatives**: multiple codegen attempts stored, user can cycle through them
- Uses `StreamingDiff` for character-level real-time buffer updates

```rust
pub struct BufferCodegen {
    alternatives: Vec<Entity<CodegenAlternative>>,
    active_alternative: usize,
    buffer: Entity<MultiBuffer>,
    range: Range<Anchor>,
    builder: Arc<PromptBuilder>,
    is_insertion: bool,
    session_id: Uuid,
}
```

### 9.3 Terminal Inline Assistant

`crates/agent_ui/src/terminal_inline_assistant.rs`

A separate inline assistant for terminal panels -- the user can ask the AI to generate shell commands that get inserted into the terminal.

### 9.4 Agent Location Tracking

When tools are executing, the agent's "location" is tracked and displayed in the UI:

```rust
// crates/agent/src/edit_agent.rs
project.set_agent_location(Some(AgentLocation {
    buffer: buffer.downgrade(),
    position: language::Anchor::min_for_buffer(buffer_id),
}), cx);
```

This shows the user which file and position the agent is currently working on, creating a "cursor following" effect.

### 9.5 Streaming Fuzzy Edit Matching

`crates/agent/src/edit_agent/streaming_fuzzy_matcher.rs`

When the `EditAgent` receives streamed model output for file edits, it uses a streaming fuzzy matcher to find where in the buffer the edit should be applied. This handles cases where the model's output does not exactly match the buffer contents (e.g., due to whitespace differences or minor formatting changes).

### 9.6 Streaming Diff Algorithm

`crates/streaming_diff/src/streaming_diff.rs`

A custom character-level diff algorithm using a dynamic programming matrix that processes text incrementally:

```rust
pub struct StreamingDiff {
    old: Vec<char>,
    new: Vec<char>,
    scores: Matrix,
    old_text_ix: usize,
    new_text_ix: usize,
    equal_runs: BTreeSet<(OrderedFloat<f64>, usize)>,
}
```

The algorithm:
1. Receives new characters as they stream from the LLM
2. Computes optimal alignment against the existing buffer content
3. Emits `CharOperation::Insert` and `CharOperation::Delete` events
4. Groups character operations into line-level operations (`LineOperation`)
5. Applies operations to the buffer in real-time, showing edits as they arrive

### 9.7 Thread Summarization

The agent supports automatic thread summarization for the thread history:

```rust
// crates/agent_settings/
pub const SUMMARIZE_THREAD_PROMPT: &str = include_str!("prompts/summarize_thread_prompt.txt");
pub const SUMMARIZE_THREAD_DETAILED_PROMPT: &str = include_str!("prompts/summarize_thread_detailed_prompt.txt");
```

A separate (potentially smaller/faster) model generates summaries via `thread_summary_model`.

### 9.8 ACP (Agent Client Protocol)

Zed has its own protocol separate from MCP for agent-to-frontend communication:

- `agent_client_protocol` crate defines the protocol types
- `acp_thread` crate manages the protocol-level thread state
- `acp_tools` crate provides a debugging inspector UI for ACP connections
- This is used for the Zed agent's native server mode and remote agent control

### 9.9 Code Block Path-Based Formatting

Zed's system prompt enforces a unique code block format:

```
```path/to/file.rs#L123-456
code here
```
```

Instead of the standard ```` ```language ```` syntax, Zed requires a file path after the opening backticks. This enables:
- Direct linking to files in the project
- Line number ranges for context
- Clickable code blocks in the UI

### 9.10 Edit Prediction with Edit Events

The Zeta prediction model receives not just the current buffer state but also a history of recent edit events (insertions, deletions, cursor movements). This gives the model temporal context about what the user is doing, not just where they are.

### 9.11 Model-Specific Settings

```rust
// crates/agent_settings/

pub struct AgentSettings {
    pub default_model: Option<LanguageModelSelection>,
    pub inline_assistant_model: Option<LanguageModelSelection>,      // separate model for inline
    pub commit_message_model: Option<LanguageModelSelection>,        // separate for commit msgs
    pub thread_summary_model: Option<LanguageModelSelection>,        // separate for summaries
    pub inline_alternatives: Vec<LanguageModelSelection>,            // multiple models for inline
    pub model_parameters: Vec<LanguageModelParameters>,              // per-model temperature overrides
    pub profiles: IndexMap<AgentProfileId, AgentProfileSettings>,    // tool configs per profile
}
```

This allows users to use different models for different tasks -- e.g., a cheaper/faster model for summaries, a smarter model for the agent, and Copilot for inline predictions.

---

## Summary of Key Patterns

| Pattern | Zed's Approach |
|---------|---------------|
| Agent loop | Single async loop in `Thread::run_turn_internal`, event batching |
| Tool definition | `AgentTool` trait with `JsonSchema` derive, compile-time name validation |
| Tool errors | Same `Output` type for success and failure (not `anyhow::Error`) |
| LLM abstraction | `LanguageModel` trait with `stream_completion` returning event stream |
| Provider model | `LanguageModelProvider` trait, registered in global `LanguageModelRegistry` |
| Context | Handlebars templates for system prompt, XML-tagged mention context |
| Prompt caching | Last message marked `cache: true` (Anthropic-style) |
| Edit application | Streaming fuzzy match + `StreamingDiff` for character-level buffer updates |
| MCP | Full MCP client + server, tools namespaced as `mcp:<server>:<tool>` |
| Subagents | `spawn_agent` tool with depth limit of 1 |
| Permissions | Granular per-tool, per-category, pattern-based always-allow/deny |
| Edit prediction | Separate pipeline (Zeta), not part of agent -- cursor-triggered, edit-event-aware |
| State management | GPUI entity system (`Entity<T>`, `Context<T>`, `AsyncApp`) |
| Templates | Handlebars with `rust_embed`, not string concatenation |
| Serialization | Thread messages are `Serialize`/`Deserialize` for persistence in `db.rs` |

### File Size Indicators

| File | Lines | Notes |
|------|-------|-------|
| `thread.rs` | ~152k chars | The agent loop, tool trait, message types |
| `agent_panel.rs` | ~190k chars | Main UI panel |
| `edit_prediction.rs` | ~104k chars | Prediction engine |
| `agent_diff.rs` | ~81k chars | Multibuffer diff review |
| `tool_permissions.rs` | ~77k chars | Permission system |
| `inline_assistant.rs` | ~92k chars | Inline code assistant |
| `buffer_codegen.rs` | ~76k chars | Streaming code generation |
| `connection_view.rs` | ~225k chars | Provider connection UI |
