---
title: "C++ Backend/TUI Migration Plan (C++ Milestone 1)"
description: "Planning artifact for migrating the Rust backend and terminal app to C++ with CMake."
order: 10
updated: "2026-04-24"
---

# C++ Backend/TUI Migration Plan (C++ Milestone 1)

This is a planning-only artifact for a staged migration of AVA's backend and terminal application from Rust to C++. It does not represent implemented C++ runtime migration work.

## Milestone 2 Bootstrap Status

As of 2026-04-22, the repo now includes an initial C++ workspace bootstrap in `cpp/` (root `CMakeLists.txt`, dependency modules, placeholder libraries/apps/tests). This is implementation scaffolding for the build graph only and is intentionally **not** a behavior-parity backend port.

Current implementation note: the active C++ Milestone 4 slice now includes foundational `ava_config` and `ava_session` implementations. Shared session/tree DTOs are treated as `ava_types`-owned data, `ava_session` owns persistence/branch logic, and file-backed config/trust/credential persistence is routed through the current `ava_platform` filesystem seam. YAML/TOML parsing, keychain/OAuth flows, and advanced search/ranking remain explicitly deferred.

Current implementation note: the Milestone 4 `ava_runtime` composition target now includes the active foundational libraries (`ava_types`, `ava_control_plane`, `ava_platform`, `ava_config`, `ava_session`) with real C++ config/session persistence baselines.

## Milestone 5 LLM Foundation Status

As of 2026-04-23, `cpp/` now includes an initial real `ava_llm` foundation slice:

1. Core provider infrastructure is active (capabilities, error classification/retryability, provider interface/wrapper, retry + circuit-breaker + thinking config primitives, provider kind/normalization helpers, pricing/token estimation helpers, and provider factory plumbing).
2. `MockProvider` is implemented for real runtime/testing use.
3. Exactly one production provider is active in this milestone: `OpenAI` (blocking request path + blocking SSE chunk collection path using CPR when enabled).
4. Additional provider names are present in factory plumbing but intentionally return explicit not-implemented errors in this milestone.

Milestone boundary note: advanced routing, dynamic credential refresh, long-tail provider parity, and broader async/runtime migration remain deferred (`cpp/MILESTONE5_BOUNDARIES.md`).

The practical bootstrap surface is documented in `cpp/README.md`.

## Milestone 6 Tools Foundation Status

As of 2026-04-23, `cpp/` now includes an initial real `ava_tools` Milestone 6 slice:

1. A real C++ tool registry is active with tool interface, metadata/schema listing, tier/source tracking, middleware chaining, tool-input backfill hook support, and call-id normalization to the incoming tool-call id.
2. Retry helpers now implement the scoped Rust M6 baseline (`MAX_RETRIES=2`, `100ms/200ms` backoff, retryable read-only tool gating, transient/permanent failure heuristics).
3. A simplified permission middleware seam is active with allow/deny/ask inspection results and fail-closed behavior when approval is required but no bridge is attached.
4. Real core tools now exist for `read`, `write`, `edit` (M22 now partially lifts the prior exact-only limitation for non-`replace_all` edits with a bounded deterministic cascade while preserving exact/replace-all compatibility), `bash`, `glob`, `grep`, `git`, and `git_read`.

Milestone boundary note: plugin-manager hooks, MCP bridge integration, browser/web tool surfaces (`web_fetch`, `web_search`), advanced Rust edit-engine parity (hashline/weighted fuzzy/merge-style recovery), and fuller `ava_platform` unification for tool-side file/process execution remain deferred (`cpp/MILESTONE6_BOUNDARIES.md`, `cpp/MILESTONE22_BOUNDARIES.md`).

## Milestone 7 Agent Runtime-Core Status

As of 2026-04-23, `cpp/` now includes an initial real `ava_agent` Milestone 7 runtime-core slice:

