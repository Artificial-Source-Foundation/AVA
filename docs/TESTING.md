# AVA Testing

## Normal Test Run

```sh
cmake -S . -B build -DAVA_BUILD_TESTS=ON
scripts/build.sh --build-dir build
scripts/run-tests.sh --build-dir build
```

Preset equivalent:

```sh
cmake --preset dev
scripts/build.sh
scripts/run-tests.sh
```

The build and test runners default to the `build` tree, detect the available logical cores, and supply that positive job count to CMake/CTest. Use `--jobs N`, `CMAKE_BUILD_PARALLEL_LEVEL=N`, or `CTEST_PARALLEL_LEVEL=N` to cap concurrency; append build options such as `--target` or CTest options such as `-R`. They share a build-tree safety lock so builds, fixed integration-test roots, and CTest logs cannot collide. An interrupted or untrappably terminated wrapper leaves `.ava-build-tree.lock.d` fail-closed because detached descendants cannot be ruled out; verify that no build/test worker remains before removing that directory manually.

Do not use `cmake --build build --target test --parallel N` to request parallel test execution: it only parallelizes the build tool around one CTest command and does not propagate `N` to CTest. Use `scripts/run-tests.sh --build-dir build --jobs N` for the locked runner, or `ctest --test-dir build --parallel N` when directly diagnosing CTest behavior without the script's build-tree safety lock.

The test suite is built as one `ava_tests` CTest target from focused test sources under `tests/`. The LSP and MCP tests also build and use fake servers from `tests/support/`.

Plugin/MCP contract changes should also follow [`docs/plugin-compatibility-policy.md`](plugin-compatibility-policy.md). Keep checked-in golden fixtures small and deterministic under `tests/golden/ava-080/`, and prefer existing `ava_tests` plugin/MCP suites for contract assertions.

Focused release provenance/package tests are offline and deterministic:

```sh
scripts/run-tests.sh -R '^ava_release\.(provenance|install_component|package_linux)$'
scripts/package-linux.sh --require-release-qualified --output-dir /absolute/path/outside/AVA
```

The strict package command creates a fresh private Release build tree with Gitache/libcwd disabled, the pinned in-tree nlohmann JSON source selected, and CMake FetchContent fully disconnected. It rejects supplied binaries, dirty/mismatched source dependencies, non-x86_64 output, and unexpected ELF dynamic dependencies. Temporary, work, and publication directories are classified against the checkout by descriptor ancestry and device/inode identity, so invoking the script through a symlink cannot make a physically in-checkout directory appear external; logical source paths remain unchanged. Publication rechecks the already-opened output descriptor's ancestry before mutation and at namespace revalidation points. Static symlink, directory-identity, ownership, mode, and no-replace guarantees remain mandatory. A malicious process running concurrently under the same effective UID is outside this packaging threat model because it can directly alter the checkout and release inputs; this exclusion does not weaken those static guarantees. Ordinary `--binary` package workflows remain available but their packaged `PROVENANCE.json` is explicitly unqualified; no signing, SBOM, or attestation is implied.

Privacy-safe diagnostics have focused unit and full-binary coverage:

```sh
scripts/run-tests.sh --build-dir build -R '^ava_tests\.(diagnostics|runtime_diagnostics|mcp|plugin)$' --output-on-failure
scripts/run-tests.sh --build-dir build -R '^ava_cli\.(doctor_support|runtime_diagnostics)$' --output-on-failure
```

These suites use canary secrets and isolated XDG roots to verify passive doctor behavior, private descriptor-safe storage, bounded identity-aliased traces, drop/failure counters, typed last-failure records, unique support publication, MCP/plugin public-failure sanitization, and absence of raw values from model/session/RPC/export/support surfaces. ACP trace grammar is covered by `ava_cli.runtime_diagnostics` and the ordinary ACP tests. No live provider or network access is required.

## Headless Tool Smoke

