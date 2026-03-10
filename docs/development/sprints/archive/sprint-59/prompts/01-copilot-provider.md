# Sprint 59: GitHub Copilot Provider + Sub-Agent Requests

## Context

You are working on **AVA**, a Rust-first AI coding agent with a Ratatui TUI. The project uses a workspace of ~21 crates under `crates/`. GitHub Copilot device code authentication already works (users can connect), but there is **no LLM provider implementation** — connecting does nothing because `create_provider()` doesn't handle `"copilot"` and no models appear in the selector.

### Key conventions
- Read `CLAUDE.md` and `AGENTS.md` at the project root before starting
- All new code is Rust — no TypeScript
- Run `cargo clippy --workspace` and `cargo test --workspace` after each phase
- Provider trait: `LLMProvider` in `crates/ava-llm/src/provider.rs`
- Existing providers: `crates/ava-llm/src/providers/` (anthropic, openai, gemini, ollama, openrouter)
- Credentials: `crates/ava-config/src/credentials.rs` — `ProviderCredential` with `oauth_token`, `oauth_refresh_token`, `oauth_expires_at` fields
- Model catalog: `crates/ava-config/src/model_catalog/` — `fallback.rs` (hardcoded), `fetch.rs` (models.dev), `types.rs`
- Model selector UI: `crates/ava-tui/src/widgets/model_selector.rs` — `PROVIDER_SECTIONS`, `ModelSection` enum
- Auth: `crates/ava-auth/` — device code flow already implemented, OAuth config has Copilot client ID `Iv1.b507a08c87ecfe98`

### Reference implementations (READ THESE FIRST)
- **Goose (Rust)**: `docs/reference-code/goose/crates/goose/src/providers/githubcopilot.rs` — full Rust provider with token exchange
- **OpenCode**: `docs/reference-code/opencode/packages/opencode/src/plugin/copilot.ts` — sub-agent header injection, token exchange
- **OpenCode**: `docs/reference-code/opencode/packages/opencode/src/provider/sdk/copilot/copilot-provider.ts` — provider SDK
- **pi-mono**: `docs/reference-code/pi-mono/packages/ai/src/utils/oauth/github-copilot.ts` — OAuth + token refresh
- **pi-mono**: `docs/reference-code/pi-mono/packages/ai/src/providers/github-copilot-headers.ts` — header construction, x-initiator logic

---

## Phase 1: Research & Understand

Read ALL reference implementations listed above. Understand:
1. The token exchange flow (GitHub OAuth token → Copilot API token)
2. How the Copilot API token response contains the regional API endpoint
3. The `x-initiator` header and how sub-agent requests are detected
4. The dual API format (Anthropic for Claude models, OpenAI for GPT/Gemini)
5. Required headers (User-Agent, Editor-Version, etc.)
6. How reasoning/thinking responses work with `reasoning_text` + `reasoning_opaque`

Also read these existing files to understand conventions:
- `crates/ava-llm/src/providers/mod.rs` — provider factory
- `crates/ava-llm/src/providers/anthropic.rs` — AnthropicProvider (has `third_party` flag for custom base URLs)
- `crates/ava-llm/src/providers/openai.rs` — OpenAIProvider (has `with_base_url()`)
- `crates/ava-llm/src/provider.rs` — LLMProvider trait
- `crates/ava-config/src/credentials.rs` — ProviderCredential struct
- `crates/ava-auth/src/device_code.rs` — existing device code flow
- `crates/ava-auth/src/tokens.rs` — OAuthTokens struct

**Before proceeding to Phase 2, invoke the Code Reviewer sub-agent to verify you fully understand the token exchange flow and have identified all files that need modification.**

---

## Phase 2: Copilot Token Exchange

The GitHub OAuth token obtained from device code flow is NOT directly usable with the Copilot API. It must be exchanged for a Copilot-specific token.

### Task 2a: Add token exchange to `ava-auth`

Create `crates/ava-auth/src/copilot.rs`:

```rust
/// Copilot-specific token exchange and management.
///
/// Flow: GitHub OAuth access_token → POST api.github.com/copilot_internal/v2/token
///     → { token, expires_at, endpoints: { api: "https://api.individual.githubcopilot.com" } }

pub struct CopilotToken {
    /// The Copilot API token (NOT the GitHub OAuth token).
    pub token: String,
    /// Unix timestamp when this token expires.
    pub expires_at: u64,
    /// Regional API endpoint extracted from token response.
    pub api_endpoint: String,
}
```

