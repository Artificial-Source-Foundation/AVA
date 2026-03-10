# Rust Hot-Path Performance Audit

> **Audit date:** 2026-03-08
> **Scope:** `crates/ava-agent/`, `crates/ava-llm/`, `crates/ava-tools/`, `crates/ava-tui/`, `crates/ava-commander/`
> **Focus:** Clone abuse, allocation patterns, async overhead, lock contention

---

## Summary

| Category | Findings | HIGH | MEDIUM | LOW |
|----------|----------|------|--------|-----|
| Clone abuse | 18 | 4 | 7 | 7 |
| Allocation patterns | 6 | 2 | 2 | 2 |
| Blocking async | 3 | 2 | 1 | 0 |
| Lock contention | 4 | 0 | 2 | 2 |
| **Total** | **31** | **8** | **12** | **11** |

### Critical hot paths identified

1. **Agent streaming loop** (`ava-agent/src/agent_loop/mod.rs`) — 40+ clones per turn, messages cloned 2-3×, Session cloned on completion
2. **TUI render cycle** (`ava-tui/src/ui/`) — `content.clone()` and `sep.clone()` on every frame (~60 FPS)
3. **TUI event handling** (`ava-tui/src/app/`) — 30+ `block_in_place` calls blocking the UI thread
4. **Tool execution** (`ava-tools/src/registry.rs`) — `arguments.clone()` on every tool call

---

## Hot Path Clones

### HIGH Priority

#### 1. Agent loop: Messages cloned 2-3× per turn

**File:** `crates/ava-agent/src/agent_loop/mod.rs`
**Lines:** ~285-320 (streaming loop)

The `run_streaming()` method clones messages multiple times per turn:

- `assistant_message.clone()` pushed to context
- Same message cloned again for session persistence
- `tool_calls.clone()` passed to execution
- `response_text.clone()` for event emission

**Impact:** For a 50-turn conversation with 128K context, this means ~150 unnecessary String/Vec clones of growing message lists.

**Fix:** Use `Arc<Message>` for shared ownership, or restructure to move messages into their final destination. For event emission, use `&str` references or `Arc<str>`.

#### 2. Agent loop: Session.clone() on completion

**File:** `crates/ava-agent/src/agent_loop/mod.rs`
**Lines:** ~350-360

```rust
AgentEvent::Complete(session.clone())
```

Clones the ENTIRE conversation history (all messages, all tool results) just to emit a completion event.

**Impact:** For a long session with 50+ messages, this is a multi-KB allocation at completion time. Combined with the TUI receiving this event, it means the full session exists in 3 places simultaneously.

**Fix:** Use `Arc<Session>` or emit `AgentEvent::Complete(Arc::new(session))` and share the reference.

#### 3. Tool registry: arguments.clone() on every execution

**File:** `crates/ava-tools/src/registry.rs`
**Line:** 140

```rust
tool.execute(tool_call.arguments.clone())
```

Every tool execution clones the entire JSON `Value` (which can be large for file writes, multi-edits, etc.). The `Tool::execute` trait takes `Value` by ownership.

**Impact:** A `write_file` call with a 10KB file content clones that 10KB JSON blob unnecessarily.

**Fix:** Change `Tool::execute` signature to take `&Value` or `Cow<Value>`. Most tools only need to deserialize the arguments, which can work from a reference.

#### 4. Tool execution: result tuple clones

**File:** `crates/ava-agent/src/agent_loop/tool_execution.rs`
**Line:** 75

```rust
read_results[idx_pos].clone()
```

Clones `(ToolResult, ToolExecution)` tuples when collecting results. `ToolResult` contains the full output String.

**Fix:** Use `into_iter()` with indexed swap-remove, or restructure to avoid the intermediate Vec.

### MEDIUM Priority

#### 5. LLM router: CredentialStore clone on cache miss

**File:** `crates/ava-llm/src/router.rs`
**Line:** ~75