After provider streaming, tool schema, permission, or dispatcher changes, run a live headless smoke with configured provider auth when credentials are available. Keep the workspace isolated under a temporary directory outside the repository so mutating tools do not touch the checkout.

Recommended coverage:

- `ava --print ... --json --allow read-only` for `read_file`, `list_directory`, `glob`, and `grep`. This verifies provider tool-call streaming, read/search permission auto-allow, `.gitignore` behavior, and tool progress events.
- `ava --print ... --json --allow-tool skill` for `skill` when a test skill is available. This verifies the explicit `skill` headless allow path and bounded skill loading behavior.
- `ava --print ... --json --allow-tool task` for `task` when a small subagent prompt is available. This verifies the explicit `task`/`TaskRun` headless allow path, child-session creation, recursive-task hiding, and background job metadata when `background:true` is used.
- `ava --print ... --json --allow-tool webfetch` for `webfetch`. This verifies the explicit `network.fetch` headless allow path and real bounded HTTP fetch behavior.
- `ava --print ... --json --allow-tool websearch` for `websearch`. This verifies the explicit `network.search` headless allow path and bounded search result shaping.
- `ava --print ... --json` without `--allow-tool webfetch` for a prompt that asks for `webfetch`. This verifies `network.fetch` ask prompts fail closed by the default headless resolver; workspace-policy allow decisions may still proceed without prompting.
- `ava --rpc` with a small JSONL harness that answers `permission_requested` with `permission_reply` for `write_file`, `edit_file`, `apply_patch`, and `bash`. The checked-in headless bash cleanup smoke verifies that timed-out shell process groups do not leave a child process behind.
- `scripts/run-tests.sh -R '^ava_cli\.headless_e2e_model_smoke$'` for the full-binary fake-provider coding-agent smoke. This is the default release gate for provider request/response handling, sequential tool dispatch, RPC permission replies, session persistence, provider continuation requests with tool results, replay through `--continue`, and clean exit in one run.
- `ava --rpc` with `question_reply` for the `question` tool.
- `ava --rpc` command-registry smokes for `list_commands` and `invoke_command` across prompt commands, skills, plugin commands, and MCP prompt commands.
- `ava --rpc` plugin/MCP diagnostics smokes for plugin discovery/validation/static resources/enablement/fail-closed execution, MCP server list/inspect/restart, invalid MCP config containment, and fail-closed MCP tool discovery without a TUI resolver. `ava_cli.headless_rpc_sample_plugin` copies `examples/plugins/todo/` into an isolated project plugin directory and verifies the real sample's discovery, resources, enable/disable flow, and fail-closed command execution without starting the sample process.
- `./build/ava_tests plugin` after plugin authoring changes. The plugin suite validates the checked-in sample under `examples/plugins/todo/` through source-path fixture plumbing instead of duplicating the sample manifest or protocol JSON, and remains the coverage for successful sample entrypoint execution.
- `./build/ava_tests mcp` after MCP contract changes. The MCP suite uses the local fake MCP server and golden fixtures for representative tool schema, resource read, and audit shapes; MCP resource behavior must stay behind explicit read-style permission coverage.

`lsp_diagnostics` is capability-gated in normal headless runtime. `ava_tests.lsp` uses the stable fake server to cover default-off/global-only exact `clangd` opt-in, rejection of automatic `gopls`/`rust-analyzer`, executable hardlink/replacement rejection, replacement-sensitive launch permission identity, logical per-root cache deduplication, pull and routed publish diagnostics, full-text versioned `didChange`, cache bounds, malformed/out-of-workspace notifications, absolute deadlines, cancellation, environment filtering, and cleanup without downloads or provider calls. `ava_tests.lsp_real_clangd_smoke` is an explicitly opt-in offline real-server smoke. By default it returns CTest skip code 77 even when `clangd` is installed. When opted in, it discovers an already-installed safe `clangd`, uses no credentials or network access, runs against a private finite fixture, proves initialization plus definition, and cleans up; it also skips when `clangd` is absent or unsafe. It never installs or downloads clangd.