Implementation details:
- `POST https://api.github.com/copilot_internal/v2/token` with `Authorization: token {github_oauth_token}` header
- Parse response JSON: `{ "token": "...", "expires_at": 1234, "endpoints": { "api": "https://..." } }`
- If `endpoints.api` is missing, fall back to extracting from the token string itself: parse `proxy-ep=xxx` field and convert `proxy.xxx` → `api.xxx`
- Default fallback: `https://api.individual.githubcopilot.com`
- Token typically expires in ~30 minutes — must be refreshable
- Add `exchange_copilot_token(github_token: &str) -> Result<CopilotToken, AuthError>`
- Add `is_expired(&self) -> bool` method
- Register module in `crates/ava-auth/src/lib.rs`

### Task 2b: Store Copilot token in credentials

The exchanged Copilot token needs caching. Options:
- Store `copilot_token` and `copilot_token_expires_at` and `copilot_api_endpoint` in `ProviderCredential`
- OR use a separate in-memory cache in the provider (preferred — token is short-lived, ~30 min)

Choose the in-memory approach: the `CopilotProvider` will cache the token internally and re-exchange when expired.

**Before proceeding to Phase 3, invoke the Code Reviewer sub-agent to verify the token exchange implementation is correct and handles edge cases (expired tokens, network failures, missing endpoint fields).**

---

## Phase 3: CopilotProvider Implementation

Create `crates/ava-llm/src/providers/copilot.rs` implementing the `LLMProvider` trait.

### Key design decisions:

1. **Dual API routing**: Copilot serves Claude models via Anthropic Messages API and GPT/Gemini via OpenAI Chat Completions API. The provider must detect which format to use based on model name:
   - `claude-*` → Anthropic Messages format (`/chat/completions` endpoint but with Anthropic message structure)
   - Everything else → OpenAI Chat Completions format (`/chat/completions`)

   **Important**: Despite the dual API formats, ALL requests go through the SAME `/chat/completions` endpoint on the Copilot API. The Copilot proxy handles the routing. So use OpenAI-compatible format for ALL models — the proxy translates for Claude models automatically.

2. **Token management**: Hold a `RwLock<Option<CopilotToken>>` internally. Before each request, check if token is expired and re-exchange if needed.

3. **Sub-agent header (`x-initiator`)**: This is the KEY feature. Add a method or flag to mark requests as agent-initiated:
   - Inspect the messages array: if the last message role is NOT `"user"`, set `x-initiator: agent`
   - Otherwise set `x-initiator: user`
   - This must be done in the `generate()` and `generate_stream()` methods

4. **Required headers** (on EVERY request):
   ```
   Authorization: Bearer {copilot_token}
   X-Initiator: user|agent
   Openai-Intent: conversation-edits
   User-Agent: GitHubCopilotChat/0.35.0
   Editor-Version: vscode/1.107.0
   Editor-Plugin-Version: copilot-chat/0.35.0
   Copilot-Integration-Id: vscode-chat
   Content-Type: application/json
   Accept: application/json
   ```

5. **Streaming**: Use SSE streaming (same as OpenAI provider). The Copilot API returns standard OpenAI-format streaming chunks.

6. **Cost**: All models are $0 (subscription-billed). `estimate_cost()` should return 0.

### Structure:

```rust
pub struct CopilotProvider {
    pool: Arc<ConnectionPool>,
    github_oauth_token: String,
    model: String,
    cached_token: Arc<RwLock<Option<CopilotToken>>>,
}

impl CopilotProvider {
    pub fn new(pool: Arc<ConnectionPool>, github_oauth_token: String, model: &str) -> Self { ... }

    /// Ensure we have a valid Copilot API token, exchanging if needed.
    async fn ensure_token(&self) -> Result<CopilotToken> { ... }

    /// Determine x-initiator value from message history.
    fn infer_initiator(messages: &[Message]) -> &'static str { ... }

    /// Build headers for a Copilot API request.
    fn build_headers(token: &str, initiator: &str) -> HeaderMap { ... }
}
```

### Register in `crates/ava-llm/src/providers/mod.rs`:

1. Add `pub mod copilot;` and `pub use copilot::CopilotProvider;`
2. Add to `base_url_for_provider()`: `"copilot" => Some("https://api.individual.githubcopilot.com")` (note: actual URL comes from token exchange, this is just the fallback)
3. Add match arm in `create_provider()`:
   ```rust
   "copilot" => {
       let entry = credential.ok_or_else(|| AvaError::MissingApiKey {
           provider: "copilot".to_string(),
       })?;
       // Copilot uses OAuth token, not API key
       let oauth_token = entry.oauth_token.as_deref()
           .ok_or_else(|| AvaError::MissingApiKey {
               provider: "copilot (not connected — use /connect copilot)".to_string(),
           })?;
       Ok(Box::new(CopilotProvider::new(pool, oauth_token.to_string(), model)))
   }
   ```
