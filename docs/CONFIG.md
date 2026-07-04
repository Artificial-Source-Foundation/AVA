# AVA Configuration

AVA uses XDG paths on Linux.

| Kind | Path |
| --- | --- |
| Config directory | `$XDG_CONFIG_HOME/ava` or `~/.config/ava` |
| Auth file | `$XDG_CONFIG_HOME/ava/auth.json` or `~/.config/ava/auth.json` |
| Session state | `$XDG_STATE_HOME/ava/sessions` or `~/.local/state/ava/sessions` |
| Project trust state | `$XDG_STATE_HOME/ava/project-trust.json` or `~/.local/state/ava/project-trust.json` |

## TUI Display

The TUI has built-in `dark`, `light`, and `plain` display modes. Persist a built-in or custom mode with `/theme`:

```sh
/theme light
/theme plain
/theme ocean
/theme reset
```

The setting is stored in `$XDG_CONFIG_HOME/ava/display.json`:

```json
{
  "theme": "light"
}
```

After editing `display.json` by hand, the interactive TUI automatically reloads the file without restarting. `/reload theme` is still available for an explicit retry or diagnostic. Invalid JSON or unsupported theme values are reported with the config path and supported values, and the previous active theme remains in use.

Custom themes live under `$XDG_CONFIG_HOME/ava/themes/*.json`. AVA accepts a lean Pi-style file with `name`, optional `vars`, and an AVA-specific eight-role `colors` object:

```json
{
  "name": "ocean",
  "vars": {
    "primary": "#0066cc",
    "paper": 255
  },
  "colors": {
    "text": "",
    "muted": 242,
    "success": 34,
    "warning": "#ffaa00",
    "error": "#ff0000",
    "accent": "primary",
    "screenBg": "paper",
    "composerBg": 236
  }
}
```

Color values can be `""` for the terminal default, a 0-255 xterm color number, a 6-digit hex RGB string approximated to xterm-256, or a variable name from `vars`. Custom theme names must be unique, non-empty, and free of whitespace or path separators. `/settings` shows valid custom theme files as selectable theme rows; invalid custom files are ignored during discovery and reported when directly selected through `display.json` or `/theme`.

You can also override the theme for the current process:

```sh
AVA_TUI_THEME=light ava
AVA_TUI_THEME=plain ava
NO_COLOR=1 ava
```

`dark` is the default fallback ncurses palette. `light` selects a light built-in palette. `plain` disables ANSI styling. Precedence is `NO_COLOR`, then `AVA_TUI_THEME`, then `display.json`, then terminal background inference from `COLORFGBG`, then the built-in dark fallback. The `/settings` TUI view reports the active display mode and source, and exposes selectable theme rows that write `display.json`. During an interactive TUI run, AVA polls `display.json` and the selected custom theme file and applies valid changes automatically.

## External Editor

Set `VISUAL` or `EDITOR` to the editor command AVA should use for the TUI external-editor shortcut. Pressing Ctrl+G writes the current composer draft to an owner-only temporary file, restores the terminal to shell mode, runs the configured editor command with that file path, and replaces the draft with the saved file when the editor exits successfully. If the editor exits nonzero, AVA keeps the original draft.

## Clipboard Image Paste

Pressing Ctrl+V in the TUI imports a supported image from the clipboard as a pending attachment when AVA can read one through Linux clipboard helpers. AVA probes `wl-paste` for Wayland and `xclip` for X11, accepts PNG, JPEG, WebP, and GIF bytes, then stores the image through the same session-owned attachment path used by `/attach`. Set `AVA_CLIPBOARD_IMAGE_FILE=/absolute/path/to/image.png` to bypass the OS clipboard and import that image file; this is intended for deterministic terminal smoke tests.

## Auth

Create an OpenAI OAuth credential with:

```sh
ava connect openai
```

The command opens an OpenAI login method picker. Browser OAuth prints an OpenAI authorization URL, waits for the callback on `http://localhost:1455/auth/callback`, and stores the resulting credential owner-only at the AVA auth path. Headless OAuth uses OpenAI's device-code flow and prints `https://auth.openai.com/codex/device` plus the code to enter.

Interactive provider login is available for API keys:

```sh
ava auth login
ava login anthropic
ava auth login moonshot --api-key
ava connect kimi --api-key
```

When the provider is omitted, `ava auth login`, `ava login`, and interactive `ava connect` open a searchable terminal provider picker before asking for login method and secret. Secrets are read without terminal echo when stdin is a TTY. In the TUI, use `/connect` or `/login` to open the same provider flow as a modal; OpenAI shows browser OAuth, headless OAuth, and API key options.

Headless API-key setup is available for OpenAI, Anthropic, Moonshot/Kimi, and other provider ids:

```sh
ava connect openai --headless-oauth
printf '%s\n' "$OPENAI_API_KEY" | ava connect openai --api-key-stdin
printf '%s\n' "$ANTHROPIC_API_KEY" | ava connect anthropic --api-key-stdin
ava connect moonshot --api-key-env MOONSHOT_API_KEY
ava connect kimi --api-key-env KIMI_API_KEY
```

