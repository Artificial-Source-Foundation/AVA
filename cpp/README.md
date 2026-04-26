# C++ Workspace Foundation

This `cpp/` tree is the scoped C++ backend/TUI migration workspace for the Rust-to-C++ backend/TUI migration plan.

It remains intentionally scoped and honest:

- It creates a real CMake workspace and dependency wiring.
- It now includes real foundational implementations in `ava_types`, `ava_control_plane`, `ava_platform`, `ava_config`, `ava_session`, `ava_llm`, `ava_tools`, a scoped runtime-core `ava_agent`, and a smallest-honest `ava_orchestration` contracts/data slice.
- The current `ava_runtime` aggregate includes the implemented M10-M34 scoped backend/headless/TUI migration slices: blocking headless execution, an FTXUI TUI lane, orchestration-owned runtime composition, native blocking subagents, interactive request lifecycle seams, streaming/cancellation events, MCP stdio MVP wiring plus CPR-gated HTTP JSON execution, budget/queue/compaction controls, session recovery checkpoints, provider streaming slices, daily-use headless CLI polish, deterministic headless evidence, and final hardening cleanup.
- It still does **not** claim runtime parity or a production C++ backend port.

## Included Targets

Libraries:

1. `ava_types`
2. `ava_control_plane`
3. `ava_platform`
4. `ava_config`
5. `ava_session`
6. `ava_llm`
7. `ava_tools`
8. `ava_agent`
9. `ava_orchestration`
10. `ava_runtime` (thin composition target for the in-scope foundational libraries)

Executables:

1. `ava_cli`
2. `ava_tui` (requires resolved FTXUI linkage)
3. `ava_smoke` (optional via `AVA_BUILD_SMOKE`)

Tests:

1. `ava_cpp_tests` (Catch2 aggregate covering foundational unit tests)
2. `ava_llm_tests` (leaf-target Catch2 tests for the scoped `ava_llm` slice)
3. `ava_tools_tests` (leaf-target Catch2 tests for the scoped `ava_tools` slice)
4. `ava_agent_tests` (leaf-target Catch2 tests for the scoped `ava_agent` runtime-core slice)
5. `ava_orchestration_tests` (leaf-target Catch2 tests for the scoped `ava_orchestration` contracts/data slice)
6. `ava_app_tests` (focused Catch2 tests for headless CLI parsing/session/event seams)
7. `ava_app_integration_tests` (focused integration tests for scripted headless tool loops, workspace/trust/resume behavior, approval rejection, and optional env-gated live-provider smoke)
8. `ava_tui_tests` (focused TUI state/event/dock/scroll behavior tests)
9. `ava_config_tests` (leaf-target tests for config, trust, credentials, agents, and model registry behavior)
10. `ava_session_tests` (leaf-target tests for SQLite-backed session persistence and tree behavior)
11. `ava_mcp_tests` (leaf-target tests for scoped MCP protocol/config/client/manager/transport behavior)
12. `ava_m3_foundation_tests` (targeted original M3 foundation regression lane)
13. `ava_m3_runtime_tests` / CTest `ava_m3_runtime` (C++ adoption M3 real `ava_cli` dogfood lane)
14. `ava_m6_e2e_tests` / CTest `ava_m6_e2e` (C++ adoption M6 deterministic end-to-end evidence lane)

## Implemented Foundations

1. **`ava_types`**
   - Build metadata (`BuildInfo`, `current_build_info()`)
   - Tool DTOs (`Tool`, `ToolCall`, `ToolResult`) with `nlohmann_json` serialization
   - Role + queue-tier enums and string helpers
   - Streaming DTOs (`TokenUsage`, `StreamToolCall`, `StreamChunk`)
   - Thinking helpers (`ThinkingLevel` cycling + loose parser)
   - Context attachments with `@mention` parsing (`@file:`, `@folder:`, `@codebase:`, bare path forms)

2. **`ava_control_plane`**
   - Canonical command table and lookup helpers aligned to frozen Rust wire strings
   - Canonical event table and lookup helpers aligned to frozen Rust wire strings
   - Queue tier/command mapping helpers
   - Minimal interactive request lifecycle ownership seam (`InteractiveRequestStore`) for approval/question/plan with request-id + run-id + pending/resolved/cancelled/timeout state tracking

3. **`ava_platform`**
   - Real blocking local filesystem primitives (`read_file`, `write_file`, `create_dir_all`, `exists`, `is_directory`, `metadata`)
   - Shared DTOs for `FileInfo`, `CommandOutput`, and `ExecuteOptions`
    - Preserved `platform_tag()`
    - Explicitly deferred command-execution runtime behavior for a later milestone

