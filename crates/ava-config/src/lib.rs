//! AVA Configuration Module
//!
//! Manages configuration settings for the AVA system.

use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use tokio::fs;
use tokio::sync::RwLock;
use uuid::Uuid;

pub mod agents;
pub mod credential_commands;
pub mod credentials;
pub mod keychain;
pub mod model_catalog;
pub mod routing;
pub mod thinking;
pub mod trust;

pub use agents::{default_agents, AgentOverride, AgentsConfig, ResolvedAgent};
pub use ava_auth;

pub(crate) async fn write_file_atomic(path: &Path, content: &str) -> Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .await
            .map_err(|e| AvaError::IoError(e.to_string()))?;
    }

    let file_name = path.file_name().ok_or_else(|| {
        AvaError::IoError(format!(
            "Could not determine file name for {}",
            path.display()
        ))
    })?;
    let temp_path = path.with_file_name(format!(
        ".{}.{}.tmp",
        file_name.to_string_lossy(),
        Uuid::new_v4()
    ));

    // Create temp file with restrictive permissions before writing content
    // to avoid a window where sensitive data is world-readable.
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        // Create empty file first, then restrict permissions, then write content
        fs::write(&temp_path, "")
            .await
            .map_err(|e| AvaError::IoError(e.to_string()))?;
        std::fs::set_permissions(&temp_path, std::fs::Permissions::from_mode(0o600))
            .map_err(|e| AvaError::IoError(e.to_string()))?;
        fs::write(&temp_path, content)
            .await
            .map_err(|e| AvaError::IoError(e.to_string()))?;
    }

    #[cfg(not(unix))]
    {
        fs::write(&temp_path, content)
            .await
            .map_err(|e| AvaError::IoError(e.to_string()))?;
    }

    fs::rename(&temp_path, path)
        .await
        .map_err(|e| AvaError::IoError(e.to_string()))?;

    Ok(())
}
pub use credential_commands::{
    execute_credential_command, execute_credential_command_with_tester, provider_name, redact_key,
    CredentialCommand,
};
pub use credentials::{
    known_providers, standard_env_var, CredentialStore, PendingProviderRefresh, ProviderCredential,
    ProviderCredentialState,
};
pub use keychain::{redact_key_for_log, KeychainManager, MigrationResult};
pub use model_catalog::{fallback_catalog, CatalogModel, CatalogState, ModelCatalog};
pub use routing::{RoutingConfig, RoutingMode, RoutingProfile, RoutingTarget, RoutingTargets};
pub use thinking::{ProviderThinkingBudgetConfig, ThinkingBudgetConfig};
pub use trust::{is_project_trusted, trust_project};

/// LLM provider configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LlmConfig {
    pub provider: String,
    pub model: String,
    pub api_key: Option<String>,
    pub max_tokens: usize,
    pub temperature: f32,
    #[serde(default)]
    pub thinking_budgets: ThinkingBudgetConfig,
    #[serde(default)]
    pub routing: RoutingConfig,
}

impl Default for LlmConfig {
    fn default() -> Self {
        Self {
            provider: "openai".to_string(),
            model: "gpt-5.3-codex".to_string(),
            api_key: None,
            max_tokens: 4096,
            temperature: 0.7,
            routing: RoutingConfig::default(),
            thinking_budgets: ThinkingBudgetConfig::default(),
        }
    }
}

/// Editor configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EditorConfig {
    pub default_editor: String,
    pub tab_size: usize,
    pub use_spaces: bool,
}

impl Default for EditorConfig {
    fn default() -> Self {
        Self {
            default_editor: "vscode".to_string(),
            tab_size: 4,
            use_spaces: true,
        }
    }
}

/// UI configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UiConfig {
    pub theme: String,
    pub font_size: usize,
    pub show_line_numbers: bool,
}

impl Default for UiConfig {
    fn default() -> Self {
        Self {
            theme: "dark".to_string(),
            font_size: 14,
            show_line_numbers: true,
        }
    }
}

/// Features configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FeaturesConfig {
    pub enable_git: bool,
    pub enable_lsp: bool,
    pub enable_mcp: bool,
    /// When true (default), audit log entries are persisted to `~/.ava/audit.db`.
    /// Set to false to keep audit entries in memory only (lost on app close).
    #[serde(default = "default_true")]
    pub audit_logging: bool,
    /// When true, write structured JSONL logs per session to `~/.ava/log/{session-id}.jsonl`.
    /// Each line records turn number, role, tool calls, token usage, and duration.
    /// Default: false (opt-in).
    #[serde(default)]
    pub session_logging: bool,
    /// When true and `--review` is passed, run a forced code review after the agent edits files.
    /// Normal review is handled by the agent itself via `subagent(agent_type: "review")`.
    /// Default: false (agent decides when to review).
    #[serde(default)]
    pub auto_review: bool,
    /// When true (default), the codebase is indexed in-memory (BM25 + PageRank)
    /// on first use, enabling the `codebase_search` tool. The index uses ~5-20 MB
    /// of RAM depending on project size. Disable for low-memory environments or
    /// projects where grep/glob are sufficient.
    #[serde(default = "default_true")]
    pub enable_codebase_index: bool,
}