```rust
self.credentials.read().await.clone()
```

Clones the entire CredentialStore (contains API keys for all providers) every time the provider cache misses.

**Fix:** Only extract the specific credential needed, or cache credentials per-provider.

#### 6. Tool execution: tool_call.clone() passed to execute

**File:** `crates/ava-agent/src/agent_loop/tool_execution.rs`
**Line:** ~101

The entire `ToolCall` struct (including JSON arguments) is cloned before passing to `self.tools.execute()`.

**Fix:** Pass `&ToolCall` to the registry, which only needs a reference for dispatch.

#### 7. Permission middleware: result.clone() on every tool call

**File:** `crates/ava-tools/src/permission_middleware.rs`
**Line:** 45

```rust
Ok(result.clone())
```

The `after()` hook clones `ToolResult` on every tool execution. The middleware signature forces this.

**Fix:** Change `after()` signature to return `Option<ToolResult>` (None = pass-through unchanged) or take owned `ToolResult`.

#### 8. Commander: message.clone() in session merging

**File:** `crates/ava-commander/src/lib.rs`
**Line:** 280

```rust
for message in &session.messages {
    combined.add_message(message.clone());
}
```

Clones every message when merging worker sessions. Worker sessions are consumed after this.

**Fix:** Use `drain()` or `into_iter()` since the source session isn't needed afterward.

#### 9. TUI: content.clone() in message rendering

**File:** `crates/ava-tui/src/state/messages.rs`

`self.content.clone()` called in `to_lines()` for User/ToolCall/System messages on every render frame.

**Fix:** Return `&str` or `Cow<str>` from rendering methods. Content doesn't need to be owned by the renderer.

#### 10. Event handler: call.clone() for approval request

**File:** `crates/ava-tui/src/app/event_handler.rs`
**Line:** 56

`call.clone()` when building `ApprovalRequest`. Only happens when approval is needed (not yolo mode).

**Fix:** Acceptable in non-yolo mode (infrequent), but could use `Arc<ToolCall>` if approval is common.

#### 11. Event handler: goal.clone() in submit_goal

**File:** `crates/ava-tui/src/app/event_handler.rs`
**Lines:** 132, 182

Goal string cloned to push into UI messages AND pass to agent. Could pass `&str` to message push.

### LOW Priority

#### 12. Status bar: sep.clone() 8× per render frame

**File:** `crates/ava-tui/src/ui/status_bar.rs`

`sep.clone()` called 8+ times per frame for a `Span` separator.

**Fix:** Use a function `fn sep() -> Span` or make it a const.

#### 13. UI mod: model selector clone in render

**File:** `crates/ava-tui/src/ui/mod.rs`

Model selector string cloned during render dispatch.

#### 14. Stuck detector: response.trim().to_string()

**File:** `crates/ava-agent/src/stuck.rs`
**Line:** 90

Allocates a new String every turn for comparison. Could hash instead.

#### 15. Stuck detector: call.arguments.to_string()

**File:** `crates/ava-agent/src/stuck.rs`
**Line:** 109

Serializes JSON Value to String for loop detection signature. Could use hash comparison.

#### 16. Agent loop: tool_call.name.clone() for activity display

**File:** `crates/ava-tui/src/app/event_handler.rs`
**Line:** 47

Small string clone for UI activity indicator. Negligible.

#### 17. Commander: worker field clones for events

**File:** `crates/ava-commander/src/lib.rs`
**Lines:** 218-219, 258

`worker.lead.clone()` and `worker.task.description.clone()` for event emission. Infrequent.

#### 18. Voice config: voice_config.clone() duplicated

**File:** `crates/ava-tui/src/app/event_handler.rs`
**Lines:** 260, 278

Both if/else branches in `stop_and_transcribe` do identical work including `self.voice_config.clone()`. Dead code duplication — the `self.transcriber.is_none()` branch and else branch are identical.

---

## Allocation Patterns

### HIGH Priority

