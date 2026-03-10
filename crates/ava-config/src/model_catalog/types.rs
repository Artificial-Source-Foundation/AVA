//! Core types for the model catalog.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::SystemTime;

use serde::{Deserialize, Serialize};
use tokio::sync::RwLock;

use super::{fallback_catalog, REFRESH_INTERVAL};

/// A single model from the catalog.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CatalogModel {
    /// Model ID (e.g., "claude-sonnet-4-6", "gpt-5.1-codex")
    pub id: String,
    /// Human-readable name (e.g., "Claude Sonnet 4.6", "GPT-5.1 Codex")
    pub name: String,
    /// Provider ID from models.dev (e.g., "anthropic", "openai", "google")
    pub provider_id: String,
    /// Whether this model supports tool/function calling
    pub tool_call: bool,
    /// Input cost per million tokens (USD)
    pub cost_input: Option<f64>,
    /// Output cost per million tokens (USD)
    pub cost_output: Option<f64>,
    /// Context window size
    pub context_window: Option<u64>,
    /// Maximum output tokens
    pub max_output: Option<u64>,
}

impl CatalogModel {
    /// Format cost as display string (e.g., "$3/$15", "free").
    pub fn cost_display(&self) -> String {
        match (self.cost_input, self.cost_output) {
            (Some(input), Some(output)) if input == 0.0 && output == 0.0 => "free".to_string(),
            (Some(input), Some(output)) => format!("${}/${}", format_cost(input), format_cost(output)),
            _ => String::new(),
        }
    }

    /// Map models.dev provider ID to AVA's internal provider name.
    pub fn ava_provider(&self) -> &str {
        match self.provider_id.as_str() {
            "google" => "gemini",
            other => other,
        }
    }

    /// Return the model ID suitable for the given AVA provider.
    /// For OpenRouter, returns "provider/id" format.
    /// For direct providers, maps models.dev IDs to API-expected IDs.
    pub fn api_model_id(&self, ava_provider: &str) -> String {
        match ava_provider {
            "openrouter" => format!("{}/{}", self.provider_id, self.id),
            "anthropic" => self.anthropic_api_id(),
            _ => self.id.clone(),
        }
    }

    fn anthropic_api_id(&self) -> String {
        match self.id.as_str() {
            "claude-opus-4.6" => "claude-opus-4-6".to_string(),
            "claude-sonnet-4.6" => "claude-sonnet-4-6".to_string(),
            "claude-sonnet-4.5" => "claude-sonnet-4-20250514".to_string(),
            "claude-haiku-4.5" => "claude-haiku-4-5-20251001".to_string(),
            other => other.replace('.', "-"),
        }
    }
}

fn format_cost(cost: f64) -> String {
    if cost == 0.0 {
        return "0".to_string();
    }
    if cost >= 1.0 && cost == cost.floor() {
        format!("{}", cost as u64)
    } else {
        // Trim trailing zeros
        let s = format!("{:.4}", cost);
        s.trim_end_matches('0').trim_end_matches('.').to_string()
    }
}

/// The full model catalog, organized by provider.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct ModelCatalog {
    /// Provider ID → list of models (only tool_call capable)
    pub providers: HashMap<String, Vec<CatalogModel>>,
    /// When the catalog was last fetched (Unix timestamp)
    #[serde(default)]
    pub fetched_at: u64,
}

impl ModelCatalog {
    /// Whether the catalog has any models.
    pub fn is_empty(&self) -> bool {
        self.providers.values().all(|v| v.is_empty())
    }

    /// Merge fallback models into this catalog for any whitelisted models
    /// that are missing from the dynamic API data. This ensures models that
    /// exist (e.g., on models.dev website or OpenAI) but aren't yet in the
    /// API JSON still appear in the selector.
    pub fn merge_fallback(&mut self) {
        let fallback = fallback_catalog();
        for (provider_id, fallback_models) in &fallback.providers {
            let existing = self.providers.entry(provider_id.clone()).or_default();
            for fm in fallback_models {
                if !existing.iter().any(|m| m.id == fm.id) {
                    existing.push(fm.clone());
                }
            }
        }
    }

    /// Get models for a specific provider (using AVA's provider names).
    pub fn models_for(&self, ava_provider: &str) -> &[CatalogModel] {
        // Map AVA provider name to models.dev provider name
        let dev_provider = match ava_provider {
            "gemini" => "google",
            other => other,
        };
        self.providers
            .get(dev_provider)
            .map(|v| v.as_slice())
            .unwrap_or_default()
    }

