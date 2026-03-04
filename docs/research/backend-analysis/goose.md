# Goose Backend Architecture Analysis

> Block (formerly Square) - Open-source AI coding agent
> Rust + Tauri desktop app, Apache-2.0 license
> Codebase version: 1.26.0

---

## 1. Project Structure

Goose is a Rust workspace with 8 crates:

```
goose/
├── crates/
│   ├── goose/              # Core library — agent loop, providers, tools, sessions
│   ├── goose-server/       # HTTP server (Axum) — REST API for desktop/CLI
│   ├── goose-cli/          # CLI binary — terminal interface
│   ├── goose-mcp/          # Built-in MCP servers (memory, computercontroller, etc.)
│   ├── goose-acp/          # Agent Communication Protocol server + macros
│   ├── goose-acp-macros/   # Proc macros for ACP
│   ├── goose-test/         # Integration test harness
│   └── goose-test-support/ # Test utilities
├── ui/                     # Desktop frontend (Electron/Tauri webview)
├── vendor/                 # Vendored dependencies (v8)
├── evals/                  # Evaluation benchmarks
├── examples/               # Example recipes
└── services/               # External service configs
```

### Core Crate Module Map (`crates/goose/src/`)

```
agents/
├── agent.rs                 # Main Agent struct + reply loop (1400+ lines)
├── container.rs             # Docker container ID wrapper
├── extension.rs             # ExtensionConfig enum (7 variants)
├── extension_manager.rs     # MCP client lifecycle manager
├── extension_malware_check.rs
├── mcp_client.rs            # GooseClient — MCP client handler
├── platform_tools.rs        # Platform-level tools (scheduler)
├── platform_extensions/     # In-process extensions (developer, analyze, etc.)
│   ├── developer/           # shell, write, edit, tree tools
│   ├── analyze/             # Tree-sitter code analysis
│   ├── summon.rs            # Subagent delegation + knowledge loading
│   ├── todo.rs              # Per-session todo list
│   ├── apps.rs              # HTML/CSS/JS sandboxed apps
│   ├── chatrecall.rs        # Cross-session search
│   ├── ext_manager.rs       # Enable/disable extensions at runtime
│   ├── code_execution.rs    # Code-mode (behind feature flag)
│   └── tom.rs               # "Top of Mind" — inject context every turn
├── prompt_manager.rs        # System prompt builder
├── retry.rs                 # Recipe retry logic
├── subagent_handler.rs      # Subagent spawning
├── subagent_task_config.rs  # Subagent config
├── final_output_tool.rs     # Structured output collection
├── large_response_handler.rs # >200k char responses → temp file
├── tool_execution.rs        # Permission-gated tool dispatch
├── reply_parts.rs           # Stream reply assembly
├── moim.rs                  # "Message Of Immediate Mind" injection
└── builtin_skills/          # Embedded markdown skills

providers/
├── base.rs                  # Provider trait + MessageStream type
├── anthropic.rs             # Anthropic Claude
├── openai.rs                # OpenAI GPT
├── google.rs                # Google Gemini
├── azure.rs                 # Azure OpenAI
├── bedrock.rs               # AWS Bedrock
├── gcpvertexai.rs           # GCP Vertex AI
├── openrouter.rs            # OpenRouter
├── ollama.rs                # Ollama local
├── litellm.rs               # LiteLLM proxy
├── databricks.rs            # Databricks
├── snowflake.rs             # Snowflake Cortex
├── xai.rs                   # xAI Grok
├── venice.rs                # Venice AI
├── tetrate.rs               # Tetrate
├── githubcopilot.rs         # GitHub Copilot
├── chatgpt_codex.rs         # ChatGPT Codex
├── claude_code.rs           # Claude Code
├── cursor_agent.rs          # Cursor Agent
├── codex.rs                 # Codex CLI
├── gemini_cli.rs            # Gemini CLI
├── sagemaker_tgi.rs         # SageMaker TGI
├── lead_worker.rs           # Dual-model provider (lead + worker)
├── toolshim.rs              # Tool calling for models without native support
├── local_inference/         # Local model inference
├── provider_registry.rs     # Dynamic provider registry
├── canonical/               # Canonical model registry (JSON data)
├── declarative/             # YAML-declared custom providers
├── formats/                 # Wire format adapters (OpenAI, Anthropic)
├── api_client.rs            # HTTP client wrapper
├── retry.rs                 # Provider-level retry
├── errors.rs                # ProviderError enum
├── catalog.rs               # Provider catalog metadata
└── usage_estimator.rs       # Token usage estimation

context_mgmt/               # Context window management
conversation/               # Message types, serialization
session/                    # SQLite session persistence
config/                     # YAML config, paths, permissions, experiments
permission/                 # Permission inspector + judge + store
security/                   # Prompt injection detection (patterns + ML)
prompts/                    # Jinja2 system prompt templates
recipe/                     # YAML recipe system
hints/                      # .goosehints / AGENTS.md loading
oauth/                      # OAuth device code flow
otel/                       # OpenTelemetry tracing
```