#### 1. Agent loop: String::new() for streaming response without capacity

**File:** `crates/ava-agent/src/agent_loop/mod.rs`
**Line:** ~271

```rust
let mut full_response = String::new();
```

This accumulates the full LLM response via `push_str()` in the streaming loop. Typical responses are 1-4KB. Without `with_capacity()`, this causes multiple reallocations as the string grows (0 → 1 → 2 → 4 → 8 → ... bytes).

**Fix:** `String::with_capacity(4096)` — a reasonable default for LLM responses.

#### 2. Agent loop: 3× Vec::new() per turn without capacity

**File:** `crates/ava-agent/src/agent_loop/mod.rs`
**Lines:** ~299-302

```rust
let mut tool_calls = Vec::new();
let mut tool_results = Vec::new();
let mut tool_executions = Vec::new();
```

Created every turn. Typical tool call batches are 1-5 items.

**Fix:** `Vec::with_capacity(4)` for each.

### MEDIUM Priority

#### 3. Tool execution: 2× Vec::new() per execution batch

**File:** `crates/ava-agent/src/agent_loop/tool_execution.rs`
**Lines:** ~51-52

Two Vecs created per execution batch for results and read-results, without capacity hints.

**Fix:** `Vec::with_capacity(tool_calls.len())`.

#### 4. Message mapping: system_parts allocation

**File:** `crates/ava-llm/src/providers/common/message_mapping.rs`

System message parts collected without capacity hint. Could pre-calculate.

### LOW Priority

#### 5. Stuck detector: last_responses Vec with remove(0)

**File:** `crates/ava-agent/src/stuck.rs`
**Line:** 94

```rust
self.last_responses.remove(0);
```

`remove(0)` is O(n) — shifts all elements. For a 3-element Vec this is negligible, but `VecDeque` would be semantically correct.

#### 6. Widespread .to_string() in config/catalog initialization

**Files:** Various config and catalog files across all crates.

1318+ `.to_string()`/`.to_owned()` calls found, but the vast majority are in initialization code (tool registration, provider catalogs, error construction). Not hot path — no action needed.

---

## Blocking Async Calls

### HIGH Priority

#### 1. TUI modals: 14 block_in_place calls

**File:** `crates/ava-tui/src/app/modals.rs`

14 occurrences of `block_in_place(|| handle.block_on(...))` for:

- Credential loading
- Model listing
- Provider discovery
- MCP server listing

These block the entire TUI event loop thread. During model switching or credential loading, the UI freezes completely — no rendering, no input handling.

**Fix:** Move these to background tasks via `tokio::spawn()` and receive results via the existing `AppEvent` channel. Show a loading indicator in the modal.

#### 2. TUI commands: 12 block_in_place calls

**File:** `crates/ava-tui/src/app/commands.rs`

12 occurrences for slash command handling (model switching, tool listing, etc.). Same issue as modals — blocks the UI thread.

**Fix:** Same as above — spawn async tasks, receive results via the `AppEvent` channel.

### MEDIUM Priority

#### 3. TUI app: 4 block_in_place calls in initialization

**File:** `crates/ava-tui/src/app/mod.rs`

4 occurrences in app initialization and state updates. Less critical since they happen infrequently (startup, config reload).

**Fix:** Lower priority, but should still be async for consistency.

---

## Lock Patterns

### MEDIUM Priority

#### 1. AgentStack: Multiple RwLock fields accessed sequentially

**File:** `crates/ava-agent/src/stack.rs`

AgentStack holds 6+ `RwLock` fields: `tools`, `codebase_index`, `provider_override`, `model_override`, `mcp`, `thinking_level`.

`current_model()` acquires `provider_override.read().await` then `model_override.read().await` sequentially. While each lock is dropped before the next `.await`, the sequential acquisition adds latency.

**Fix:** Consider bundling related overrides into a single `RwLock<OverrideState>` struct to reduce lock acquisition count.

#### 2. Permission middleware: RwLock read on every tool call

