# AVA Configuration

AVA uses XDG paths on Linux.

| Kind | Path |
| --- | --- |
| Config directory | `$XDG_CONFIG_HOME/ava` or `~/.config/ava` |
| Auth file | `$XDG_CONFIG_HOME/ava/auth.json` or `~/.config/ava/auth.json` |
| Session state | `$XDG_STATE_HOME/ava/sessions` or `~/.local/state/ava/sessions` |

## Auth

Create an OpenAI OAuth credential with:

```sh
ava connect openai
```

The command opens an OpenAI login method picker. Browser OAuth prints an OpenAI authorization URL, waits for the callback on `http://localhost:1455/auth/callback`, and stores the resulting credential owner-only at the AVA auth path. Headless OAuth uses OpenAI's device-code flow and prints `https://auth.openai.com/codex/device` plus the code to enter.

Interactive provider login is available for API keys and bearer tokens:

```sh
ava auth login
ava login anthropic
ava auth login moonshot --api-key
ava connect kimi --oauth-token
```

When the provider is omitted, `ava auth login`, `ava login`, and interactive `ava connect` open a searchable terminal provider picker before asking for login method and secret. Secrets are read without terminal echo when stdin is a TTY. In the TUI, use `/connect` or `/login` to open the same provider flow as a modal; OpenAI shows browser OAuth, headless OAuth, API key, and bearer-token options.

Headless API-key setup is available for OpenAI, Anthropic, Moonshot/Kimi, and other provider ids:

```sh
ava connect openai --headless-oauth
printf '%s\n' "$OPENAI_API_KEY" | ava connect openai --api-key-stdin
printf '%s\n' "$ANTHROPIC_API_KEY" | ava connect anthropic --api-key-stdin
ava connect moonshot --api-key-env MOONSHOT_API_KEY
ava connect kimi --api-key-env KIMI_API_KEY
```

Bearer-token providers can use the OAuth-token variants:

```sh
printf '%s\n' "$ANTHROPIC_OAUTH_TOKEN" | ava connect anthropic --oauth-token-stdin
ava connect anthropic --oauth-token-env ANTHROPIC_OAUTH_TOKEN
```

OAuth token format:

```json
{"openai":{"type":"oauth","access_token":"...","refresh_token":"...","expires_at":1893456000}}
```

API key format:

```json
{"openai":{"type":"api_key","api_key":"sk-..."},"anthropic":{"type":"api_key","api_key":"sk-ant-..."}}
```

Auth files are written owner-only. Provider credential setup preserves existing provider entries in the same auth file. Explicit AVA auth entries take precedence; AVA also attempts to read legacy `~/.ava/credentials.json` and the legacy-compatible XDG auth file for OpenAI migration when no AVA OpenAI credential is stored.

OAuth credentials refresh automatically before use when a refresh token is present. If refresh fails or the credential has no refresh token, rerun `ava connect openai`.

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

Model entries are additive overrides. `provider` and `id` are required; omitted fields on built-in model overrides inherit the built-in metadata, including `context_window_tokens`. For brand-new custom models, set `context_window_tokens` so token percentage and context-aware compaction can work. Pricing values are USD per one million tokens and are local static metadata; AVA does not fetch live prices. Cost is reported only when the saved provider usage and configured pricing are complete for the billable token types in that assistant response.

Accepted pricing aliases include `input_usd_per_1m`, `output_usd_per_1m`, `cache_read_usd_per_1m`, `cache_write_usd_per_1m`, and `reasoning_usd_per_1m`.

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
