# Environment variables

This is the reference for environment input read by AVA and its repository scripts. Configuration files and command-line options are documented in [CONFIG.md](CONFIG.md); provider behavior is summarized in [providers.md](https://github.com/Artificial-Source/AVA/blob/develop/docs/providers.md). Values described as secrets must not be put in project files, shell history, logs, or command lines.

## Resolution and secret handling

AVA reads its process environment at the point the relevant feature is used. A stored provider credential in `$XDG_CONFIG_HOME/ava/auth.json` takes precedence over an environment credential. For Anthropic, when no stored credential exists, `ANTHROPIC_OAUTH_TOKEN`, then `ANTHROPIC_AUTH_TOKEN`, then `ANTHROPIC_API_KEY` wins. Nonempty provider base-URL overrides replace their provider default. `NO_COLOR` wins over `AVA_TUI_THEME`; the full theme ordering is in [CONFIG.md](CONFIG.md#tui-display).

Environment credentials are deliberately not persisted merely by using AVA. `ava connect <provider> --api-key-env NAME` is the explicit operation that reads an arbitrary named variable and stores it owner-only. AVA status and diagnostics report credential source/status, never its value; provider diagnostics also redact raw provider bodies. See [diagnostics.md](diagnostics.md).

## Stable runtime variables

| Variable | Meaning |
| --- | --- |
| `AVA_SESSION_TITLES` | `on` defers to `session-titles.json`; `off` disables automatic session-title generation. Other values are rejected. |
| `AVA_TUI_THEME` | Process-only built-in theme override: `dark`, `light`, or `plain`. Unknown/empty values fall through to automatic detection. |
| `AVA_TUI_TMUX_HYPERLINKS` | Set to `1` to opt into OSC 8 links in tmux after tmux itself has been configured to forward hyperlinks. It affects only the tmux fallback. |
| `AVA_DISABLE_BROWSER_OPEN` | Any presence prevents AVA from launching a browser for OAuth; AVA still prints the URL. |
| `AVA_CLIPBOARD_IMAGE_FILE` | Absolute image file to import for Ctrl+V instead of probing the Linux clipboard. Intended primarily for deterministic use; supported image types are the same as clipboard paste. |
| `AVA_EXTERNAL_EDITOR_FILE` | Internal, short-lived path supplied by AVA only while it invokes `VISUAL`/`EDITOR`; editor commands use it instead of a positional filename. Do not pre-set it as configuration. |

### Provider credentials and endpoints

These values are credentials; export them only into the AVA process that needs them.

| Provider | Credential variable(s) | Base URL variable |
| --- | --- | --- |
| OpenAI | `OPENAI_API_KEY` | None (the native runtime uses its configured default) |
| Anthropic | `ANTHROPIC_OAUTH_TOKEN`, `ANTHROPIC_AUTH_TOKEN`, or `ANTHROPIC_API_KEY` | `ANTHROPIC_BASE_URL` |
| DeepSeek | `DEEPSEEK_API_KEY` | `DEEPSEEK_BASE_URL` |
| Gemini | `GEMINI_API_KEY` | `GEMINI_BASE_URL` |
| Kimi | `KIMI_API_KEY` | `KIMI_BASE_URL` |
| Moonshot | `MOONSHOT_API_KEY` | `MOONSHOT_BASE_URL` |
| OpenRouter | `OPENROUTER_API_KEY` | `OPENROUTER_BASE_URL` |

Future provider ids use the generic uppercase `<PROVIDER_ID>_API_KEY` lookup only if a runtime provider is added. Do not infer support from that convention. Defaults, OAuth details, and implemented-provider boundaries are in [providers.md](https://github.com/Artificial-Source/AVA/blob/develop/docs/providers.md).

## Standard desktop and shell inputs

| Variable | Use |
| --- | --- |
| `HOME` | Trusted-home/config compatibility and built-in LSP user-bin discovery. It must be usable as an absolute home where required. |
| `XDG_CONFIG_HOME`, `XDG_STATE_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_RUNTIME_DIR` | XDG roots for AVA state/config and normal platform integration. AVA's exact paths are documented in [CONFIG.md](CONFIG.md). |
| `PATH` | Command discovery and browser lookup in AVA. It is not blindly forwarded to sealed commands or LSP servers. |
| `PWD` | Preserves the logical startup working-directory identity where safe. |
| `VISUAL`, `EDITOR` | External-editor command, tried in that order, for the TUI editor shortcut. |
| `BROWSER` | Browser command preference for OAuth URL opening. |
| `NO_COLOR` | Presence disables color and overrides all theme selection. |
| `COLORFGBG` | Terminal background hint used only after explicit theme settings. |
| `TERM`, `TERM_PROGRAM`, `TERMINAL_EMULATOR`, `TMUX`, `TMUX_PANE`, `KITTY_WINDOW_ID`, `GHOSTTY_RESOURCES_DIR`, `ITERM_SESSION_ID`, `WEZTERM_EXECUTABLE`, `WEZTERM_PANE`, `WT_SESSION`, `WARP_SESSION_ID`, `WARP_TERMINAL_SESSION_UUID`, `TERMUX_VERSION` | Terminal/image/hyperlink capability detection. These are hints, not security authority. |
| `WAYLAND_DISPLAY` | Selects Wayland clipboard probing; AVA otherwise can use X11 clipboard helpers. |

## Developer debugging controls

| Variable | Meaning |
| --- | --- |
| `AVA_NO_DEBUG_OUTPUT` | Suppresses libcwd debug initialization unless `AVA_DEBUG_OUTPUT_DIR` is explicitly nonempty. Used by tests and useful to silence developer debug output. |
| `AVA_DEBUG_OUTPUT_DIR` | Absolute directory for libcwd debug output. Empty does not opt in; a relative path is rejected. |
| `LIBCWD_NO_STARTUP_MSGS` | libcwd compatibility switch used to suppress its startup message. |
| `LIBCWD_RCFILE_NAME`, `LIBCWD_RCFILE_OVERRIDE_NAME` | libcwd configuration inputs, not AVA product settings. |

`AVA_DEBUG_MAXLEN` is a **CMake cache variable**, not an environment variable; see [build configuration](https://github.com/Artificial-Source/AVA/blob/develop/docs/build-configuration.md).

## Child-process inheritance

Child programs do **not** receive AVA's ambient environment as a product contract.

* Sealed `bash` execution receives AVA-created private HOME/XDG/TMP roots, a bounded PATH, and a constructed environment; provider credentials and arbitrary `AVA_*` values are excluded.
* LSP servers receive only `HOME`, `USER`, `LOGNAME`, `TMPDIR`/`TMP`/`TEMP`, `LANG`, `LANGUAGE`, `LC_ALL`, other `LC_*`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `TERM`, and `COLORTERM`, plus a fixed trusted `PATH`. In particular all `AVA_*`, provider/cloud/API/token/secret variables, and arbitrary inherited values are not forwarded.
* MCP has its own explicit configuration/environment contract; see [mcp.md](mcp.md). Project/session MCP launches use a clean environment, while legacy global/project behavior is separately documented there.

## Supported maintainer and script inputs

These variables are supported by the named repository scripts; they are not AVA runtime configuration. Prefer the script's command-line option where one exists.

| Variable | Script and meaning |
| --- | --- |
| `AVA_CMAKE_COMMAND` | `scripts/build.sh`: CMake executable/wrapper; defaults to `cmake`. Primarily a runner test seam. |
| `AVA_CTEST_COMMAND` | `scripts/run-tests.sh`: CTest executable/wrapper; defaults to `ctest`. Primarily a runner test seam. |
| `AVA_LIVE_PROVIDER_SMOKE` | Explicit opt-in gate for credentialed live-provider CTests, dogfood scripts, and `live-provider-matrix.sh`; use `1`. No live provider runs by default. |
| `AVA_EXE` | `live-model-dogfood.sh`, `live-coding-dogfood.sh`, and `live-provider-matrix.sh`: AVA executable path. The dogfood scripts default to `./build/ava`; the matrix derives it from its build directory unless set. |
| `AVA_BUILD_DIR` | `live-provider-matrix.sh`: configured build tree, default `build`; used to derive executables for matrix targets. |
| `AVA_TESTS_EXE` | `live-provider-matrix.sh`: `ava_tests` executable override for the `provider-live-smoke` target; otherwise derived from `AVA_BUILD_DIR`. |
| `AVA_LIVE_DOGFOOD_ROOT` | `live-model-dogfood.sh`: explicit evidence/work root instead of a temporary directory. The script recursively removes and recreates this path before use, so it must name a dedicated disposable directory. |
| `AVA_LIVE_DOGFOOD_KEEP` | Any nonempty value keeps model-dogfood logs/root after the run; otherwise cleanup removes it. It does not prevent the initial removal of an explicit root. |
| `AVA_LIVE_CODING_DOGFOOD_ROOT` | `live-coding-dogfood.sh`: explicit coding-dogfood evidence/work root. It has the same remove-and-recreate requirement as `AVA_LIVE_DOGFOOD_ROOT`. |
| `AVA_LIVE_CODING_DOGFOOD_KEEP` | Any nonempty value keeps coding-dogfood logs/root; otherwise cleanup removes it. |
| `AVA_LIVE_PROVIDER_MATRIX_TARGET` | `live-provider-matrix.sh`: `provider-live-smoke` (default), `model-dogfood`, or `coding-dogfood`. The older `AVA_LIVE_MATRIX_TARGET` is a compatibility fallback only when this variable is unset. |
| `AVA_LIVE_PROVIDER_MATRIX_SUMMARY` | `live-provider-matrix.sh`: aggregate TSV path, defaulting beneath `${TMPDIR:-/tmp}`; a relative value resolves from the caller's starting directory. The script creates or truncates this file. |
| `AVA_LIVE_PROVIDER_MATRIX_KEEP` | Any nonempty value retains the matrix temporary root and child logs and propagates the matching dogfood keep control. |
| `AVA_LIVE_<PROVIDER>_MODEL` | Optional model override used by live selection/matrix code: `<PROVIDER>` is `OPENAI`, `ANTHROPIC`, `DEEPSEEK`, `GEMINI`, `KIMI`, `MOONSHOT`, or `OPENROUTER`. Defaults and result classification are in [TESTING.md](TESTING.md#provider-live-smoke-matrix). |

`AVA_EXE`, dogfood roots, and keep flags affect evidence execution/storage only; they do not enable the credential gate. Live runs can consume credentials and make paid/network provider calls, so opt in deliberately and review retained logs as private material.

## Optional smoke gates

These are stable opt-in test controls, not normal AVA settings:

| Variable | Opt-in coverage |
| --- | --- |
| `AVA_TUI_TMUX_SMOKE` | Isolated tmux TUI scenarios. |
| `AVA_TUI_OSC8_SMOKE` | Direct PTY OSC 8 hyperlink smoke. |
| `AVA_TUI_KITTY_IMAGE_SMOKE`, `AVA_TUI_ITERM2_IMAGE_SMOKE` | Direct PTY inline-image protocol smokes. |
| `AVA_TUI_TERMINAL_LIFECYCLE_SMOKE` | Direct PTY terminal lifecycle smoke. |
| `AVA_LSP_REAL_CLANGD_SMOKE` | Offline smoke against an already-installed safe `clangd`; never installs/downloads it. |

See [TESTING.md](TESTING.md) for exact commands, prerequisites, skip-77 behavior, and release classification. Optional ACP SDK/acpx interop is selected by CMake options rather than runtime environment variables.

## Implementation-only test namespaces

The test harnesses also create private coordination variables. They are deliberately **not** supported user or maintainer controls and should not be copied into shell profiles or external automation:

- `AVA_PACKAGE_TEST_*` and `AVA_PACKAGE_CLEANUP_CLOSE_PIPES` coordinate package fixtures, snapshots, mutation markers, and process-cleanup regressions.
- `AVA_PARALLEL_*` is internal state owned by `parallel-runner-common.sh` for its build-tree lock, child process group, and signal escalation. Setting these externally can break fail-closed cleanup.
- ACP/Zed fixture variables such as `AVA_ACP_STDERR`, `AVA_ACP_WRAPPER_PROOF`, `AVA_ACP_SUBPROCESS_PARENT_SECRET`, and `AVA_ZED_DOGFOOD_PHASE_ROOT` are harness-to-child channels, not ACP configuration.
- Fixture/canary names such as `AVA_*_FAKE_KEY_NOT_A_SECRET`, `AVA_*_SECRET_SENTINEL`, `AVA_LSP_TEST_SECRET`, and one-off marker/gate variables exist only to prove isolation or non-disclosure. Their exact inventory may change with tests.

CTest sets controls such as `AVA_SESSION_TITLES=off`, `AVA_NO_DEBUG_OUTPUT=1`, and fake executable/root definitions for deterministic cases. Treat all undocumented `AVA_*` values observed only in fixtures as implementation detail unless this page or the owning current test guide explicitly declares them supported.