```sh
scripts/run-tests.sh --build-dir build -R '^ava_tests\.lsp$'
AVA_LSP_REAL_CLANGD_SMOKE=1 scripts/run-tests.sh --build-dir build -R '^ava_tests\.lsp_real_clangd_smoke$'
```

## End-To-End AVA Tool Smoke

The deterministic full-tool smoke is part of default CTest:

```sh
scripts/run-tests.sh -R '^ava_cli\.headless_e2e_model_smoke$'
```

The smoke starts the built `ava --rpc` binary against `ava_fake_provider_server`, seeds a temporary workspace under the build tree, and drives `read_file`, `grep`, `list_directory`, `apply_patch`, and `bash` in one provider-backed turn. It uses `--allow read-only` only for read/search prompts; the outside-temp edit target and verification command must be resolved through RPC `permission_reply`. Assertions cover tool lifecycle events, successful tool results, permission events and persisted `permission_decision` session entries, `get_session_stats`, `validate_session`, `get_messages`, provider request-log continuation with prior tool results, and replay through `ava --rpc --continue`. Subagent/task and background job coverage lives in `ava_tests.agent_loop`; run that suite after changing `task`, subagent config, or `BackgroundJobRegistry` behavior.

Latest local result: 2026-07-05 `cmake --build --preset dev --target ava ava_fake_provider_server ava_tests`, `ctest --test-dir build -R '^ava_cli\.headless_e2e_model_smoke$' --output-on-failure`, `ctest --preset dev --output-on-failure`, and `git --no-pager diff --check` passed. The full default CTest run passed 59/59 with expected skips for credential-gated provider live smoke and opt-in TUI PTY smokes.

Opt-in full-binary live dogfood is intentionally separate from the default release gate:

```sh
AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-model-dogfood.sh
```

Set `AVA_EXE=/path/to/ava` when the binary is not `./build/ava`. Set `AVA_LIVE_DOGFOOD_ROOT=/tmp/ava-live-dogfood` to choose the temporary workspace, and `AVA_LIVE_DOGFOOD_KEEP=1` when the RPC logs should remain available for review.

The live dogfood script uses the first configured provider credential from the provider live-smoke matrix, writes an isolated `models.json`, starts `ava --rpc --allow read-only`, asks the live model to read `src/live-smoke.txt` with `read_file`, then validates session stats/messages. It prints one classification: `passed`, `skipped/not opted in`, `skipped/no credential`, `credential/auth-blocked`, `provider/rate-limited`, `network-blocked`, `provider-behavior/inconclusive`, or `AVA regression`. Treat provider-behavior/inconclusive as live dogfood evidence that the deterministic fake-provider E2E remains the release gate, not as a release blocker by itself.

Latest local result: 2026-07-05 `AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-model-dogfood.sh` returned `classification=passed` against DeepSeek after the smoke was fixed to run from its isolated workspace.

Live coding dogfood exercises a broader coding-agent loop with a real provider:

```sh
AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-coding-dogfood.sh
```

Set `AVA_EXE=/path/to/ava` when the binary is not `./build/ava`. Set `AVA_LIVE_CODING_DOGFOOD_ROOT=/tmp/ava-live-coding-dogfood` to choose the evidence root, and `AVA_LIVE_CODING_DOGFOOD_KEEP=1` when the RPC output, stderr, workspace, and session files should remain available for review.

The live coding dogfood script creates an isolated workspace, trusts a project-local `coding-smoke` skill, asks the live model to load that skill, read a target file, apply an exact edit through `apply_patch` or `edit_file`, and reply with the marker. It validates permission prompts/replies, successful tool results, the mutated file, session JSONL `tool_call`/`tool_result`/`permission_decision` entries, and `validate_session` output. It uses the same classification vocabulary as `live-model-dogfood.sh`; classify model non-compliance separately from AVA-owned regressions.

## Sanitizers

