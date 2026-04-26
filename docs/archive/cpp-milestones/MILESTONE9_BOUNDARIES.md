# C++ Milestone 9 Boundaries

This note records what is implemented for the C++ headless CLI proof lane and what remains intentionally deferred.

## Implemented in Milestone 9

1. `cpp/apps/ava/main.cpp` now runs a real blocking headless CLI path instead of only version/smoke scaffolding.
2. Narrow, practical CLI parsing was added for:
   - positional goal
   - `--provider`
   - `--model`
   - `--continue`
   - `--session`
   - `--json`
   - `--max-turns`
   - `--auto-approve`
   - retained: `--version`, `--smoke`
3. Session startup resolution now supports:
   - new-session start
   - continue-latest
   - continue-by-id
4. Headless startup now applies CLI precedence over persisted headless metadata for provider/model/max-turns.
5. Blocking runtime execution now wires the existing C++ foundations together:
   - `ava_session::SessionManager`
   - `ava_llm::create_provider(...)`
   - default `ava_tools` registry
   - `ava_agent::AgentRuntime`
6. Output now supports plain text and consumer-facing NDJSON event lines.
   - This is intentionally a practical emitter, not a full canonical event mirror.
   - Canonical overlapping lifecycle tag spelling is preserved for `complete` and `error`.
7. Non-interactive approval behavior is explicit for this milestone:
   - default path remains non-hanging
   - mutating tools require approval and fail closed when no bridge exists
   - `--auto-approve` wires a scoped allow-all approval bridge for the M9 proof lane
8. Session updates and run metadata are persisted back through `ava_session`; a successful rerun clears stale `headless.last_run.error` metadata from earlier failed runs in the same session.
9. Focused C++ app tests were added for CLI parsing, session startup resolution, metadata precedence, and NDJSON tag spelling.

## Explicitly Deferred

1. Full Rust headless parity (async/streaming-first runtime behavior and richer lifecycle/event surfaces).
2. Full interactive approval lifecycle (question/plan/approval bridges with live user interaction).
3. Watch mode and voice mode.
4. Daemon/server/background runtime modes.
5. Full queue parity (`follow-up`, `later`, stdin queue protocol parity) beyond explicit M9 proof needs.
6. Full canonical event stream mirroring and broader control-plane event completeness.
7. Broader provider parity beyond what current C++ `ava_llm` already supports.

Milestone 9 is intentionally scoped: it delivers the smallest honest working headless CLI lane without claiming full Rust runtime parity.
