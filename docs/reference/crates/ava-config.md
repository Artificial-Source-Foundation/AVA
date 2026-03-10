# ava-config

Configuration management for the AVA system. Handles main config, credentials, sub-agent settings, model catalog, and per-project state.

## Modules

### Main Config (`src/lib.rs`)

`Config` is the top-level configuration loaded from `~/.config/ava/config.yaml`:

```rust
pub struct Config {
    pub llm: LlmConfig,         // provider, model, api_key, max_tokens, temperature
    pub editor: EditorConfig,    // default_editor, tab_size, use_spaces
    pub ui: UiConfig,            // theme, font_size, show_line_numbers
    pub features: FeaturesConfig,// enable_git, enable_lsp, enable_mcp
    pub fallback: Option<FallbackConfig>, // automatic provider failover
    pub voice: VoiceConfig,      // whisper model, silence detection, auto_submit
    pub instructions: Vec<String>, // extra instruction file paths/globs
}
```

`ConfigManager` wraps config and credentials in `Arc<RwLock<>>` for concurrent async access. Supports load from YAML or JSON, save, reload, and in-memory updates.

`ProjectState` stores per-project ephemeral data (last used provider/model) in `.ava/state.json`.

**File**: `crates/ava-config/src/lib.rs` (lines 1-497)

### Credentials (`src/credentials.rs`)

`CredentialStore` manages API keys for 16 known providers with a 3-tier lookup chain:

1. `AVA_{PROVIDER}_API_KEY` environment variable
2. Standard env vars (e.g., `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`)
3. Stored credentials from `~/.ava/credentials.json`

```rust
pub struct ProviderCredential {
    pub api_key: String,
    pub base_url: Option<String>,
    pub org_id: Option<String>,
    pub oauth_token: Option<String>,
    pub oauth_refresh_token: Option<String>,
    pub oauth_expires_at: Option<String>,
    pub oauth_account_id: Option<String>,
}
```

The `resolve()` method implements the lookup chain. `is_placeholder_key()` detects dummy keys like "sk-xxx".

**File**: `crates/ava-config/src/credentials.rs` (lines 1-428)

### Agents (`src/agents.rs`)

TOML-based sub-agent configuration with two-level merge:

- `~/.ava/agents.toml` -- global defaults
- `.ava/agents.toml` -- project-level overrides (takes precedence)

```rust
pub struct AgentsConfig {
    pub defaults: AgentDefaults,           // model, max_turns, enabled
    pub agents: HashMap<String, AgentOverride>, // per-agent overrides
}
```

`get_agent(name)` returns a `ResolvedAgent` that merges defaults with agent-specific overrides.

**File**: `crates/ava-config/src/agents.rs` (lines 1-420)

### Credential Commands (`src/credential_commands.rs`)

`CredentialCommand` enum (Set/Remove/List/Test) for managing credentials via CLI. `execute_credential_command()` processes commands against a `CredentialStore`. `redact_key()` masks API keys for display (e.g., "sk-ab...yz").

**File**: `crates/ava-config/src/credential_commands.rs` (lines 1-327)

### Model Catalog (`src/model_catalog/`)

Dynamic model catalog with compiled-in fallback.

**Registry** (`registry.rs`): `ModelRegistry` loaded from `registry.json` via `include_str!` at compile time. Global singleton via `OnceLock`. Provides `find()`, `find_for_provider()`, `pricing()`, and `normalize()` with fuzzy matching (strips version suffixes, handles aliases).

**File**: `crates/ava-config/src/model_catalog/registry.rs` (lines 1-237)

**Types** (`types.rs`): `CatalogModel` (id, name, provider_id, tool_call support, costs, limits), `ModelCatalog` with provider HashMap, cache load/save, `needs_refresh` flag. `CatalogState` wraps in `Arc<RwLock>` with background refresh.

**File**: `crates/ava-config/src/model_catalog/types.rs` (lines 1-261)

**Fetch** (`fetch.rs`): `ModelCatalog::fetch()` from models.dev API. `from_raw()` parses response, filtering through a curated whitelist and deduplicating.

**File**: `crates/ava-config/src/model_catalog/fetch.rs` (lines 1-158)

**Fallback** (`fallback.rs`): `CURATED_MODELS` whitelist and `fallback_catalog()` generated from the compiled registry when the API is unavailable.

**File**: `crates/ava-config/src/model_catalog/fallback.rs` (lines 1-763)

## Source Files

| File | Lines | Purpose |
|------|------:|---------|
| `src/lib.rs` | 497 | Config, ConfigManager, ProjectState |
| `src/credentials.rs` | 428 | CredentialStore, 3-tier lookup |
| `src/agents.rs` | 420 | AgentsConfig, TOML merge |
| `src/credential_commands.rs` | 327 | CLI credential management |
| `src/model_catalog/mod.rs` | 315 | Module root, tests |
| `src/model_catalog/registry.rs` | 237 | Compiled-in ModelRegistry |
| `src/model_catalog/types.rs` | 261 | CatalogModel, CatalogState |
| `src/model_catalog/fetch.rs` | 158 | models.dev API fetch |
| `src/model_catalog/fallback.rs` | 763 | Curated whitelist, fallback catalog |