1. A new `ava_agent` library is active under `cpp/include/ava/agent` + `cpp/src/agent` and wired into `ava_runtime`.
2. The C++ runtime now includes a blocking headless-first core loop that can assemble a system prompt, call an `ava_llm::Provider`, parse/coalesce tool calls, execute tools through `ava_tools::ToolRegistry`, mutate an existing `ava_types::SessionRecord`, and terminate on completion, max-turn, error, or simple stuck conditions.
3. A practical minimal queue and stuck-detector baseline are implemented for Milestone 7 scope (not full Rust parity layering).
4. A deterministic event callback seam (`AgentEvent`) is available for future headless/TUI integration.

Milestone boundary note: orchestration/subagents, async/streaming-first runtime behavior, full Rust stuck-detector and prompt-engine parity, compaction/reflection/recovery layers, and plugin/MCP runtime integration remain deferred (`cpp/MILESTONE7_BOUNDARIES.md`).

## Milestone 8 Orchestration Contracts/Data Status

As of 2026-04-23, `cpp/` now includes an initial real `ava_orchestration` Milestone 8 contracts/data slice:

1. A new `ava_orchestration` library is active under `cpp/include/ava/orchestration` + `cpp/src/orchestration` and wired into `ava_runtime`.
2. Subagent runtime-profile contracts are now present: `MAX_AGENT_DEPTH`, `SubAgentRuntimeProfile`, `runtime_profile_for(...)`, and `apply_runtime_profile_to_registry(...)` (implemented in Milestone 8 as explicit non-mutating profile-aware tool filtering over registry listings).
3. Effective subagent catalog/data helpers are present: `EffectiveSubagentDefinition`, config-owned agent/default DTOs from `ava_config`, `effective_subagent_definitions(...)`, and `build_subagent_system_prompt(...)`.
4. The model parsing seam is now present: `parse_model_spec(...)` with known-provider parsing, catalog inference fallback, and `openrouter` default fallback.
5. Lightweight orchestration-owned DTO contracts are now present for stack/task seams: `AgentStackConfig`, `AgentRunResult`, `TaskResult`, and `TaskSpawner` (plus `NoopTaskSpawner` for test utility).

Milestone boundary note: full `AgentStack`/`stack_run.rs` runtime behavior, MCP/plugin runtime composition, async/background subagent execution, and full session/memory/budget orchestration parity remain deferred (`cpp/MILESTONE8_BOUNDARIES.md`). Config/default DTO ownership stays in `ava_config`, and runtime-owned background spawn semantics remain deferred.

## Milestone 9 Headless CLI Status

As of 2026-04-23, `cpp/` now includes a smallest-honest C++ Milestone 9 blocking headless CLI proof lane:

1. `cpp/apps/ava/main.cpp` now dispatches a real headless run path instead of only smoke/version output.
2. A narrow CLI subset is implemented for the proof lane (`goal`, `--provider`, `--model`, `--continue`, `--session`, `--json`, `--max-turns`, `--auto-approve`, plus retained `--version`/`--smoke`).
3. Startup/session resolution supports new session, continue-latest, and continue-by-id with CLI precedence over persisted headless metadata for provider/model/max-turns.
4. The path runs existing C++ foundations end-to-end (`ava_session`, `ava_llm`, default `ava_tools`, `ava_agent`) and persists updated sessions/metadata.
5. Headless output supports plain text and practical consumer-facing NDJSON with canonical overlapping lifecycle tag spelling preserved for `complete`/`error`.

Milestone boundary note: this remains a blocking, non-interactive proof slice only; full async/streaming-first behavior, full interactive approval lifecycle, watch/voice, daemon/server mode, fuller queue parity, and full Rust headless/TUI parity remain deferred (`cpp/MILESTONE9_BOUNDARIES.md`).

## Milestone 10 Headless Validation Status

As of 2026-04-23, `cpp/` now includes a smallest-honest C++ Milestone 10 validation slice for the existing Milestone 9 headless path:

