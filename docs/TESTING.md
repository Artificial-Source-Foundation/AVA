# AVA Testing

## Normal Test Run

```sh
cmake -S . -B build -DAVA_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Preset equivalent:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The test suite is built as one `ava_tests` CTest target from focused test sources under `tests/`. The LSP and MCP tests also build and use fake servers from `tests/support/`.

Plugin/MCP contract changes should also follow [`docs/plugin-compatibility-policy.md`](plugin-compatibility-policy.md). Keep checked-in golden fixtures small and deterministic under `tests/golden/ava-080/`, and prefer existing `ava_tests` plugin/MCP suites for contract assertions.

## Headless Tool Smoke

After provider streaming, tool schema, permission, or dispatcher changes, run a live headless smoke with configured provider auth when credentials are available. Keep the workspace isolated under a temporary directory outside the repository so mutating tools do not touch the checkout.

Recommended coverage:

- `ava --print ... --json --allow read-only` for `read_file`, `list_directory`, `glob`, and `grep`. This verifies provider tool-call streaming, read/search permission auto-allow, `.gitignore` behavior, and tool progress events.
- `ava --print ... --json --allow-tool skill` for `skill` when a test skill is available. This verifies the explicit `skill` headless allow path and bounded skill loading behavior.
- `ava --print ... --json --allow-tool webfetch` for `webfetch`. This verifies the explicit `network.fetch` headless allow path and real bounded HTTP fetch behavior.
- `ava --print ... --json --allow-tool websearch` for `websearch`. This verifies the explicit `network.search` headless allow path and bounded search result shaping.
- `ava --print ... --json` without `--allow-tool webfetch` for a prompt that asks for `webfetch`. This verifies `network.fetch` ask prompts fail closed by the default headless resolver; workspace-policy allow decisions may still proceed without prompting.
- `ava --rpc` with a small JSONL harness that answers `permission_requested` with `permission_reply` for `write_file`, `edit_file`, `apply_patch`, and `bash`. The checked-in headless bash cleanup smoke verifies that timed-out shell process groups do not leave a child process behind.
- `ctest --test-dir build -R '^ava_cli\.headless_e2e_model_smoke$' --output-on-failure` for the full-binary fake-provider coding-agent smoke. This is the default release gate for provider request/response handling, sequential tool dispatch, RPC permission replies, session persistence, provider continuation requests with tool results, replay through `--continue`, and clean exit in one run.
- `ava --rpc` with `question_reply` for the `question` tool.
- `ava --rpc` command-registry smokes for `list_commands` and `invoke_command` across prompt commands, skills, plugin commands, and MCP prompt commands.
- `ava --rpc` plugin/MCP diagnostics smokes for plugin discovery/validation/static resources/enablement/fail-closed execution, MCP server list/inspect/restart, invalid MCP config containment, and fail-closed MCP tool discovery without a TUI resolver. `ava_cli.headless_rpc_sample_plugin` copies `examples/plugins/todo/` into an isolated project plugin directory and verifies the real sample's discovery, resources, enable/disable flow, and fail-closed command execution without starting the sample process.
- `./build/ava_tests plugin` after plugin authoring changes. The plugin suite validates the checked-in sample under `examples/plugins/todo/` through source-path fixture plumbing instead of duplicating the sample manifest or protocol JSON, and remains the coverage for successful sample entrypoint execution.
- `./build/ava_tests mcp` after MCP contract changes. The MCP suite uses the local fake MCP server and golden fixtures for representative tool schema, resource read, and audit shapes; MCP resource behavior must stay behind explicit read-style permission coverage.

`lsp_diagnostics` is capability-gated in normal headless runtime. Verify it through `ava_tests` and the fake LSP server unless a local diagnostics provider is configured for a live run.

## End-To-End AVA Tool Smoke

The deterministic full-tool smoke is part of default CTest:

```sh
ctest --test-dir build -R '^ava_cli\.headless_e2e_model_smoke$' --output-on-failure
```

The smoke starts the built `ava --rpc` binary against `ava_fake_provider_server`, seeds a temporary workspace under the build tree, and drives `read_file`, `grep`, `list_directory`, `apply_patch`, and `bash` in one provider-backed turn. It uses `--allow read-only` only for read/search prompts; the outside-temp edit target and verification command must be resolved through RPC `permission_reply`. Assertions cover tool lifecycle events, successful tool results, permission events and persisted `permission_decision` session entries, `get_session_stats`, `validate_session`, `get_messages`, provider request-log continuation with prior tool results, and replay through `ava --rpc --continue`.

Latest local result: 2026-07-05 `cmake --build --preset dev --target ava ava_fake_provider_server ava_tests`, `ctest --test-dir build -R '^ava_cli\.headless_e2e_model_smoke$' --output-on-failure`, `ctest --preset dev --output-on-failure`, and `git --no-pager diff --check` passed. The full default CTest run passed 59/59 with expected skips for credential-gated provider live smoke and opt-in TUI PTY smokes.

Opt-in full-binary live dogfood is intentionally separate from the default release gate:

```sh
AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-model-dogfood.sh
```

Set `AVA_EXE=/path/to/ava` when the binary is not `./build/ava`. Set `AVA_LIVE_DOGFOOD_ROOT=/tmp/ava-live-dogfood` to choose the temporary workspace, and `AVA_LIVE_DOGFOOD_KEEP=1` when the RPC logs should remain available for review.

The live dogfood script uses the first configured provider credential from the provider live-smoke matrix, writes an isolated `models.json`, starts `ava --rpc --allow read-only`, asks the live model to read `src/live-smoke.txt` with `read_file`, then validates session stats/messages. It prints one classification: `passed`, `skipped/not opted in`, `skipped/no credential`, `credential/auth-blocked`, `provider/rate-limited`, `network-blocked`, `provider-behavior/inconclusive`, or `AVA regression`. Treat provider-behavior/inconclusive as live dogfood evidence that the deterministic fake-provider E2E remains the release gate, not as a release blocker by itself.

Latest local result: 2026-07-05 `AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-model-dogfood.sh` returned `classification=skipped/no credential`.

## Sanitizers

```sh
cmake -S . -B build-sanitize -DAVA_ENABLE_SANITIZERS=ON -DAVA_BUILD_TESTS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