    /// All models across all providers.
    pub fn all_models(&self) -> Vec<&CatalogModel> {
        self.providers.values().flat_map(|v| v.iter()).collect()
    }

    /// Whether the catalog needs a refresh.
    pub fn needs_refresh(&self) -> bool {
        if self.fetched_at == 0 {
            return true;
        }
        let now = SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();
        now.saturating_sub(self.fetched_at) > REFRESH_INTERVAL.as_secs()
    }

    /// Load from local cache file.
    pub async fn load_cached(path: &Path) -> Result<Self, String> {
        let content = tokio::fs::read_to_string(path)
            .await
            .map_err(|e| format!("Failed to read cache: {e}"))?;
        serde_json::from_str(&content).map_err(|e| format!("Failed to parse cache: {e}"))
    }

    /// Save to local cache file.
    pub async fn save_cache(&self, path: &Path) -> Result<(), String> {
        if let Some(parent) = path.parent() {
            tokio::fs::create_dir_all(parent)
                .await
                .map_err(|e| format!("Failed to create cache dir: {e}"))?;
        }
        let content =
            serde_json::to_string(self).map_err(|e| format!("Failed to serialize catalog: {e}"))?;
        tokio::fs::write(path, content)
            .await
            .map_err(|e| format!("Failed to write cache: {e}"))
    }

    /// Default cache path: ~/.ava/cache/models.json
    pub fn default_cache_path() -> Result<PathBuf, String> {
        let home = dirs::home_dir()
            .ok_or_else(|| "Could not resolve home directory".to_string())?;
        Ok(home.join(".ava").join("cache").join("models.json"))
    }
}

/// Shared catalog state for the application.
#[derive(Debug, Clone)]
pub struct CatalogState {
    inner: Arc<RwLock<ModelCatalog>>,
}

impl Default for CatalogState {
    fn default() -> Self {
        Self {
            inner: Arc::new(RwLock::new(ModelCatalog::default())),
        }
    }
}

impl CatalogState {
    /// Load catalog: try cache first, then fetch from network.
    pub async fn load() -> Self {
        let state = Self::default();

        // Try loading from cache
        if let Ok(cache_path) = ModelCatalog::default_cache_path() {
            if let Ok(cached) = ModelCatalog::load_cached(&cache_path).await {
                *state.inner.write().await = cached;

                // If cache is fresh enough, we're done
                if !state.inner.read().await.needs_refresh() {
                    tracing::debug!("Model catalog loaded from cache (fresh)");
                    return state;
                }
                tracing::debug!("Model catalog loaded from cache (stale, will refresh)");
            }
        }

        // Try fetching fresh data
        match ModelCatalog::fetch().await {
            Ok(catalog) => {
                // Save to cache
                if let Ok(cache_path) = ModelCatalog::default_cache_path() {
                    if let Err(e) = catalog.save_cache(&cache_path).await {
                        tracing::warn!("Failed to save model catalog cache: {e}");
                    }
                }
                *state.inner.write().await = catalog;
                tracing::debug!("Model catalog fetched from models.dev");
            }
            Err(e) => {
                tracing::warn!("Failed to fetch model catalog: {e}");
                // If we have cached data (even stale), keep using it
                // Otherwise we'll fall back to hardcoded in the TUI
            }
        }

        state
    }

    /// Get a snapshot of the current catalog.
    pub async fn get(&self) -> ModelCatalog {
        self.inner.read().await.clone()
    }

    /// Refresh in the background (non-blocking).
    pub fn spawn_background_refresh(&self) {
        let state = self.clone();
        tokio::spawn(async move {
            loop {
                tokio::time::sleep(REFRESH_INTERVAL).await;
                tracing::debug!("Refreshing model catalog...");
                match ModelCatalog::fetch().await {
                    Ok(catalog) => {
                        if let Ok(cache_path) = ModelCatalog::default_cache_path() {
                            let _ = catalog.save_cache(&cache_path).await;
                        }
                        *state.inner.write().await = catalog;
                        tracing::debug!("Model catalog refreshed");
                    }
                    Err(e) => {
                        tracing::warn!("Background model catalog refresh failed: {e}");
                    }
                }
            }
        });
    }
}
