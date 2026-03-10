# ava-auth

Authentication subsystem supporting OAuth PKCE, Device Code, API key, and GitHub Copilot token exchange flows.

## How It Works

### Entry Point (`src/lib.rs`)

`authenticate(provider_info)` dispatches to the appropriate flow based on `ProviderInfo`:

```rust
pub enum AuthResult {
    ApiKey(String),
    OAuth { access_token: String, refresh_token: Option<String>, expires_at: Option<String>, account_id: Option<String> },
    DeviceCode { access_token: String, refresh_token: Option<String>, expires_at: Option<String>, account_id: Option<String> },
}
```

`AuthError` covers flow failures: InvalidGrant, TokenExpired, NetworkError, BrowserError, CallbackTimeout, UnsupportedProvider.

`ProviderInfo` defines 23 providers across 5 groups:

| Group | Providers |
|-------|-----------|
| OpenAI | openai, azure-openai |
| Anthropic | anthropic |
| Google | gemini, google-ai, vertex-ai |
| GitHub | copilot, github |
| ApiKey | openrouter, ollama, together, groq, mistral, deepseek, cohere, perplexity, fireworks, replicate, anyscale, alibaba, zhipu, kimi, minimax |

**File**: `crates/ava-auth/src/lib.rs`

### PKCE Flow (`src/pkce.rs`)

RFC 7636 compliant PKCE parameter generation:

```rust
pub struct PkceParams {
    pub code_verifier: String,   // 128 random bytes, URL-safe base64
    pub code_challenge: String,  // SHA-256 of verifier, URL-safe base64
}
```

`generate_pkce()` creates cryptographically random PKCE parameters.

**File**: `crates/ava-auth/src/pkce.rs`

### OAuth Configuration (`src/config.rs`)

`OAuthConfig` structs for OpenAI (PKCE) and Copilot (DeviceCode). `build_auth_url()` constructs the authorization URL with PKCE challenge, redirect URI, scope, and state parameters.

### Token Exchange (`src/tokens.rs`)

- `exchange_code_for_tokens(code, verifier, config)` -- exchanges auth code for tokens
- `refresh_token(refresh_token, config)` -- refreshes an expired token
- `decode_jwt_payload(token)` -- base64 decodes JWT payload without verification
- `extract_account_id(token)` -- extracts account/org ID from JWT claims

**File**: `crates/ava-auth/src/tokens.rs`

### Callback Server (`src/callback.rs`)

`listen_for_callback(port, timeout)` starts a one-shot TCP server to receive the OAuth redirect. Extracts the authorization code from the query parameters.

### Browser (`src/browser.rs`)

`open_browser(url)` uses the `open` crate to launch the system browser for OAuth authorization.

### Device Code Flow (`src/device_code.rs`)

For providers like GitHub Copilot that use the Device Authorization Grant:

```rust
pub struct DeviceCodeResponse {
    pub device_code: String,
    pub user_code: String,
    pub verification_uri: String,
    pub expires_in: u64,
    pub interval: u64,
}
```

- `request_device_code(config)` -- initiates the flow
- `poll_device_code(device_code, config)` -- polls for user authorization with `slow_down` handling (increases interval when server requests it)

### Copilot Token Exchange (`src/copilot.rs`)

Converts a GitHub OAuth token into a Copilot API token:

```rust
pub struct CopilotToken {
    pub token: String,
    pub expires_at: i64,
    pub endpoints: CopilotEndpoints,
}
```

`exchange_copilot_token(github_token)` calls `https://api.github.com/copilot_internal/v2/token` and extracts the API endpoint from the token string itself.

**File**: `crates/ava-auth/src/copilot.rs`

## Source Files

| File | Lines | Purpose |
|------|------:|---------|
| `src/lib.rs` | -- | AuthResult, AuthError, ProviderInfo, authenticate() |
| `src/config.rs` | -- | OAuthConfig, build_auth_url |
| `src/tokens.rs` | -- | Token exchange, refresh, JWT decode |
| `src/pkce.rs` | -- | RFC 7636 PKCE generation |
| `src/callback.rs` | -- | OAuth redirect callback server |
| `src/browser.rs` | -- | System browser launcher |
| `src/device_code.rs` | -- | Device Authorization Grant flow |
| `src/copilot.rs` | -- | GitHub-to-Copilot token exchange |
