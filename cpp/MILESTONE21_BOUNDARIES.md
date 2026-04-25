# C++ Milestone 21 Boundaries

M21 starts the post-M20 parity-expansion loop with the narrow headless/event contract slice. It adds the canonical C++ `SubagentComplete` event kind, headless NDJSON projection, and native blocking child-run emission plumbing without promoting the broader task-tool/default-headless delegation route.

## In Scope

1. Add `AgentEventKind::SubagentComplete` with parent `run_id`, parent tool `call_id`, child `session_id`, delegated description, and optional message count payload fields.
2. Project `SubagentComplete` through the C++ headless NDJSON emitter using the canonical `subagent_complete` tag.
3. Let `NativeBlockingTaskSpawner` emit a best-effort `SubagentComplete` event through an optional parent event sink after a successful child run is persisted and parent run/call correlation is available.
4. Keep TUI event handling exhaustive for the expanded `AgentEventKind` inventory.
5. Add focused unit coverage for canonical NDJSON shape and spawner event correlation.

## Out of Scope

1. Full default headless end-to-end subagent tool routing; the current default C++ tool registry still does not expose a task/subagent tool.
2. Full scripted NDJSON stream parity beyond the new canonical `subagent_complete` projection case.
3. Background subagent execution, live subagent update events, async scheduler parity, and provider hard-kill interruption.
4. Full TUI modal/widget parity for child-run/task presentation.

## Validation

```bash
just cpp-build cpp-debug
./build/cpp/debug/tests/ava_app_tests "ndjson subagent complete event emits canonical fields"
./build/cpp/debug/tests/ava_orchestration_tests "native blocking task spawner runs child sessions"
./build/cpp/debug/tests/ava_tui_tests
```

## Decision Point

M21 closes the event-contract projection primitive needed by `EX-004`, but does not by itself remove the exception because the default C++ headless tool surface still lacks the delegated task/subagent route that would produce this event end to end for normal CLI consumers.

## Follow-Up Green-Fix Notes

- `NativeBlockingTaskSpawner` now emits the observational `SubagentComplete` event for child runs that terminate at `max_turns` without an error, matching `TaskResult::ok()` semantics and keeping successful child terminal states visible to parent event sinks.
- Malformed `subagent_complete` NDJSON projection now reports the missing canonical field name and preserves a canonical error `run_id` field even when the malformed event omitted parent run correlation.
- Focused coverage now locks optional `message_count` omission, missing-`run_id` error shape, and max-turns child-run event emission.
