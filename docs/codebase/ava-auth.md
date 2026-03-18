# ava-auth

> OAuth and provider authentication for AVA (shared by CLI + Desktop).

## Public API

| Type/Function | Description |
|--------------|-------------|
| `authenticate(provider_id)` | Start auth flow for a provider (PKCE, device code, or API key) |
| `all_providers()` | Returns static list of 23 supported LLM providers |
| `provider_info(id)` | Lookup provider metadata by ID |
| `AuthResult` | Enum: OAuth(tokens), DeviceCodePending(response), or NeedsApiKey |
| `AuthError` | thiserror-based error enum with specific variants |
| `ProviderInfo` | Metadata: name, description, auth flows, env var, base URL |
| `ProviderGroup` | Enum: Popular or Other (for UI grouping) |
| `OAuthTokens` | access_token, refresh_token, expires_at, id_token |
| `exchange_code_for_tokens()` | PKCE token exchange |
| `refresh_token()` | OAuth token refresh flow |
| `decode_jwt_payload()` | Decode JWT without verification (metadata only) |
| `extract_account_id()` | Extract ChatGPT account ID from id_token |
| `generate_pkce()` | Generate PKCE verifier, challenge, state |
| `listen_for_callback()` | One-shot HTTP server for OAuth callback |
| `open_browser()` | Cross-platform browser launch |
| `request_device_code()` | Start device code flow |
| `poll_device_code()` | Poll for device code authorization |
| `exchange_copilot_token()` | Exchange GitHub token for Copilot token |
| `CopilotToken` | Copilot API token with expiry and endpoint |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Core auth flow orchestration, provider definitions, main exports |
| `tokens.rs` | Token exchange, refresh, JWT decoding utilities |
| `callback.rs` | One-shot HTTP server for OAuth callback (CSRF validation) |
| `config.rs` | OAuth provider configs (OpenAI PKCE, Copilot device code) |
| `pkce.rs` | PKCE parameter generation (RFC 7636) |
| `device_code.rs` | Device code flow (GitHub Copilot) |
| `copilot.rs` | Copilot-specific token exchange and endpoint validation |
| `browser.rs` | Cross-platform browser opening |

## Dependencies

Uses: None (no internal AVA crate dependencies)

Used by:
- `ava-tui` — CLI/TUI authentication
- `ava-llm` — Provider credential management
- `ava-config` — Configuration integration
- `src-tauri` — Desktop app authentication

## Key Patterns

- **Error handling**: Uses `thiserror` with specific error variants (StateMismatch, CallbackTimeout, etc.)
- **Security**: PKCE state validation prevents CSRF; JWT decoding explicitly does NOT verify signatures
- **Provider registry**: Hardcoded static array of 23 providers with grouped display support
- **Flow types**: PKCE (browser), DeviceCode (GitHub), ApiKey (most providers)
- **HTTP client**: Centralized `http_client()` with 30s timeout and 10s connect timeout
- **Copilot**: Validates API endpoints against allowlist of known hosts
