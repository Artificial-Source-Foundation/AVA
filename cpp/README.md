# C++ Workspace Foundation (Milestone 12)

This `cpp/` tree is the **Milestone 12 foundational-runtime + bounded FTXUI TUI validation/cleanup slice** for the Rust-to-C++ backend/TUI migration plan.

It remains intentionally scoped and honest:

- It creates a real CMake workspace and dependency wiring.
- It now includes real foundational implementations in `ava_types`, `ava_control_plane`, `ava_platform`, `ava_config`, `ava_session`, `ava_llm`, `ava_tools`, a scoped runtime-core `ava_agent`, and a smallest-honest `ava_orchestration` contracts/data slice.
- The current `ava_runtime` aggregate includes all implemented Milestone 10 foundations, Milestone 11 adds a smallest-honest interactive terminal lane (`ava_tui`) on top of that blocking runtime stack, and Milestone 12 tightens that same lane with bounded parity-validation/cleanup work.
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

1. `ava_cpp_tests` (Catch2-based unit tests for type/control-plane/platform/config/session foundations)
2. `ava_llm_tests` (leaf-target Catch2 tests for the scoped `ava_llm` slice)
3. `ava_tools_tests` (leaf-target Catch2 tests for the scoped `ava_tools` slice)
4. `ava_agent_tests` (leaf-target Catch2 tests for the scoped `ava_agent` runtime-core slice)
5. `ava_orchestration_tests` (leaf-target Catch2 tests for the scoped `ava_orchestration` contracts/data slice)
6. `ava_app_tests` (focused Catch2 tests for Milestone 9 headless CLI parsing/session/event seams)
7. `ava_app_integration_tests` (focused Milestone 10 integration tests for scripted tool loops, approval rejection, and optional env-gated live-provider smoke)
8. `ava_tui_tests` (focused Milestone 11 + 12 tests for bounded TUI state/event/scroll behavior)

## Milestone 12 Implemented Foundations

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

3. **`ava_platform`**
   - Real blocking local filesystem primitives (`read_file`, `write_file`, `create_dir_all`, `exists`, `is_directory`, `metadata`)
   - Shared DTOs for `FileInfo`, `CommandOutput`, and `ExecuteOptions`
    - Preserved `platform_tag()`
    - Explicitly deferred command-execution runtime behavior for a later milestone

4. **`ava_config`**
    - XDG + legacy-aware app path resolution for config/data/state/cache
    - Trusted-project persistence (`trusted_projects.json`) with process-cache invalidation
    - JSON credential store persistence with provider env-override precedence
    - Embedded model registry fixture with alias normalization/pricing/loop-prone helpers
    - File-backed config/trust/credential persistence routed through the current `ava_platform` filesystem primitives

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
     - One real production provider implementation for this milestone: `OpenAI` (blocking HTTP + SSE chunk collection via CPR when enabled).

7. **`ava_tools` (scoped Milestone 6 core-tool-system slice)**
     - Real tool registry with tool interface, tool metadata/schema exposure, tier/source tracking, middleware chain, and call-id normalization.
     - Rust-aligned retry helper behavior for retryable read-only tools (`MAX_RETRIES=2`, backoff `100ms/200ms`, transient/permanent heuristics).
     - Simplified permission middleware seam with explicit fail-closed behavior when approval is required but no approval bridge exists.
     - Current Milestone 6 tool execution remains intentionally local to `ava_tools`; deeper unification of file/process execution behind `ava_platform` is deferred.
     - Real core tools for `read`, `write`, `edit` (narrow exact/replace-all strategy set), `bash`, `glob`, `grep`, `git`, and `git_read`.
      - Honest default registration boundary: `web_fetch`/`web_search` are deferred and not registered as fake defaults in this milestone.

8. **`ava_agent` (scoped Milestone 7 runtime-core slice)**
     - Minimal agent runtime loop capable of prompt assembly, provider turn execution, tool-call parsing, tool execution, session transcript mutation, and bounded completion.
     - Deterministic event emission seam (`AgentEvent`) for future headless/TUI consumers.
     - Practical minimal message queue and stuck detector baselines adapted from Rust intent.
      - Blocking execution model only; async/streaming/orchestration parity is deferred.