**File:** `crates/ava-tools/src/permission_middleware.rs`
**Line:** 27

```rust
let context = self.context.read().await;
```

Acquires RwLock read on `InspectionContext` for every tool call's `before()` hook. The context rarely changes (only on permission grants).

**Fix:** Consider using `arc_swap::ArcSwap` for read-heavy/write-rare patterns, or cache the context locally.

### LOW Priority (SAFE patterns)

#### 3. ModelRouter: Double-check locking pattern

**File:** `crates/ava-llm/src/router.rs`

The `route()` method reads credentials lock, drops it, then takes write lock on providers. This is a correct double-check locking pattern — no issues.

#### 4. Commander workers: Arc<Mutex<AgentLoop>> per worker

**File:** `crates/ava-commander/src/lib.rs`
**Line:** 100

Each Worker has its own `Arc<Mutex<AgentLoop>>`. Lock held for entire worker lifetime in `run_worker()`, but no contention since workers don't share AgentLoops.

---

## Recommended Fix Order

### Phase 1 — Quick wins (1-2 days)

1. Add `with_capacity()` to hot-path Vec/String allocations (HIGH alloc #1, #2)
2. Fix `sep.clone()` in status_bar (trivial)
3. Return `&str` from message rendering instead of cloning content

### Phase 2 — Tool execution path (2-3 days)

4. Change `Tool::execute` to take `&Value` instead of owned `Value` (HIGH clone #3)
5. Change middleware `after()` signature to avoid mandatory clone (MEDIUM clone #7)
6. Pass `&ToolCall` through registry instead of cloning (MEDIUM clone #6)

### Phase 3 — Agent loop restructure (3-5 days)

7. Use `Arc<Message>` or `Arc<str>` for shared message content (HIGH clone #1)
8. Emit `Arc<Session>` on completion instead of cloning (HIGH clone #2)
9. Restructure tool result collection to avoid intermediate clones (HIGH clone #4)

### Phase 4 — Async cleanup (3-5 days)

10. Replace ALL `block_in_place` in modals/commands with `tokio::spawn` + channel (HIGH blocking #1, #2)
11. Add loading indicators for async modal operations

### Phase 5 — Lock optimization (1-2 days)

12. Bundle AgentStack overrides into single RwLock
13. Consider ArcSwap for permission context

---

## Appendix: Methodology

### Files audited (read in full)

- `crates/ava-agent/src/agent_loop/mod.rs`
- `crates/ava-agent/src/agent_loop/tool_execution.rs`
- `crates/ava-agent/src/stack.rs`
- `crates/ava-agent/src/stuck.rs`
- `crates/ava-llm/src/router.rs`
- `crates/ava-llm/src/pool.rs`
- `crates/ava-llm/src/providers/common/message_mapping.rs`
- `crates/ava-tools/src/registry.rs`
- `crates/ava-tools/src/permission_middleware.rs`
- `crates/ava-tui/src/app/mod.rs`
- `crates/ava-tui/src/app/commands.rs`
- `crates/ava-tui/src/app/modals.rs`
- `crates/ava-tui/src/app/event_handler.rs`
- `crates/ava-tui/src/ui/mod.rs`
- `crates/ava-tui/src/ui/status_bar.rs`
- `crates/ava-tui/src/state/messages.rs`
- `crates/ava-commander/src/lib.rs`

### Search patterns used

| Pattern | Scope | Matches |
|---------|-------|---------|
| `.clone()` | ava-agent/src | 78 |
| `.clone()` | ava-llm/src | 12 |
| `.clone()` | ava-tools/src | 44 |
| `.clone()` | ava-tui/src | 96 |
| `block_in_place\|block_on` | workspace | 41 |
| `RwLock\|Mutex` | workspace | 73 |
| `String::new()` | workspace | 89 |
| `Vec::new()` | workspace | 116+ |
| `.to_string()\|.to_owned()` | workspace | 1318+ |
