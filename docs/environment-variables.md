# Environment variables

This is the reference for environment input read by AVA and its repository scripts. Configuration files and command-line options are documented in [CONFIG.md](CONFIG.md); provider behavior is summarized in [providers.md](providers.md). Values described as secrets must not be put in project files, shell history, logs, or command lines.

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

Future provider ids use the generic uppercase `<PROVIDER_ID>_API_KEY` lookup only if a runtime provider is added. Do not infer support from that convention. Defaults, OAuth details, and implemented-provider boundaries are in [providers.md](providers.md).

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

`AVA_DEBUG_MAXLEN` is a **CMake cache variable**, not an environment variable; see [build configuration](build-configuration.md).

## Child-process inheritance

Child programs do **not** receive AVA's ambient environment as a product contract.

* Sealed `bash` execution receives AVA-created private HOME/XDG/TMP roots, a bounded PATH, and a constructed environment; provider credentials and arbitrary `AVA_*` values are excluded.
* LSP servers receive only `HOME`, `USER`, `LOGNAME`, `TMPDIR`/`TMP`/`TEMP`, `LANG`, `LANGUAGE`, `LC_ALL`, other `LC_*`, `XDG_CONFIG_HOME`, `XDG_CACHE_HOME`, `XDG_DATA_HOME`, `XDG_STATE_HOME`, `TERM`, and `COLORTERM`, plus a fixed trusted `PATH`. In particular all `AVA_*`, provider/cloud/API/token/secret variables, and arbitrary inherited values are not forwarded.
* MCP has its own explicit configuration/environment contract; see [mcp.md](mcp.md). Project/session MCP launches use a clean environment, while legacy global/project behavior is separately documented there.

## Test, smoke, and harness-only variables

The following are not user-facing product API. They gate tests, point harnesses at fake executables, inject canaries, or test runner behavior. Do not set them in a normal AVA shell profile.

| Group | Variables |
| --- | --- |
| Live provider smoke | `AVA_LIVE_PROVIDER_SMOKE`; model overrides `AVA_LIVE_OPENAI_MODEL`, `AVA_LIVE_ANTHROPIC_MODEL`, `AVA_LIVE_DEEPSEEK_MODEL`, `AVA_LIVE_GEMINI_MODEL`, `AVA_LIVE_KIMI_MODEL`, `AVA_LIVE_MOONSHOT_MODEL`, `AVA_LIVE_OPENROUTER_MODEL` |
| Optional UI/LSP smokes | `AVA_TUI_TMUX_SMOKE`, `AVA_TUI_OSC8_SMOKE`, `AVA_TUI_KITTY_IMAGE_SMOKE`, `AVA_TUI_ITERM2_IMAGE_SMOKE`, `AVA_TUI_TERMINAL_LIFECYCLE_SMOKE`, `AVA_LSP_REAL_CLANGD_SMOKE` |
| Build/test wrapper seams | `AVA_CMAKE_COMMAND`, `AVA_CTEST_COMMAND`, `AVA_PACKAGE_CLEANUP_CLOSE_PIPES`, and the `AVA_PACKAGE_TEST_*` namespace |
| ACP/Zed harnesses | `AVA_ACP_STDERR`, `AVA_ACP_SUBPROCESS_PARENT_SECRET`, `AVA_ACP_WRAPPER_PROOF`, `AVA_ZED_DOGFOOD_PHASE_ROOT`, and fake-key markers such as `AVA_ACP_INTEROP_FAKE_KEY_NOT_A_SECRET`, `AVA_ACPX_FAKE_KEY_NOT_A_SECRET`, and `AVA_ZED_DOGFOOD_FAKE_KEY_NOT_A_SECRET` |
| Test sentinels | `AVA_TEST_MOONSHOT_KEY`, `AVA_LSP_TEST_SECRET`, and `AVA_*_SECRET_SENTINEL`/canary values used only to prove non-disclosure |

CTest also sets `AVA_SESSION_TITLES=off` and normally `AVA_NO_DEBUG_OUTPUT=1` for deterministic test behavior. Optional ACP settings are CMake options, not runtime environment settings. See [TESTING.md](TESTING.md) and [acp.md](acp.md).