4. Update the error message in the `_` fallback arm to include `copilot` in the available providers list

**Before proceeding to Phase 4, invoke the Code Reviewer sub-agent to verify:**
- The provider correctly implements all 5 LLMProvider trait methods
- Token exchange is lazy (only on first request) and cached
- x-initiator logic correctly inspects last message role
- All required headers are present
- Error handling is robust (network failures, token expiry, invalid responses)

---

## Phase 4: Model Catalog + Selector UI

### Task 4a: Add Copilot models to fallback catalog

In `crates/ava-config/src/model_catalog/fallback.rs`:

1. Add to `CURATED_MODELS`:
```rust
("copilot", &[
    "claude-sonnet-4",
    "claude-sonnet-4.5",
    "claude-sonnet-4.6",
    "claude-opus-4.5",
    "claude-opus-4.6",
    "claude-haiku-4.5",
    "gpt-4.1",
    "gpt-4o",
    "gpt-5",
    "gpt-5-mini",
    "gpt-5.1",
    "gpt-5.1-codex",
    "gpt-5.1-codex-max",
    "gpt-5.2",
    "gemini-2.5-pro",
    "o3-mini",
]),
```

2. Add to `fallback_catalog()` with $0 cost for all models:
```rust
providers.insert(
    "copilot".to_string(),
    vec![
        CatalogModel {
            id: "claude-sonnet-4.6".to_string(),
            name: "Claude Sonnet 4.6".to_string(),
            provider_id: "copilot".to_string(),
            tool_call: true,
            cost_input: Some(0.0),
            cost_output: Some(0.0),
            context_window: Some(200_000),
            max_output: Some(64_000),
        },
        // ... add all models with appropriate context_window/max_output
    ],
);
```

Use these context/output limits from the reference:
- Claude models: 128k context, 16-64k output (varies by model)
- GPT models: 64-128k context, 16-128k output
- Gemini models: 128k context, 64k output

### Task 4b: Add Copilot section to model selector

In `crates/ava-tui/src/widgets/model_selector.rs`:

1. Add `Copilot` variant to `ModelSection` enum:
```rust
pub enum ModelSection {
    Recent,
    Copilot,  // NEW — before Anthropic since it's popular + free
    Anthropic,
    OpenAI,
    OpenRouter,
    Gemini,
    Ollama,
}
```

2. Add label:
```rust
Self::Copilot => "GitHub Copilot (free)".to_string(),
```

3. Add to `PROVIDER_SECTIONS` — put it FIRST (before anthropic) since it's free and popular:
```rust
const PROVIDER_SECTIONS: &[ProviderEntry] = &[
    ("copilot", "copilot", || ModelSection::Copilot),  // NEW
    ("anthropic", "anthropic", || ModelSection::Anthropic),
    ("openai", "openai", || ModelSection::OpenAI),
    ("openrouter", "openrouter", || ModelSection::OpenRouter),
    ("google", "gemini", || ModelSection::Gemini),
];
```

4. The `build_select_items()` function already handles OAuth-configured providers via `is_oauth_configured()` check — Copilot should work automatically since it stores `oauth_token`.

### Task 4c: Fetch models from Copilot API (optional enhancement)

The Copilot API has a `/models` endpoint that returns the current model list. Consider adding a fetch in the model catalog:
- `GET {copilot_api_endpoint}/models` with Bearer token
- Parse response to get available model IDs
- This would keep the model list current without hardcoding

If time-constrained, skip this and rely on the hardcoded fallback list.

**Before proceeding to Phase 5, invoke the Code Reviewer sub-agent to verify the catalog entries have correct context/output limits and the model selector properly shows Copilot models when connected.**

---

## Phase 5: Sub-Agent Request Infrastructure

The `x-initiator` header is Copilot-specific, but the concept of "agent-initiated vs user-initiated" turns could benefit other providers too. Implement it cleanly.

### Task 5a: Add initiator detection to LLMProvider trait (optional)

Two approaches:
1. **Copilot-only** (simpler): Just implement `infer_initiator()` inside `CopilotProvider` and add the header there
2. **Trait-level** (more general): Add an optional method to `LLMProvider` trait

**Choose approach 1** (Copilot-only) for now. Keep it simple.

### Task 5b: Implement x-initiator logic

In `CopilotProvider`:
```rust
/// Determine if this is a user-initiated or agent-initiated request.
/// Agent-initiated = tool responses, continuations (last message role != "user")
/// Copilot doesn't charge extra for agent-initiated turns.
fn infer_initiator(messages: &[Message]) -> &'static str {
    match messages.last() {
        Some(msg) if msg.role == "user" => "user",
        _ => "agent",
    }
}
```