```sh
cmake -S . -B build-sanitize -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON
scripts/build.sh --build-dir build-sanitize --jobs 2
scripts/run-tests.sh --build-dir build-sanitize --jobs 2
```

Preset equivalent:

```sh
cmake --preset sanitize
scripts/build.sh --build-dir build-sanitize --jobs 2
scripts/run-tests.sh --build-dir build-sanitize --jobs 2
```

The explicit sanitizer cap avoids multiplying ASan/UBSan memory demand on smaller CI and developer hosts; raise it locally after measuring available memory.

The sanitizer preset enables AddressSanitizer and UndefinedBehaviorSanitizer for supported non-MSVC builds.

## Formatting And Static Checks

Format changed C++ files with the repository `.clang-format`:

```sh
clang-format -i <changed-cpp-or-header-files>
```

Run clang-tidy against changed implementation files after configuring the build:

```sh
clang-tidy <changed-cpp-files> -p build
```

Before handing work off, check for whitespace and patch-format issues:

```sh
git --no-pager diff --check
```

## Coverage Areas

The product-level Pi-parity checklist is mapped to automated suites, CLI/RPC smokes, opt-in terminal smokes, live-provider smokes, or explicit manual/docs evidence in [`docs/product/mvp-coverage-ledger.md`](product/mvp-coverage-ledger.md). Keep that ledger current when a checklist row changes from partial/deferred to present.

### Pi MVP Parity Release Evidence

Pi's reference tree has broad Jest/Vitest coverage across provider protocols, coding-agent sessions, settings/packages, export, and a TypeScript virtual-terminal harness. AVA's MVP evidence uses CTest plus explicit opt-in smokes instead of copying Pi's harness architecture:

| Evidence area | AVA evidence | Release rule |
| --- | --- | --- |
| Provider protocol regressions | `ava_tests.provider_openai`, `ava_tests.provider_anthropic`, `ava_tests.provider_gemini`, `ava_tests.config_context_auth_oauth`, and `ava_tests.provider_live_smoke` | Local protocol tests must pass. Live provider cases are credential-gated and classified in the matrix below. |
| Headless/RPC automation | `ava_cli.headless_print_*`, `ava_cli.headless_rpc_*`, `ava_cli.headless_tool_visibility`, `ava_cli.headless_performance_smoke`, `ava_cli.headless_e2e_model_smoke` | Fake-provider smokes must pass in default CTest; live credentials are not required. |
| Subagent/background jobs | `ava_tests.agent_loop` task-subagent and `BackgroundJobRegistry` tests; opt-in live task/coding dogfood when credentials are present | Foreground/background task delegation, custom subagent config, cancellation, retained job snapshots, and explicit `--allow-tool task` behavior must remain covered by deterministic tests. |
| TUI/editor/renderer | `ava_tests.tui_composer` plus gated `ava_tui.tmux_smoke_*`, `ava_tui.kitty_image_smoke`, and `ava_tui.osc8_smoke` | Deterministic renderer/editor tests are required; PTY smokes are run when prerequisites exist and otherwise skip with code 77. |
| Virtual-terminal decision | AVA does not add a Pi-style TypeScript virtual terminal for MVP. Renderer tests assert visible rows/widths and tmux/PTY captures assert real terminal behavior. | Revisit a screen-model parser only if tmux/PTY smoke flakes or cannot cover a terminal protocol that renderer tests cannot prove. |
| Performance thresholds | `ava_tests.tui_composer` large-render budgets and `ava_cli.headless_performance_smoke` | Treat budget failures as release regressions unless the threshold is intentionally raised with profiling evidence. |
| Side-effect safety | [`docs/engineering/side-effect-safety-checklist.md`](engineering/side-effect-safety-checklist.md) | New side-effect classes must answer the permission/audit/cancellation/output-bound/test questions before release. |
| Documentation consistency | `README.md`, `docs/USAGE.md`, `docs/CONFIG.md`, `docs/headless-protocol.md`, product docs, and area logs | A broad MVP cut must rerun `git --no-pager diff --check` and reconcile checked rows with the coverage ledger. |

