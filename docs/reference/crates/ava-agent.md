# ava-agent

> Core agent execution loop with tool calling, stuck detection, and streaming event emission.

## Overview

`ava-agent` is the central orchestration crate for AVA's AI agent runtime. It owns the loop that sends messages to LLM providers, executes tool calls returned by the model, detects stuck/looping behavior, manages context compaction, and emits streaming events for UI consumption. It also provides the `AgentStack` -- a high-level facade that composes every subsystem (LLM routing, sessions, memory, tools, MCP, codebase indexing) into a single entry point for running agent tasks.

The crate does **not** own any LLM provider implementations or tool implementations directly; it depends on `ava-llm` for providers and `ava-tools` for the tool registry and built-in tools.

## Architecture

```
                      AgentStack (stack.rs)
                     /    |    |    \     \
              ModelRouter  Tools  Session  Memory  CodebaseIndex
                   |         |
              LLMProvider  ToolRegistry
                   |         |
               AgentLoop (agent_loop/mod.rs)
              /    |     |        \
     SystemPrompt  Context  StuckDetector  Reflection
                   |
             ContextManager (ava-context)
```

**AgentStack** is the top-level entry point used by the TUI and headless CLI. It wires together all subsystems and delegates actual agent execution to **AgentLoop**.

**AgentLoop** is the inner loop that runs turns: call the LLM, parse tool calls, execute tools, check for stuck states, compact context when needed, and repeat until the model signals completion or a limit is hit.

**StuckDetector** monitors the conversation for degenerate patterns (empty responses, identical responses, tool call loops, error loops, cost overruns) and recommends corrective actions.

## Key Types

### AgentLoop
`crates/ava-agent/src/agent_loop/mod.rs:28` -- The core execution loop.

```rust
pub struct AgentLoop {
    pub llm: Box<dyn LLMProvider>,
    pub tools: ToolRegistry,
    pub context: ContextManager,
    pub config: AgentConfig,
    pub(crate) last_request_hash: Option<u64>,
    pub(crate) last_request_time: Option<Instant>,
    history: Vec<Message>,
}
```

Holds a boxed LLM provider, the tool registry, a context manager for conversation history, configuration, and dedup state. The `history` field allows injecting prior conversation turns (set via `with_history()`).

### AgentConfig
`crates/ava-agent/src/agent_loop/mod.rs:40` -- Per-run configuration.

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `max_turns` | `usize` | -- | Maximum turns (0 = unlimited) |
| `max_budget_usd` | `f64` | `0.0` | CLI-level cost cap (0 = unlimited) |
| `token_limit` | `usize` | -- | Context window size in tokens |
| `model` | `String` | -- | Model identifier for cost estimation |
| `max_cost_usd` | `f64` | `1.0` | Stuck detector cost threshold |
| `loop_detection` | `bool` | `true` | Enable/disable stuck detection |
| `custom_system_prompt` | `Option<String>` | `None` | Replaces default system prompt when set |
| `thinking_level` | `ThinkingLevel` | `Off` | Extended thinking mode (Off/Low/Medium/High) |
| `system_prompt_suffix` | `Option<String>` | `None` | Appended to system prompt (mode/project instructions) |

### AgentEvent
`crates/ava-agent/src/agent_loop/mod.rs:73` -- Events emitted during streaming execution.

| Variant | Payload | Purpose |
|---------|---------|---------|
| `Token(String)` | Text fragment | Streamed text tokens for live display |
| `Thinking(String)` | Reasoning text | Extended thinking content (displayed separately) |
| `ToolCall(ToolCall)` | Tool invocation | Emitted before tool execution |
| `ToolResult(ToolResult)` | Tool output | Emitted after tool execution |
| `Progress(String)` | Status message | Turn numbers, context compaction, limit notifications |
| `Complete(Session)` | Final session | Agent finished successfully |
| `Error(String)` | Error message | Fatal error; stream ends |
| `ToolStats(ToolStats)` | Aggregate stats | Tool execution statistics (emitted at completion) |
| `TokenUsage { input_tokens, output_tokens, cost_usd }` | Usage data | Per-turn token usage and estimated cost |
| `SubAgentComplete { call_id, session_id, messages, description, input_tokens, output_tokens, cost_usd }` | Sub-agent data | A spawned sub-agent finished; includes its full conversation |

