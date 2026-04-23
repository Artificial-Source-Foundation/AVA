# C++ Milestone 7 Boundaries

This note records what is implemented for the C++ `ava_agent` runtime-core slice and what remains intentionally deferred.

## Implemented in Milestone 7

1. New `ava_agent` static library wired into `ava_runtime`.
2. Real bounded runtime-core surface under `cpp/include/ava/agent` and `cpp/src/agent`:
   - agent config and completion/event DTOs
   - deterministic event-sink callback seam for headless/TUI integration
3. Minimal message queue adapted from Rust `message_queue.rs`:
   - steering / follow-up / post-complete tier routing
   - drain helpers and post-complete group sequencing
   - current blocking runtime consumption is wired for steering-tier injection only; broader follow-up/post-complete execution policy remains deferred
4. Practical baseline stuck detector:
   - repeated empty-response stop
   - repeated identical-response stop
   - repeated single tool-call signature nudge-then-stop flow
5. Response helpers inspired by `agent_loop/response.rs`:
   - streamed tool-call accumulation/finalization helpers
   - tool-call envelope parsing from model text
   - coalescing helper for provider-native tool calls plus fallback text envelopes
6. Blocking core agent loop:
   - simple system prompt assembly from config + tool inventory
   - provider turn execution through `ava_llm::Provider`
   - tool execution through `ava_tools::ToolRegistry`
   - session transcript mutation using existing `ava_types::SessionRecord`/`SessionMessage`
   - completion on assistant response, max-turn limit, provider/runtime error, or stuck condition
7. Focused Catch2 coverage for queue behavior, response helpers, tool-call loop execution, and stuck stop behavior.

## Explicitly Deferred

1. `ava-agent-orchestration` / subagent runtime ownership and any multi-agent composition.
2. MCP/plugin manager integration in the loop.
3. Async/streaming-first agent loop execution; Milestone 7 is intentionally blocking.
4. Full Rust stuck-detector layering (cost accounting, alternating pattern families, model-loop-prone tuning matrix, LLM judge, monitor integration).
5. Full Rust prompt stack parity (large provider/family prompt tuning tables, prompt rule injection, cache boundaries, reflection/steering layers).
6. Runtime compaction, prompt caching, context-repair/recovery, post-edit validation, and advanced continuation/reflection logic.
7. Full control-plane command lifecycle parity and interactive approval/question/plan ownership in the runtime core.

Milestone 7 is intentionally honest and scoped: it lands a real C++ `ava_agent` runtime-core baseline without claiming orchestration or full Rust runtime parity.