4. **`ava_config`**
     - XDG + legacy-aware app path resolution for config/data/state/cache
     - Trusted-project persistence (`trusted_projects.json`) with process-cache invalidation
     - JSON credential store persistence with provider env-override precedence
     - OAuth credential updates that preserve static fields, plus explicit key-redaction helpers and native-keychain unavailability reporting
     - Routing profile structs/JSON normalization and project-local `.ava/state.json` persistence for recent/model-mode selections
     - Embedded model registry fixture with alias normalization/pricing/loop-prone helpers
     - File-backed config/trust/credential persistence routed through the current `ava_platform` filesystem primitives
     - Native OS keychain, encrypted fallback/migration, OAuth refresh flows, full YAML config loading, and runtime routing-profile selection remain deferred

5. **`ava_session`**
    - Real blocking SQLite persistence for sessions/messages
    - Session CRUD baseline (`create`, `save`, `get`, `list_recent`, `add_message`)
    - Conversation tree/branch baseline (`get_tree`, `get_branch`, `branch_from`, `switch_branch`, `get_branch_leaves`)
    - Shared session/tree DTOs now live in `ava_types`, with `ava_session` focused on persistence and branch algorithms

6. **`ava_llm`**
    - Provider capability surface and provider-kind helpers.
    - Provider error classification + retryability helpers.
    - Retry primitives (`RetryMode`, `OverloadTracker`, `RetryBudget`) and `CircuitBreaker`.
    - Thinking configuration primitives (`ThinkingConfig`, `ResolvedThinkingConfig`, fallback/support enums).
    - Message normalization helpers for cross-provider handoff (light strip/normalize behavior).
    - Heuristic token/cost helpers.
    - Provider factory plumbing with explicit deferred-provider error surfacing.
    - Real `MockProvider` implementation.
    - Scoped production provider protocols in the current milestone lane:
      - `OpenAI` (blocking HTTP + SSE chunk collection via CPR when enabled), reused for protocol-compatible `openrouter`, `inception`, and `zai` factory wiring.
      - `Anthropic` (Messages API generation and CPR-gated SSE streaming; default no-CPR builds fail transport explicitly), reused for protocol-compatible `alibaba`, `kimi`, and `minimax` factory wiring.
      - Native Gemini, Copilot OAuth/device flow, Ollama local behavior, provider-specific routing/reasoning payloads, and live-provider validation remain deferred.

7. **`ava_tools` (scoped Milestone 6 core-tool-system slice)**
     - Real tool registry with tool interface, tool metadata/schema exposure, tier/source tracking, middleware chain, and call-id normalization.
     - Rust-aligned retry helper behavior for retryable read-only tools (`MAX_RETRIES=2`, backoff `100ms/200ms`, transient/permanent heuristics).
     - Simplified permission middleware seam with explicit fail-closed behavior when approval is required but no approval bridge exists.
     - Current Milestone 6 tool execution remains intentionally local to `ava_tools`; deeper unification of file/process execution behind `ava_platform` is deferred.
      - Real core tools for `read`, `write`, `edit` (narrow exact/replace-all strategy set), `bash`, `glob`, `grep`, `git`, and `git_read`.
      - Headless parity helpers now include process-local `todo_write`/`todo_read` checklist state and a registry-backed `tool_search` discovery tool.
      - `web_fetch`/`web_search` remain deferred from the default C++ tool surface until a safe native HTTP transport lane is promoted (for example CPR-backed wiring) without widening this milestone slice.
      - The delegated task/subagent tool route remains deferred in C++ until run/call context threading is promoted through the runtime seam.

8. **`ava_agent` (scoped Milestone 7 runtime-core slice)**
     - Minimal agent runtime loop capable of prompt assembly, provider turn execution, tool-call parsing, tool execution, session transcript mutation, and bounded completion.
     - Deterministic event emission seam (`AgentEvent`) for future headless/TUI consumers.
     - Practical minimal message queue and stuck detector baselines adapted from Rust intent.
       - Blocking execution model with scoped streaming/cancellation/budget/queue/compaction/session-recovery seams; broad async/background orchestration parity remains deferred.

