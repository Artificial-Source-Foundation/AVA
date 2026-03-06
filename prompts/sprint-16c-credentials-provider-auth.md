# Sprint 16c: Credentials & Provider Auth (Rust)

> For AI coding agent. Estimated: 5 features, mix S/M effort.
> Run `cargo test --workspace` after each feature.
> Depends on: Sprint 16a (Rust agent stack must compile)

---

## Role

You are implementing Sprint 16c (Credentials & Provider Auth) for AVA, a multi-agent AI coding assistant.

Read these files first:
- `CLAUDE.md` (conventions, Rust-first architecture)
- `AGENTS.md` (code standards, common workflows)
- `crates/ava-config/src/lib.rs` (current config system)
- `crates/ava-llm/src/provider.rs` (LLM provider trait)
- `crates/ava-llm/src/providers/` (existing provider impls)

**Context**: AVA's Rust LLM providers (`crates/ava-llm/`) currently have no way to load API keys at runtime. The `ava-config` crate has a single `api_key: Option<String>` field, but real usage needs per-provider credentials (Anthropic, OpenAI, Google, OpenRouter, Ollama each have different auth). This sprint adds a proper credential store, wires it into the LLM providers, and adds CLI credential management commands.

**Why**: Without this, the Rust agent stack (Sprint 16a) can't actually call any LLM API. This is a critical path blocker.

---

## Pre-Implementation: Read Existing Code

Before writing any code, read:
- `crates/ava-config/src/lib.rs` — Current Config struct (needs CredentialStore integration)
- `crates/ava-llm/src/providers/anthropic.rs` — How Anthropic provider is constructed
- `crates/ava-llm/src/providers/openai.rs` — How OpenAI provider is constructed
- `crates/ava-llm/src/providers/openrouter.rs` — How OpenRouter provider is constructed
- `crates/ava-llm/src/providers/gemini.rs` — How Gemini provider is constructed
- `crates/ava-llm/src/providers/ollama.rs` — How Ollama provider is constructed (no key needed)
- `crates/ava-llm/src/router.rs` — ModelRouter (needs credential injection)

---

## Feature 1: Credential Store

### What to Build
A secure credential storage system in `ava-config` that manages per-provider API keys.

**File:** `crates/ava-config/src/credentials.rs` (new)

```rust
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::{Path, PathBuf};

/// Per-provider credential entry
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProviderCredential {
    /// API key or token
    pub api_key: String,
    /// Optional base URL override (e.g., custom OpenAI-compatible endpoint)
    pub base_url: Option<String>,
    /// Optional organization ID (OpenAI)
    pub org_id: Option<String>,
}

/// Credential store — loads from ~/.ava/credentials.json
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct CredentialStore {
    /// Provider name -> credential mapping
    /// Keys: "anthropic", "openai", "openrouter", "gemini", "ollama", etc.
    pub providers: HashMap<String, ProviderCredential>,
}

impl CredentialStore {
    /// Load credentials from file, falling back to env vars
    pub async fn load(path: &Path) -> Result<Self>;

    /// Load from default path (~/.ava/credentials.json)
    pub async fn load_default() -> Result<Self>;

    /// Save credentials to file (chmod 600 on Unix)
    pub async fn save(&self, path: &Path) -> Result<()>;

    /// Get credential for a provider, checking file then env var
    /// Env var pattern: AVA_<PROVIDER>_API_KEY (e.g., AVA_ANTHROPIC_API_KEY)
    /// Also checks standard env vars: ANTHROPIC_API_KEY, OPENAI_API_KEY, etc.
    pub fn get(&self, provider: &str) -> Option<&ProviderCredential>;

    /// Set credential for a provider
    pub fn set(&mut self, provider: &str, credential: ProviderCredential);

    /// Remove credential for a provider
    pub fn remove(&mut self, provider: &str) -> bool;

    /// List all configured provider names
    pub fn providers(&self) -> Vec<&str>;

    /// Check which providers have valid credentials (key + optional connectivity)
    pub fn configured_providers(&self) -> Vec<&str>;
}
```

**Implementation details:**
- File format: JSON at `~/.ava/credentials.json`
- On Unix: `chmod 600` the credentials file after writing (only owner can read)
- Env var fallback order:
  1. `AVA_ANTHROPIC_API_KEY` (AVA-specific)
  2. `ANTHROPIC_API_KEY` (standard)
  3. File-based credential
- For Ollama: no key needed, but allow base_url override
- Validate that the file doesn't contain placeholder values ("sk-xxx", "your-key-here")