fn default_true() -> bool {
    true
}

impl Default for FeaturesConfig {
    fn default() -> Self {
        Self {
            enable_git: true,
            enable_lsp: true,
            enable_mcp: true,
            audit_logging: true,
            session_logging: false,
            auto_review: false,
            enable_codebase_index: true,
        }
    }
}

/// Fallback provider configuration for automatic failover
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FallbackConfig {
    pub provider: String,
    pub model: String,
}

/// Voice input configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VoiceConfig {
    /// Whisper model to use (e.g., "whisper-1" for API, "base" for local)
    pub model: String,
    /// Language hint for transcription (ISO 639-1, e.g., "en")
    pub language: Option<String>,
    /// RMS amplitude below which audio is considered silence (0.0–1.0)
    pub silence_threshold: f32,
    /// Seconds of continuous silence before auto-stop
    pub silence_duration_secs: f32,
    /// Maximum recording duration in seconds
    pub max_duration_secs: u32,
    /// Automatically submit transcribed text to the agent
    pub auto_submit: bool,
}

impl Default for VoiceConfig {
    fn default() -> Self {
        Self {
            model: "whisper-1".to_string(),
            language: None,
            silence_threshold: 0.01,
            silence_duration_secs: 2.5,
            max_duration_secs: 60,
            auto_submit: false,
        }
    }
}

/// Claude Code integration configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ClaudeCodeConfig {
    /// Path to claude binary (None = auto-detect via PATH)
    #[serde(default)]
    pub binary_path: Option<PathBuf>,
    /// Whether to persist CC sessions (default: false)
    #[serde(default)]
    pub session_persistence: bool,
    /// Default max turns per invocation
    #[serde(default = "claude_code_default_max_turns")]
    pub default_max_turns: u32,
    /// Default max budget per invocation in USD
    #[serde(default = "claude_code_default_max_budget")]
    pub default_max_budget_usd: f64,
    /// Default allowed tools for CC
    #[serde(default = "claude_code_default_allowed_tools")]
    pub default_allowed_tools: Vec<String>,
}

fn claude_code_default_max_turns() -> u32 {
    10
}

fn claude_code_default_max_budget() -> f64 {
    5.0
}

fn claude_code_default_allowed_tools() -> Vec<String> {
    vec!["Read".to_string(), "Grep".to_string(), "Glob".to_string()]
}

impl Default for ClaudeCodeConfig {
    fn default() -> Self {
        Self {
            binary_path: None,
            session_persistence: false,
            default_max_turns: claude_code_default_max_turns(),
            default_max_budget_usd: claude_code_default_max_budget(),
            default_allowed_tools: claude_code_default_allowed_tools(),
        }
    }
}

/// A single path-based permission rule defined in config.yaml.
///
/// This is a config-level mirror of `ava_permissions::glob_rules::GlobRule`
/// so that `ava-config` does not depend on `ava-permissions`.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PathRule {
    /// Glob pattern (e.g., `*.env`, `src/**/*.rs`, `/etc/*`).
    pub pattern: String,
    /// Action to take when the pattern matches: `"allow"`, `"ask"`, or `"deny"`.
    pub action: String,
}

/// Permission rules that can be defined in config.yaml alongside permissions.toml.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct PermissionsConfig {
    /// Glob-based path rules. First match wins. Merged after permissions.toml rules.
    #[serde(default)]
    pub path_rules: Vec<PathRule>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqAgentOverride {
    pub id: String,
    #[serde(default = "default_hq_agent_enabled")]
    pub enabled: bool,
    #[serde(default)]
    pub model_spec: String,
    #[serde(default)]
    pub system_prompt: String,
}