### AgentStack
`crates/ava-agent/src/stack.rs:71` -- High-level facade composing all subsystems.

```rust
pub struct AgentStack {
    pub router: ModelRouter,
    pub tools: Arc<RwLock<ToolRegistry>>,
    pub session_manager: Arc<SessionManager>,
    pub memory: Arc<MemorySystem>,
    pub config: ConfigManager,
    pub platform: Arc<StandardPlatform>,
    pub codebase_index: Arc<RwLock<Option<Arc<CodebaseIndex>>>>,
    // ... runtime overrides, MCP, thinking, todos, question bridge, agents config
    pub parent_session_id: RwLock<Option<String>>, // Set by TUI for sub-agent linking
}
```

`AgentStack` is `Send` (verified by a compile-time assertion at `stack.rs:782`). It uses `RwLock` for mutable state (provider/model overrides, thinking level, mode prompt) to allow safe concurrent access.

### AgentStackConfig
`crates/ava-agent/src/stack.rs:101` -- Construction parameters for `AgentStack`.

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `data_dir` | `PathBuf` | `~/.ava` | Root directory for databases and config |
| `provider` | `Option<String>` | `None` | CLI provider override |
| `model` | `Option<String>` | `None` | CLI model override |
| `max_turns` | `usize` | `0` | Turn limit (0 = unlimited) |
| `max_budget_usd` | `f64` | `0.0` | Budget cap (0 = unlimited) |
| `yolo` | `bool` | `false` | Skip permission checks |
| `injected_provider` | `Option<Arc<dyn LLMProvider>>` | `None` | Testing: bypass ModelRouter |

### AgentRunResult
`crates/ava-agent/src/stack.rs:111` -- Return value from `AgentStack::run()`.

```rust
pub struct AgentRunResult {
    pub success: bool,
    pub turns: usize,
    pub session: Session,
}
```

### AgentTaskSpawner
`crates/ava-agent/src/stack.rs:567` -- Implements the `TaskSpawner` trait for sub-agent creation.

When the agent calls the `task` tool, the spawner creates a child `AgentLoop` with its own tool registry (core tools only, no todos), a custom system prompt, and capped turn limits. Sub-agent sessions are persisted with `is_sub_agent` and `parent_id` metadata, and a `SubAgentComplete` event is emitted for TUI display.

### StuckDetector
`crates/ava-agent/src/stuck.rs:15` -- Tracks stuck/loop detection state across turns.

### StuckAction
`crates/ava-agent/src/stuck.rs:8` -- Recommendation from the detector.

```rust
pub enum StuckAction {
    Continue,              // Normal; keep going
    InjectMessage(String), // Nudge the model with a corrective message
    Stop(String),          // Terminate the agent loop
}
```

### LLMProvider (re-export)
`crates/ava-agent/src/llm_trait.rs:1` -- Re-exports `ava_llm::provider::{LLMProvider, LLMResponse}`.

### ReflectionLoop
`crates/ava-agent/src/reflection.rs:38` -- Coordinates error classification, fix generation, and retry execution for tool failures.

## Flows

### Agent Loop (`run()`)
`crates/ava-agent/src/agent_loop/mod.rs:175`

Step-by-step execution of a non-streaming agent run:

1. **Create session and detector** -- Fresh `Session` and `StuckDetector` instances.
2. **Inject system prompt** (`inject_system_prompt()`, line 159) -- If `custom_system_prompt` is set, use it; otherwise call `build_system_prompt()` with tool definitions. Appends `system_prompt_suffix` if present. Added as a `System` message to the context.
3. **Inject conversation history** -- Any messages set via `with_history()` are added to context and session (enables multi-turn conversations).
4. **Add goal message** -- The user's goal is added as a `User` message.
5. **Enter turn loop**:
   - **Check turn limit** -- If `max_turns > 0` and limit reached, force a summary via `force_summary()` and break.
   - **Check budget limit** -- If `max_budget_usd > 0` and estimated cost exceeds it, force summary and break.
   - **Generate LLM response** -- Call `generate_response_with_thinking()` which delegates to either `generate_with_thinking()` (if thinking enabled) or `generate_response()`. Both include a dedup guard (skip if same content hash within 2 seconds).
   - **Merge token usage** -- Accumulate `TokenUsage` from the response.
   - **Execute tool calls** -- Call `execute_tool_calls_tracked()` which separates read-only tools (executed concurrently via `join_all`) from write tools (executed sequentially). Results are recorded in the `StuckDetector`'s `ToolMonitor`.
   - **Stuck detection** -- Call `detector.check()` with response, tool calls, and results. On `InjectMessage`, add a nudge to context and continue. On `Stop`, add a system message and break. On `Continue`, proceed.
   - **Skip empty responses** -- If both text and tool calls are empty, skip adding to context.
   - **Add assistant message** -- With tool calls attached.
   - **Natural completion** -- If the model responded with text but no tool calls, the task is complete; return the session.
   - **Add tool results** -- Each tool result added as a `Tool` message with the corresponding `tool_call_id`.
   - **Self-correction hint** -- If any tool result is an error, inject a `User` message: "Tool call failed: {first_line}. Try a different approach."
   - **Context compaction** -- If `context.should_compact()`, run `compact_async()`.
   - **Completion tool** -- If any tool call is `attempt_completion`, return the session.