### Tests
- `crates/ava-config/src/credentials.rs` (inline tests)
- Test: Load from file
- Test: Save and reload roundtrip
- Test: Env var fallback (set env, verify it's picked up)
- Test: Provider CRUD (set, get, remove, list)
- Test: Default path resolves to ~/.ava/credentials.json
- Test: Missing file returns empty store (not error)

---

## Feature 2: Wire Credentials into LLM Providers

### What to Build
Update each LLM provider constructor to accept credentials from the CredentialStore.

**File:** `crates/ava-llm/src/providers/mod.rs` (modify)

Add a factory function:

```rust
use ava_config::CredentialStore;

/// Create a provider from the credential store
pub fn create_provider(
    provider_name: &str,
    model: &str,
    credentials: &CredentialStore,
) -> Result<Box<dyn LLMProvider>> {
    let cred = credentials.get(provider_name);

    match provider_name {
        "anthropic" => {
            let key = cred
                .map(|c| c.api_key.clone())
                .or_else(|| std::env::var("ANTHROPIC_API_KEY").ok())
                .ok_or_else(|| AvaError::ConfigError(
                    "No Anthropic API key. Set AVA_ANTHROPIC_API_KEY or add to ~/.ava/credentials.json".into()
                ))?;
            Ok(Box::new(AnthropicProvider::new(&key, model)))
        }
        "openai" => { /* similar */ }
        "openrouter" => { /* similar, with base_url override */ }
        "gemini" => { /* similar */ }
        "ollama" => {
            let base_url = cred
                .and_then(|c| c.base_url.clone())
                .unwrap_or_else(|| "http://localhost:11434".to_string());
            Ok(Box::new(OllamaProvider::new(&base_url, model)))
        }
        _ => Err(AvaError::ConfigError(format!("Unknown provider: {}", provider_name))),
    }
}
```

**Modify each provider** to accept key in constructor:
- `anthropic.rs`: `AnthropicProvider::new(api_key: &str, model: &str)`
- `openai.rs`: `OpenAIProvider::new(api_key: &str, model: &str)`
- `openrouter.rs`: `OpenRouterProvider::new(api_key: &str, model: &str)`
- `gemini.rs`: `GeminiProvider::new(api_key: &str, model: &str)`
- `ollama.rs`: `OllamaProvider::new(base_url: &str, model: &str)`

**Update Cargo.toml:** Add `ava-config` as dependency of `ava-llm`.

### Tests
- Test: `create_provider("anthropic", "claude-sonnet-4-20250514", &store)` succeeds with valid key
- Test: `create_provider("anthropic", ...)` fails with clear error when no key
- Test: `create_provider("ollama", ...)` succeeds without key
- Test: `create_provider("unknown", ...)` returns error
- Test: base_url override for OpenRouter/Ollama

---

## Feature 3: ModelRouter Credential Integration

### What to Build
Update the ModelRouter to use CredentialStore when creating providers for routing.

**File:** `crates/ava-llm/src/router.rs` (modify)

Read this file first, then update:
- `ModelRouter::new()` should accept `CredentialStore`
- When routing to a provider, use `create_provider()` with credentials
- Cache created provider instances (avoid recreating on every call)
- Support runtime credential updates (e.g., user adds key mid-session)

```rust
pub struct ModelRouter {
    credentials: Arc<RwLock<CredentialStore>>,
    providers: RwLock<HashMap<String, Arc<dyn LLMProvider>>>,
    // ... existing fields
}

impl ModelRouter {
    pub fn new(credentials: CredentialStore) -> Self;

    /// Update credentials at runtime
    pub async fn update_credentials(&self, credentials: CredentialStore);

    /// Route to provider, creating if needed
    pub async fn route(&self, provider: &str, model: &str) -> Result<Arc<dyn LLMProvider>>;

    /// List available providers (those with credentials configured)
    pub async fn available_providers(&self) -> Vec<String>;
}
```

### Tests
- Test: Route to cached provider (same instance returned)
- Test: Credential update invalidates cache
- Test: Available providers matches configured credentials
- Test: Route to unconfigured provider returns clear error

---

## Feature 4: CLI Credential Commands

### What to Build
CLI subcommands for managing credentials, usable from the future Ratatui TUI or as standalone commands.

**File:** `crates/ava-config/src/credential_commands.rs` (new)

```rust
/// CLI-callable credential operations
pub enum CredentialCommand {
    /// Set a provider's API key
    Set { provider: String, api_key: String, base_url: Option<String> },
    /// Remove a provider's credentials
    Remove { provider: String },
    /// List all configured providers (redacted keys)
    List,
    /// Test a provider's credentials by making a minimal API call
    Test { provider: String },
}

/// Execute a credential command
pub async fn execute_credential_command(
    cmd: CredentialCommand,
    store: &mut CredentialStore,
) -> Result<String>;
```

**Implementation:**
- `Set`: Save to store, write file, confirm "Anthropic API key saved"
- `Remove`: Remove from store, write file, confirm
- `List`: Show providers with redacted keys ("sk-...abc1", first 4 + last 4 chars)
- `Test`: Call `create_provider()`, send a minimal prompt ("Hello"), verify response comes back
  - Print: "anthropic: OK (claude-sonnet-4-20250514 responded in 1.2s)"
  - Print: "openai: FAIL (401 Unauthorized)"

**Key redaction function:**
```rust
fn redact_key(key: &str) -> String {
    if key.len() <= 8 {
        "****".to_string()
    } else {
        format!("{}...{}", &key[..4], &key[key.len()-4..])
    }
}
```

### Tests
- Test: Set and list shows redacted key
- Test: Remove and list shows empty
- Test: Redact function with various key lengths
- Test: Test command with mock provider

---

## Feature 5: Integration — ConfigManager + CredentialStore

### What to Build
Wire CredentialStore into ConfigManager so credentials load automatically at startup.

**File:** `crates/ava-config/src/lib.rs` (modify)

```rust
pub struct ConfigManager {
    config: Arc<RwLock<Config>>,
    credentials: Arc<RwLock<CredentialStore>>,
    config_path: PathBuf,
    credentials_path: PathBuf,
}

impl ConfigManager {
    /// Load both config and credentials
    pub async fn load() -> Result<Self>;

    /// Get credential store
    pub async fn credentials(&self) -> CredentialStore;

    /// Update credentials
    pub async fn update_credentials<F>(&self, f: F) -> Result<()>
    where
        F: FnOnce(&mut CredentialStore);

    /// Save credentials to disk
    pub async fn save_credentials(&self) -> Result<()>;
}
```

**Default paths:**
- Config: `~/.config/ava/config.yaml` (or `$XDG_CONFIG_HOME/ava/config.yaml`)
- Credentials: `~/.ava/credentials.json` (separate from config for security)

**Also update `crates/ava-config/src/lib.rs`:**
- Add `pub mod credentials;` and `pub mod credential_commands;`
- Re-export `CredentialStore` and `ProviderCredential` from crate root

### Tests
- Test: ConfigManager loads both config and credentials
- Test: Credentials path defaults to ~/.ava/credentials.json
- Test: Update credentials and save roundtrip
- Test: Missing credentials file doesn't error (empty store)

---

## Post-Implementation Verification

After ALL 5 features:

1. `cargo test -p ava-config` — credential store tests
2. `cargo test -p ava-llm` — provider factory tests
3. `cargo test --workspace` — full workspace
4. `cargo clippy --workspace` — no warnings
5. Verify `~/.ava/credentials.json` format works with existing credentials
6. Commit: `git commit -m "feat(sprint-16c): credential store and provider auth for Rust agent stack"`

---

## File Change Summary

| Action | File |
|--------|------|
| CREATE | `crates/ava-config/src/credentials.rs` |
| CREATE | `crates/ava-config/src/credential_commands.rs` |
| MODIFY | `crates/ava-config/src/lib.rs` (add modules, wire CredentialStore into ConfigManager) |
| MODIFY | `crates/ava-config/Cargo.toml` (add dirs dep if needed) |
| MODIFY | `crates/ava-llm/src/providers/mod.rs` (add create_provider factory) |
| MODIFY | `crates/ava-llm/src/providers/anthropic.rs` (constructor takes api_key) |
| MODIFY | `crates/ava-llm/src/providers/openai.rs` (constructor takes api_key) |
| MODIFY | `crates/ava-llm/src/providers/openrouter.rs` (constructor takes api_key) |
| MODIFY | `crates/ava-llm/src/providers/gemini.rs` (constructor takes api_key) |
| MODIFY | `crates/ava-llm/src/providers/ollama.rs` (constructor takes base_url) |
| MODIFY | `crates/ava-llm/src/router.rs` (credential injection + caching) |
| MODIFY | `crates/ava-llm/Cargo.toml` (add ava-config dependency) |
