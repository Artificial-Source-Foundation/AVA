# C++ Adoption Milestone 6 Boundaries — End-to-End Adoption Evidence

Adoption-loop **Milestone 6** is an evidence-only slice. It reuses the existing C++ runtime/tool surfaces to prove an end-to-end deterministic headless run with the compiled `ava_cli` binary.

This is distinct from historical **C++ Milestone 6** (`ava_tools` foundation).

## In Scope

1. Add a deterministic non-live CTest lane (`ava_m6_e2e`) that executes the compiled `ava_cli` binary.
2. Isolate runtime state via explicit `HOME` and `XDG_*` directories for each test run.
3. Drive the run with `AVA_MOCK_PROVIDER_RESPONSES_FILE` and fixed scripted responses.
4. Exercise a real built-in tool call (`read`) through the runtime loop (not a mocked tool registry shortcut).
5. Validate NDJSON output for session context, tool call/result, token usage, completion reason, and basic event ordering.
6. Validate SQLite/session persistence for the produced `session_id` and transcript metadata.
7. Add a focused local runner script (`scripts/testing/cpp-m6-e2e.sh`) for this lane.

## Out Of Scope

1. Any new backend, tool, provider, or product feature behavior.
2. Live-provider soak by default (network/provider-auth runs remain opt-in).
3. Full Rust-vs-C++ golden parity claims for the entire headless NDJSON schema.
4. Web or desktop parity claims.
5. Broad migration/architecture refactors beyond this evidence lane.

## Validation Commands

```bash
ionice -c 3 nice -n 15 cmake --build --preset cpp-debug --target ava_m6_e2e_tests
ionice -c 3 nice -n 15 ctest --preset cpp-debug -R '^ava_m6_e2e$' --output-on-failure
ionice -c 3 nice -n 15 scripts/testing/cpp-m6-e2e.sh
```

## Acceptance Bar

Milestone 6 adoption evidence is complete when `ava_m6_e2e` deterministically proves the compiled non-live headless path from prompt -> mock provider -> real tool execution -> NDJSON -> SQLite/session persistence with isolated app state, and documentation keeps scope explicit (no live-soak-by-default, no full golden parity claim, no web/desktop parity claim).