1. `ava_llm::MockProvider` now supports queued full `LlmResponse` scripting (including tool calls and usage), enabling deterministic no-network tool-loop tests.
2. The headless app exposes a narrow test seam via `run_headless_blocking(..., provider_override)` so integration tests can run the real M9 path with scripted providers.
3. Focused C++ integration coverage now validates (a) scripted tool call -> tool execution -> completion transcript flow, (b) mutating-tool rejection when `--auto-approve` is absent, and (c) an opt-in live OpenAI smoke test gated by environment flags.

Milestone boundary note: this is validation-only progress, not parity expansion. Full streaming parity, multi-provider live parity, benchmark-corpus evaluation, and richer automation remain explicitly deferred (`cpp/MILESTONE10_BOUNDARIES.md`).

## Milestone 3 Foundational-Library Status

As of 2026-04-22, `cpp/` now includes the first real non-placeholder implementations for the leaf libraries:

1. `ava_types`: foundational DTOs/enums/helpers (including tool JSON serialization, queue-tier enums, thinking helpers, and `@mention` attachment parsing).
2. `ava_control_plane`: canonical command/event tables and lookup helpers using frozen Rust contract wire strings.
3. `ava_platform`: blocking local filesystem primitives (`read_file`, `write_file`, `create_dir_all`, `exists`, `is_directory`, `metadata`) plus base execution DTOs.

Boundary clarification: `ava_config` and `ava_session` remain intentionally deferred to Milestone 4, and Milestone 3 still does not claim async runtime/session persistence parity.

## Milestone 4 Config/Session Foundation Status

As of 2026-04-23, `cpp/` now includes a minimal real Milestone 4 foundation slice for `ava_config` and `ava_session`:

1. `ava_config`: XDG path resolution, trust store JSON persistence, credential-store JSON persistence with env overrides, and an embedded model registry fixture.
2. `ava_session`: blocking SQLite-backed session/message persistence with core branch/tree operations.

Milestone boundary note: the C++ tree intentionally defers full YAML/TOML config parsing, OS keychain integration, OAuth refresh/device/browser flows, advanced FTS ranking, and interactive credential prompts (`cpp/MILESTONE4_BOUNDARIES.md`).

Current scope:

1. Migrate the backend/runtime crates that power the CLI, headless mode, and TUI.
2. Use CMake as the build system for the new C++ tree.
3. Defer web (`crates/ava-web`) and desktop/Tauri (`src-tauri/`) until the backend/TUI path is stable.

## Migration Intent

The current backend/TUI path is not one crate; it is a stack.

Current relevant source inventory (rough crate-level sizing snapshot):

1. `crates/ava-types/src`: 8 files, about 3.0k LOC
2. `crates/ava-control-plane/src`: 7 files, about 2.7k LOC
3. `crates/ava-platform/src`: 3 files, about 0.8k LOC
4. `crates/ava-config/src`: 12 files, about 7.0k LOC
5. `crates/ava-session/src`: 6 files, about 2.1k LOC
6. `crates/ava-llm/src`: 27 files, about 14.4k LOC
7. `crates/ava-tools/src`: 51 files, about 16.0k LOC
8. `crates/ava-agent/src`: 34 files, about 17.4k LOC
9. `crates/ava-agent-orchestration/src`: 10 files, about 5.0k LOC
10. `crates/ava-tui/src`: 146 files, about 52.3k LOC

Authoritative scoped source count for the crates listed above: 304 `.rs` files (verified).

That means this should be treated as a phased replacement, not a single-step rewrite.

## Recommended C++ Baseline

1. Language: C++20 minimum
2. Build system: CMake 3.28+
3. Testing: Catch2
4. Formatting/linting: `clang-format`, `clang-tidy`
5. JSON: `nlohmann_json`
6. Logging: `spdlog`
7. String formatting: `fmt`
8. CLI argument parsing: `CLI11`
9. SQLite: system `sqlite3` plus a thin local wrapper
10. HTTP: `libcurl` or `cpr` on top of `libcurl`
11. TUI: `FTXUI`

Why `FTXUI`: the current app is state-heavy and widget-heavy; it needs a real component/event model, not a minimal ncurses port.