Preset equivalent:

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

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
| Provider protocol regressions | `ava_tests.provider_openai`, `ava_tests.provider_anthropic`, `ava_tests.config_context_auth_oauth`, and `ava_tests.provider_live_smoke` | Local protocol tests must pass. Live provider cases are credential-gated and classified in the matrix below. |
| Headless/RPC automation | `ava_cli.headless_print_*`, `ava_cli.headless_rpc_*`, `ava_cli.headless_tool_visibility`, `ava_cli.headless_performance_smoke`, `ava_cli.headless_e2e_model_smoke` | Fake-provider smokes must pass in default CTest; live credentials are not required. |
| TUI/editor/renderer | `ava_tests.tui_composer` plus gated `ava_tui.tmux_smoke`, `ava_tui.kitty_image_smoke`, and `ava_tui.osc8_smoke` | Deterministic renderer/editor tests are required; PTY smokes are run when prerequisites exist and otherwise skip with code 77. |
| Virtual-terminal decision | AVA does not add a Pi-style TypeScript virtual terminal for MVP. Renderer tests assert visible rows/widths and tmux/PTY captures assert real terminal behavior. | Revisit a screen-model parser only if tmux/PTY smoke flakes or cannot cover a terminal protocol that renderer tests cannot prove. |
| Performance thresholds | `ava_tests.tui_composer` large-render budgets and `ava_cli.headless_performance_smoke` | Treat budget failures as release regressions unless the threshold is intentionally raised with profiling evidence. |
| Side-effect safety | [`docs/engineering/side-effect-safety-checklist.md`](engineering/side-effect-safety-checklist.md) | New side-effect classes must answer the permission/audit/cancellation/output-bound/test questions before release. |
| Documentation consistency | `README.md`, `docs/USAGE.md`, `docs/CONFIG.md`, `docs/headless-protocol.md`, product docs, and area logs | A broad MVP cut must rerun `git --no-pager diff --check` and reconcile checked rows with the coverage ledger. |

### Provider Live-Smoke Matrix

Run live smokes only when credentials are intentionally present in the environment:

```sh
AVA_LIVE_PROVIDER_SMOKE=1 ctest --test-dir build -R provider_live_smoke --output-on-failure
AVA_LIVE_PROVIDER_SMOKE=1 sh scripts/live-model-dogfood.sh
```

`ava_tests.provider_live_smoke` verifies provider transport/connectivity. `scripts/live-model-dogfood.sh` is the opt-in full-binary dogfood path for CLI/RPC startup, agent loop, read-only tool use, permission policy, and session persistence with a live model.