---

## 2. Tools

Goose has no hard-coded tool list in the core. All tools are provided by **extensions** — either "platform extensions" (in-process) or external MCP servers. Platform extensions expose tools through the `McpClientTrait` interface.

### Developer Extension (platform, in-process)

The core coding tools live in `crates/goose/src/agents/platform_extensions/developer/`:

| Tool | File | Description |
|------|------|-------------|
| `shell` | `shell.rs` | Execute shell commands in user's default shell. Returns structured `{stdout, stderr, exit_code, timed_out}`. Output capped at 2000 lines / 50KB per stream; overflow saved to temp file. Supports timeout. |
| `write` | `edit.rs` | Create new file or overwrite existing. Creates parent directories automatically. |
| `edit` | `edit.rs` | Find-and-replace edit. Requires exact `before` text match (must be unique). Empty `after` = delete. Shows nearby context on mismatch. |
| `tree` | `tree.rs` | Directory listing with line counts. Respects `.gitignore`. Configurable depth. |

**Key design choice**: No `read_file` tool. The system prompt instructs the agent to use `shell` with `cat`, `sed`, `rg`, etc. for reading. This is a minimalist approach — fewer tools, more shell usage.

### Analyze Extension (platform, in-process)

File: `crates/goose/src/agents/platform_extensions/analyze/`

| Tool | Description |
|------|-------------|
| `analyze` | Tree-sitter powered code analysis. Accepts a path + optional `focus` symbol. Returns directory overviews with symbol listings, or call graphs when focused on a specific symbol. Supports 10 languages (Go, Java, JavaScript, Kotlin, Python, Ruby, Rust, Swift, TypeScript). Uses `rayon` for parallel parsing. |

### Summon Extension (platform, in-process)

File: `crates/goose/src/agents/platform_extensions/summon.rs`

| Tool | Description |
|------|-------------|
| `load` | Inject knowledge from recipes, skills, or built-in skills into the current context. Can list available sources. |
| `delegate` | Spawn isolated subagent sessions (sync or async). Subagents get their own provider, extensions, and conversation. Supports custom instructions, parameters, provider/model override, and async execution with status polling. |

### Todo Extension (platform, in-process)

File: `crates/goose/src/agents/platform_extensions/todo.rs`

| Tool | Description |
|------|-------------|
| `todo__read` | Read the current session's todo checklist. |
| `todo__write` | Update the session's todo checklist (markdown format). |

### Apps Extension (platform, in-process)

File: `crates/goose/src/agents/platform_extensions/apps.rs`

| Tool | Description |
|------|-------------|
| `apps__*` | Create and manage custom Goose "apps" — HTML/CSS/JS sandboxed windows. The agent can create interactive dashboards, utilities, etc. |

### Chat Recall Extension (platform, in-process)

File: `crates/goose/src/agents/platform_extensions/chatrecall.rs`

| Tool | Description |
|------|-------------|
| `chatrecall__*` | Search past conversations and load session summaries for contextual memory. |

### Extension Manager Extension (platform, in-process)

File: `crates/goose/src/agents/platform_extensions/ext_manager.rs`

| Tool | Description |
|------|-------------|
| `platform__manage_extensions` | Discover, enable, and disable extensions at runtime. Searches available extensions. |

### Top Of Mind (TOM) Extension (platform, in-process)

File: `crates/goose/src/agents/platform_extensions/tom.rs`

| Tool | Description |
|------|-------------|
| (no tools) | Injects custom context into every turn via `GOOSE_MOIM_MESSAGE_TEXT` and `GOOSE_MOIM_MESSAGE_FILE` environment variables. Pure context injection, no exposed tools. |

### Computer Controller MCP Server (built-in, separate process)

File: `crates/goose-mcp/src/computercontroller/`

| Tool | Description |
|------|-------------|
| `web_scrape` | Fetch URL content, save as text/JSON/binary. |
| `automation_script` | Run shell/batch/Ruby/PowerShell scripts. |
| `computer_control` | macOS: Peekaboo CLI passthrough (see, click, type, hotkey). Windows/Linux: automation scripts. |
| `cache` | List/view/delete/clear cached files. |
| `pdf_tool` | Extract text or images from PDFs. |
| `docx_tool` | Extract text from or create/update DOCX files with structured content. |
| `xlsx_tool` | Read/analyze Excel spreadsheets. |

