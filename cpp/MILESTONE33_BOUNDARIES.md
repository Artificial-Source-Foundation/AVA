# C++ Milestone 33 Boundaries — Headless Integration Proof

Milestone 33 moves the C++ lane from scoped feature slices into an end-to-end headless proof. The goal is to validate that the current C++ backend can run realistic non-interactive sessions through the shared composition path without claiming web, desktop, or full Rust runtime parity.

## In Scope

1. **Scripted headless integration proof:** exercise the `ava` C++ headless app through real runtime composition, session startup, provider override, tool registry wiring, queue handling, and session persistence.
2. **Workspace/trust proof:** cover `--cwd`, `AVA_WORKING_DIRECTORY`, and `--trust` behavior through the app boundary, including persisted workspace metadata and trusted-project storage effects.
3. **Tool-loop proof:** run deterministic scripted assistant tool loops using read-only and mutating core tools under the existing permission policy, including safe auto-approve behavior and fail-closed dangerous paths.
4. **Resume/recovery proof:** verify resumed headless sessions preserve runtime metadata, apply M30 recovery before new work, and save checkpointed session state during long-running tool loops.
5. **NDJSON contract proof:** assert stable event ordering and required fields for run start, assistant deltas/responses, tool calls/results, budget warnings, compaction, checkpoints, completion, and errors.
6. **Focused evidence docs:** record the exact local commands and remaining intentional deferrals in `CHANGELOG.md` and `docs/project/backlog.md`.

## Out Of Scope

1. Live-provider soak runs that require network credentials by default.
2. Web/desktop parity, Tauri IPC behavior, or browser automation surfaces.
3. Full Rust async/background scheduler parity, hard provider aborts, or process hard-kill recovery.
4. Full MCP HTTP/SSE/OAuth, custom TOML tools, plugin runtime, and browser-tool parity.
5. Full semantic compaction/summarization, JSONL audit logging, bookmarks/search, and cross-runtime UUID normalization.

## Validation Commands

```bash
git diff --check
ionice -c 3 nice -n 15 just cpp-build cpp-debug
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_app_integration_tests "~[live]"
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_app_tests "[ava_app]"
ionice -c 3 nice -n 15 ./build/cpp/debug/tests/ava_agent_tests "[ava_agent]"
```

## Evidence Checklist

1. Non-live app-boundary tests cover `--cwd`, `AVA_WORKING_DIRECTORY`, and `--trust` through `run_headless_blocking`.
2. Resume-by-session coverage verifies interrupted-tool recovery metadata and synthesized failed tool results before new work.
3. Queue coverage verifies CLI follow-up and post-complete messages are promoted through real runtime turns.
4. NDJSON coverage asserts session context, run lifecycle, assistant delta, checkpoint, tool call/result, budget warning, and completion fields/order.
5. Existing permission integration tests continue to prove fail-closed mutating-tool behavior with and without headless auto-approve.

## Deferred From Earlier Milestones

1. Compaction event projection remains covered at lower layers until C++ exposes a headless configuration path for runtime compaction thresholds.
2. MCP HTTP/SSE/OAuth, custom TOML tools, plugin runtime, and browser tools remain outside this evidence lane.
3. Live provider behavior remains opt-in through `[live]` tests and is not required for deterministic M33 acceptance.
4. Signal cancellation and process hard-kill behavior remain M34/future hardening concerns beyond the non-live proof lane.

## Decision Point For M34

After M33, the remaining acceptance question is whether the C++ lane has enough deterministic evidence for the scoped backend/headless claim. M34 should be a hardening and final-evidence pass, not a feature-broadening milestone; see `cpp/MILESTONE34_BOUNDARIES.md` for the exact scope and validation lane.
