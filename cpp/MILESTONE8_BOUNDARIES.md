# C++ Milestone 8 Boundaries

This note records what is implemented for the C++ `ava_orchestration` slice and what remains intentionally deferred.

## Implemented in Milestone 8

1. New `ava_orchestration` static library wired into `cpp/src/CMakeLists.txt`, `ava_runtime`, and a dedicated Catch2 leaf test target (`ava_orchestration_tests`).
2. Real contracts/data headers under `cpp/include/ava/orchestration/` and implementation under `cpp/src/orchestration/`.
3. Subagent runtime-profile slice:
   - `MAX_AGENT_DEPTH`
   - `SubAgentRuntimeProfile`
   - `runtime_profile_for(...)`
   - `apply_runtime_profile_to_registry(...)` implemented as a **non-mutating profile-aware listing filter** over the current registry snapshot
   - `build_subagent_system_prompt(...)` with profile-aware guidance text
4. Effective catalog/data slice:
    - `EffectiveSubagentDefinition`
    - `effective_subagent_definitions(...)` with built-in template IDs + config-owned agent/default overrides and disabled-agent omission behavior
5. Model-spec seam:
   - `parse_model_spec(...)` with explicit known-provider parsing, catalog inference fallback, and `openrouter` default fallback.
6. Stack/task DTO seam (contracts only):
    - `AgentStackConfig`
    - `AgentRunResult`
    - `TaskResult`
    - `TaskSpawner` plus a tiny `NoopTaskSpawner` implementation for test/contract use.

## Explicitly Deferred

1. Full Rust `AgentStack` runtime/orchestration port and `stack_run.rs` behavior.
2. MCP runtime ownership, plugin-aware registry building, and runtime tool registration lifecycle parity.
3. Async/background subagent execution ownership and coroutine/streaming runtime behavior.
4. Full session/memory/budget integration and parity with Rust orchestration internals.
5. External runtime bridges and full provider/runtime execution composition.

Milestone 8 is intentionally honest and scoped: it lands a real C++ orchestration **contracts-and-data** slice without claiming full orchestration runtime parity. Config/default DTO ownership stays in `ava_config`, while runtime/background spawn ownership stays deferred.