### Provider Live-Smoke Matrix

Run live smokes only when credentials are intentionally present in the environment:

```sh
AVA_LIVE_PROVIDER_SMOKE=1 scripts/run-tests.sh -R provider_live_smoke
AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-model-dogfood.sh
AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-coding-dogfood.sh
```

`ava_tests.provider_live_smoke` verifies provider transport/connectivity. `scripts/live-model-dogfood.sh` is the opt-in full-binary dogfood path for CLI/RPC startup, agent loop, read-only tool use, permission policy, and session persistence with a live model. `scripts/live-coding-dogfood.sh` extends that coverage to skill loading, file mutation, edit permission prompts, and persisted coding-session evidence. Both dogfood scripts use the shared provider selector in `scripts/live-provider-selection.sh` so the first available OpenAI, Anthropic, DeepSeek, Gemini, Kimi, Moonshot, or OpenRouter credential is handled consistently.

| Provider | Credential env | Default model / override | Expected CTest behavior without credentials | Latest local result field |
| --- | --- | --- | --- | --- |
| OpenAI | `OPENAI_API_KEY` | `gpt-4.1-mini` / `AVA_LIVE_OPENAI_MODEL` | Skipped unless gate and key are set | Record `passed`, `skipped/no credential`, `credential/auth-blocked`, `provider/rate-limited`, `network-blocked`, or `AVA regression`. |
| Anthropic API key | `ANTHROPIC_API_KEY` | `claude-sonnet-4-5` / `AVA_LIVE_ANTHROPIC_MODEL` | Skipped unless gate and key are set | Same classification. |
| Anthropic OAuth bearer | `ANTHROPIC_OAUTH_TOKEN` or `ANTHROPIC_AUTH_TOKEN` | `claude-sonnet-4-5` / `AVA_LIVE_ANTHROPIC_MODEL` | Skipped unless gate and token are set | Same classification; interactive Anthropic OAuth remains deferred. |
| DeepSeek | `DEEPSEEK_API_KEY` | `deepseek-v4-flash` / `AVA_LIVE_DEEPSEEK_MODEL` | Skipped unless gate and key are set | Same classification; local tests cover `reasoning_effort=high|max`; live runs classify endpoint/auth/network results. |
| Gemini | `GEMINI_API_KEY` | `gemini-2.5-pro` / `AVA_LIVE_GEMINI_MODEL` | Skipped unless gate and key are set | Same classification; local tests cover native GenerateContent request/response and SSE parsing. |
| Kimi | `KIMI_API_KEY` | `kimi-k2-thinking` / `AVA_LIVE_KIMI_MODEL` | Skipped unless gate and key are set | Same classification. |
| Moonshot | `MOONSHOT_API_KEY` | `kimi-k2.6` / `AVA_LIVE_MOONSHOT_MODEL` | Skipped unless gate and key are set | Same classification. |
| OpenRouter | `OPENROUTER_API_KEY` | `moonshotai/kimi-k2.6` / `AVA_LIVE_OPENROUTER_MODEL` | Skipped unless gate and key are set | Same classification. |

Do not record secret values in release notes. A failed live case is classified as an AVA regression only after credentials, provider availability, model access, and local network reachability are ruled out.

### Performance Release Thresholds

| Path | Current deterministic threshold |
| --- | --- |
| Large TUI transcript redraw | `ava_tests.tui_composer` renders a mixed 900-item transcript at 120-column width within 5 seconds across four redraw passes and preserves requested dimensions/widths. |
| Large tool-output card | `ava_tests.tui_composer` renders collapsed/expanded previews for 20,000 output lines within 2 seconds while using backend total/omitted counts. |
| Very long TUI transcript | `ava_tests.tui_composer` renders a 900+ item transcript frame within 20 seconds and keeps every line width-bounded. |
| Bounded transcript tail parity | `ava_tests.tui_composer` verifies the tail-window renderer matches the full-render visible window for representative scroll offsets. |
| Headless startup/search/replay | `ava_cli.headless_performance_smoke` has a 30 second RPC/search driver timeout and 15 second `--continue` replay timeout using the fake provider. |