OpenAI OAuth credential format:

```json
{"openai":{"type":"oauth","access_token":"...","refresh_token":"...","expires_at":1893456000}}
```

API key format:

```json
{"openai":{"type":"api_key","api_key":"sk-..."},"anthropic":{"type":"api_key","api_key":"sk-ant-..."}}
```

Anthropic Claude OAuth bearer tokens can also be supplied with `ANTHROPIC_OAUTH_TOKEN` or the Anthropic SDK-compatible `ANTHROPIC_AUTH_TOKEN`. When no stored Anthropic credential is present, `ANTHROPIC_OAUTH_TOKEN` is preferred over `ANTHROPIC_AUTH_TOKEN`, and both are preferred over `ANTHROPIC_API_KEY`. Stored Anthropic OAuth entries use the provider-scoped auth shape below; `refresh_token`, `expires_at`, `account_id`, and `source` are optional, but expired or near-expiry OAuth entries require a refresh token to be used safely. AVA writes canonical `refresh_token` and `expires_at` fields; it also accepts `refresh` and `expires` aliases when reading manually-created files.

```json
{"anthropic":{"type":"oauth","access_token":"...","refresh_token":"...","expires_at":1893456000,"account_id":"acct_...","source":"claude"}}
```

Auth files are written owner-only. Provider credential setup preserves existing provider entries in the same auth file. Explicit AVA auth entries take precedence; AVA also attempts to read legacy `~/.ava/credentials.json` and the legacy-compatible XDG auth file for OpenAI migration when no AVA OpenAI credential is stored.

OAuth credentials refresh automatically before use when a refresh token is present. If OpenAI refresh fails or the credential has no refresh token, rerun `ava connect openai`; for stored Anthropic OAuth, update `auth.json` with a fresh OAuth credential or remove the stored Anthropic entry before relying on Anthropic environment credentials.

## Models

Built-in default: `openai/gpt-5.5`.

Optional model override file: `$XDG_CONFIG_HOME/ava/models.json`.

```json
{
  "default_provider": "openai",
  "default_model": "gpt-5.5",
  "models": [
    {
      "provider": "openai",
      "id": "gpt-5.5",
      "family": "gpt-5",
      "context_window_tokens": 200000,
      "max_output_tokens": 16384,
      "pricing": {
        "input_per_million": 1.25,
        "output_per_million": 10.0,
        "cache_read_per_million": 0.125,
        "cache_write_per_million": 1.25,
        "reasoning_per_million": 10.0
      }
    }
  ]
}
```

Model entries are additive overrides. `provider` and `id` are required; omitted fields on built-in model overrides inherit the built-in metadata, including `context_window_tokens`. For brand-new custom models, set `context_window_tokens` so token percentage and context-aware compaction can work. Optional fields include `display_name`, `family`, `api_family`, `context_window_tokens`, `max_output_tokens`, `supports_tools`, `supports_streaming`, `supports_reasoning`, `reports_usage`, `input_modalities`, `output_modalities`, `reasoning_levels`, `reasoning_format`, `compatibility_quirks`, and `pricing`. `input_modalities` currently recognizes `text` and `image`; models that omit `image` reject replayed image attachments before provider requests. Built-in OpenAI Responses and Anthropic Messages image-capable profiles declare `text` plus `image`; custom compatible-provider image models should do the same only when their endpoint accepts chat-completions image URL blocks. `/models <query>` and the TUI model selector report advisory diagnostics for custom models that omit context windows, input modalities, API family, support flags, usage/pricing metadata, or reasoning levels. These diagnostics do not block switching when the provider is registered; rows for unregistered providers are disabled because backend model switching rejects them. Pricing values are USD per one million tokens and are local static metadata; AVA does not fetch live prices. Cost is reported only when the saved provider usage and configured pricing are complete for the billable token types in that assistant response.

Accepted pricing aliases include `input_usd_per_1m`, `output_usd_per_1m`, `cache_read_usd_per_1m`, `cache_write_usd_per_1m`, and `reasoning_usd_per_1m`.

Reasoning controls are model/API-family specific. OpenAI Responses models accept reasoning level/effort metadata. OpenAI chat-completions compatible routes accept level-only controls where supported. Anthropic Messages models accept `enabled` with a budget at least 1024 tokens and below the model output limit; `adaptive` is accepted only for profiles that explicitly list it and does not accept a manual budget. Kimi/Moonshot-style compatible routes can preserve `reasoning_content` when their model metadata declares the matching reasoning format and compatibility quirk.

Provider request metadata also supports prompt/cache-control hints where a provider API can use them. Anthropic content-part serialization preserves native tool use/results, thinking signatures or redacted thinking markers, cache usage, stop reasons, and provider-native reasoning blocks in session replay without exposing opaque signatures in exports.

## LSP Servers

Optional LSP config files:

```text
$XDG_CONFIG_HOME/ava/lsp.json
<workspace>/.ava/lsp.json
```

Both files use the same explicit schema. Global servers are loaded before workspace servers. Missing files simply leave LSP tools unavailable; malformed present files disable the configured provider for that context.

