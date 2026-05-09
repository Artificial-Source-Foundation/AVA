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
- `ava --print ... --json --allow-tool webfetch` for `webfetch`. This verifies the explicit `network.fetch` headless allow path and real bounded HTTP fetch behavior.
- `ava --print ... --json` without `--allow-tool webfetch` for a prompt that asks for `webfetch`. This verifies `network.fetch` ask prompts fail closed by the default headless resolver; workspace-policy allow decisions may still proceed without prompting.
- `ava --rpc` with a small JSONL harness that answers `permission_requested` with `permission_reply` for `write_file`, `edit_file`, `apply_patch`, and `bash`. The checked-in headless bash cleanup smoke verifies that timed-out shell process groups do not leave a child process behind.
- `ava --rpc` with `question_reply` for the `question` tool.
- `ava --rpc` command-registry smokes for `list_commands` and `invoke_command` across prompt commands, skills, plugin commands, and MCP prompt commands.
- `ava --rpc` plugin/MCP diagnostics smokes for plugin discovery/validation/static resources/enablement/fail-closed execution, MCP server list/inspect/restart, invalid MCP config containment, and fail-closed MCP tool discovery without a TUI resolver. `ava_cli.headless_rpc_sample_plugin` copies `examples/plugins/todo/` into an isolated project plugin directory and verifies the real sample's discovery, resources, enable/disable flow, and fail-closed command execution without starting the sample process.
- `./build/ava_tests plugin` after plugin authoring changes. The plugin suite validates the checked-in sample under `examples/plugins/todo/` through source-path fixture plumbing instead of duplicating the sample manifest or protocol JSON, and remains the coverage for successful sample entrypoint execution.
- `./build/ava_tests mcp` after MCP contract changes. The MCP suite uses the local fake MCP server and golden fixtures for representative tool schema, resource read, and audit shapes; MCP resource behavior must stay behind explicit read-style permission coverage.

`lsp_diagnostics` is capability-gated in normal headless runtime. Verify it through `ava_tests` and the fake LSP server unless a local diagnostics provider is configured for a live run.

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

The `ava_tests` binary covers:

- mode parsing
- session JSONL storage, resume, listing, corruption handling, and permissions
- XDG path handling
- OpenAI auth loading/storage and OAuth refresh preflight
- model and prompt configuration
- provider request/SSE parsing, including OpenAI Responses function-call starts from `response.output_item.added`, Anthropic native tools/thinking/cache usage, Kimi/Moonshot-compatible reasoning-content vectors, and OpenRouter-compatible request/error vectors
- permission audit persistence, file/search/bash/webfetch/LSP tools, bash process-group cleanup, spill files, and atomic file writes
- tool dispatcher and agent loop
- command registry discovery/invocation for built-ins, prompt commands, skills, plugin commands, and MCP prompts
- plugin manifest/discovery/enablement, out-of-process plugin runner behavior, plugin tools/commands/static resources/events, diagnostics, containment, failure cases, and the checked-in sample plugin workflow
- MCP stdio config, initialize, tool listing/calls, prompt listing/get, tool broker registration, diagnostics, and fake-server success/error/exit cases
- print mode and JSONL RPC success, denial/recovery, malformed input, cancellation, and refresh paths
- TUI rendering, input, keybindings, palette, permission prompt, markdown, UTF-8, and scroll helpers

Add regression tests for every safety-sensitive bug fix.

## TUI / ncurses Focused Validation

For TUI-only changes, the focused suite is:

```sh
cmake --build build --target ava_tests
./build/ava_tests tui_composer
ctest --test-dir build --output-on-failure -R "ava_tests.tui_composer"
```

The suite includes the CI-safe ncurses baseline currently available in-tree: configured `ESCDELAY`, escape buffering/discard for CSI/OSC/DCS/bracketed-paste markers, mouse wheel/click mapping at the composer layer, resize stress renders, Unicode/CJK/combining/emoji width and cursor placement, `newterm` smokes for xterm/screen terminfo plus tmux/kitty/wezterm/ssh-like environment variables without a real TTY, and large/very-long transcript performance budgets. These are initialization and rendering smokes, not a substitute for real terminal behavior coverage. Batch 4 keeps full ncurses redraw because the stress baseline remains bounded; differential redraw/damage tracking is deferred until real PTY/tmux evidence shows a need. Real PTY/tmux automation remains a follow-up; use `docs/dev/ncurses-notes.md` for the manual smoke checklist and known unsupported terminal features until a deterministic harness exists.

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

For provider live smokes, use environment credentials only; the smoke suite intentionally does not read or print auth files. The CTest suite `ava_tests.provider_live_smoke` is skipped by default; opt in with `AVA_LIVE_PROVIDER_SMOKE=1` plus one or more provider credentials such as `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`, `ANTHROPIC_OAUTH_TOKEN`, `ANTHROPIC_AUTH_TOKEN`, `KIMI_API_KEY`, `MOONSHOT_API_KEY`, or `OPENROUTER_API_KEY`. Optional model overrides are `AVA_LIVE_OPENAI_MODEL`, `AVA_LIVE_ANTHROPIC_MODEL`, `AVA_LIVE_KIMI_MODEL`, `AVA_LIVE_MOONSHOT_MODEL`, and `AVA_LIVE_OPENROUTER_MODEL`. Record each non-OpenAI result as one of: passed, credential/auth-blocked, provider/rate-limited, network-blocked, or AVA regression. The current 0.90 evidence has OpenAI and Kimi-for-coding live-smoked, with Kimi-for-coding accepted as the additional production-quality provider path. Anthropic endpoint auth remains blocked by the configured key, and Moonshot/OpenRouter-compatible prompts are blocked by missing auth for follow-up provider-breadth validation.