The `ava_tests` binary covers:

- mode parsing
- session JSONL storage, resume, listing, corruption handling, and permissions
- XDG path handling
- OpenAI auth loading/storage and OAuth refresh preflight
- model and prompt configuration
- provider request/SSE parsing, including OpenAI Responses function-call starts from `response.output_item.added`, Anthropic native tools/thinking/cache usage, Gemini GenerateContent request/response and SSE vectors, DeepSeek/Kimi/Moonshot-compatible reasoning-content vectors, and OpenRouter-compatible request/error vectors
- permission audit persistence, file/search/bash/webfetch/LSP tools, bash process-group cleanup, spill files, and atomic file writes
- tool dispatcher and agent loop
- command registry discovery/invocation for built-ins, prompt commands, skills, plugin commands, and MCP prompts
- plugin manifest/discovery/enablement, out-of-process plugin runner behavior, plugin tools/commands/static resources/events, diagnostics, containment, failure cases, and the checked-in sample plugin workflow
- MCP stdio config, initialize, tool listing/calls, prompt listing/get, tool broker registration, diagnostics, and fake-server success/error/exit cases
- print mode and JSONL RPC success, denial/recovery, malformed input, cancellation, refresh paths, and TTY-bound terminal-control sanitization
- TUI rendering, input, keybindings, palette, permission prompt, markdown, UTF-8, and scroll helpers

Add regression tests for every safety-sensitive bug fix.

## TUI / ncurses Focused Validation

For TUI-only changes, the focused suite is:

```sh
scripts/build.sh --target ava_tests
./build/ava_tests tui_composer
scripts/run-tests.sh -R "ava_tests.tui_composer"
```

The suite includes the CI-safe ncurses baseline currently available in-tree: configured `ESCDELAY`, escape buffering/discard for CSI/OSC/DCS/bracketed-paste markers, mouse wheel/click mapping at the composer layer, resize stress renders, Unicode/CJK/combining/emoji width and cursor placement, `newterm` smokes for xterm/screen terminfo plus tmux/kitty/wezterm/ssh-like environment variables without a real TTY, and large/very-long transcript performance budgets.

Real terminal coverage exists as prerequisite-gated CTest smokes. The historical
pre-F1 F0 semantic inventory, four-row shell observations, artifact policy, and
future evidence gaps are recorded in `docs/roadmap/frontend-evidence-baseline.md`.
The current `main_startup_trust_keybinds` scenario generates four F1
plain-text composer captures below its evidence root: `160x48`
(`frontend-f1-wide-idle-composer.txt`, input/footer rows 46/47), `120x36`
(`frontend-f1-ordinary-idle-composer.txt`, rows 34/35), `80x24`
(`frontend-f1-narrow-idle-composer.txt`, rows 22/23), and `100x12`
(`frontend-f1-short-idle-composer.txt`, rows 10/11). Rows are zero-based.

```sh
AVA_TUI_TMUX_SMOKE=1 scripts/run-tests.sh --jobs 13 -R '^ava_tui\.tmux_smoke_'
AVA_TUI_KITTY_IMAGE_SMOKE=1 scripts/run-tests.sh -R ava_tui.kitty_image_smoke
AVA_TUI_OSC8_SMOKE=1 scripts/run-tests.sh -R ava_tui.osc8_smoke
```

The tmux family dispatches 13 independent scenarios. Each gets a guarded leaf under `build/tui-tmux-smoke/<scenario>/`, its own HOME/XDG/workspace, private config-free tmux socket, and separate evidence directory at `build/tui-tmux-smoke/<scenario>/evidence/`. Drivers enforce a 50-second internal deadline, clean private tmux/provider process groups on SIGINT or SIGTERM, and receive a 10-second graceful-cleanup window before CTest's 60-second outer timeout. The fake-provider request logs remain at `build/tui-tmux-smoke/active_run/active-provider-requests.log` and `build/tui-tmux-smoke/restore_followup/restore-provider-requests.log`.

