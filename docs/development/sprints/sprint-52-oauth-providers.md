# Sprint 52 — OAuth & Provider Authentication (shared crate)

> Unified auth for CLI + Desktop. PKCE browser login, device code flow, API key management. All OpenCode providers covered.

## Goal

Create `crates/ava-auth/` — a shared authentication crate used by BOTH the TUI/CLI and Desktop app. Support every provider OpenCode has, plus OAuth flows for OpenAI and GitHub Copilot.

## Phase 1: Research (mandatory — do this BEFORE any code)

### Step 1 — Study AVA Desktop OAuth (EXISTING CODE TO REUSE)

Read these files completely:

1. **`src/services/auth/oauth-config.ts`** — OAuth configs, PKCE generation, JWT decode:
   - `OAuthConfig` interface (clientId, authorizationUrl, tokenUrl, scopes, redirectPort, flow)
   - `OAUTH_CONFIGS` map — OpenAI PKCE config + Copilot device code config
   - `generatePKCE()` — verifier (64 chars), challenge (SHA-256 base64url), state (32 chars)
   - `decodeJwtPayload()` — extract claims from id_token
   - `extractAccountId()` — get ChatGPT account ID from id_token organizations

2. **`src/services/auth/oauth.ts`** — Flow orchestration:
   - `startOAuthFlow()` — PKCE: generate params → store → build auth URL → start callback server → open browser → exchange code
   - `startDeviceCodeFlow()` — request device code from GitHub → return user_code + verification_uri
   - `pollDeviceCodeAuth()` — poll token endpoint every N seconds, handle slow_down/pending/expired

3. **`src/services/auth/oauth-tokens.ts`** — Token exchange:
   - `exchangeCodeForTokens()` — POST to tokenUrl with grant_type=authorization_code + code_verifier
   - `refreshOAuthToken()` — POST with grant_type=refresh_token
   - `storeOAuthCredentials()` — dual storage (localStorage + core credential store)

4. **`src-tauri/src/commands/oauth.rs`** — Rust callback server:
   - `oauth_listen(port)` — TCP bind, parse GET /callback?code=X&state=Y, return HTML success page, 120s timeout
   - `oauth_copilot_device_start(client_id, scope)` — POST to github.com/login/device/code
   - `oauth_copilot_device_poll(client_id, device_code)` — POST to github.com/login/oauth/access_token

5. **`crates/ava-config/src/credentials.rs`** — Current credential store:
   - `ProviderCredential { api_key, base_url, org_id }` — needs OAuth fields added
   - `CredentialStore` — load/save/get with env var fallback

6. **`crates/ava-config/src/credential_commands.rs`** — CLI commands:
   - `CredentialCommand::Set/Remove/List/Test`

### Step 2 — Study Codex CLI OAuth (reference implementation)

Read `docs/reference-code/codex-cli/codex-rs/login/src/`:

1. **`pkce.rs`** — Rust PKCE generation (same algorithm as TypeScript version)
2. **`device_code_auth.rs`** — Device code flow with polling + backoff
3. **`server.rs`** — OAuth callback server (blocking variant)
4. **`codex-rs/core/src/auth.rs`** — Auth modes: ApiKey, Chatgpt (OAuth), token refresh
5. **`codex-rs/rmcp-client/src/oauth.rs`** — MCP OAuth with keyring storage

### Step 3 — Study OpenCode Provider List

Read `docs/reference-code/opencode/packages/opencode/src/provider/provider.ts`:

- All 21 providers and their auth methods
- Environment variable names per provider
- Provider priority ordering
- How unconfigured providers are grayed out

## Phase 2: Implementation

### Story 1 — Create `crates/ava-auth/` Crate

New crate for all authentication logic, shared between CLI and Desktop.

```
crates/ava-auth/
├── Cargo.toml
├── src/
│   ├── lib.rs           # pub mod exports
│   ├── config.rs        # OAuthConfig, provider configs
│   ├── pkce.rs          # PKCE generation (verifier, challenge, state)
│   ├── callback.rs      # localhost callback server
│   ├── device_code.rs   # device code flow (request + poll)
│   ├── tokens.rs        # token exchange, refresh, storage
│   └── browser.rs       # open browser URL
```

