# C++ Milestone 20 Boundaries

M20 is the scoped backend/headless/TUI completion gate for the post-M16 roadmap. It aggregates M17-M19 evidence, adds focused closure tests for remaining completion-critical contract rows, and records deferred-inventory guardrails without promoting web/desktop/MCP/plugin/provider/auth/config breadth.

## In Scope

1. M17-M19 focused evidence remains green.
2. Headless NDJSON tool-call/tool-result `call_id` correlation is covered.
3. Explicit resume-by-ID preserves tool-heavy message metadata (`tool_calls`, `tool_results`, `tool_call_id`, and branch head).
4. Cancellation preserves session transcript order/parent links without executing post-cancel tools.
5. Headless `--auto-approve` rejects high-risk mutating tools per `EX-001`; safe read-only tool execution remains covered.
6. Exact edit no-match behavior is deterministic and leaves file content unchanged.
7. Deferred RP-4A/RP-4B and intentional non-goal buckets remain documented guardrails, not completion blockers.

## Out of Scope

1. Canonical headless `subagent_complete` NDJSON parity; M19 child terminal summaries are the scoped C++ evidence for this completion pass and the divergence is tracked as `EX-004`.
2. Full scripted NDJSON stream parity beyond focused event/correlation checks.
3. Full Rust edit strategy parity (hashline/fuzzy/recovery).
4. Full async/background scheduler parity and hard-kill provider interruption.
5. Web/desktop migration, MCP/plugin breadth, and provider/auth/config long-tail breadth.

## Validation

```bash
just cpp-configure cpp-debug
just cpp-build cpp-debug
./build/cpp/debug/tests/ava_app_tests "ndjson tool call and result correlate call_id"
./build/cpp/debug/tests/ava_app_tests "resume by id preserves tool heavy message metadata"
./build/cpp/debug/tests/ava_agent_tests "agent runtime cancellation preserves session transcript integrity"
./build/cpp/debug/tests/ava_app_integration_tests "headless auto approve rejects dangerous mutating tool"
./build/cpp/debug/tests/ava_app_integration_tests "headless scripted tool loop executes tool and persists transcript"
./build/cpp/debug/tests/ava_tools_tests "edit no match returns error without mutating file"
just cpp-test cpp-debug --output-on-failure
git diff --check
```

## Decision Point

DP-4 outcome: scoped completion-critical backend/headless/TUI gates are closed without promoting deferred buckets. Canonical `subagent_complete` NDJSON parity remains explicitly excepted by `EX-004`; full modal/widget parity, web/desktop parity, MCP/plugin breadth, broad provider/auth/config breadth, full Rust edit-strategy parity, and broad async/hard-kill cancellation remain deferred or intentional non-goals.

## Follow-Up Green-Fix Notes

- Runtime-created tool-result messages now populate the typed `tool_call_id` field in addition to the JSON `content`/`tool_results` payload, keeping tool-heavy transcripts safe for later repair/cleanup utilities.
- Headless auto-approve risk matching treats `low` as safe alongside `safe`, while explicit integration coverage now verifies `write`, `edit`, and `bash` high-risk mutating calls remain rejected under `--auto-approve`.