```json
{
  "version": 1,
  "servers": [
    {
      "id": "cpp",
      "argv": ["clangd", "--background-index"],
      "file_extensions": [".cpp", ".h"],
      "language_id": "cpp",
      "timeout_ms": 3000
    }
  ]
}
```

`id` is a unique short identifier using letters, digits, `_`, `-`, or `.`. `argv` is a non-empty JSON string array executed directly without a shell. `file_extensions` is an optional JSON string array; when omitted or empty, the server can match any file. `language_id` defaults to `plaintext` and is sent in bounded `textDocument/didOpen` notifications for definition/reference queries. `timeout_ms` defaults to `3000` and must be a base-10 integer from `100` through `30000`. Known fields reject wrong JSON types, mixed arrays, duplicate server ids, control bytes, oversized values, symlinked config files, and configs over 64 KiB.

Using an LSP tool first requests `lsp.query` for the target file or workspace. Starting the configured subprocess separately requests high-risk `lsp.server.launch`; persistent permission rules match the exact JSON-array encoded argv string, not a shell command line.

## Compaction

Optional compaction config file: `$XDG_CONFIG_HOME/ava/compaction.json`.

```json
{
  "model": "gpt-5.5",
  "auto_threshold_tokens": 0,
  "keep_recent_tokens": 2048,
  "keep_recent_messages": 6,
  "max_summary_bytes": 16384
}
```

`/compact` generates a provider-backed summary and records a compaction boundary in the session. Automatic compaction runs before a provider request when the active context estimate reaches the effective threshold. If `auto_threshold_tokens` is omitted, AVA uses about 80% of the configured model context window, or an 80,000-token fallback when the model window is unknown. If `auto_threshold_tokens` is present with value `0`, automatic compaction is explicitly disabled.

`keep_recent_messages` and `keep_recent_tokens` bound the recent transcript tail stored with a compaction entry. The tail is best-effort continuation context and may be truncated with a marker. `max_summary_bytes` rejects unexpectedly large provider summaries before they are appended to the session.

## Prompts

Prompt override path:

```text
$XDG_CONFIG_HOME/ava/prompts/<provider>/<family>/<mode>.txt
```

Examples:

```text
~/.config/ava/prompts/openai/gpt-5.5/build.txt
~/.config/ava/prompts/openai/gpt-5.5/plan.txt
```

System prompt resource paths:

```text
$XDG_CONFIG_HOME/ava/SYSTEM.md
$XDG_CONFIG_HOME/ava/APPEND_SYSTEM.md
$WORKSPACE/.ava/SYSTEM.md
$WORKSPACE/.ava/APPEND_SYSTEM.md
```

`SYSTEM.md` replaces the selected built-in or provider/family/mode prompt text.
`APPEND_SYSTEM.md` appends to the selected prompt text. Context files and loaded
skills are still appended after these resources. Trusted project files win over
global files with the same name; untrusted project files are skipped and the
global file is used when present.

Process-local CLI prompt overrides:

```sh
ava --system-prompt "Use this system prompt" --append-system-prompt "Extra instruction"
ava --append-system-prompt "First extra instruction" --append-system-prompt "Second extra instruction"
```

`--system-prompt` replaces built-in prompts, provider/family prompt overrides,
and `SYSTEM.md` files for the current process. Repeated
`--append-system-prompt` values are joined with blank lines and replace
discovered `APPEND_SYSTEM.md` files for the current process. Context files,
prompt commands, skills, and plugin prompt/skill resources are still appended
after the CLI prompt text.

Prompt command template paths:

```text
$XDG_CONFIG_HOME/ava/commands/*.md
$XDG_CONFIG_HOME/ava/command/*.md
$WORKSPACE/.ava/commands/*.md
$WORKSPACE/.ava/command/*.md
```

The filename becomes the slash command name, for example `review.md` becomes `/review`.
Frontmatter can set `description`, `argument-hint`, `argument_hint`, or `hint`.
The template body supports `$1`, `$2`, `$@`, `$ARGUMENTS`, `${1:-default}`,
`${@:N}`, `${@:N:L}`, and `$$`.

## Project Trust

AVA stores project trust decisions outside the workspace in `$XDG_STATE_HOME/ava/project-trust.json`.
The file records normalized workspace paths and whether project-local resources are trusted.
Project `AGENTS.md` context files still load without a trust decision, but these project-local resources are skipped until the workspace is trusted:

```text
$WORKSPACE/.ava/commands/
$WORKSPACE/.ava/command/
$WORKSPACE/.ava/skills/
$WORKSPACE/.agents/skills/
$WORKSPACE/.claude/skills/
$WORKSPACE/.ava/plugins/
$WORKSPACE/.ava/mcp.json
$WORKSPACE/.ava/lsp.json
$WORKSPACE/.ava/SYSTEM.md
$WORKSPACE/.ava/APPEND_SYSTEM.md
```

Use `/trust status` to inspect the current decision, `/trust project` to trust this workspace,
`/trust deny` to keep project resources skipped, and `/trust clear` to remove the explicit decision.
