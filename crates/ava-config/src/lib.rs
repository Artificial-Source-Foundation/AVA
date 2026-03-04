//! AVA Configuration Module
//!
//! Manages configuration settings for the AVA system.

use ava_types::Result;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use tokio::fs;
use tokio::sync::RwLock;

/// LLM provider configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LlmConfig {
    pub provider: String,
    pub model: String,
    pub api_key: Option<String>,
    pub max_tokens: usize,
    pub temperature: f32,
}

impl Default for LlmConfig {
    fn default() -> Self {
        Self {
            provider: "openai".to_string(),
            model: "gpt-4".to_string(),
            api_key: None,
            max_tokens: 4096,
            temperature: 0.7,
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
}

impl Default for FeaturesConfig {
    fn default() -> Self {
        Self {
            enable_git: true,
            enable_lsp: true,
            enable_mcp: true,
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
}

/// Configuration manager with auto-reload support
pub struct ConfigManager {
    config: Arc<RwLock<Config>>,
    config_path: PathBuf,
}

impl ConfigManager {
    /// Load configuration from default location
    pub async fn load() -> Result<Self> {
        let config_path = Self::default_config_path()?;
        Self::load_from(config_path).await
    }

    /// Load configuration from specific path
    pub async fn load_from(path: PathBuf) -> Result<Self> {
        let config = if path.exists() {
            let content = fs::read_to_string(&path)
                .await
                .map_err(|e| ava_types::AvaError::IoError(e.to_string()))?;

            // Try YAML first, then JSON
            serde_yaml::from_str(&content)
                .or_else(|_| serde_json::from_str(&content))
                .map_err(|e| ava_types::AvaError::SerializationError(e.to_string()))?
        } else {
            Config::default()
        };

        Ok(Self {
            config: Arc::new(RwLock::new(config)),
            config_path: path,
        })
    }

    /// Get default configuration path based on platform
    fn default_config_path() -> Result<PathBuf> {
        let config_dir = dirs::config_dir().ok_or_else(|| {
            ava_types::AvaError::ConfigError("Could not find config directory".to_string())
        })?;

        Ok(config_dir.join("ava").join("config.yaml"))
    }

    /// Save configuration to file
    pub async fn save(&self) -> Result<()> {
        let config = self.config.read().await;
        let content = serde_yaml::to_string(&*config)
            .map_err(|e| ava_types::AvaError::SerializationError(e.to_string()))?;

        // Ensure directory exists
        if let Some(parent) = self.config_path.parent() {
            fs::create_dir_all(parent)
                .await
                .map_err(|e| ava_types::AvaError::IoError(e.to_string()))?;
        }

        fs::write(&self.config_path, content)
            .await
            .map_err(|e| ava_types::AvaError::IoError(e.to_string()))?;

        Ok(())
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
                .map_err(|e| ava_types::AvaError::IoError(e.to_string()))?;

            serde_yaml::from_str(&content)
                .or_else(|_| serde_json::from_str(&content))
                .map_err(|e| ava_types::AvaError::SerializationError(e.to_string()))?
        } else {
            Config::default()
        };

        let mut config = self.config.write().await;
        *config = new_config;

        Ok(())
    }

    /// Get configuration file path
    pub fn path(&self) -> &Path {
        &self.config_path
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

        let manager = ConfigManager::load_from(config_path.clone()).await.unwrap();
        manager.save().await.unwrap();

        assert!(config_path.exists());

        let loaded = ConfigManager::load_from(config_path).await.unwrap();
        let config = loaded.get().await;
        assert_eq!(config.llm.provider, "openai");
    }

    #[tokio::test]
    async fn test_config_update() {
        let temp_dir = TempDir::new().unwrap();
        let config_path = temp_dir.path().join("config.yaml");

        let manager = ConfigManager::load_from(config_path).await.unwrap();
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

        // Create initial config
        let manager = ConfigManager::load_from(config_path.clone()).await.unwrap();
        manager
            .update(|c| c.llm.model = "custom-model".to_string())
            .await
            .unwrap();
        manager.save().await.unwrap();

        // Load fresh and verify
        let manager2 = ConfigManager::load_from(config_path).await.unwrap();
        let config = manager2.get().await;
        assert_eq!(config.llm.model, "custom-model");
    }
}