| Provider | Credential env | Default model / override | Expected CTest behavior without credentials | Latest local result field |
| --- | --- | --- | --- | --- |
| OpenAI | `OPENAI_API_KEY` | `gpt-4.1-mini` / `AVA_LIVE_OPENAI_MODEL` | Skipped unless gate and key are set | Record `passed`, `skipped/no credential`, `credential/auth-blocked`, `provider/rate-limited`, `network-blocked`, or `AVA regression`. |
| Anthropic API key | `ANTHROPIC_API_KEY` | `claude-sonnet-4-5` / `AVA_LIVE_ANTHROPIC_MODEL` | Skipped unless gate and key are set | Same classification. |
| Anthropic OAuth bearer | `ANTHROPIC_OAUTH_TOKEN` or `ANTHROPIC_AUTH_TOKEN` | `claude-sonnet-4-5` / `AVA_LIVE_ANTHROPIC_MODEL` | Skipped unless gate and token are set | Same classification; interactive Anthropic OAuth remains deferred. |
| DeepSeek | `DEEPSEEK_API_KEY` | `deepseek-v4-flash` / `AVA_LIVE_DEEPSEEK_MODEL` | Skipped unless gate and key are set | Same classification; local tests cover `reasoning_effort=high|max`; live runs classify endpoint/auth/network results. |
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
- provider request/SSE parsing, including OpenAI Responses function-call starts from `response.output_item.added`, Anthropic native tools/thinking/cache usage, DeepSeek/Kimi/Moonshot-compatible reasoning-content vectors, and OpenRouter-compatible request/error vectors
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
cmake --build build --target ava_tests
./build/ava_tests tui_composer
ctest --test-dir build --output-on-failure -R "ava_tests.tui_composer"
```

The suite includes the CI-safe ncurses baseline currently available in-tree: configured `ESCDELAY`, escape buffering/discard for CSI/OSC/DCS/bracketed-paste markers, mouse wheel/click mapping at the composer layer, resize stress renders, Unicode/CJK/combining/emoji width and cursor placement, `newterm` smokes for xterm/screen terminfo plus tmux/kitty/wezterm/ssh-like environment variables without a real TTY, and large/very-long transcript performance budgets.

Real terminal coverage exists as prerequisite-gated CTest smokes:

```sh
AVA_TUI_TMUX_SMOKE=1 ctest --test-dir build -R ava_tui.tmux_smoke --output-on-failure
AVA_TUI_KITTY_IMAGE_SMOKE=1 ctest --test-dir build -R ava_tui.kitty_image_smoke --output-on-failure
AVA_TUI_OSC8_SMOKE=1 ctest --test-dir build -R ava_tui.osc8_smoke --output-on-failure
```

The MVP strategy is renderer/editor reducers first, then PTY/tmux assertions for terminal protocols and cleanup. AVA intentionally does not require a separate virtual-terminal parser for MVP; add one only if focused renderer tests plus the existing PTY smokes stop providing stable evidence.

## 0.90 Release-Candidate Checklist

Before treating a 0.90 release-candidate audit as complete, run and record:

```sh
cmake --build build --target ava ava_tests
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -R "ava_tests\.(session|agent_loop|agent_loop_resilience|app_print|app_runtime|app_command_classification|config_context_auth_oauth|provider_openai|provider_anthropic|app_command_registry|app_compaction|core_json_permission|plugin|mcp)|ava_cli\.headless_rpc_"
git --no-pager diff --check
```

When C++ or CTest behavior changed, also run a sanitizer build/test, using the preset when it works or the direct build directory fallback:

```sh
cmake --build build-sanitize --target ava_tests
ctest --test-dir build-sanitize --output-on-failure
```

Run `clang-format` on changed C++ files and `clang-tidy <changed-cpp-files> -p build` when `clang-tidy` is available in PATH. If it is unavailable, record that environment limitation in the release journal.

Use `docs/versions/0.90.md` as the release-candidate test evidence map. It maps 1.0 capability areas to `ava_tests` suite names, `ava_cli.headless_rpc_*` scripts, golden fixtures, and live/manual smoke expectations.

For provider live smokes, use environment credentials only; the smoke suite intentionally does not read or print auth files. The CTest suite `ava_tests.provider_live_smoke` is skipped by default; opt in with `AVA_LIVE_PROVIDER_SMOKE=1` plus one or more provider credentials such as `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`, `ANTHROPIC_OAUTH_TOKEN`, `ANTHROPIC_AUTH_TOKEN`, `DEEPSEEK_API_KEY`, `KIMI_API_KEY`, `MOONSHOT_API_KEY`, or `OPENROUTER_API_KEY`. Optional model overrides are `AVA_LIVE_OPENAI_MODEL`, `AVA_LIVE_ANTHROPIC_MODEL`, `AVA_LIVE_DEEPSEEK_MODEL`, `AVA_LIVE_KIMI_MODEL`, `AVA_LIVE_MOONSHOT_MODEL`, and `AVA_LIVE_OPENROUTER_MODEL`.

Record each provider case as one of: passed, skipped/no credential, credential/auth-blocked, provider/rate-limited, network-blocked, or AVA regression. The full-binary live dogfood script may also report provider-behavior/inconclusive when the model returns a valid response but does not perform the requested read-only tool call. A provider-breadth release claim needs the focused provider suites (`ava_tests.config_context_auth_oauth`, `ava_tests.provider_openai`, `ava_tests.provider_anthropic`, `ava_tests.provider_live_smoke`) plus a dated matrix of the enabled live cases, model ids, result classifications, and whether any failure is credentials/provider/network/AVA-owned. Anthropic interactive OAuth is not a live-smoke requirement because Anthropic does not document a third-party authorization or device flow; use `ANTHROPIC_API_KEY`, `ANTHROPIC_OAUTH_TOKEN`, or `ANTHROPIC_AUTH_TOKEN` only when the credential is already available. The current 0.90 evidence has OpenAI and Kimi-for-coding live-smoked, with Kimi-for-coding accepted as the additional production-quality provider path. Anthropic endpoint auth remains blocked by the configured key, and Moonshot/OpenRouter-compatible prompts are blocked by missing auth for follow-up provider-breadth validation.