### Memory MCP Server (built-in, separate process)

File: `crates/goose-mcp/src/memory/`

| Tool | Description |
|------|-------------|
| `remember_memory` | Store categorized memories with tags. Global or project-local storage. |
| `retrieve_memories` | Retrieve memories by category. |
| `remove_memory_category` | Remove entire category of memories. |
| `remove_specific_memory` | Remove a specific memory entry. |

### Other Built-in MCP Servers

| Server | File | Description |
|--------|------|-------------|
| `autovisualiser` | `crates/goose-mcp/src/autovisualiser/` | Generate interactive HTML charts/visualizations (Chart.js, D3, Mermaid, Leaflet maps). |
| `tutorial` | `crates/goose-mcp/src/tutorial/` | Built-in tutorials (building MCP extensions, creating games). |
| `peekaboo` | `crates/goose-mcp/src/peekaboo/` | macOS-only screen annotation/interaction tool. |

### Platform Tools

| Tool | File | Description |
|------|------|-------------|
| `platform__manage_schedule` | `crates/goose/src/agents/platform_tools.rs` | Manage scheduled recipe execution (cron-style). List, create, run, pause, unpause, delete, kill, inspect, sessions. |
| `recipe__final_output` | `crates/goose/src/agents/final_output_tool.rs` | Collect structured JSON output from recipes. Schema-validated against recipe's response definition. |

---

## 3. Agent Loop

The agent loop is in `crates/goose/src/agents/agent.rs`, primarily in the `reply()` and `reply_internal()` methods (~500 lines of streaming async code).

### Flow

```
User Message
    │
    ├─→ Slash command check (/compact, /clear, /prompts, /prompt, or recipe)
    │   └─→ If matched, handle immediately and return
    │
    ├─→ Save message to session
    │
    ├─→ Check if auto-compaction needed (token ratio > threshold)
    │   └─→ If yes, compact conversation and emit HistoryReplaced event
    │
    └─→ reply_internal() — the main turn loop
        │
        ├─→ Prepare tools + system prompt
        ├─→ Spawn background session naming task
        │
        └─→ LOOP (up to max_turns, default 1000):
            │
            ├─→ Check cancellation token
            ├─→ Check if FinalOutputTool has collected output → break
            ├─→ Check turn limit → break with "reached max" message
            │
            ├─→ Inject MOIM (Top Of Mind) context
            ├─→ Maybe summarize old tool pairs (background)
            │
            ├─→ stream_response_from_provider()
            │   └─→ Provider.stream() → MessageStream (SSE-style chunks)
            │
            ├─→ For each chunk from the stream:
            │   ├─→ Emit ModelChange event (for lead-worker providers)
            │   ├─→ Update session metrics (token usage)
            │   │
            │   ├─→ Categorize tool requests:
            │   │   ├─→ Frontend tools (executed by UI)
            │   │   └─→ Remaining tools (backend execution)
            │   │
            │   ├─→ If no tool calls → append text message, continue
            │   │
            │   ├─→ If Chat mode → skip all tool calls with explanation
            │   │
            │   ├─→ Run ToolInspectionManager:
            │   │   ├─→ SecurityInspector (prompt injection detection)
            │   │   ├─→ PermissionInspector (approval/deny/ask)
            │   │   └─→ RepetitionInspector (doom loop detection)
            │   │
            │   ├─→ Handle approved tools → dispatch_tool_call()
            │   ├─→ Handle denied tools → inject DECLINED_RESPONSE
            │   ├─→ Handle needs-approval tools → yield ActionRequired event, wait for user
            │   │
            │   ├─→ Collect tool results (streamed with MCP notifications)
            │   ├─→ Large response handler (>200k chars → temp file)
            │   └─→ Append tool responses to conversation
            │
            ├─→ Handle context length exceeded → auto-compact
            ├─→ Handle retry logic (for recipes with retry config)
            └─→ Continue loop if tool calls were made, break if text-only
```

### Key Design Decisions

**Streaming-first**: The `Provider` trait's primary method is `stream()`, returning a `MessageStream` (pinned boxed stream). Text arrives word-by-word, tool calls arrive as complete objects. The `collect_stream()` helper coalesces consecutive text chunks.

**Turn-based with streaming**: Each iteration of the loop = one LLM call. Tool results are fed back immediately. The loop continues until the LLM produces a text-only response (no tool calls), hits the turn limit, or the FinalOutputTool captures structured output.

**Tool call cut-off**: Configurable via `GOOSE_TOOL_CALL_CUTOFF` (default 10). After this many tool calls per turn, old tool request/response pairs are summarized to save context.

**Cancellation**: Uses `tokio_util::CancellationToken` throughout. Checked at multiple points in the loop.