## Proposed CMake Layout

```text
cpp/
  CMakeLists.txt
  cmake/
    Dependencies.cmake
    Warnings.cmake
    Sanitizers.cmake
  include/ava/
    types/
    control_plane/
    platform/
    config/
    session/
    llm/
    tools/
    agent/
    orchestration/
    tui/
  src/
    types/
    control_plane/
    platform/
    config/
    session/
    llm/
    tools/
    agent/
    orchestration/
    tui/
  apps/
    ava/main.cpp
    ava_smoke/main.cpp
  tests/
    unit/
    integration/
```

Recommended targets:

1. `ava_types`
2. `ava_control_plane`
3. `ava_platform`
4. `ava_config`
5. `ava_session`
6. `ava_llm`
7. `ava_tools`
8. `ava_agent`
9. `ava_orchestration`
10. `ava_tui` (Phase 4 / not yet implemented)
11. `ava_cli`
12. `ava_smoke`

Recommended top-level CMake options:

1. `AVA_BUILD_SMOKE=ON`
2. `AVA_BUILD_TESTS=ON`
3. `AVA_ENABLE_WARNINGS_AS_ERRORS=OFF`
4. `AVA_ENABLE_SANITIZERS=OFF`
5. `AVA_ENABLE_LTO=OFF`
6. `AVA_WITH_FTXUI=OFF`
7. `AVA_WITH_CPR=OFF`

## Migration Order

Do not start with the TUI widgets. Start with the leaf contracts and backend seams.

### Phase 0: Contract Freeze (C++ Milestone 1)

C++ Milestone 1 is a strict freeze milestone: backend contracts, fixtures, and migration acceptance criteria must be locked before any C++ porting work starts. The authoritative freeze-file inventory and test-anchor checklist live in [cpp-contract-freeze-m1.md](cpp-contract-freeze-m1.md).

Exact freeze scope (authoritative files):

1. `crates/ava-control-plane/src/{commands.rs,events.rs,interactive.rs,sessions.rs,queue.rs,orchestration.rs}`
2. `crates/ava-control-plane/src/lib.rs`
3. `crates/ava-types/src/{lib.rs,message.rs,tool.rs,session.rs}`
4. `crates/ava-tools/src/registry.rs`
5. `crates/ava-agent/src/control_plane/{mod.rs,events.rs,sessions.rs}`
6. `crates/ava-session/src/{lib.rs,manager.rs,tree.rs,search.rs,helpers.rs,diff_tracking.rs}`
7. `crates/ava-config/src/{lib.rs,credentials.rs,keychain.rs,trust.rs,agents.rs,routing.rs,thinking.rs,credential_commands.rs,model_catalog/{mod.rs,registry.rs,types.rs,fallback.rs}}`
8. `crates/ava-tui/src/lib.rs`
9. `crates/ava-tui/src/config/cli.rs`
10. `crates/ava-tui/src/headless/{mod.rs,single.rs,common.rs,input.rs}`
11. `crates/ava-tui/src/main.rs` (freeze only the backend/TUI-relevant CLI/headless entry sub-surface: cwd override resolution/application, TUI-vs-headless selection, `run_headless(cli)` dispatch, `App::new(cli)` / `app.run()` startup, and CLI flag handoff that affects backend/TUI startup semantics; exclude `cli.trust`, background update checks / `--no_update_check`, `cli.acp_server`, `Command::Update` / `Command::SelfUpdate`, `Command::Review`, `Command::Auth`, `Command::Plugin`, `Command::Serve`, and benchmark routing)
12. Backend approval policy/classification seam: `crates/ava-permissions/src/{lib.rs,inspector.rs,policy.rs,tags.rs}` and `crates/ava-tools/src/permission_middleware.rs`
13. Runtime composition seam: `crates/ava-agent-orchestration/src/stack/mod.rs` and `crates/ava-agent/src/run_context.rs`

C++ Milestone 1 acceptance gates (all required):

