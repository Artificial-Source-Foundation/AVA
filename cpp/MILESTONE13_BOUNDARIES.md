# C++ Milestone 13 Boundaries

This note records a bounded **shared runtime composition + native blocking subagent execution baseline** pass on top of Milestone 12.

## Implemented in Milestone 13

1. Added an orchestration-owned shared runtime composition seam (`ava::orchestration::compose_runtime(...)`) that now owns:
   - session startup resolution (`new` / `continue latest` / `continue by id`)
   - provider/model/max-turn resolution (CLI/session metadata precedence, model-spec parsing, provider defaults)
   - default tool registration + permission middleware wiring (including `--auto-approve` behavior)
   - `AgentRuntime` construction from the assembled provider/tool/runtime config
2. Refactored both app entrypoints to consume the same seam:
   - `cpp/apps/ava/headless_run.cpp`
   - `cpp/apps/ava_tui/main.cpp`
   This removes duplicated runtime assembly from app-layer code and keeps app entrypoints thinner.
3. Upgraded orchestration task execution from DTO-only to a real native blocking baseline via `NativeBlockingTaskSpawner`:
   - enforces `MAX_AGENT_DEPTH`
   - enforces per-parent spawn budget limits
   - rejects disabled subagents explicitly
   - resolves effective subagent definitions using existing orchestration helpers (`effective_subagent_definitions`, `parse_model_spec`, runtime profiles)
   - caps child subagent max-turns to the parent/baseline ceiling
   - applies runtime-profile-driven tool filtering for read-only specialists
   - creates/runs real child sessions and persists lineage/completion metadata
   - reports success output vs error separately in `TaskResult`
4. Hardened the shared composition seam contract ownership:
   - runtime selection metadata now reads/writes a runtime-owned namespace first (`metadata.runtime.*`)
   - legacy `metadata.headless.*` fallback remains for compatibility
   - allowed-tool filters now validate names and fail closed on unknown entries
5. Added focused orchestration unit coverage for:
   - shared runtime composition behavior
   - native blocking child-session execution path
   - depth-limit enforcement
6. Follow-up quality tightening keeps the read-only specialist tool allowlist centralized, distinguishes explicitly disabled subagents from subagents disabled by default configuration, and adds regression coverage for explicit session resume, legacy `metadata.headless.*` fallback, and the default `TaskSpawner::spawn(...)` delegation path.

## Explicitly Deferred (still out of Milestone 13 scope)

1. Full task-tool parity and full runtime-level integration of delegated task execution.
2. MCP/plugin-manager runtime parity.
3. Async/background subagent spawn ownership in C++.
4. Full streaming parity and broader runtime architecture parity with Rust.
5. Broad `AgentStack` runtime port in C++.

Milestone 13 is intentionally narrow: it consolidates duplicated runtime assembly into orchestration ownership and lands an honest native blocking subagent execution baseline without claiming full orchestration/runtime parity.