**Error recovery**: On `ContextLengthExceeded` errors, the loop automatically compacts the conversation and retries (up to 3 attempts).

---

## 4. LLM Providers

### Provider Trait

Defined in `crates/goose/src/providers/base.rs`:

```rust
#[async_trait]
pub trait Provider: Send + Sync {
    fn get_name(&self) -> &str;

    async fn stream(
        &self,
        model_config: &ModelConfig,
        session_id: &str,
        system: &str,
        messages: &[Message],
        tools: &[Tool],
    ) -> Result<MessageStream, ProviderError>;

    async fn complete(...) -> Result<(Message, ProviderUsage), ProviderError>;  // default: collect stream
    async fn complete_fast(...) -> Result<...>;  // fast model with fallback
    fn get_model_config(&self) -> ModelConfig;
    fn retry_config(&self) -> RetryConfig;
    async fn fetch_supported_models(&self) -> Result<Vec<String>, ProviderError>;
    async fn fetch_recommended_models(&self) -> Result<Vec<String>, ProviderError>;
    async fn generate_session_name(&self, ...) -> Result<String, ProviderError>;
    async fn configure_oauth(&self) -> Result<(), ProviderError>;
    fn permission_routing(&self) -> PermissionRouting;
    fn supports_embeddings(&self) -> bool;
    async fn create_embeddings(&self, ...) -> Result<Vec<Vec<f32>>, ProviderError>;
    fn as_lead_worker(&self) -> Option<&dyn LeadWorkerProviderTrait>;
}
```

### Registered Providers (~20+)

| Provider | File | Notes |
|----------|------|-------|
| Anthropic | `anthropic.rs` | Claude models (4.0-4.6), extended thinking support |
| OpenAI | `openai.rs` | GPT + o-series, supports Chat Completions + Responses API |
| Google | `google.rs` | Gemini models |
| Azure OpenAI | `azure.rs` | Azure-hosted OpenAI models |
| AWS Bedrock | `bedrock.rs` | Multi-provider through AWS |
| GCP Vertex AI | `gcpvertexai.rs` | Google Cloud hosted |
| OpenRouter | `openrouter.rs` | Multi-provider proxy |
| Ollama | `ollama.rs` | Local inference |
| LiteLLM | `litellm.rs` | Universal proxy |
| Databricks | `databricks.rs` | Databricks-hosted models |
| Snowflake | `snowflake.rs` | Snowflake Cortex |
| xAI | `xai.rs` | Grok models |
| Venice | `venice.rs` | Venice AI |
| Tetrate | `tetrate.rs` | Tetrate (with OAuth) |
| GitHub Copilot | `githubcopilot.rs` | Copilot API |
| SageMaker TGI | `sagemaker_tgi.rs` | AWS SageMaker |
| ChatGPT Codex | `chatgpt_codex.rs` | OpenAI Codex wrapper |
| Claude Code | `claude_code.rs` | Anthropic Claude Code wrapper |
| Cursor Agent | `cursor_agent.rs` | Cursor's agent API |
| Codex | `codex.rs` | OpenAI Codex CLI |
| Gemini CLI | `gemini_cli.rs` | Google Gemini CLI |

### Provider Abstraction

- **Wire formats**: Two format adapters in `providers/formats/` — `openai` and `anthropic`. Most providers use the OpenAI-compatible format via `openai_compatible.rs`.
- **API client**: Shared `ApiClient` struct (`api_client.rs`) handles HTTP with configurable auth (API key, Bearer token, OAuth, custom headers).
- **Canonical model registry**: JSON data files map provider-specific model names to canonical IDs with context limits, modalities, and cost info.
- **Declarative providers**: YAML config can define new OpenAI/Anthropic-compatible providers without code.
- **Provider registry**: `ProviderRegistry` stores constructors as `Arc<dyn Fn>`, enabling runtime provider creation.

### Lead-Worker Provider

File: `crates/goose/src/providers/lead_worker.rs`

A unique dual-model provider that automatically switches between a "lead" model (stronger, used for first N turns) and a "worker" model (cheaper, used after). Falls back to the lead model after consecutive worker failures.

```rust
pub struct LeadWorkerProvider {
    lead_provider: Arc<dyn Provider>,
    worker_provider: Arc<dyn Provider>,
    lead_turns: usize,          // default 3
    max_failures_before_fallback: usize,  // default 2
    fallback_turns: usize,      // default 2
}
```

### Tool Shim

File: `crates/goose/src/providers/toolshim.rs`

For models without native tool/function calling support (like some Ollama models), the ToolShim:
1. Sends the text output to an "interpreter" model (default: `mistral-nemo` on Ollama)
2. Extracts tool call intentions as structured JSON
3. Augments the original message with proper tool calls

