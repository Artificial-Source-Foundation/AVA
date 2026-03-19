# ava-agent

> Core agent execution loop with tool calling and stuck detection.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `AgentLoop` | Core agent execution loop orchestrating LLM calls, tool execution, and stuck detection |
| `AgentConfig` | Configuration for agent runs (turn limits, cost caps, model identity) |
| `AgentEvent` | Events emitted during streaming execution (Token, ToolCall, ToolResult, Complete, etc.) |
| `AgentStack` | High-level stack managing router, tools, sessions, memory, and configuration |
| `AgentStackConfig` | Builder configuration for AgentStack (data_dir, provider, model, limits) |
| `AgentRunResult` | Result of an agent run (success, turns, session) |
| `StuckDetector` | Detects stuck/loop states and recommends actions |
| `StuckAction` | Action to take when stuck (Continue, InjectMessage, Stop) |
| `MessageQueue` | Three-tier message queue for mid-stream user messaging (steering, follow-up, post-complete) |
| `ReflectionAgent` | Trait for generating fixes from failed tool runs |
| `ReflectionLoop` | Coordinates error classification, fix generation, and retry execution |
| `TaskRoutingIntent` | Routing decision with profile, requirements, and reasons |
| `analyze_task()` | Analyzes task goal to determine routing intent |
| `build_system_prompt()` | Builds system prompt with tool definitions |
| `load_project_instructions()` | Loads AGENTS.md, CLAUDE.md, and skill files |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Exports AgentLoop, instructions, reflection, routing, stuck detection |
| `agent_loop/mod.rs` | AgentLoop struct, AgentConfig, AgentEvent, run_unified execution engine |
| `agent_loop/tool_execution.rs` | Tool execution with timing, plan mode checks, post-edit validation |
| `agent_loop/response.rs` | Tool call parsing from LLM responses |
| `agent_loop/repetition.rs` | Repetition detection for tool calls |
| `stack/mod.rs` | AgentStack core: routing, MCP, plugins, permissions, sub-agent spawning |
| `stack/stack_config.rs` | AgentStackConfig builder (data_dir, provider, model, limits) |
| `stack/stack_tools.rs` | Tool registration and middleware wiring |
| `stack/stack_run.rs` | Agent run loop, session management, history handling |
| `session_logger.rs` | JSONL session logger — writes structured log entries to `~/.ava/log/` (opt-in) |
| `stuck.rs` | StuckDetector with 8 detection scenarios (empty responses, loops, cost thresholds) |
| `reflection.rs` | Error classification, ReflectionAgent/ToolExecutor traits, ReflectionLoop |
| `routing.rs` | Task analysis and routing intent based on keywords and task characteristics |
| `message_queue.rs` | Three-tier queue for steering, follow-up, and post-complete messages |
| `instructions.rs` | Instruction file discovery (AGENTS.md, CLAUDE.md, skills) with trust gating |
| `instruction_resolver.rs` | Builds system prompt suffix from mode and project instructions |
| `system_prompt.rs` | Constructs system prompt with tool definitions and JSON envelope format |
| `memory_enrichment.rs` | Memory-based goal enrichment and pattern learning |
| `budget.rs` | Budget telemetry and tracking |
| `llm_trait.rs` | Re-export of LLMProvider trait from ava-llm |
| `trace.rs` | Execution tracing utilities |
| `trajectory.rs` | Conversation trajectory tracking |
| `turn_diff.rs` | Turn-by-turn diff tracking |

## Dependencies

Uses: ava-codebase, ava-context, ava-config, ava-llm, ava-memory, ava-permissions, ava-platform, ava-session, ava-mcp, ava-plugin, ava-tools, ava-types

Used by: ava-tui, src-tauri, ava-praxis

## Key Patterns

- **Error handling**: Uses `ava_types::Result` with `AvaError` for structured errors
- **Streaming/headless dual mode**: `run_unified()` supports both streaming (with event_tx) and headless execution
- **Plugin hooks**: Fires HookEvent::AgentBefore/AgentAfter, ToolBefore/ToolAfter, SessionStart/SessionEnd
- **Read-only concurrency**: Read tools execute concurrently; write tools run sequentially with steering checks between each
- **Stuck detection**: 8 scenarios (empty responses, identical responses, tool loops, error loops, cost threshold, alternating patterns, high error rate, stalled progress)
- **Plan mode**: Restricts write tools to `.ava/plans/*.md` paths only
- **Trust gating**: Project-local instructions and MCP tools require `--trust` flag
- **Tool schema pre-validation**: Validates tool call parameters against JSON schema before execution; surfaces actionable errors for malformed calls
- **Stream silence timeout**: Configurable per-chunk timeout (default 90s) via `AgentConfig.stream_timeout_secs`; cancels hung LLM streams
- **Prompt caching**: `AgentConfig.prompt_caching` flag (default true); Anthropic provider injects `cache_control` on system prompt and tool definitions for ~25% cost savings on cache hits
- **tiktoken BPE token counting**: `count_tokens()` in `instructions.rs` uses cl100k_base tokenizer for accurate token counts (replaces character-based heuristic)
- **Auto-retry for read-only tools**: Middleware retries transient failures on read-only tools (2x with exponential backoff)
- **`--verbose` / `-v` CLI flag**: `-v` info, `-vv` debug, `-vvv` trace to stderr; overrides `RUST_LOG` for debugging
- **JSONL session logging**: Opt-in via `features.session_logging: true`; writes structured entries (event type, timestamp, data) to `~/.ava/log/{session_id}.jsonl`
- **15 edit strategies**: ExactMatch, LineTrimmed, AutoBlockAnchor, Ellipsis (`...` placeholder handling), FlexibleMatch, RelativeIndent, BlockAnchor, RegexMatch, FuzzyMatch, LineNumber, TokenBoundary, IndentationAware, MultiOccurrence, ThreeWayMerge, DiffMatchPatch — with rich error feedback on failure (similar lines + "did you mean?" hints)