9. **`ava_orchestration` (scoped Milestones 8 + 13 + 14 slice)**
     - Real C++ orchestration library under `cpp/include/ava/orchestration` + `cpp/src/orchestration`.
     - Runtime-profile/catalog helpers: `MAX_AGENT_DEPTH`, `SubAgentRuntimeProfile`, `runtime_profile_for(...)`, and a non-mutating profile-aware tool filtering seam via `apply_runtime_profile_to_registry(...)`.
      - Prompt and subagent-definition helpers: `build_subagent_system_prompt(...)`, `EffectiveSubagentDefinition`, and `effective_subagent_definitions(...)` over config-owned agent/default DTOs from `ava_config`.
     - Config seam parity helper: `parse_model_spec(...)` with known-provider + model-registry inference fallback.
       - Shared runtime composition seam (`compose_runtime(...)`) now owns session startup + provider/model resolution + default tool/middleware registration + `AgentRuntime` assembly used by both `ava` headless and `ava_tui`; runtime metadata is now read/written from a runtime-owned namespace first, with legacy headless metadata fallback retained for compatibility.
       - Milestone 14 adds `InteractiveBridge` ownership in orchestration/runtime composition so approval/question/plan flows share one backend-controlled request lifecycle seam (typed kind + request-id/run-id + terminal state transitions) while keeping app adapters thin.
      - Task contracts now include a real native blocking execution path (`NativeBlockingTaskSpawner`) with depth + spawn-budget checks, disabled-agent rejection, provider/model/max-turns resolution (including parent-ceiling turn capping), read-only runtime-profile tool filtering, and child-session lineage/completion metadata persistence.
      - `TaskResult` now separates successful output from errors (`output` vs `error`) instead of overloading one text field.
      - Lightweight stack DTO contracts remain in place (`AgentStackConfig`, `AgentRunResult`, `TaskResult`, `TaskSpawner`).
        - Intentionally still no full MCP/plugin-manager parity, no async/background subagent spawning in C++, and no full Rust runtime-streaming parity.

## Dependencies

The workspace uses C++20 and CMake 3.28+.

Configured dependencies:

- `fmt`
- `spdlog`
- `nlohmann_json`
- `CLI11`
- `Catch2` (for tests)
- Optional dependencies: `FTXUI`, `CPR`
- `SQLite3` (required by active Milestone 4 `ava_session` implementation)

Where practical, `Dependencies.cmake` prefers `find_package(...)` and falls back to `FetchContent` for common header/lib dependencies.

Notes:

- Optional dependency reporting in `ava/core/build_config.hpp` reflects **resolved linkage** (found + linked), not just requested options.
- Catch2 discovery/fetch is only evaluated when `AVA_BUILD_TESTS=ON`.
- SQLite is intentionally treated as a system dependency for the active session-persistence slice. On Linux, install `libsqlite3-dev` or the distro equivalent before configuring the C++ workspace.

10. **`ava_tui` (scoped Milestone 11 interactive slice + Milestone 12 bounded cleanup + Milestone 16 parity-basics seams)**
       - Real `ava_tui` executable under `cpp/apps/ava_tui/` built on FTXUI when linked.
       - Minimal app state for a scrollable message list, single-line text composer buffer, status line, and quit action.
       - Blocking event loop that accepts keyboard input, submits prompts to existing blocking `ava_agent` runtime on a worker thread, and consumes runtime events through the existing callback/event-sink seam.
       - Milestone 12 tightening pass: stronger `AppState` event/status mapping and focused edge-case coverage (empty input, backspace on empty, page scrolling clamps, multiline + trailing-newline submission behavior).
       - Integration reused from current foundations (`ava_session`, `ava_llm`, `ava_tools`, `ava_agent`) with no new runtime architecture.
       - Milestone 16 narrow parity-basics additions: slash-command infrastructure (`/help`, `/clear`, `/model`, graceful unsupported `/compact`), input history up/down, top/bottom message navigation seams, message-range status visibility, and adapter-facing interactive request visibility/clearing state.
       - Intentionally narrow keyboard scope remains: type text, Enter submit, Up/Down history (with scroll fallback), PgUp/PgDn/Home/End message navigation, `q` quit.

## Build

Preferred preset lane:

```bash
cd cpp
cmake --list-presets=all
cmake --preset cpp-debug
cmake --build --preset cpp-debug
ctest --preset cpp-debug --output-on-failure
```

From the repository root, the equivalent helper commands are `just cpp-presets`, `just cpp-configure`, `just cpp-build`, and `just cpp-test`. Those helpers route through `scripts/dev/ensure-cmake.sh`, which uses an existing CMake 3.28+ or bootstraps the pinned CMake used by CI.

Run the same lane with `cpp-release` before considering build-system milestone changes complete. Use `cpp-werror` to validate `AVA_ENABLE_WARNINGS_AS_ERRORS=ON`, and `cpp-sanitizer` for the ASan/UBSan debug lane on GCC/Clang hosts.

Manual lane:

```bash
cmake -S cpp -B build/cpp -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cpp -j
```

Key bootstrap options:

- `AVA_BUILD_TESTS` (default `ON`)
- `AVA_BUILD_SMOKE` (default `ON`)
- `AVA_WITH_FTXUI` / `AVA_WITH_CPR` (default `OFF`; link only if package resolution succeeds)

## Run

After the default `cpp-debug` preset, app binaries live under `build/cpp/debug`. The `cpp-release` preset uses `build/cpp/release`.

```bash
./build/cpp/debug/apps/ava_cli --version
./build/cpp/debug/apps/ava_cli --smoke
./build/cpp/debug/apps/ava_cli "Summarize this repository"
./build/cpp/debug/apps/ava_cli "Continue from latest session" --continue
./build/cpp/debug/apps/ava_cli "Use this exact session" --session <session-id>
./build/cpp/debug/apps/ava_cli "Emit NDJSON" --json
./build/cpp/debug/apps/ava_cli "Allow mutating tools" --auto-approve
./build/cpp/debug/apps/ava_tui --auto-approve
./build/cpp/debug/apps/ava_smoke
ctest --preset cpp-debug --output-on-failure
```

Headless CLI flags (Milestone 9 lane retained and expanded through M32):

- positional goal
- `--provider`
- `--model`
- `--cwd`
- `--agent`
- `--trust`
- `--continue`
- `--session`
- `--json`
- `--max-turns`
- `--max-budget`
- `--auto-approve`
- `--follow-up`, `--later`, `--later-group`
- `--version`, `--smoke`

M32 also honors these environment defaults when the matching CLI flags are absent:

- `AVA_PROVIDER`
- `AVA_MODEL`
- `AVA_WORKING_DIRECTORY`
- `AVA_AGENT`

Explicit CLI flags take precedence over environment defaults. `--cwd` / `AVA_WORKING_DIRECTORY` must resolve to an existing directory and becomes the workspace root used by runtime composition and session metadata. `--trust` records that workspace as trusted through the current C++ trust store. `--agent` selects one of the builtin C++ agent profiles and can apply that profile's default turn budget when `--max-turns` was not specified.

Optional live-provider smoke (only when explicitly enabled):

```bash
AVA_LIVE_PROVIDER_TESTS=1 OPENAI_API_KEY=... ctest --test-dir build/cpp -R ava_app_integration --output-on-failure
```

By default, live-provider tests skip cleanly when env gates are not set.

Milestone 33 adds deterministic non-live headless integration proof for the current app boundary. The focused evidence lane exercises workspace selection through `--cwd` and `AVA_WORKING_DIRECTORY`, `--trust` persistence, resume/recovery repair, queued follow-up/post-complete turns, budget-warning NDJSON, and lifecycle/tool/checkpoint event ordering through `run_headless_blocking`. See `cpp/MILESTONE33_BOUNDARIES.md` for the exact scope and remaining deferrals.

C++ adoption Milestone 3 adds a deterministic real-binary dogfood lane for backend-runtime reliability. `ava_m3_runtime` runs the compiled `ava_cli` with isolated HOME/XDG directories, a mock provider response file (`AVA_MOCK_PROVIDER_RESPONSES_FILE`), `--cwd`, `--json`, and real tool execution, then verifies NDJSON tool-call/tool-result/token-usage events plus persisted SQLite session metadata. From the repository root, `scripts/testing/cpp-m3-dogfood.sh` runs the focused CTest lane.

C++ adoption Milestone 6 adds a deterministic non-live end-to-end adoption-evidence lane (`ava_m6_e2e`) that keeps the same compiled-binary + isolated HOME/XDG + mock-provider approach while tightening explicit event-order checks and SQLite/session transcript checks in one focused proof path. From the repository root, `scripts/testing/cpp-m6-e2e.sh` runs this lane. Scope remains evidence-only (`cpp/MILESTONE6_ADOPTION_BOUNDARIES.md`): no live-provider soak by default, no full Rust golden parity claim, and no web/desktop parity claim.

## C++ Adoption Loop Milestone 5 (TUI Product Parity Baseline)

Adoption-loop **Milestone 5** is a terminology convenience for the current C++ TUI product-parity baseline evidence pass. It is distinct from historical **C++ Milestone 5**, which was the `ava_llm` foundation slice.

The current baseline evidence is intentionally scoped to already-landed behavior:

1. Slash-command/operator basics: `/help`, `/clear`, `/model`, graceful unsupported `/compact`, and unknown-command handling.
2. Input history + navigation seams: Up/Down history draft restore, message range/status visibility, and top/bottom jumps.
3. Adapter-facing interactive lifecycle visibility/actions: approval/question/plan pending handles, request-id-bearing approve/reject/answer/accept-plan/reject-plan/cancel-question actions, and backend-owned lifecycle settlement.
4. Child-run observer projection: bounded active/terminal child-run metadata projection without TUI lifecycle ownership.
5. TUI option parsing evidence: provider/model/max-turns/auto-approve parse success, continue/session conflict rejection, and actionable CLI parse diagnostics.
6. Interactive dock scope: bounded UTF-8-safe preview projection, priority/sticky selection, cancellation-aware resolver behavior, and fail-closed approval gating when preview data is truncated.

Focused evidence anchors:

- `cpp/tests/unit/ava_tui_state.test.cpp`
- `docs/archive/cpp-milestones/MILESTONE11_BOUNDARIES.md`
- `docs/archive/cpp-milestones/MILESTONE12_BOUNDARIES.md`
- `docs/archive/cpp-milestones/MILESTONE16_BOUNDARIES.md`
- `docs/archive/cpp-milestones/MILESTONE18_BOUNDARIES.md`
- `docs/archive/cpp-milestones/MILESTONE19_BOUNDARIES.md`
- `docs/archive/cpp-milestones/MILESTONE26_BOUNDARIES.md`

Still explicitly deferred in this adoption-loop M5 baseline:

- full Rust TUI modal/sidebar/theme/provider-connect UX
- broader command-palette/session/model-picker parity
- richer request payload rendering and child-run modal UX
- MCP/plugin/custom-tool TUI UX parity and web/desktop UX parity

## Scope Guardrails

- Milestone 11 = Milestone 10 foundations plus a smallest-honest FTXUI interactive terminal slice.
- Milestone 12 = bounded validation/cleanup on that same TUI slice (no broad feature expansion).
- Milestone 13 = shared runtime composition ownership + native blocking subagent baseline (without broad parity claims).
- Milestone 14 (first narrow pass) = interactive control-plane lifecycle baseline + orchestration bridge wiring for approval/question/plan (`cpp/MILESTONE14_BOUNDARIES.md`).
- Milestone 15 = narrow run identity + streaming/cancellation seam pass (`cpp/MILESTONE15_BOUNDARIES.md`).
- Milestone 16 = narrow TUI workflow parity basics pass with adapter-state seams and no runtime ownership migration (`cpp/MILESTONE16_BOUNDARIES.md`).
- Milestone 32 = narrow headless CLI/config/tool polish parity (`cpp/MILESTONE32_BOUNDARIES.md`) with `--cwd`/`--agent`/`--trust`, environment defaults, builtin-agent defaults, and core-tool schema/output polish.
- Milestone 33 = deterministic non-live headless integration proof (`cpp/MILESTONE33_BOUNDARIES.md`) across workspace/trust, resume/recovery, queue turns, budget NDJSON, and event ordering.
- Milestone 34 = final hardening/evidence milestone for the scoped C++ backend/headless migration lane (`cpp/MILESTONE34_BOUNDARIES.md`) with small production hardening fixes, broader local validation, docs consistency, and residual-risk inventory without new feature breadth.
- Milestone 35 = narrow permission-classification parity hardening (`cpp/MILESTONE35_BOUNDARIES.md`) with additional critical deletion-path detections plus parser-differential heuristics (IFS, dangerous brace expansion, ANSI-C quoting, unicode whitespace) while full tree-sitter policy parity/persistent rules/audit stores/plugin hooks remain deferred.
- Milestone 36 = narrow MCP runtime breadth (`cpp/MILESTONE36_BOUNDARIES.md`) with resources/prompts protocol methods and bounded MCP result projection over the existing synchronous transport seam; HTTP/SSE, OAuth, binary blobs, custom TOML tools, and UI wiring remain deferred.
- Milestone 39 = narrow MCP remote-transport guardrails (`cpp/MILESTONE39_BOUNDARIES.md`) with parsed `http`/`sse` server declarations, safe auth metadata validation, inline-secret rejection, and fail-closed runtime reporting as the pre-execution baseline.
- Milestone 40 = narrow MCP remote HTTP execution (`cpp/MILESTONE40_BOUNDARIES.md`) with POST-only JSON-RPC over `TransportType::Http` when `AVA_WITH_CPR=ON`; SSE execution, OAuth lifecycle flows, retries/sessions, response batches, and UI/runtime parity remain deferred.
- Deferred work remains tracked in milestone boundary docs (task-tool parity, MCP/plugin-manager parity, async/background spawn ownership, and broader runtime-streaming parity).
- The current `ava_agent` slice is intentionally useful but not yet parity with the full Rust runtime behavior stack.
- No claim of full Rust behavior parity yet for async runtime, auth-heavy surfaces, or broader backend execution stack.