const fn default_hq_agent_enabled() -> bool {
    true
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqConfig {
    #[serde(default)]
    pub director_model: String,
    #[serde(default = "default_hq_tone_preference")]
    pub tone_preference: String,
    #[serde(default = "default_hq_auto_review")]
    pub auto_review: bool,
    #[serde(default)]
    pub show_costs: bool,
    #[serde(default)]
    pub agent_overrides: Vec<HqAgentOverride>,
}

fn default_hq_tone_preference() -> String {
    "technical".to_string()
}

const fn default_hq_auto_review() -> bool {
    true
}

impl Default for HqConfig {
    fn default() -> Self {
        Self {
            director_model: String::new(),
            tone_preference: default_hq_tone_preference(),
            auto_review: default_hq_auto_review(),
            show_costs: false,
            agent_overrides: vec![],
        }
    }
}

/// Main configuration struct
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct Config {
    pub llm: LlmConfig,
    pub editor: EditorConfig,
    pub ui: UiConfig,
    pub features: FeaturesConfig,
    #[serde(default)]
    pub fallback: Option<FallbackConfig>,
    #[serde(default)]
    pub voice: VoiceConfig,
    /// Claude Code integration settings
    #[serde(default)]
    pub claude_code: Option<ClaudeCodeConfig>,
    /// Extra instruction file paths (relative to project root) or glob patterns.
    /// These are loaded in addition to the standard instruction files (AGENTS.md, CLAUDE.md, etc.).
    ///
    /// Example in config.yaml:
    /// ```yaml
    /// instructions:
    ///   - "docs/ai-rules.md"
    ///   - "team/conventions.md"
    ///   - ".github/CODING_STANDARDS.md"
    /// ```
    #[serde(default)]
    pub instructions: Vec<String>,
    /// Additional repository roots to index alongside the current project root.
    /// Paths may be absolute or relative to the active working directory.
    #[serde(default)]
    pub workspace_roots: Vec<String>,
    /// Path-based permission rules (merged with permissions.toml).
    ///
    /// Example in config.yaml:
    /// ```yaml
    /// permissions:
    ///   path_rules:
    ///     - pattern: "*.env"
    ///       action: deny
    ///     - pattern: "src/**/*.rs"
    ///       action: allow
    /// ```
    #[serde(default)]
    pub permissions: PermissionsConfig,
    #[serde(default)]
    pub hq: HqConfig,
}

/// Per-project ephemeral state (stored in `.ava/state.json` in the project root).
/// Tracks the last used model and recent models for this specific project.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ProjectState {
    /// Last used provider in this project.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub last_provider: Option<String>,
    /// Last used model in this project.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub last_model: Option<String>,
    /// Recently used models (most recent first, max 5). Stored as "provider/model" keys.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub recent_models: Vec<String>,
    /// Model to use in Plan mode (format: "provider/model").
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub plan_model: Option<String>,
    /// Model to use in Code mode (format: "provider/model").
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub code_model: Option<String>,
}

impl ProjectState {
    /// Load project state from `.ava/state.json` relative to the given project root.
    pub fn load(project_root: &std::path::Path) -> Self {
        let path = project_root.join(".ava").join("state.json");
        std::fs::read_to_string(&path)
            .ok()
            .and_then(|content| serde_json::from_str(&content).ok())
            .unwrap_or_default()
    }

    /// Add a model key to the front of recent_models, dedup, cap at 5.
    pub fn push_recent_model(&mut self, key: String) {
        self.recent_models.retain(|m| m != &key);
        self.recent_models.insert(0, key);
        self.recent_models.truncate(5);
    }

    /// Save project state to `.ava/state.json` relative to the given project root.
    pub fn save(&self, project_root: &std::path::Path) -> ava_types::Result<()> {
        let dir = project_root.join(".ava");
        std::fs::create_dir_all(&dir).map_err(AvaError::from)?;
        let content = serde_json::to_string_pretty(self)
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;
        std::fs::write(dir.join("state.json"), content).map_err(AvaError::from)?;
        Ok(())
    }
}

/// Configuration manager with auto-reload support
pub struct ConfigManager {
    config: Arc<RwLock<Config>>,
    credentials: Arc<RwLock<CredentialStore>>,
    config_path: PathBuf,
    credentials_path: PathBuf,
}

impl ConfigManager {
    /// Load configuration from default location
    pub async fn load() -> Result<Self> {
        let config_path = Self::default_config_path()?;
        let credentials_path = Self::default_credentials_path()?;
        Self::load_from_paths(config_path, credentials_path).await
    }

    /// Load configuration from specific path
    pub async fn load_from(path: PathBuf) -> Result<Self> {
        let credentials_path = Self::default_credentials_path()?;
        Self::load_from_paths(path, credentials_path).await
    }