**Dependencies:** `reqwest`, `tokio`, `serde`, `serde_json`, `sha2`, `base64`, `rand`

#### 1a. PKCE Module (`pkce.rs`)

Port from TypeScript `oauth-config.ts` and Codex CLI `pkce.rs`:

```rust
pub struct PkceParams {
    pub verifier: String,    // 64 random chars [A-Za-z0-9_.-~]
    pub challenge: String,   // SHA-256(verifier) base64url-encoded
    pub state: String,       // 32 random chars for CSRF
}

pub fn generate_pkce() -> PkceParams { ... }
```

#### 1b. OAuth Config (`config.rs`)

Hardcode provider configs (ported from `OAUTH_CONFIGS` in TypeScript):

```rust
pub struct OAuthConfig {
    pub client_id: &'static str,
    pub authorization_url: &'static str,
    pub token_url: &'static str,
    pub scopes: &'static [&'static str],
    pub redirect_port: u16,
    pub redirect_path: &'static str,
    pub extra_params: &'static [(&'static str, &'static str)],
    pub flow: AuthFlow,
}

pub enum AuthFlow {
    Pkce,
    DeviceCode,
    ApiKey,
}

pub fn oauth_config(provider: &str) -> Option<&'static OAuthConfig> { ... }
```

**Hardcoded configs:**

```rust
// OpenAI — PKCE browser login
OAuthConfig {
    client_id: "app_EMoamEEZ73f0CkXaXp7hrann",
    authorization_url: "https://auth.openai.com/oauth/authorize",
    token_url: "https://auth.openai.com/oauth/token",
    scopes: &["openid", "profile", "email", "offline_access"],
    redirect_port: 1455,
    redirect_path: "/auth/callback",
    extra_params: &[
        ("id_token_add_organizations", "true"),
        ("codex_cli_simplified_flow", "true"),
        ("originator", "ava_cli"),
    ],
    flow: AuthFlow::Pkce,
}

// GitHub Copilot — device code
OAuthConfig {
    client_id: "Iv1.b507a08c87ecfe98",
    authorization_url: "https://github.com/login/device/code",
    token_url: "https://github.com/login/oauth/access_token",
    scopes: &["read:user"],
    redirect_port: 0,
    redirect_path: "",
    extra_params: &[],
    flow: AuthFlow::DeviceCode,
}
```

#### 1c. Callback Server (`callback.rs`)

Extract from `src-tauri/src/commands/oauth.rs`:

```rust
pub struct OAuthCallback {
    pub code: String,
    pub state: String,
}

/// Start a one-shot HTTP server on localhost to catch the OAuth redirect.
/// Returns the authorization code and state from the callback URL.
/// Times out after `timeout_secs` seconds (default 120).
pub async fn listen_for_callback(port: u16, timeout_secs: u64) -> Result<OAuthCallback> { ... }
```

- Bind `127.0.0.1:{port}`
- Accept single GET request
- Parse `?code=X&state=Y` from query string
- Return success HTML page to browser
- Return `OAuthCallback { code, state }`

#### 1d. Device Code Flow (`device_code.rs`)

Port from Codex CLI `device_code_auth.rs` and Tauri `oauth_copilot_device_*`:

```rust
pub struct DeviceCodeResponse {
    pub device_code: String,
    pub user_code: String,
    pub verification_uri: String,
    pub expires_in: u64,
    pub interval: u64,
}

pub struct OAuthTokens {
    pub access_token: String,
    pub refresh_token: Option<String>,
    pub expires_at: Option<u64>,  // Unix timestamp seconds
    pub id_token: Option<String>,
}

/// Request a device code from the provider.
pub async fn request_device_code(config: &OAuthConfig) -> Result<DeviceCodeResponse> { ... }

/// Poll for token after user authorizes. Returns tokens or None if expired.
/// Calls `on_status` callback for UI updates (pending, slow_down, etc.)
pub async fn poll_device_code(
    config: &OAuthConfig,
    device_code: &str,
    interval: u64,
    expires_in: u64,
) -> Result<Option<OAuthTokens>> { ... }
```