The MVP strategy is renderer/editor reducers first, then PTY/tmux assertions for terminal protocols and cleanup. AVA intentionally does not require a separate virtual-terminal parser for MVP; add one only if focused renderer tests plus the existing PTY smokes stop providing stable evidence.

## 0.90 Release-Candidate Checklist

Before treating a 0.90 release-candidate audit as complete, run and record:

```sh
scripts/build.sh --target ava ava_tests
scripts/run-tests.sh
scripts/run-tests.sh -R "ava_tests\.(session|agent_loop|agent_loop_resilience|app_print|app_runtime|app_command_classification|config_context_auth_oauth|provider_openai|provider_anthropic|provider_gemini|app_command_registry|app_compaction|core_json_permission|plugin|mcp)|ava_cli\.headless_rpc_"
git --no-pager diff --check
```

When C++ or CTest behavior changed, also run a sanitizer build/test, using the preset when it works or the direct build directory fallback:

```sh
scripts/build.sh --build-dir build-sanitize --jobs 2 --target ava_tests
scripts/run-tests.sh --build-dir build-sanitize --jobs 2
```

Run `clang-format` on changed C++ files and `clang-tidy <changed-cpp-files> -p build` when `clang-tidy` is available in PATH. If it is unavailable, record that environment limitation in the release journal.

Use `docs/versions/0.90.md` as the release-candidate test evidence map. It maps 1.0 capability areas to `ava_tests` suite names, `ava_cli.headless_rpc_*` scripts, golden fixtures, and live/manual smoke expectations.

For provider live smokes, use environment credentials only; the smoke suite intentionally does not read or print auth files. The CTest suite `ava_tests.provider_live_smoke` is skipped by default; opt in with `AVA_LIVE_PROVIDER_SMOKE=1` plus one or more provider credentials such as `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`, `ANTHROPIC_OAUTH_TOKEN`, `ANTHROPIC_AUTH_TOKEN`, `DEEPSEEK_API_KEY`, `GEMINI_API_KEY`, `KIMI_API_KEY`, `MOONSHOT_API_KEY`, or `OPENROUTER_API_KEY`. Optional model overrides are `AVA_LIVE_OPENAI_MODEL`, `AVA_LIVE_ANTHROPIC_MODEL`, `AVA_LIVE_DEEPSEEK_MODEL`, `AVA_LIVE_GEMINI_MODEL`, `AVA_LIVE_KIMI_MODEL`, `AVA_LIVE_MOONSHOT_MODEL`, and `AVA_LIVE_OPENROUTER_MODEL`.

Record each provider case as one of: passed, skipped/no credential, credential/auth-blocked, provider/rate-limited, network-blocked, or AVA regression. The full-binary live dogfood script may also report provider-behavior/inconclusive when the model returns a valid response but does not perform the requested read-only tool call. A provider-breadth release claim needs the focused provider suites (`ava_tests.config_context_auth_oauth`, `ava_tests.provider_openai`, `ava_tests.provider_anthropic`, `ava_tests.provider_gemini`, `ava_tests.provider_live_smoke`) plus a dated matrix of the enabled live cases, model ids, result classifications, and whether any failure is credentials/provider/network/AVA-owned. Anthropic interactive OAuth is not a live-smoke requirement because Anthropic does not document a third-party authorization or device flow; use `ANTHROPIC_API_KEY`, `ANTHROPIC_OAUTH_TOKEN`, or `ANTHROPIC_AUTH_TOKEN` only when the credential is already available. Historical 0.90 evidence had OpenAI and Kimi-for-coding live-smoked, with Kimi-for-coding accepted as the additional production-quality provider path at that release-candidate cut; current provider-breadth claims should use the matrix runner and classify every enabled live case separately.