    /// Load configuration and credentials from specific paths
    pub async fn load_from_paths(config_path: PathBuf, credentials_path: PathBuf) -> Result<Self> {
        let config = if config_path.exists() {
            let content = fs::read_to_string(&config_path)
                .await
                .map_err(|e| AvaError::IoError(e.to_string()))?;

            // Try YAML first, then JSON
            serde_yaml::from_str(&content)
                .or_else(|_| serde_json::from_str(&content))
                .map_err(|e| AvaError::SerializationError(e.to_string()))?
        } else {
            Config::default()
        };
        let mut config = config;
        config.llm.routing.normalize();
        config.llm.thinking_budgets.normalize_keys();

        let credentials = CredentialStore::load(&credentials_path).await?;

        Ok(Self {
            config: Arc::new(RwLock::new(config)),
            credentials: Arc::new(RwLock::new(credentials)),
            config_path,
            credentials_path,
        })
    }

    /// Get default configuration path based on platform
    fn default_config_path() -> Result<PathBuf> {
        let config_dir = dirs::config_dir()
            .ok_or_else(|| AvaError::ConfigError("Could not find config directory".to_string()))?;

        Ok(config_dir.join("ava").join("config.yaml"))
    }

    fn default_credentials_path() -> Result<PathBuf> {
        CredentialStore::default_path()
    }

    /// Save configuration to file
    pub async fn save(&self) -> Result<()> {
        let config = self.config.read().await;
        let content = serde_yaml::to_string(&*config)
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;
        write_file_atomic(&self.config_path, &content).await
    }

    /// Save credentials to file
    pub async fn save_credentials(&self) -> Result<()> {
        let credentials = self.credentials.read().await;
        credentials.save(&self.credentials_path).await
    }

    /// Get current configuration
    pub async fn get(&self) -> Config {
        self.config.read().await.clone()
    }

    /// Update configuration
    pub async fn update<F>(&self, f: F) -> Result<()>
    where
        F: FnOnce(&mut Config),
    {
        let mut config = self.config.write().await;
        f(&mut config);
        Ok(())
    }

    /// Reload configuration from disk
    pub async fn reload(&self) -> Result<()> {
        let new_config = if self.config_path.exists() {
            let content = fs::read_to_string(&self.config_path)
                .await
                .map_err(|e| AvaError::IoError(e.to_string()))?;

            serde_yaml::from_str(&content)
                .or_else(|_| serde_json::from_str(&content))
                .map_err(|e| AvaError::SerializationError(e.to_string()))?
        } else {
            Config::default()
        };
        let mut new_config = new_config;
        new_config.llm.routing.normalize();
        new_config.llm.thinking_budgets.normalize_keys();

        let mut config = self.config.write().await;
        *config = new_config;

        Ok(())
    }

    /// Get configuration file path
    pub fn path(&self) -> &Path {
        &self.config_path
    }

    /// Get credentials file path
    pub fn credentials_path(&self) -> &Path {
        &self.credentials_path
    }

    /// Get current credentials
    pub async fn credentials(&self) -> CredentialStore {
        self.credentials.read().await.clone()
    }