Polling logic:
- POST to `token_url` with `grant_type=urn:ietf:params:oauth:grant-type:device_code`
- Handle `authorization_pending` → continue
- Handle `slow_down` → increase interval by 5s
- Handle success → return tokens
- Handle expiry/error → return None

#### 1e. Token Exchange & Refresh (`tokens.rs`)

Port from TypeScript `oauth-tokens.ts`:

```rust
/// Exchange authorization code for tokens (PKCE flow).
pub async fn exchange_code_for_tokens(
    config: &OAuthConfig,
    code: &str,
    pkce: &PkceParams,
) -> Result<OAuthTokens> { ... }

/// Refresh an expired access token.
pub async fn refresh_token(
    config: &OAuthConfig,
    refresh_token: &str,
) -> Result<OAuthTokens> { ... }

/// Decode JWT id_token payload (no signature verification — already validated by auth server).
pub fn decode_jwt_payload(jwt: &str) -> Result<serde_json::Value> { ... }

/// Extract ChatGPT account ID from id_token.
pub fn extract_account_id(id_token: &str) -> Option<String> { ... }
```

#### 1f. Browser Opening (`browser.rs`)

```rust
/// Open URL in the user's default browser.
pub fn open_browser(url: &str) -> Result<()> {
    // Use `open` crate (cross-platform: xdg-open / open / start)
    open::that(url).map_err(|e| ...)
}
```

Add `open` crate to dependencies.

### Story 2 — Extend CredentialStore for OAuth Tokens

In `crates/ava-config/src/credentials.rs`, extend `ProviderCredential`:

```rust
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProviderCredential {
    pub api_key: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub base_url: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub org_id: Option<String>,
    // NEW: OAuth fields
    #[serde(skip_serializing_if = "Option::is_none")]
    pub oauth_token: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub oauth_refresh_token: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub oauth_expires_at: Option<u64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub oauth_account_id: Option<String>,
}
```

Add methods:
- `is_oauth_configured(&self) -> bool` — has oauth_token
- `is_oauth_expired(&self) -> bool` — check expires_at vs now
- `effective_api_key(&self) -> Option<&str>` — returns oauth_token if present and not expired, else api_key

Update `get()` to try OAuth token first, then API key, then env var.

Update `redact_key()` to also redact OAuth tokens in list output.

### Story 3 — Expand Provider List (All OpenCode Providers)

Update `known_providers()` in `credentials.rs` to include ALL providers:

```rust
pub struct ProviderInfo {
    pub id: &'static str,
    pub name: &'static str,
    pub auth_flow: AuthFlow,        // Pkce, DeviceCode, ApiKey
    pub env_var: Option<&'static str>,
    pub default_base_url: Option<&'static str>,
}

pub fn all_providers() -> &'static [ProviderInfo] {
    &[
        // OAuth providers (browser login)
        ProviderInfo { id: "openai", name: "OpenAI", auth_flow: AuthFlow::Pkce, env_var: Some("OPENAI_API_KEY"), default_base_url: Some("https://api.openai.com/v1") },
        ProviderInfo { id: "copilot", name: "GitHub Copilot", auth_flow: AuthFlow::DeviceCode, env_var: None, default_base_url: None },

        // API key providers
        ProviderInfo { id: "anthropic", name: "Anthropic", auth_flow: AuthFlow::ApiKey, env_var: Some("ANTHROPIC_API_KEY"), default_base_url: Some("https://api.anthropic.com") },
        ProviderInfo { id: "openrouter", name: "OpenRouter", auth_flow: AuthFlow::ApiKey, env_var: Some("OPENROUTER_API_KEY"), default_base_url: Some("https://openrouter.ai/api/v1") },
        ProviderInfo { id: "gemini", name: "Google Gemini", auth_flow: AuthFlow::ApiKey, env_var: Some("GEMINI_API_KEY"), default_base_url: None },
        ProviderInfo { id: "mistral", name: "Mistral", auth_flow: AuthFlow::ApiKey, env_var: Some("MISTRAL_API_KEY"), default_base_url: Some("https://api.mistral.ai/v1") },
        ProviderInfo { id: "groq", name: "Groq", auth_flow: AuthFlow::ApiKey, env_var: Some("GROQ_API_KEY"), default_base_url: Some("https://api.groq.com/openai/v1") },
        ProviderInfo { id: "xai", name: "xAI (Grok)", auth_flow: AuthFlow::ApiKey, env_var: Some("XAI_API_KEY"), default_base_url: Some("https://api.x.ai/v1") },
        ProviderInfo { id: "deepinfra", name: "DeepInfra", auth_flow: AuthFlow::ApiKey, env_var: Some("DEEPINFRA_API_KEY"), default_base_url: Some("https://api.deepinfra.com/v1/openai") },
        ProviderInfo { id: "together", name: "Together AI", auth_flow: AuthFlow::ApiKey, env_var: Some("TOGETHER_API_KEY"), default_base_url: Some("https://api.together.xyz/v1") },
        ProviderInfo { id: "cerebras", name: "Cerebras", auth_flow: AuthFlow::ApiKey, env_var: Some("CEREBRAS_API_KEY"), default_base_url: Some("https://api.cerebras.ai/v1") },
        ProviderInfo { id: "perplexity", name: "Perplexity", auth_flow: AuthFlow::ApiKey, env_var: Some("PERPLEXITY_API_KEY"), default_base_url: Some("https://api.perplexity.ai") },
        ProviderInfo { id: "cohere", name: "Cohere", auth_flow: AuthFlow::ApiKey, env_var: Some("COHERE_API_KEY"), default_base_url: Some("https://api.cohere.ai/v1") },
        ProviderInfo { id: "azure", name: "Azure OpenAI", auth_flow: AuthFlow::ApiKey, env_var: Some("AZURE_OPENAI_API_KEY"), default_base_url: None },
        ProviderInfo { id: "bedrock", name: "AWS Bedrock", auth_flow: AuthFlow::ApiKey, env_var: Some("AWS_BEARER_TOKEN_BEDROCK"), default_base_url: None },

        // Local providers (no auth)
        ProviderInfo { id: "ollama", name: "Ollama (local)", auth_flow: AuthFlow::ApiKey, env_var: None, default_base_url: Some("http://localhost:11434") },
    ]
}
```

### Story 4 — High-Level Auth Orchestration

In `crates/ava-auth/src/lib.rs`, create a unified auth function:

```rust
/// Start the appropriate auth flow for a provider.
/// Returns the API key or OAuth token to use.
pub async fn authenticate(provider_id: &str) -> Result<AuthResult> {
    let info = provider_info(provider_id)?;

    match info.auth_flow {
        AuthFlow::Pkce => {
            let config = oauth_config(provider_id)?;
            let pkce = generate_pkce();

            // Build auth URL
            let auth_url = build_auth_url(config, &pkce);

            // Start callback server (async, with timeout)
            let callback_fut = listen_for_callback(config.redirect_port, 120);

            // Open browser
            open_browser(&auth_url)?;

            // Wait for callback
            let callback = callback_fut.await?;

            // Validate state
            if callback.state != pkce.state {
                return Err(AuthError::StateMismatch);
            }

            // Exchange code for tokens
            let tokens = exchange_code_for_tokens(config, &callback.code, &pkce).await?;
            Ok(AuthResult::OAuth(tokens))
        }
        AuthFlow::DeviceCode => {
            let config = oauth_config(provider_id)?;

            // Request device code
            let device = request_device_code(config).await?;
            // Caller displays device.user_code and device.verification_uri

            Ok(AuthResult::DeviceCodePending(device))
        }
        AuthFlow::ApiKey => {
            // Caller prompts for API key
            Ok(AuthResult::NeedsApiKey {
                env_var: info.env_var.map(String::from),
            })
        }
    }
}

pub enum AuthResult {
    OAuth(OAuthTokens),
    DeviceCodePending(DeviceCodeResponse),
    NeedsApiKey { env_var: Option<String> },
}
```