1. Canonical command/event JSON contract is explicit and stable (`commands.rs` and `events.rs` canonical spec fixtures).
2. Interactive lifecycle semantics are frozen (`interactive.rs` request ordering, ownership, timeout, and stale-request handling behavior).
3. Session continuity/replay semantics are frozen (`sessions.rs` replay payload/session precedence behavior plus `ava-types` session/message serialization shape).
4. Queue semantics are frozen (`queue.rs` clear semantics + alias parsing and `orchestration.rs` deferred/in-flight promotion behavior).
5. CLI/headless contract surface is frozen (`lib.rs`, `config/cli.rs`, `headless/{mod.rs,single.rs,common.rs,input.rs}`, and the backend/TUI-relevant `main.rs` entry sub-surface for flags/slash handling/headless run bootstrap semantics; `cli.trust`, background update checks / `--no_update_check`, `cli.acp_server`, `Command::Update` / `Command::SelfUpdate`, `Command::Review`, `Command::Auth`, `Command::Plugin`, `Command::Serve`, and benchmark routing are explicitly excluded from this freeze gate).
6. Session/SQLite persistence compatibility is validated against the existing Rust persistence seam (`ava-session` behavior and schema/migration compatibility stay stable while backend/TUI contracts freeze).
7. JSON event-stream parity expectations are explicitly captured for headless output and backend lifecycle events, anchored to the current JSON emission path in `crates/ava-tui/src/headless/single.rs` plus existing headless tests, with the concrete checklist artifact at `docs/architecture/cpp-m1-event-stream-parity-checklist.md` required before Phase 1 starts; C++ Milestone 1 requires canonical tag spelling parity for overlapping headless lifecycle tags (`complete`, `error`, `subagent_complete`) and explicit documentation of accepted headless-emitter field-shape differences (not full field-shape equivalence).
8. Backend approval policy/classification and permission-middleware semantics are explicitly frozen/signoff-gated via `ava-permissions` + `ava-tools/src/permission_middleware.rs`, with headless as the first freeze-critical consumer.
9. Runtime composition ownership is frozen for `AgentStack` and `AgentRunContext` so startup/bootstrap ordering, config/session/router wiring, and run-context handoff remain stable for CLI/headless/TUI callers.

Compatibility rule for this milestone:

1. Internal/backend contracts remain `snake_case`.
2. Tauri IPC remains `camelCase` and is out of scope for this backend/TUI migration milestone.

Explicitly out of scope for C++ Milestone 1:

1. Any C++ production implementation or translation work (this milestone is planning/signoff only).
2. Web (`crates/ava-web`) and desktop/Tauri (`src-tauri/`) migration work.
3. Long-tail provider behavior parity beyond the core backend/TUI contract path.
4. Non-core plugin/MCP surface parity work.
5. TUI visual polish and widget-level parity.

Implementation outputs for C++ Milestone 1:

1. Locked contract checklist with explicit signoff against the freeze files above.
2. Canonical command/event/interactive fixture references (prefer existing inline/golden Rust fixtures over introducing new file-snapshot infrastructure).
3. Frozen CLI/headless contract checklist (flags, slash command behavior, and run/startup expectations).
4. Frozen session/queue semantics checklist (including continuity/replay and deferred queue behavior).
5. Migration acceptance gate list to be used as entry criteria for Phase 1 C++ work.

### Phase 1: Leaf Libraries

Port these first because the rest of the stack depends on them:

1. `ava-types`
2. `ava-control-plane`
3. `ava-platform`
4. `ava-config`
5. `ava-session`

### Phase 2: Runtime Services

Port the service crates that the agent loop consumes:

1. `ava-llm`
2. `ava-tools`
3. `ava-agent`
4. `ava-agent-orchestration`

### Phase 3: CLI and Headless First

Get a non-TUI binary working before attempting full TUI parity:

1. `ava-tui/src/main.rs`
2. `ava-tui/src/config/cli.rs`
3. `ava-tui/src/headless/`

Success condition: the C++ binary can run headless end to end.

### Phase 4: Interactive TUI

Only after headless is correct:

1. port state containers
2. port event dispatch
3. port core widgets
4. port advanced widgets and polish

## File-By-File Migration Map

The list below is the practical owner map for the C++ port. The goal is to preserve boundaries where they are already useful and simplify only where the Rust split is too Cargo-specific.

### 1. Shared Types

Source files:

1. `crates/ava-types/src/lib.rs`
2. `crates/ava-types/src/message.rs`
3. `crates/ava-types/src/tool.rs`
4. `crates/ava-types/src/session.rs`
5. `crates/ava-types/src/context.rs`
6. `crates/ava-types/src/plan.rs`
7. `crates/ava-types/src/todo.rs`
8. `crates/ava-types/src/error.rs`

Migration plan:

1. Move these into `include/ava/types/` and `src/types/` almost one-for-one.
2. Treat them as DTOs and enums, not service classes.
3. Add JSON serialization alongside type definitions because these types are transport-facing.

### 2. Shared Control Plane

Source files:

1. `crates/ava-control-plane/src/lib.rs`
2. `crates/ava-control-plane/src/commands.rs`
3. `crates/ava-control-plane/src/events.rs`
4. `crates/ava-control-plane/src/interactive.rs`
5. `crates/ava-control-plane/src/sessions.rs`
6. `crates/ava-control-plane/src/queue.rs`
7. `crates/ava-control-plane/src/orchestration.rs`

Migration plan:

1. Keep these as a pure contract library with no UI code and no HTTP code.
2. Preserve current snake_case field names at the JSON layer.
3. Use this library as the compatibility seam while web and desktop remain in Rust.

### 3. Platform Abstraction

Source files:

1. `crates/ava-platform/src/lib.rs`
2. `crates/ava-platform/src/fs.rs`
3. `crates/ava-platform/src/shell.rs`

Migration plan:

1. Rebuild as a narrow filesystem/process abstraction over `std::filesystem` and subprocess helpers.
2. Keep shell execution and file I/O behind interfaces because tools depend on them heavily.
3. Keep path-safety behavior close to the tool layer, not here.

### 4. Config, Credentials, and Trust

Primary files:

1. `crates/ava-config/src/lib.rs`
2. `crates/ava-config/src/credentials.rs`
3. `crates/ava-config/src/keychain.rs`
4. `crates/ava-config/src/trust.rs`
5. `crates/ava-config/src/agents.rs`
6. `crates/ava-config/src/routing.rs`
7. `crates/ava-config/src/thinking.rs`
8. `crates/ava-config/src/model_catalog/*`

Migration plan:

1. Keep path resolution and config loading early in the migration because `AgentStack` uses them immediately.
2. Preserve XDG layout and legacy compatibility rules.
3. Split secure credential storage behind an interface so keychain support can land after plaintext/local-file fallback.
4. Port `model_catalog` as data-driven JSON/YAML parsing rather than hand-coded constants.

### 5. Session Persistence

Primary files:

1. `crates/ava-session/src/lib.rs`
2. `crates/ava-session/src/manager.rs`
3. `crates/ava-session/src/tree.rs`
4. `crates/ava-session/src/search.rs`
5. `crates/ava-session/src/helpers.rs`
6. `crates/ava-session/src/diff_tracking.rs`

Migration plan:

1. Port this as an early C++ SQLite library because resume/replay depends on it.
2. Preserve the current DB schema before optimizing anything.
3. Keep search/tree helpers separate from raw repository classes.

### 6. LLM Layer

Primary files:

1. `crates/ava-llm/src/provider.rs`
2. `crates/ava-llm/src/router.rs`
3. `crates/ava-llm/src/retry.rs`
4. `crates/ava-llm/src/pool.rs`
5. `crates/ava-llm/src/circuit_breaker.rs`
6. `crates/ava-llm/src/message_transform.rs`
7. `crates/ava-llm/src/thinking.rs`
8. `crates/ava-llm/src/providers/*.rs`

Migration plan:

1. First port the provider interface, response types, retry logic, and router.
2. Then port only the providers needed to keep development moving.
3. Recommended initial provider subset: Anthropic, OpenAI, OpenRouter, mock.
4. Defer long-tail providers until the agent loop is stable.

Important note:

1. `provider.rs` is the C++ interface contract.
2. `router.rs` is the backend selection policy.
3. `providers/*` are adapters and should stay replaceable.

### 7. Tool System

Primary files and folders:

1. `crates/ava-tools/src/lib.rs`
2. `crates/ava-tools/src/registry.rs`
3. `crates/ava-tools/src/permission_middleware.rs`
4. `crates/ava-tools/src/core/mod.rs`
5. `crates/ava-tools/src/core/read.rs`
6. `crates/ava-tools/src/core/write.rs`
7. `crates/ava-tools/src/core/edit.rs`
8. `crates/ava-tools/src/core/bash.rs`
9. `crates/ava-tools/src/core/glob.rs`
10. `crates/ava-tools/src/core/grep.rs`
11. `crates/ava-tools/src/core/web_fetch.rs`
12. `crates/ava-tools/src/core/web_search.rs`
13. `crates/ava-tools/src/core/git_read.rs`
14. `crates/ava-tools/src/core/question.rs`
15. `crates/ava-tools/src/core/plan.rs`
16. `crates/ava-tools/src/core/todo.rs`
17. `crates/ava-tools/src/core/task.rs`
18. `crates/ava-tools/src/edit/*`
19. `crates/ava-tools/src/git/*`

Migration plan:

1. Port the default 9 tools first because they define the core product surface.
2. Port plan/question/todo next because the current TUI depends on them.
3. Port `task` only after subagent orchestration is available.
4. Keep custom-tool, plugin, browser, and MCP-specific surfaces behind compile options until the base tool contract is stable.

Important hot spots:

1. `core/mod.rs`: registration order and product surface
2. `permission_middleware.rs`: approval boundary
3. `edit/*`: hardest file-mutation logic to port safely
4. `core/bash.rs`: sandbox, quoting, truncation, and non-interactive discipline

### 8. Agent Runtime

Primary files:

1. `crates/ava-agent/src/lib.rs`
2. `crates/ava-agent/src/run_context.rs`
3. `crates/ava-agent/src/message_queue.rs`
4. `crates/ava-agent/src/routing.rs`
5. `crates/ava-agent/src/system_prompt.rs`
6. `crates/ava-agent/src/instructions.rs`
7. `crates/ava-agent/src/stuck.rs`
8. `crates/ava-agent/src/reflection.rs`
9. `crates/ava-agent/src/streaming_diff.rs`
10. `crates/ava-agent/src/session_logger.rs`
11. `crates/ava-agent/src/control_plane/events.rs`
12. `crates/ava-agent/src/control_plane/sessions.rs`
13. `crates/ava-agent/src/agent_loop/mod.rs`
14. `crates/ava-agent/src/agent_loop/completion.rs`
15. `crates/ava-agent/src/agent_loop/response.rs`
16. `crates/ava-agent/src/agent_loop/tool_execution.rs`
17. `crates/ava-agent/src/agent_loop/steering.rs`
18. `crates/ava-agent/src/agent_loop/context_recovery.rs`
19. `crates/ava-agent/src/agent_loop/repetition.rs`
20. `crates/ava-agent/src/agent_loop/sidechain.rs`

Migration plan:

1. Port `run_context`, `message_queue`, `routing`, and `system_prompt` before the loop body.
2. Port `instructions.rs` early because project instruction loading is large and highly stateful.
3. Treat `agent_loop/mod.rs` plus `tool_execution.rs` as the critical backend heart of the port.
4. Keep the current split between pure contracts and backend-only helper code.

Major risk files already identified from size/complexity:

1. `crates/ava-agent/src/instructions.rs`
2. `crates/ava-agent/src/agent_loop/mod.rs`
3. `crates/ava-agent/src/agent_loop/tool_execution.rs`
4. `crates/ava-agent/src/stuck.rs`
5. `crates/ava-agent/src/system_prompt.rs`