    /// Update credentials in memory
    pub async fn update_credentials<F>(&self, f: F) -> Result<()>
    where
        F: FnOnce(&mut CredentialStore),
    {
        let mut credentials = self.credentials.write().await;
        f(&mut credentials);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[tokio::test]
    async fn test_config_default() {
        let config = Config::default();
        assert_eq!(config.llm.provider, "openai");
        assert_eq!(config.editor.tab_size, 4);
        assert_eq!(config.ui.theme, "dark");
    }

    #[tokio::test]
    async fn test_config_save_and_load() {
        let temp_dir = TempDir::new().unwrap();
        let config_path = temp_dir.path().join("config.yaml");
        let credentials_path = temp_dir.path().join("credentials.json");

        let manager = ConfigManager::load_from_paths(config_path.clone(), credentials_path.clone())
            .await
            .unwrap();
        manager.save().await.unwrap();

        assert!(config_path.exists());

        let loaded = ConfigManager::load_from_paths(config_path, credentials_path)
            .await
            .unwrap();
        let config = loaded.get().await;
        assert_eq!(config.llm.provider, "openai");
    }

    #[tokio::test]
    async fn test_config_update() {
        let temp_dir = TempDir::new().unwrap();
        let config_path = temp_dir.path().join("config.yaml");
        let credentials_path = temp_dir.path().join("credentials.json");

        let manager = ConfigManager::load_from_paths(config_path, credentials_path)
            .await
            .unwrap();
        manager
            .update(|c| c.llm.model = "gpt-3.5-turbo".to_string())
            .await
            .unwrap();

        let config = manager.get().await;
        assert_eq!(config.llm.model, "gpt-3.5-turbo");
    }

    #[tokio::test]
    async fn test_config_reload() {
        let temp_dir = TempDir::new().unwrap();
        let config_path = temp_dir.path().join("config.yaml");
        let credentials_path = temp_dir.path().join("credentials.json");

        // Create initial config
        let manager = ConfigManager::load_from_paths(config_path.clone(), credentials_path.clone())
            .await
            .unwrap();
        manager
            .update(|c| c.llm.model = "custom-model".to_string())
            .await
            .unwrap();
        manager.save().await.unwrap();

        // Load fresh and verify
        let manager2 = ConfigManager::load_from_paths(config_path, credentials_path)
            .await
            .unwrap();
        let config = manager2.get().await;
        assert_eq!(config.llm.model, "custom-model");
    }

    #[tokio::test]
    async fn test_config_manager_loads_credentials() {
        let temp_dir = TempDir::new().unwrap();
        let config_path = temp_dir.path().join("config.yaml");
        let credentials_path = temp_dir.path().join("credentials.json");

        let mut credentials = CredentialStore::default();
        credentials.set(
            "openai",
            ProviderCredential {
                api_key: "sk-config-manager".to_string(),
                base_url: None,
                org_id: None,
                oauth_token: None,
                oauth_refresh_token: None,
                oauth_expires_at: None,
                oauth_account_id: None,
                litellm_compatible: None,
                loop_prone: None,
            },
        );
        credentials.save(&credentials_path).await.unwrap();

        let manager = ConfigManager::load_from_paths(config_path, credentials_path)
            .await
            .unwrap();
        let loaded = manager.credentials().await;
        assert_eq!(loaded.get("openai").unwrap().api_key, "sk-config-manager");
    }

    #[test]
    fn test_credentials_path_defaults_to_home_ava_credentials_json() {
        let path = ConfigManager::default_credentials_path().unwrap();
        let path_str = path.to_string_lossy();
        assert!(path_str.ends_with(".ava/credentials.json"));
    }

    #[tokio::test]
    async fn test_update_credentials_and_save_roundtrip() {
        let temp_dir = TempDir::new().unwrap();
        let config_path = temp_dir.path().join("config.yaml");
        let credentials_path = temp_dir.path().join("credentials.json");

        let manager = ConfigManager::load_from_paths(config_path, credentials_path.clone())
            .await
            .unwrap();

        manager
            .update_credentials(|store| {
                store.set(
                    "anthropic",
                    ProviderCredential {
                        api_key: "sk-anthropic".to_string(),
                        base_url: None,
                        org_id: None,
                        oauth_token: None,
                        oauth_refresh_token: None,
                        oauth_expires_at: None,
                        oauth_account_id: None,
                        litellm_compatible: None,
                        loop_prone: None,
                    },
                );
            })
            .await
            .unwrap();
        manager.save_credentials().await.unwrap();

        let reloaded = CredentialStore::load(&credentials_path).await.unwrap();
        assert_eq!(reloaded.get("anthropic").unwrap().api_key, "sk-anthropic");
    }

    #[tokio::test]
    async fn test_missing_credentials_file_is_empty_store() {
        let temp_dir = TempDir::new().unwrap();
        let config_path = temp_dir.path().join("config.yaml");
        let credentials_path = temp_dir.path().join("missing-credentials.json");

        let manager = ConfigManager::load_from_paths(config_path, credentials_path)
            .await
            .unwrap();
        let credentials = manager.credentials().await;
        assert!(credentials.providers().is_empty());
    }

    #[tokio::test]
    async fn test_routing_config_is_normalized_on_load() {
        let temp_dir = TempDir::new().unwrap();
        let config_path = temp_dir.path().join("config.yaml");
        let credentials_path = temp_dir.path().join("credentials.json");

        let mut config = Config::default();
        config.llm.provider = "anthropic".to_string();
        config.llm.model = "claude-sonnet-4.6".to_string();
        config.llm.routing.mode = RoutingMode::Conservative;
        config.llm.routing.targets.cheap.provider = Some(" OpenAI ".to_string());
        config.llm.routing.targets.cheap.model = Some(" gpt-4o-mini ".to_string());
        fs::write(&config_path, serde_yaml::to_string(&config).unwrap())
            .await
            .unwrap();

        let manager = ConfigManager::load_from_paths(config_path, credentials_path)
            .await
            .unwrap();
        let config = manager.get().await;

        assert_eq!(config.llm.routing.mode, RoutingMode::Conservative);
        assert_eq!(
            config.llm.routing.targets.cheap.provider.as_deref(),
            Some("openai")
        );
        assert_eq!(
            config.llm.routing.targets.cheap.model.as_deref(),
            Some("gpt-4o-mini")
        );
    }
}