### Story 5 — Desktop App Integration (Tauri Bridge)

Update `src-tauri/src/commands/oauth.rs` to call `ava-auth` instead of inline code:

```rust
// BEFORE: inline TCP server code
// AFTER: delegate to shared crate
#[tauri::command]
pub async fn oauth_listen(port: u16) -> Result<OAuthCallback, String> {
    ava_auth::callback::listen_for_callback(port, 120)
        .await
        .map_err(|e| e.to_string())
}
```

Same for `oauth_copilot_device_start` and `oauth_copilot_device_poll`.

Update `src-tauri/Cargo.toml` to depend on `ava-auth`.

The TypeScript frontend continues calling the same Tauri commands — zero frontend changes needed.

### Story 6 — TUI `/connect` OAuth Integration

Update `crates/ava-tui/src/widgets/provider_connect.rs` to handle all three auth flows:

**Screen 1 — Provider List** (already exists from Sprint 51b, update it):

```
── Provider Status ──────────────────────────
  ✓  openrouter        sk-or...a1b2         API key
  ✓  anthropic         sk-an...c3d4         API key
  ✓  openai            OAuth (ChatGPT)      Browser login
  ✗  copilot           not configured       Device code
  ✗  gemini            not configured       API key
  ✗  mistral           not configured       API key
  ✗  groq              not configured       API key
  ✗  xai               not configured       API key
  ✗  deepinfra         not configured       API key
  ✗  together          not configured       API key
  ●  ollama            localhost:11434      Local

  [Enter] Configure  [d] Disconnect  [t] Test  [Esc] Close
```

Show auth flow type badge next to each provider.

**Screen 2a — API Key Input** (for ApiKey providers — already exists):
Keep as is.

**Screen 2b — Browser OAuth** (for PKCE providers like OpenAI):

```
── Sign in to OpenAI ────────────────────────

  Opening browser for authentication...

  If the browser didn't open, visit:
  https://auth.openai.com/oauth/authorize?...

  ⠹ Waiting for authorization... 15s

  [Esc] Cancel
```

Flow:
1. Call `ava_auth::authenticate("openai")` which opens browser + starts callback server
2. Show waiting spinner
3. On callback: exchange code → store token → show success
4. On timeout/cancel: show error

**Screen 2c — Device Code** (for GitHub Copilot):

```
── Sign in to GitHub Copilot ────────────────

  Enter this code on github.com:

      ABCD-1234

  Visit: https://github.com/login/device

  ⠹ Waiting for authorization... 45s

  [Enter] Open browser  [Esc] Cancel
```

Flow:
1. Call `request_device_code()` to get user_code
2. Display code prominently (bold, large)
3. Enter opens browser to verification_uri
4. Poll in background via `poll_device_code()`
5. On success: store token → show success message
6. On expiry: show "Code expired, try again"

### Story 7 — Token Refresh Integration

In `crates/ava-llm/src/providers/` (for providers that use OAuth tokens):

Before each API call, check if the OAuth token is expired:

```rust
// In provider's generate/generate_stream method:
async fn ensure_valid_token(&self, store: &mut CredentialStore) -> Result<String> {
    let cred = store.get(&self.provider_name)?;

    if let Some(ref oauth_token) = cred.oauth_token {
        if cred.is_oauth_expired() {
            if let Some(ref refresh) = cred.oauth_refresh_token {
                let config = ava_auth::oauth_config(&self.provider_name)?;
                let new_tokens = ava_auth::refresh_token(config, refresh).await?;
                // Update stored tokens
                store.update_oauth_tokens(&self.provider_name, &new_tokens)?;
                return Ok(new_tokens.access_token);
            }
            return Err(AuthError::TokenExpiredNoRefresh);
        }
        return Ok(oauth_token.clone());
    }

    Ok(cred.api_key.clone())
}
```

### Story 8 — `ava auth` CLI Subcommand

Add `auth` subcommand to the CLI for headless/scriptable auth:

In `crates/ava-tui/src/config/cli.rs`:

```rust
#[derive(Subcommand)]
pub enum AuthCommand {
    /// Sign in to a provider (opens browser for OAuth, prompts for API key)
    Login { provider: String },
    /// Remove credentials for a provider
    Logout { provider: String },
    /// List all configured providers
    List,
    /// Test connection to a provider
    Test { provider: String },
}
```

Behavior:
- `ava auth login openai` → opens browser, PKCE flow, prints success
- `ava auth login copilot` → shows device code, polls, prints success
- `ava auth login anthropic` → prompts for API key (stdin), saves
- `ava auth list` → prints provider status table
- `ava auth test openrouter` → tests connection, prints result
- `ava auth logout openai` → removes credentials

## Files Modified

**New crate:**
- `crates/ava-auth/Cargo.toml`
- `crates/ava-auth/src/lib.rs`
- `crates/ava-auth/src/config.rs`
- `crates/ava-auth/src/pkce.rs`
- `crates/ava-auth/src/callback.rs`
- `crates/ava-auth/src/device_code.rs`
- `crates/ava-auth/src/tokens.rs`
- `crates/ava-auth/src/browser.rs`

**Modified:**
- `Cargo.toml` (workspace members)
- `crates/ava-config/src/credentials.rs` (OAuth fields, all_providers)
- `crates/ava-config/src/credential_commands.rs` (auth flow dispatch)
- `crates/ava-config/Cargo.toml` (depend on ava-auth)
- `crates/ava-tui/Cargo.toml` (depend on ava-auth)
- `crates/ava-tui/src/widgets/provider_connect.rs` (OAuth UI screens)
- `crates/ava-tui/src/app/modals.rs` (OAuth modal state machine)
- `crates/ava-tui/src/config/cli.rs` (auth subcommand)
- `src-tauri/Cargo.toml` (depend on ava-auth)
- `src-tauri/src/commands/oauth.rs` (delegate to ava-auth)

## Validation

```bash
cargo test --workspace
cargo clippy --workspace

# Unit tests:
cargo test -p ava-auth          # PKCE generation, JWT decode, URL building
cargo test -p ava-config        # OAuth credential storage, provider list

# Manual TUI testing:
# 1. cargo run --bin ava
# 2. /connect → provider list shows all 16 providers with auth flow badges
# 3. Select OpenAI → browser opens → authorize → token stored → ✓ Connected
# 4. Select GitHub Copilot → device code shown → enter on github.com → ✓ Connected
# 5. Select Anthropic → API key input (masked) → save → ✓ Connected
# 6. /providers → all configured show ✓ with redacted keys/tokens
# 7. Test disconnect → removes credentials

# CLI testing:
# 8. ava auth login openai → browser opens, PKCE flow completes
# 9. ava auth login copilot → device code displayed, polls until authorized
# 10. ava auth login anthropic → prompts for API key
# 11. ava auth list → shows all providers with status
# 12. ava auth test openrouter → tests connection
# 13. ava auth logout openai → removes OAuth token

# Desktop testing (if applicable):
# 14. npm run tauri dev → settings → providers → OAuth still works via shared crate
```

## Rules

- Phase 1 (research) MUST complete before Phase 2
- Read ACTUAL source code in `src/services/auth/`, `src-tauri/src/commands/oauth.rs`, and `docs/reference-code/` — don't guess
- ALL crypto (PKCE, random) must use cryptographically secure sources (`rand::rngs::OsRng` or `getrandom`)
- NEVER log or display full API keys or OAuth tokens — always redact
- OAuth client IDs are public (safe to hardcode) — client SECRETS must never be hardcoded
- State parameter validation is MANDATORY (CSRF protection)
- Token storage file MUST have 0o600 permissions (already enforced by CredentialStore)
- The `ava-auth` crate must NOT depend on TUI or Tauri crates (it's a shared library)
- Desktop app integration should be minimal — just replace inline Tauri command code with `ava-auth` calls
- `serde(skip_serializing_if = "Option::is_none")` on all new optional credential fields to keep JSON clean
- Conventional commit: `feat(auth): oauth providers — pkce, device code, shared crate`