### Streaming Agent Loop (`run_streaming()`)
`crates/ava-agent/src/agent_loop/mod.rs:293`

Same flow as `run()` but uses `async_stream::stream!` to yield `AgentEvent` variants as they occur:

- LLM streaming: yields `Token` and `Thinking` events as chunks arrive.
- Tool call fragments are accumulated via `ToolCallAccumulator` and finalized into complete `ToolCall` objects.
- Token usage is computed from accumulated stream usage and emitted as `TokenUsage` events.
- `ToolCall` events are emitted before execution; `ToolResult` events after.
- `Progress` events at each turn and on stuck detection.
- `ToolStats` and `Complete` emitted at the end.
- Empty responses emit an `Error` event and terminate the stream.

### AgentStack::run()
`crates/ava-agent/src/stack.rs:385`

The high-level entry point used by the TUI:

1. **Resolve provider** -- Try primary provider via `ModelRouter`; fall back to `FallbackConfig` if primary is unavailable. Emits a `Progress` event on fallback.
2. **Determine turn limit** -- Explicit arg overrides stored config; both can be 0 (unlimited).
3. **Build system prompt suffix** -- Concatenates mode-specific suffix (from agent modes) and project instructions (from `load_project_instructions_with_config()`).
4. **Enrich goal with memories** -- Extracts keywords from goal (filtering stopwords), searches `MemorySystem`, appends top 5 memories (capped at 2000 chars).
5. **Set up context** -- Creates `ContextManager` with a hybrid condenser (3-stage: priority, relevance, summarization) using codebase PageRank scores for relevance weighting.
6. **Build tool registry** -- Core tools + todo tools + question tool + custom TOML tools + MCP bridge tools + task tool (with `AgentTaskSpawner`).
7. **Create and run AgentLoop** -- If `event_tx` is provided, uses streaming mode with cancellation support (`tokio::select!`). Otherwise uses synchronous `run()`.
8. **Return `AgentRunResult`** -- With success flag, turn count, and final session.

### Tool Execution
`crates/ava-agent/src/agent_loop/tool_execution.rs`

- **Read-only tools** (line 14): `read`, `glob`, `grep`, `hover`, `references`, `definition`, `web_fetch`, `todo_read` -- executed concurrently.
- **Write tools**: everything else -- executed sequentially.
- **Truncation** (line 25): Results exceeding 50,000 bytes are truncated with a note.
- **Contextual instructions** (line 117): When the `read` tool succeeds, the system walks from the file's directory up to the project root looking for `AGENTS.md` files and appends their content to the tool result.

### Response Parsing
`crates/ava-agent/src/agent_loop/response.rs`

Two paths for extracting tool calls from LLM responses:

1. **Native tool calling** (providers that support it): Stream chunks carry `StreamToolCall` fragments. `accumulate_tool_call()` (line 30) collects fragments by index, building up id, name, and arguments JSON. `finalize_tool_calls()` (line 53) parses the accumulated JSON and assigns UUIDs for missing IDs.

2. **Text-based fallback** (non-native providers): `parse_tool_calls()` (line 72) attempts to parse the entire response as JSON, looking for `tool_calls` (array) or `tool_call` (single) keys in the expected envelope format.

The dedup guard in `generate_response()` (line 104) and `generate_response_with_thinking()` (line 156) hashes the last message's content and skips the LLM call if the same hash appears within 2 seconds, preventing rapid duplicate requests.