9. **`ava_orchestration` (scoped Milestone 8 contracts/data slice)**
     - Real C++ orchestration library under `cpp/include/ava/orchestration` + `cpp/src/orchestration`.
     - Runtime-profile/catalog helpers: `MAX_AGENT_DEPTH`, `SubAgentRuntimeProfile`, `runtime_profile_for(...)`, and a non-mutating profile-aware tool filtering seam via `apply_runtime_profile_to_registry(...)`.
      - Prompt and subagent-definition helpers: `build_subagent_system_prompt(...)`, `EffectiveSubagentDefinition`, and `effective_subagent_definitions(...)` over config-owned agent/default DTOs from `ava_config`.
     - Config seam parity helper: `parse_model_spec(...)` with known-provider + model-registry inference fallback.
      - Lightweight stack/task DTO contracts only: `AgentStackConfig`, `AgentRunResult`, `TaskResult`, and `TaskSpawner` (plus `NoopTaskSpawner` for test utility).
      - Intentionally no full `AgentStack` runtime port, no MCP/plugin runtime composition, and no async/background subagent execution ownership.

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

10. **`ava_tui` (scoped Milestone 11 interactive slice + Milestone 12 bounded cleanup)**
       - Real `ava_tui` executable under `cpp/apps/ava_tui/` built on FTXUI when linked.
       - Minimal app state for a scrollable message list, single-line text composer buffer, status line, and quit action.
       - Blocking event loop that accepts keyboard input, submits prompts to existing blocking `ava_agent` runtime on a worker thread, and consumes runtime events through the existing callback/event-sink seam.
       - Milestone 12 tightening pass: stronger `AppState` event/status mapping and focused edge-case coverage (empty input, backspace on empty, page scrolling clamps, multiline + trailing-newline submission behavior).
       - Integration reused from current foundations (`ava_session`, `ava_llm`, `ava_tools`, `ava_agent`) with no new runtime architecture.
       - Intentionally narrow keyboard scope: type text, Enter submit, Up/Down/PgUp/PgDn scroll, `q` quit.

## Build

```bash
cmake -S cpp -B build/cpp -DCMAKE_BUILD_TYPE=Debug
cmake --build build/cpp -j
```

Key bootstrap options:

- `AVA_BUILD_TESTS` (default `ON`)
- `AVA_BUILD_SMOKE` (default `ON`)
- `AVA_WITH_FTXUI` / `AVA_WITH_CPR` (default `OFF`; link only if package resolution succeeds)

## Run

```bash
./build/cpp/apps/ava_cli --version
./build/cpp/apps/ava_cli --smoke
./build/cpp/apps/ava_cli "Summarize this repository"
./build/cpp/apps/ava_cli "Continue from latest session" --continue
./build/cpp/apps/ava_cli "Use this exact session" --session <session-id>
./build/cpp/apps/ava_cli "Emit NDJSON" --json
./build/cpp/apps/ava_cli "Allow mutating tools" --auto-approve
./build/cpp/apps/ava_tui --auto-approve
./build/cpp/apps/ava_smoke
ctest --test-dir build/cpp --output-on-failure
```

Headless CLI flags (Milestone 9 lane retained in M10):

- positional goal
- `--provider`
- `--model`
- `--continue`
- `--session`
- `--json`
- `--max-turns`
- `--auto-approve`
- `--version`, `--smoke`

Optional live-provider smoke (only when explicitly enabled):

```bash
AVA_LIVE_PROVIDER_TESTS=1 OPENAI_API_KEY=... ctest --test-dir build/cpp -R ava_app_integration --output-on-failure
```

By default, live-provider tests skip cleanly when env gates are not set.

## Scope Guardrails

- Milestone 11 = Milestone 10 foundations plus a smallest-honest FTXUI interactive terminal slice.
- Milestone 12 = bounded validation/cleanup on that same TUI slice (no broad feature expansion).
- Deferred work is tracked in `cpp/MILESTONE12_BOUNDARIES.md` (full modal/sidebar/task UX, mouse/voice/watch, subagent/background panes, autocomplete/slash UX, streaming parity, and advanced layout/theme/approval UX).
- The current `ava_agent` slice is intentionally useful but not yet parity with the full Rust runtime behavior stack.
- No claim of full Rust behavior parity yet for async runtime, auth-heavy surfaces, or broader backend execution stack.