---

## 5. Context/Token Management

### Token Counting

File: `crates/goose/src/token_counter.rs`

Uses `tiktoken-rs` (OpenAI's tokenizer) with a hash-based LRU cache (10,000 entries). Counts tokens for messages, tools (with overhead estimates for JSON schema encoding), and system prompts.

### Auto-Compaction

File: `crates/goose/src/context_mgmt/mod.rs`

**Threshold**: Configurable via `GOOSE_AUTO_COMPACT_THRESHOLD` (default: 0.8 = 80% of context limit).

**Process**:
1. Before each reply, check `current_tokens / context_limit > threshold`
2. Token count sourced from session metadata (provider-reported) or estimated via tiktoken
3. If needed, compact:
   - Call LLM to summarize the full conversation
   - Mark original messages as `agent_invisible` (but keep `user_visible` for UI)
   - Insert summary as `agent_only` message
   - Preserve the most recent user message (not summarized)
   - Add continuation text based on context (conversation vs tool loop vs manual)

**Recovery compaction**: On `ContextLengthExceeded` errors during the agent loop, performs emergency compaction (up to 3 attempts with progressively aggressive summarization).

**Tool pair summarization**: Background task that summarizes old tool call/response pairs beyond the `tool_call_cut_off` threshold. Currently disabled via feature flag (`ENABLE_TOOL_PAIR_SUMMARIZATION = false`).

**Manual compaction**: Users can trigger via `/compact` command.

### Large Response Handler

File: `crates/goose/src/agents/large_response_handler.rs`

Tool responses exceeding 200,000 characters are written to a temporary file. The agent receives a message pointing to the file path instead of inline content.

### MOIM (Message Of Immediate Mind)

File: `crates/goose/src/agents/moim.rs`

Before each LLM call, extensions can inject "top of mind" context. The TOM extension reads from `GOOSE_MOIM_MESSAGE_TEXT` env var or `GOOSE_MOIM_MESSAGE_FILE` and injects it as a user message before the last assistant message. This is ephemeral — not persisted to conversation history.

---

## 6. Session Management

### Storage

File: `crates/goose/src/session/session_manager.rs`

Sessions are stored in **SQLite** via `sqlx`. Database location: `$APPDATA/goose/sessions.db` (schema version 7).

### Session Structure

```rust
pub struct Session {
    pub id: String,
    pub working_dir: PathBuf,
    pub name: String,
    pub user_set_name: bool,
    pub session_type: SessionType,  // User, Scheduled, SubAgent, Hidden, Terminal, Gateway
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub extension_data: ExtensionData,  // Per-extension state (todo, enabled extensions)
    pub total_tokens: Option<i32>,
    pub input_tokens: Option<i32>,
    pub output_tokens: Option<i32>,
    pub accumulated_total_tokens: Option<i32>,
    pub accumulated_input_tokens: Option<i32>,
    pub accumulated_output_tokens: Option<i32>,
    pub schedule_id: Option<String>,
    pub recipe: Option<Recipe>,
    pub user_recipe_values: Option<HashMap<String, String>>,
    pub conversation: Option<Conversation>,
    pub message_count: usize,
    pub provider_name: Option<String>,
    pub model_config: Option<ModelConfig>,
}
```

### Session Types

| Type | Purpose |
|------|---------|
| `User` | Normal user-initiated sessions |
| `Scheduled` | Cron-scheduled recipe executions |
| `SubAgent` | Spawned by the `delegate` tool |
| `Hidden` | Internal sessions not shown in UI |
| `Terminal` | CLI terminal sessions |
| `Gateway` | Gateway/remote sessions |

### Session Features

- **Auto-naming**: After 3 user messages, spawns a background task to generate a short title via the LLM's `complete_fast()` method
- **Message persistence**: Each message added individually via `add_message()`
- **Conversation replacement**: For compaction, the entire conversation can be atomically replaced
- **Extension state persistence**: Extensions loaded in a session are serialized to `extension_data` so they can be restored when resuming
- **Builder pattern for updates**: `SessionUpdateBuilder` allows partial updates without loading/saving full session
- **Chat history search**: FTS5 full-text search across past sessions (in `chat_history_search.rs`)
- **Diagnostics**: System info collection for debugging (`diagnostics.rs`)

### No Branching

Unlike some other agent frameworks, Goose does not support conversation branching or DAG-style message histories. Conversations are linear `Vec<Message>` with visibility metadata.

---

## 7. Extension/Plugin System

### Core Design: Everything is an MCP Client

Goose's extension model is built entirely on MCP (Model Context Protocol). Every extension — whether external or built-in — is an MCP server that the agent communicates with via the `McpClientTrait`.

### Extension Types (ExtensionConfig)

Defined in `crates/goose/src/agents/extension.rs`:

| Type | Transport | Description |
|------|-----------|-------------|
| `Stdio` | stdin/stdout | External process MCP server |
| `StreamableHttp` | HTTP streaming | Remote MCP server via HTTP |
| `Builtin` | In-memory duplex | Built-in MCP server (memory, computercontroller, etc.) |
| `Platform` | In-process | Direct Rust implementation via `McpClientTrait` |
| `Frontend` | UI channel | Tools executed by the desktop frontend |
| `InlinePython` | uvx subprocess | Python code executed via `uvx` |
| `Sse` | (deprecated) | Legacy SSE transport, kept for config compatibility |

### Extension Manager

File: `crates/goose/src/agents/extension_manager.rs`

The `ExtensionManager` manages the lifecycle of all extensions:
- Stores `HashMap<String, Extension>` (name -> MCP client + config + server info)
- Caches aggregated tool lists with atomic version counter
- Resolves env vars and secrets for extension configs
- Supports dynamic add/remove at runtime
- Validates extension malware signatures before loading
- Collects MCP resources from extensions for context injection

### Platform Extensions

Platform extensions run in-process and implement `McpClientTrait` directly in Rust. They get a `PlatformExtensionContext` with access to the extension manager and session manager.

Registered via a static `PLATFORM_EXTENSIONS` HashMap:

| Name | Default Enabled | Unprefixed Tools | Description |
|------|----------------|-------------------|-------------|
| `developer` | yes | yes | Shell, write, edit, tree |
| `analyze` | yes | yes | Tree-sitter code analysis |
| `summon` | yes | yes | Knowledge loading + subagent delegation |
| `todo` | yes | no | Session todo list |
| `apps` | yes | no | Sandboxed HTML apps |
| `chatrecall` | no | no | Cross-session search |
| `extensionmanager` | yes | no | Dynamic extension management |
| `tom` | yes | no | Top-of-mind context injection |
| `code_execution` | no | yes | Code mode (feature-gated) |

### Built-in MCP Servers

File: `crates/goose-mcp/src/lib.rs`

Built-in servers use in-memory `DuplexStream` transport (no subprocess):

```rust
pub static BUILTIN_EXTENSIONS: HashMap<&str, SpawnServerFn> = [
    ("autovisualiser", AutoVisualiserRouter),
    ("computercontroller", ComputerControllerServer),
    ("memory", MemoryServer),
    ("tutorial", TutorialServer),
];
```

Each is a full MCP server implementing `ServerHandler` from the `rmcp` crate.

### Tool Prefixing

Extension tools are prefixed with the extension name (e.g., `memory__remember_memory`) unless `unprefixed_tools: true`. The developer extension's `shell`, `write`, `edit`, `tree` tools are unprefixed for natural use.

### Environment Variable Security

The `Envs` struct in `extension.rs` blocks 31 dangerous environment variables from being overridden by extensions:
- Binary path manipulation: `PATH`, `PATHEXT`
- Dynamic linker hijacking: `LD_PRELOAD`, `DYLD_INSERT_LIBRARIES`
- Language runtime hijacking: `PYTHONPATH`, `NODE_OPTIONS`, `CLASSPATH`
- Windows-specific: `APPINIT_DLLS`, `ComSpec`

---

## 8. MCP Integration

### MCP Client

File: `crates/goose/src/agents/mcp_client.rs`

The `GooseClient` implements `ClientHandler` from the `rmcp` crate and wraps it with the `McpClientTrait`:

```rust
pub trait McpClientTrait: Send + Sync {
    async fn list_tools(&self, session_id: &str, ...) -> Result<ListToolsResult, Error>;
    async fn call_tool(&self, session_id: &str, name: &str, ...) -> Result<CallToolResult, Error>;
    fn get_info(&self) -> Option<&InitializeResult>;
    async fn list_resources(&self, ...) -> Result<ListResourcesResult, Error>;
    async fn read_resource(&self, ...) -> Result<ReadResourceResult, Error>;
    async fn list_prompts(&self, ...) -> Result<ListPromptsResult, Error>;
    async fn get_prompt(&self, ...) -> Result<GetPromptResult, Error>;
    async fn subscribe(&self) -> mpsc::Receiver<ServerNotification>;
    async fn get_moim(&self, session_id: &str) -> Option<String>;
}
```

Key features:
- **Sampling support**: The client handles `createMessage` callbacks from MCP servers, routing them to the agent's LLM provider
- **Elicitation support**: Handles `createElicitation` for interactive user input during tool execution
- **Notification forwarding**: MCP server notifications are streamed alongside tool results
- **Session context**: Passes `session_id` and `working_dir` via MCP extensions metadata headers
- **Cancellation**: Sends `cancelled` notifications when operations are cancelled

### Transports

Goose supports three MCP transports:
1. **Stdio**: `TokioChildProcess` — spawns subprocess, communicates via stdin/stdout
2. **StreamableHTTP**: `StreamableHttpClientTransport` — HTTP streaming with optional auth
3. **In-memory**: `DuplexStream` — for built-in extensions

The `rmcp` crate (v0.16) provides all transport implementations.

### MCP Server (goose-mcp)

File: `crates/goose-mcp/`

Goose also includes its own MCP servers that run as built-in extensions. These use the `rmcp` SDK's `#[tool]` and `#[tool_router]` proc macros for declarative tool definition.

### ACP (Agent Communication Protocol)

File: `crates/goose-acp/`

Goose implements ACP — an agent-to-agent protocol. The `GooseAcpAgent` wraps the full agent loop and exposes it as an ACP server. This enables:
- Remote agent sessions with MCP server configuration
- Tool call streaming with location metadata (file path + line number)
- Permission request/response flow
- Model switching per session
- Session listing and management

---

## 9. Permissions/Safety

### Goose Modes

File: `crates/goose/src/config/goose_mode.rs`

| Mode | Behavior |
|------|----------|
| `Auto` | All tools auto-approved, no user confirmation |
| `Approve` | Read-only tools auto-approved, others require user approval |
| `SmartApprove` | Like Approve but with learned preferences |
| `Chat` | No tool execution — agent explains what it would do |

### Tool Inspection Pipeline

File: `crates/goose/src/tool_inspection.rs`

Three inspectors run sequentially on every tool call:

1. **SecurityInspector** (highest priority)
   - Pattern-based detection of dangerous commands (40+ patterns)
   - Categories: filesystem destruction, remote code execution, data exfiltration, privilege escalation, command injection
   - Risk levels: Low (0.45), Medium (0.60), High (0.75), Critical (0.95)
   - Optional ML-based classification via external service
   - Threshold-based: findings below threshold are logged but not blocking

2. **PermissionInspector** (medium priority)
   - Checks user-defined permissions (AlwaysAllow, AskBefore, NeverAllow)
   - Pre-approves read-only and regular tools
   - Requires approval for unknown tools and extension management

3. **RepetitionInspector** (lowest priority)
   - Detects doom loops (same tool + same arguments repeated)
   - Configurable max repetitions
   - Resets on different tool calls

### Inspection Result Merging

Security and other inspectors can escalate (override) permission decisions:
- `Deny` overrides `Allow` and `RequireApproval`
- `RequireApproval` overrides `Allow`
- `Allow` never overrides other decisions

### Permission Persistence

File: `crates/goose/src/config/permission.rs`

User permission decisions are persisted to `$CONFIG/permission.yaml`:
- "Always Allow" → saves to `always_allow` list
- "Never Allow" → saves to `never_allow` list

### Security Patterns

File: `crates/goose/src/security/patterns.rs`

40+ regex patterns for dangerous commands:
- `rm -rf /` variants
- `dd` disk destruction
- `curl | bash` remote execution
- `python -c exec` code injection
- Data exfiltration (curl/wget to external IPs)
- SSH key theft
- Environment variable manipulation
- Privilege escalation (sudo, chmod)

### Docker Containerization

File: `crates/goose/src/agents/container.rs`

When a `Container` is set on the agent, all stdio extensions are launched via `docker exec` in the specified container. This provides process-level isolation for extension execution.

---

## 10. Git Integration

Goose has **no built-in git tools**. Git operations are handled through the `shell` tool — the agent uses `git` commands directly.

The closest git-related feature is the `.goosehints` / `AGENTS.md` file loading system (in `crates/goose/src/hints/`), which reads project-specific instructions similar to how `.cursorrules` or `CLAUDE.md` work.

---

## 11. Unique Features

### Recipe System

Files: `crates/goose/src/recipe/`

Recipes are YAML files that define reusable agent workflows:

```yaml
version: "1.0.0"
title: "Code Review"
description: "Review code changes"
instructions: "Review the diff and provide feedback"
prompt: "Please review my latest changes"
extensions:
  - type: builtin
    name: developer
settings:
  goose_provider: openai
  goose_model: gpt-4o
parameters:
  - name: branch
    type: string
    required: true
response:
  json_schema: { ... }
retry:
  max_retries: 3
  checks:
    - type: shell
      command: "npm test"
sub_recipes:
  - name: lint_check
    instructions: "Run linting"
```

Key recipe features:
- **Structured output**: `FinalOutputTool` forces the agent to return JSON matching a schema
- **Retry logic**: Automated retry with shell-based success checks
- **Sub-recipes**: Composable nested recipes
- **Template parameters**: `{{param}}` substitution
- **Scheduling**: Cron-based recipe execution via `platform__manage_schedule` tool
- **Slash commands**: Recipes can be invoked as `/command-name` from chat

### Subagent Delegation

Files: `crates/goose/src/agents/subagent_handler.rs`, `summon.rs`

The `delegate` tool spawns fully isolated subagent sessions:
- Own provider + model (configurable, can be different from parent)
- Own set of extensions
- Own conversation history
- Can run synchronously (blocking) or asynchronously (returns task ID)
- Async tasks can be polled for status
- Results streamed back via MCP notifications

### Lead-Worker Provider

File: `crates/goose/src/providers/lead_worker.rs`

A unique provider pattern that automatically manages model switching:
- Uses a stronger "lead" model for the first N turns (planning, understanding)
- Switches to a cheaper "worker" model for execution
- Falls back to lead model on consecutive worker failures
- Emits `ModelChange` events so the UI can show which model is active

### Tool Shim for Non-Tool-Calling Models

File: `crates/goose/src/providers/toolshim.rs`

Enables tool use with models that don't natively support function calling (like vanilla Ollama models). Uses a secondary model to interpret text output and extract tool call intentions.

### Declarative Custom Providers

File: `crates/goose/src/providers/declarative/`

Users can define new providers via YAML config without writing Rust code:

```yaml
providers:
  my-provider:
    format: openai
    base_url: https://my-api.com/v1
    env_key: MY_API_KEY
    default_model: my-model
```

Providers catalog with 100+ pre-defined provider metadata entries enables auto-detection.

### Canonical Model Registry

File: `crates/goose/src/providers/canonical/`

A bundled JSON database mapping model names across providers to canonical IDs, with:
- Context window limits
- Input/output modalities
- Tool calling support
- Release dates
- Per-token pricing

Used for `fetch_recommended_models()` — filters models to only those with text input + tool calling support, sorted by release date.

### MOIM (Message Of Immediate Mind)

File: `crates/goose/src/agents/moim.rs`

A novel context injection system where extensions can provide "top of mind" information that gets injected as an ephemeral user message before each LLM call. This is not persisted to conversation history — it's purely transient context. The TOM extension uses this for user-configurable context injection via environment variables.

### Goose Apps

File: `crates/goose/src/agents/platform_extensions/apps.rs`

The agent can create HTML/CSS/JS "apps" that run in sandboxed windows. This enables creating dashboards, interactive tools, and utilities through conversation.

### Frontend Tools

The `Frontend` extension type allows the desktop UI to register tools that are executed by the frontend (not the backend). The agent calls them like any other tool, but execution is delegated to the UI process via a channel.

### Platform Extensions as In-Process MCP Clients

Rather than running every extension as a subprocess, Goose's "platform extensions" implement the `McpClientTrait` directly in Rust. They run in the same process as the agent, giving them direct access to the session manager, extension manager, and other agent internals — while still using the same MCP-like tool interface.

### OpenTelemetry Integration

File: `crates/goose/src/otel/`

Full OpenTelemetry support with tracing, metrics, and log export. Provider calls are instrumented with span attributes for model name, token counts, and tool usage.

### Security Classification Service

File: `crates/goose/src/security/classification_client.rs`

Beyond regex patterns, Goose optionally integrates with an external ML classification service for prompt injection detection. Two classifiers:
- **Prompt classifier**: Analyzes conversation context for injection attempts
- **Command classifier**: Analyzes shell commands for malicious intent

Both are opt-in via config flags (`SECURITY_PROMPT_CLASSIFIER_ENABLED`, `SECURITY_COMMAND_CLASSIFIER_ENABLED`).

---

## Summary: Architecture Comparison Points

| Aspect | Goose Approach |
|--------|---------------|
| **Language** | Pure Rust (no TypeScript backend) |
| **Extension model** | MCP-native — every extension is an MCP server |
| **Core tools** | Minimal (4): shell, write, edit, tree. No read_file — uses shell+cat |
| **Tool calling** | Via providers natively + ToolShim for non-supporting models |
| **Context management** | LLM-based summarization at 80% threshold |
| **Session storage** | SQLite with sqlx |
| **Provider count** | 20+ providers with canonical model registry |
| **Safety** | 3-layer inspection (security patterns, permissions, repetition) |
| **Subagents** | Full isolation with own provider/extensions/conversation |
| **Unique features** | Lead-Worker provider, recipes, MOIM, apps, declarative providers |
| **Git** | No built-in tools — uses shell |
| **Docker** | Container mode routes stdio extensions through docker exec |