## Stuck Detection
`crates/ava-agent/src/stuck.rs`

The `StuckDetector` tracks 8 scenarios, checked in order at `check()` (line 66):

| # | Scenario | Threshold | Action |
|---|----------|-----------|--------|
| 1 | Empty response | 2 consecutive | `Stop` |
| 2 | Identical response | 3 consecutive | `Stop` |
| 3 | Tool call loop | Same tool+args 3 times | `InjectMessage` |
| 4 | Error loop | 3 consecutive all-error turns | `InjectMessage` |
| 5 | Cost threshold | `estimated_cost > max_cost_usd` | `Stop` |
| 6 | Alternating tool pattern | Detected by `ToolMonitor` | `InjectMessage` |
| 7 | High error rate | >50% of last 10 calls | `InjectMessage` |
| 8 | Stalled progress | 5 turns with no tools or completion | `InjectMessage` |

All detection is disabled when `config.loop_detection == false`.

Cost estimation uses the LLM provider's `estimate_tokens()` and `estimate_cost()` methods (line 81-82). The detector also owns a `ToolMonitor` instance that records every tool execution for pattern analysis.

## Instructions System
`crates/ava-agent/src/instructions.rs`

### Project Instructions (System Prompt)

`load_project_instructions()` / `load_project_instructions_with_config()` discovers and loads instruction files in this order:

1. **Global**: `~/.ava/AGENTS.md`
2. **Ancestor directories**: Walk from project root up to the nearest `.git` boundary, loading `AGENTS.md` and `CLAUDE.md` at each level (outermost first).
3. **Project root files**: `AGENTS.md`, `CLAUDE.md`, `.cursorrules`, `.github/copilot-instructions.md`
4. **Project .ava dir**: `.ava/AGENTS.md`
5. **Rule files**: `.ava/rules/*.md` -- sorted alphabetically, with optional YAML frontmatter for path-scoped rules
6. **Extra paths**: User-configured paths/globs from `config.yaml` `instructions:` field

All files are deduplicated by canonical path and prefixed with `# From: <path>`. Empty/whitespace-only files are skipped.

### Scoped Rules (Frontmatter)

Rule files in `.ava/rules/` can have YAML frontmatter with `paths:` globs:

```markdown
---
paths:
  - "**/*.py"
  - "scripts/**"
---
Always use type hints in Python files.
```

If `paths:` is present, the rule is only included when at least one matching file exists in the project. Rules without frontmatter always load.

### Contextual Instructions (Tool Results)

`contextual_instructions_for_file()` (line 253) is called when the `read` tool executes. It walks from the read file's directory up to the project root, returning the first (most specific) `AGENTS.md` found. This content is appended to the tool result, so the model sees directory-specific instructions in context.

## System Prompt
`crates/ava-agent/src/system_prompt.rs`

`build_system_prompt()` generates the default system prompt:

- **Identity**: "You are AVA, an AI coding assistant."
- **Rules**: Read before modify, prefer native tools, run tests, call `attempt_completion` when done.
- **Tool listing** (native mode): Brief name + description for each tool.
- **Tool listing** (text mode): Full JSON schemas with the envelope format (`{"tool_calls": [...]}`).
- **attempt_completion**: Always mentioned as a virtual tool if not already in the registry.

## Reflection System
`crates/ava-agent/src/reflection.rs`

The `ReflectionLoop` provides automated error classification and fix retry:

- **Error classification** (`analyze_error()`, line 76): Pattern-matches error text against known categories:
  - `Syntax`: "syntaxerror", "unexpected token"
  - `Import`: "cannot find module", "unresolved import"
  - `Type`: "typeerror", "mismatched types"
  - `Command`: "command not found"
- **Fix generation**: Delegates to a `ReflectionAgent` trait implementor.
- **Retry**: Executes the generated fix via a `ToolExecutor` trait implementor.

This system is composable -- the traits allow different fix strategies and execution backends.

## Token Tracking

Token usage flows through two paths:

1. **Non-streaming** (`run()`): `generate_response_with_thinking()` returns `Option<TokenUsage>`. Merged into `total_usage` via `merge_usage()` (line 134). Set on `session.token_usage` at return.