This maps to OpenCode's logic:
```typescript
const last = body.messages[body.messages.length - 1]
const isAgent = last?.role !== "user"
headers["x-initiator"] = isAgent ? "agent" : "user"
```

**Before proceeding to Phase 6, invoke the Code Reviewer sub-agent to verify the x-initiator logic correctly identifies agent vs user turns and is applied to all request paths (streaming and non-streaming).**

---

## Phase 6: Update Auth Description + Connection Pool Pre-warming

### Task 6a: Update provider auth metadata

In `crates/ava-auth/src/lib.rs`, update the Copilot `ProviderInfo`:
```rust
ProviderInfo {
    id: "copilot",
    name: "GitHub Copilot",
    description: "Free with Copilot subscription",  // Was empty ""
    auth_flows: &[AuthFlow::DeviceCode],
    env_var: None,
    default_base_url: Some("https://api.individual.githubcopilot.com"),
    group: ProviderGroup::Popular,
},
```

### Task 6b: Add pre-warming

In `crates/ava-llm/src/providers/mod.rs`, add to `base_url_for_provider()`:
```rust
"copilot" => Some("https://api.individual.githubcopilot.com"),
```

This enables connection pool pre-warming on startup.

### Task 6c: Update provider count in tests

Check `crates/ava-auth/src/lib.rs` test `all_providers_has_expected_count()` — update if provider count changed.

**Before proceeding to Phase 7, invoke the Code Reviewer sub-agent to verify auth metadata is complete and pre-warming configuration is correct.**

---

## Phase 7: Tests

### Task 7a: Unit tests for token exchange

In `crates/ava-auth/src/copilot.rs`:
- Test `CopilotToken::is_expired()` with expired/valid timestamps
- Test endpoint extraction from token string (proxy-ep parsing)
- Test fallback to default endpoint

### Task 7b: Unit tests for CopilotProvider

In `crates/ava-llm/src/providers/copilot.rs`:
- Test `infer_initiator()` with various message sequences:
  - Last message is user → "user"
  - Last message is assistant → "agent"
  - Last message is tool → "agent"
  - Empty messages → "agent"
- Test `build_headers()` includes all required headers
- Test model name → API format routing (claude-* vs others)

### Task 7c: Integration test for model catalog

- Test that `fallback_catalog()` contains copilot models
- Test that `CURATED_MODELS` includes copilot entry
- Test that copilot models have $0 cost

### Task 7d: Model selector test

- Test that Copilot section appears when OAuth is configured
- Test that Copilot section is hidden when not connected

### Verification

Run and confirm:
```bash
cargo test --workspace
cargo clippy --workspace
```

All tests must pass, zero clippy warnings.

**Invoke the Code Reviewer sub-agent for a FINAL review of ALL changes across all phases. Verify:**
1. Token exchange flow is complete (OAuth → Copilot token → API requests)
2. CopilotProvider implements all LLMProvider trait methods correctly
3. x-initiator header is set on every request based on last message role
4. Model catalog has correct models with $0 cost
5. Model selector shows Copilot section when connected
6. All required Copilot headers are present (User-Agent, Editor-Version, etc.)
7. Error handling is robust throughout
8. No clippy warnings, all tests pass

---

## Files Modified (Expected)

| File | Change |
|------|--------|
| `crates/ava-auth/src/lib.rs` | Register copilot module, update description |
| `crates/ava-auth/src/copilot.rs` | **NEW** — token exchange |
| `crates/ava-llm/src/providers/mod.rs` | Add copilot module, create_provider arm, base_url |
| `crates/ava-llm/src/providers/copilot.rs` | **NEW** — CopilotProvider |
| `crates/ava-config/src/model_catalog/fallback.rs` | Add copilot models |
| `crates/ava-tui/src/widgets/model_selector.rs` | Add Copilot section |

## Acceptance Criteria

- [ ] `cargo test --workspace` passes
- [ ] `cargo clippy --workspace` clean
- [ ] Copilot appears in model selector when OAuth-connected
- [ ] All Copilot models show "$0" / "free" cost
- [ ] Token exchange happens lazily on first API request
- [ ] Expired tokens are automatically re-exchanged
- [ ] `x-initiator: agent` header sent on tool-response/continuation turns
- [ ] `x-initiator: user` header sent on user-initiated turns
- [ ] All required Copilot headers present on every request
- [ ] Claude models and GPT/Gemini models both work through the same endpoint
