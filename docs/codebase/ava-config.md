# ava-config

> Configuration management, credential storage, model catalog, and agent settings for AVA.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `Config` | Main configuration struct: llm, editor, ui, features, fallback, voice, claude_code, instructions, workspace_roots |
| `ConfigManager` | Async config manager with auto-reload, atomic writes, credential store integration |
| `LlmConfig` | Provider, model, api_key, max_tokens, temperature, routing, thinking_budgets |
| `EditorConfig` | default_editor, tab_size, use_spaces |
| `UiConfig` | theme, font_size, show_line_numbers |
| `FeaturesConfig` | enable_git, enable_lsp, enable_mcp |
| `VoiceConfig` | Whisper model, language, silence thresholds, auto_submit |
| `ClaudeCodeConfig` | Binary path, session_persistence, max_turns, max_budget_usd, allowed_tools |
| `ProjectState` | Per-project state: last_provider, last_model, recent_models (max 5) |
| `CredentialStore` | Provider credential management with env var overrides and OAuth refresh |
| `ProviderCredential` | api_key, base_url, org_id, oauth_token, oauth_refresh_token, oauth_expires_at |
| `ProviderCredentialState` | Enum: Ready, RefreshNeeded |
| `PendingProviderRefresh` | Existing credential, refresh token, OAuth config for token renewal |
| `CredentialCommand` | CLI commands: Set, Remove, List, Test |
| `execute_credential_command()` | Execute credential command with default tester |
| `execute_credential_command_with_tester()` | Execute with injected tester for testing |
| `provider_name()` | Map provider ID to display name (e.g., "openai" → "OpenAI") |
| `redact_key()` | Redact API key for display: `sk-1...abcd` |
| `KeychainManager` | OS keychain (primary) + encrypted file fallback for credentials |
| `MigrationResult` | Enum: NoFileFound, Migrated { count } |
| `redact_key_for_log()` | Redact key showing only last 4 chars: `****...abcd` |
| `AgentsConfig` | Load/merge agent configs from global and project TOML files |
| `AgentDefaults` | Default model, max_turns, enabled for all sub-agents |
| `AgentOverride` | Per-agent overrides: enabled, model, max_turns, prompt, temperature, provider, allowed_tools, max_budget_usd |
| `ResolvedAgent` | Fully merged agent config after applying defaults + overrides |
| `default_agents()` | Predefined templates: build, plan, explore, review, task |
| `ThinkingBudgetConfig` | Hierarchical thinking budgets: default → provider → model |
| `ProviderThinkingBudgetConfig` | Provider-level defaults and per-model budgets |
| `validate_budget()` | Clamp budget to MAX_THINKING_BUDGET (100K tokens) |
| `RoutingConfig` | Enable/disable model routing with cheap/capable targets |
| `RoutingMode` | Enum: Off, Conservative |
| `RoutingProfile` | Enum: Cheap, Capable |
| `RoutingTarget` | Provider + model pair for routing |
| `RoutingTargets` | cheap and capable target configuration |
| `is_project_trusted()` | Check if project root is in trusted_projects.json |
| `trust_project()` | Add project to trusted list |
| `ModelCatalog` | Fetched model metadata from models.dev API |
| `CatalogModel` | Model ID, name, provider_id, tool_call, costs, limits |
| `CatalogState` | Thread-safe catalog with background refresh |
| `fallback_catalog()` | Hardcoded fallback models when fetch/cache fail |
| `ModelRegistry` | Compile-time embedded registry from registry.json |
| `RegisteredModel` | Model with aliases, capabilities, limits, cost |
| `registry()` | Global lazy-initialized ModelRegistry singleton |
| `write_file_atomic()` | Atomic file write with restricted permissions (0o600) |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Config, ConfigManager, LlmConfig, EditorConfig, UiConfig, FeaturesConfig, VoiceConfig, ClaudeCodeConfig, ProjectState, re-exports all modules |
| `credentials.rs` | CredentialStore, ProviderCredential, ProviderCredentialState, PendingProviderRefresh, env var handling, OAuth refresh |
| `credential_commands.rs` | CredentialCommand enum, execute functions, provider_name(), redact_key() |
| `keychain.rs` | KeychainManager with OS keychain + AES-256-GCM encrypted fallback, MigrationResult |
| `agents.rs` | AgentsConfig, AgentDefaults, AgentOverride, ResolvedAgent, default_agents() templates |
| `thinking.rs` | ThinkingBudgetConfig, ProviderThinkingBudgetConfig, validate_budget() |
| `routing.rs` | RoutingConfig, RoutingMode, RoutingProfile, RoutingTarget, RoutingTargets |
| `trust.rs` | is_project_trusted(), trust_project() for project trust management |
| `model_catalog/mod.rs` | Module exports, constants (REFRESH_INTERVAL = 1 hour) |
| `model_catalog/types.rs` | CatalogModel, ModelCatalog, CatalogState with caching and background refresh |
| `model_catalog/fetch.rs` | HTTP fetch from models.dev, from_raw() parsing, whitelist filtering |
| `model_catalog/fallback.rs` | CURATED_MODELS whitelist, fallback_catalog() from embedded registry |
| `model_catalog/registry.rs` | ModelRegistry, RegisteredModel, capabilities, limits, cost, registry() singleton |

## Dependencies

Uses: ava-types, ava-auth, serde, serde_json, serde_yaml, tokio, dirs, reqwest, toml, tracing, uuid

Optional: keyring, aes-gcm, pbkdf2, sha2, rand, base64, rpassword (enabled via "keychain" feature)

Used by: ava-tui, ava-agent, ava-tools, ava-llm

## Key Patterns

- **Atomic file writes**: `write_file_atomic()` creates temp file, sets 0o600 permissions, then renames
- **Env var overrides**: `AVA_<PROVIDER>_API_KEY` and standard env vars (OPENAI_API_KEY, etc.) override file credentials
- **OAuth token refresh**: Automatic refresh on expiry with fallback to static API key on failure
- **Dual storage**: OS keychain preferred (macOS Keychain, Linux Secret Service, Windows Credential Manager); AES-256-GCM encrypted file fallback for headless systems
- **Master password**: From `AVA_MASTER_PASSWORD` env var or interactive prompt
- **Config merging**: Global `~/.ava/agents.toml` merged with project `.ava/agents.toml`, project values take precedence
- **Agent resolution**: explicit override → defaults section → predefined template → bare defaults
- **Hierarchical budgets**: Model-specific → provider default → global default → None
- **Case normalization**: All provider/model keys normalized to lowercase
- **Budget validation**: Clamped to 100K token maximum
- **Routing targets**: Cheap/capable profiles with normalization and completeness checking
- **Trust persistence**: `~/.ava/trusted_projects.json` stores canonical project paths
- **Catalog refresh**: Background task every 60 minutes with graceful shutdown flag
- **Whitelist filtering**: Only CURATED_MODELS with `tool_call: true` included from models.dev API
- **Provider mapping**: models.dev "google" → AVA "gemini", hosting providers flattened
- **API ID formatting**: Provider-specific formatting (e.g., Anthropic dates, OpenRouter prefixes)
- **Embedded registry**: `registry.json` compiled in as fallback and single source of truth
- **Lazy singleton**: `registry()` uses `std::sync::OnceLock` for global access
- **Redaction**: API keys redacted in Debug output and logs (showing only first/last 4 chars or suffix)