2. **Streaming** (`run_streaming()`): Usage arrives in `StreamChunk.usage` fields (may be split across `message_start` and `message_delta` events for Anthropic). Accumulated into a single `TokenUsage`, then emitted as `AgentEvent::TokenUsage` with cost computed via `model_pricing_usd_per_million()` and `estimate_cost_with_cache_usd()`.

For sub-agents, token usage is extracted from the sub-agent's `session.token_usage` and included in the `SubAgentComplete` event.

## Configuration

### AgentConfig (per-run)
Set programmatically when constructing `AgentLoop`. See the Key Types section above.

### agents.toml (sub-agent configuration)
`AgentsConfig` is loaded from `~/.ava/agents.toml` and `.ava/agents.toml`. Controls sub-agent behavior:

```toml
[task]
enabled = true
max_turns = 10
prompt = "Custom system prompt for sub-agents..."
# model = "provider/model"  # planned but not yet wired
```

### config.yaml (project configuration)
Via `ConfigManager`, controls:
- `llm.provider` / `llm.model` -- default provider and model
- `fallback.provider` / `fallback.model` -- automatic failover
- `instructions` -- extra instruction file paths/globs

### Project state (.ava/state.json)
Persists last-used provider/model per project. Written by `switch_model()` (line 310).

## Dependencies

### Depends on
| Crate | Purpose |
|-------|---------|
| `ava-llm` | LLM provider trait and implementations, model pricing, connection pool |
| `ava-tools` | Tool trait, registry, core tools, MCP bridge, monitor |
| `ava-context` | Context window management, hybrid condensation |
| `ava-types` | Shared types (Message, Session, ToolCall, TokenUsage, ThinkingLevel) |
| `ava-config` | Configuration management, credentials, agents.toml |
| `ava-session` | Session persistence (SQLite) |
| `ava-memory` | Persistent key-value memory |
| `ava-codebase` | Code indexing (BM25 + PageRank) |
| `ava-platform` | File system and shell abstractions |
| `ava-mcp` | MCP server configuration and management |

### Depended on by
| Crate | Purpose |
|-------|---------|
| `ava-commander` | Multi-agent orchestration (creates `AgentLoop` instances for workers) |
| `ava-tui` | TUI binary (creates and drives `AgentStack`) |

## Examples

### Creating and running an AgentLoop directly

```rust
use ava_agent::{AgentLoop, AgentConfig};
use ava_context::ContextManager;
use ava_tools::registry::ToolRegistry;

let config = AgentConfig {
    max_turns: 10,
    max_budget_usd: 0.0,
    token_limit: 128_000,
    model: "claude-sonnet-4".to_string(),
    max_cost_usd: 5.0,
    loop_detection: true,
    custom_system_prompt: None,
    thinking_level: ThinkingLevel::Off,
    system_prompt_suffix: None,
};

let mut agent = AgentLoop::new(
    Box::new(provider),
    registry,
    ContextManager::new(128_000),
    config,
).with_history(prior_messages);

// Non-streaming
let session = agent.run("Fix the bug in main.rs").await?;

// Streaming
let mut stream = agent.run_streaming("Fix the bug in main.rs").await;
while let Some(event) = stream.next().await {
    match event {
        AgentEvent::Token(t) => print!("{t}"),
        AgentEvent::Complete(session) => break,
        _ => {}
    }
}
```

### Using AgentStack (typical TUI usage)

```rust
use ava_agent::stack::{AgentStack, AgentStackConfig};

let (stack, question_rx) = AgentStack::new(AgentStackConfig {
    data_dir: PathBuf::from("/home/user/.ava"),
    provider: Some("openrouter".to_string()),
    model: Some("anthropic/claude-sonnet-4".to_string()),
    max_turns: 0,
    ..Default::default()
}).await?;

let result = stack.run(
    "Refactor the error handling",
    0,              // max_turns (0 = use config)
    Some(event_tx), // streaming events for TUI
    cancel_token,
    vec![],         // conversation history
).await?;
```

### Stuck detection standalone

```rust
use ava_agent::stuck::{StuckDetector, StuckAction};

let mut detector = StuckDetector::new();
match detector.check(&response, &tool_calls, &results, &config, llm.as_ref()) {
    StuckAction::Continue => { /* proceed */ }
    StuckAction::InjectMessage(msg) => { /* add nudge to context */ }
    StuckAction::Stop(reason) => { /* terminate loop */ }
}
```
