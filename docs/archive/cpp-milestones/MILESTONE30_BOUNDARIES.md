# C++ Milestone 30 Boundaries — Session Recovery And Checkpoint Persistence

Milestone 30 adds the smallest backend/headless recovery slice needed after the M29 long-run runtime work. The goal is not full Rust session feature parity; it is to make resumed C++ headless sessions structurally safe after interrupted tool execution and to bound crash data loss with runtime checkpoint events.

## In Scope

1. Runtime-owned session repair before new work starts:
   - Add `SessionRecord` recovery helpers for interrupted tool calls and malformed transcript structure.
   - Synthesize failed tool-result messages for assistant tool calls that never received a tool result.
   - Remove empty assistant messages, orphan tool messages, trailing non-user messages after terminal assistant responses, adjacent duplicate messages, and merge consecutive pre-existing user messages.
   - Preserve a valid `branch_head` after recovery and record recovery counts under `metadata.agent.last_recovery`.

2. Runtime checkpoint events:
   - Add `AgentEventKind::Checkpoint`.
   - Emit checkpoints after recovery, queued-message insertion, compaction, assistant persistence points, tool-result persistence points, and partial assistant persistence on cancellation.
   - Save the session from the headless event sink when a checkpoint event is observed.

3. Event projection and TUI handling:
   - Project checkpoint events through headless NDJSON/text surfaces.
   - Treat checkpoint events as a lightweight TUI status update.

4. Focused evidence:
   - Unit coverage for orphaned tool-call recovery before the provider prompt.
   - Unit coverage for empty-assistant/consecutive-user repair.
   - Unit coverage for checkpoint emission after a tool result.
   - Existing session/app/orchestration/headless tests remain green.

## Out Of Scope

1. Full JSONL session logging and rotation parity.
2. Bookmark/search UI and session-management features.
3. Cross-runtime UUID migration for C++ runtime-generated message IDs.
4. Hard-kill crash simulation and external process restart tests.
5. Semantic LLM summarization or compaction recovery beyond the M29 structural compaction slice.
6. Subagent/background session recovery rollups beyond existing child-session metadata.

## Validation

```bash
git diff --check
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_agent_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_session_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_app_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_tui_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_orchestration_tests
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_app_integration_tests "~[live]"
```

## Decision Point

The next recovery decision is whether to prioritize JSONL/audit session logging or cross-runtime message-ID normalization before the final M33/M34 headless evidence pass. Both remain deferred from this milestone.
