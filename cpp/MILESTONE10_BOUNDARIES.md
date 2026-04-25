# C++ Milestone 10 Boundaries

This note records the smallest honest **headless-runtime validation slice** added on top of Milestone 9.

## Implemented in Milestone 10

1. Added a real scripted-response seam for tool-loop validation by extending `ava_llm::MockProvider` to support queued `ava::llm::LlmResponse` objects (including tool calls and usage), not only plain text strings.
2. Added a narrow headless app test seam by overloading `run_headless_blocking(...)` with an optional provider override so integration tests can run the real M9 path without network.
3. Added focused C++ integration tests for:
   - scripted tool loop (tool call -> tool execution -> completion) through the headless path
   - multiple tool calls emitted from one assistant turn through the headless path
   - NDJSON session context and tool call/result correlation in JSON output mode
   - mutating-tool rejection without `--auto-approve`
   - `max_turns` terminal-state persistence and NDJSON completion reason emission
   - stale `metadata.headless.last_run.error` cleanup after a later successful rerun
   - opt-in live OpenAI smoke through the same headless path, gated by env (`AVA_LIVE_PROVIDER_TESTS`, `OPENAI_API_KEY`, and `AVA_WITH_CPR`)
4. Updated CMake test wiring to build/run the new integration target (`ava_app_integration_tests`).

## Explicitly Deferred (still out of Milestone 10 scope)

1. Full async/streaming parity for the headless runtime loop.
2. Multi-provider live parity and cross-provider behavior matrix.
3. Benchmark-style corpus evaluation or exact-text quality scoring in C++ tests.
4. Richer approval UX/lifecycle beyond the current scoped non-interactive behavior and `--auto-approve` lane.
5. Broader automation harnesses beyond this targeted validation slice.

Milestone 10 is intentionally small and validation-focused: it proves real M9 path behavior with deterministic scripted tests plus optional live-provider smoke, without claiming full runtime/provider parity.
