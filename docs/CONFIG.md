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

The command prints an OpenAI authorization URL, waits for the browser callback on `http://localhost:1455/auth/callback`, and stores the resulting credential owner-only at the AVA auth path.

OAuth token format:

```json
{"openai":{"type":"oauth","access_token":"...","refresh_token":"...","expires_at":1893456000}}
```

API key format:

```json
{"openai":{"type":"api_key","api_key":"sk-..."}}
```

Auth files are written owner-only. AVA also attempts to read legacy `~/.ava/credentials.json` and opencode's XDG auth file for migration.

OAuth credentials refresh automatically before use when a refresh token is present. If refresh fails or the credential has no refresh token, rerun `ava connect openai`.

## Models

Built-in default: `openai/gpt-5.5`.

Optional model override file: `$XDG_CONFIG_HOME/ava/models.json`.

```json
{
  "default_provider": "openai",
  "default_model": "gpt-5.5",
  "models": [
    {"provider":"openai","id":"gpt-5.5","family":"gpt-5","supports_tools":true,"supports_streaming":true}
  ]
}
```

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