### 9. Orchestration Stack

Primary files:

1. `crates/ava-agent-orchestration/src/lib.rs`
2. `crates/ava-agent-orchestration/src/stack/mod.rs`
3. `crates/ava-agent-orchestration/src/stack/stack_config.rs`
4. `crates/ava-agent-orchestration/src/stack/stack_run.rs`
5. `crates/ava-agent-orchestration/src/stack/stack_tools.rs`
6. `crates/ava-agent-orchestration/src/stack/stack_mcp.rs`
7. `crates/ava-agent-orchestration/src/subagents/mod.rs`
8. `crates/ava-agent-orchestration/src/subagents/catalog.rs`
9. `crates/ava-agent-orchestration/src/subagents/config.rs`
10. `crates/ava-agent-orchestration/src/subagents/effective.rs`

Migration plan:

1. Collapse only if it simplifies the C++ build graph; otherwise keep it separate.
2. Start with `stack/mod.rs` and `stack_run.rs` because they assemble the whole runtime.
3. Leave MCP wiring optional in the first usable milestone.

### 10. CLI, Headless, and TUI

Primary entry files:

1. `crates/ava-tui/src/main.rs`
2. `crates/ava-tui/src/lib.rs`
3. `crates/ava-tui/src/config/cli.rs`
4. `crates/ava-tui/src/headless/mod.rs`
5. `crates/ava-tui/src/headless/single.rs`
6. `crates/ava-tui/src/app/mod.rs`

Migration plan:

1. Port `main.rs`, CLI parsing, and headless execution before any full-screen UI.
2. Keep the first C++ milestone focused on `ava --headless` and non-TTY CLI behavior.
3. Port the interactive TUI only after backend parity is proven through headless flows.

Interactive TUI subareas:

1. `crates/ava-tui/src/state/*`: port next; these are the best bridge between backend events and UI state.
2. `crates/ava-tui/src/app/*`: then port event dispatch, command routing, and interaction handling.
3. `crates/ava-tui/src/ui/*` and `crates/ava-tui/src/widgets/*`: port after state and event flow are stable.

Recommended TUI deferrals for the first C++ milestone:

1. `benchmark*`
2. `audio.rs`, `transcribe.rs`, `voice` paths
3. advanced provider-connect polish
4. non-essential background UX polish

Supporting crates / explicit deferrals note:

1. `ava-db` and `ava-auth` are supporting seams and are not first-class C++ Milestone 1 port targets.
2. `ava-permissions` is in-scope only as the narrow backend approval policy/classification seam above; broader permission-system porting is deferred.
3. Optional plugin/MCP-heavy surfaces remain explicitly out of current port scope except where needed to preserve core backend/TUI contract behavior.

## What To Defer Explicitly

These should not block the first backend/TUI migration slice:

1. `crates/ava-web/`
2. `src-tauri/`
3. benchmark-only paths in `ava-tui`
4. voice/audio support
5. MCP-heavy optional paths
6. long-tail providers in `ava-llm`
7. plugin/extension parity beyond what is required for core local runs

## First Usable C++ Milestone

The first milestone should not aim for full Rust parity.

It should aim for this:

1. CMake builds `ava_cli` successfully
2. config/trust/session loading works
3. one provider works end to end
4. the 9 default tools work
5. the agent loop runs headless end to end
6. sessions persist and resume
7. JSON event output works for automation

If that milestone is solid, the TUI migration becomes a surface-porting problem instead of a backend-correctness problem.

## Immediate Next Steps

1. Create the `cpp/` tree with the target layout above and a root `CMakeLists.txt`.
2. Add the foundational C++ libraries first: `ava_types`, `ava_control_plane`, `ava_platform`.
3. Add golden fixtures from the current Rust backend for commands, events, and sessions.
4. Port `ava-config` and `ava-session` next so a headless executable can start with real persisted state.
5. Only then start on `ava-llm`, `ava-tools`, and the agent loop.
