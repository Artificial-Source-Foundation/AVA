//! Core types for the model catalog.

use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use serde::{Deserialize, Serialize};
use tokio::sync::RwLock;

use super::fallback_catalog;

/// A single model from the catalog.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CatalogModel {
    /// Model ID (e.g., "claude-sonnet-4-6", "gpt-5.1-codex")
    pub id: String,
    /// Human-readable name (e.g., "Claude Sonnet 4.6", "GPT-5.1 Codex")
    pub name: String,
    /// Provider ID used by the curated AVA catalog (e.g., "anthropic", "openai", "google")
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
            (Some(input), Some(output)) => {
                format!("${}/${}", format_cost(input), format_cost(output))
            }
            _ => String::new(),
        }
    }

    /// Map catalog provider ID to AVA's internal provider name.
    pub fn ava_provider(&self) -> &str {
        match self.provider_id.as_str() {
            "google" => "gemini",
            other => other,
        }
    }

    /// Return the model ID suitable for the given AVA provider.
    /// For OpenRouter, returns "provider/id" format.
    /// For direct providers, maps curated catalog IDs to API-expected IDs.
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
    /// that are missing from the primary curated set.
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
        // Map AVA provider name to catalog provider name
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
}

/// Shared catalog state for the application.
#[derive(Debug, Clone)]
pub struct CatalogState {
    inner: Arc<RwLock<ModelCatalog>>,
    /// Shutdown flag for the background refresh task.
    shutdown: Arc<AtomicBool>,
}

impl Default for CatalogState {
    fn default() -> Self {
        Self {
            inner: Arc::new(RwLock::new(ModelCatalog::default())),
            shutdown: Arc::new(AtomicBool::new(false)),
        }
    }
}

impl CatalogState {
    /// Load the repo-owned catalog.
    pub async fn load() -> Self {
        let state = Self::default();
        *state.inner.write().await = fallback_catalog();
        state
    }

    /// Get a snapshot of the current catalog.
    pub async fn get(&self) -> ModelCatalog {
        self.inner.read().await.clone()
    }

    /// Background refresh is intentionally disabled for the repo-owned catalog.
    pub fn spawn_background_refresh(&self) {
        let _ = &self.shutdown;
    }

    /// Signal the background refresh task to stop.
    pub fn stop_background_refresh(&self) {
        self.shutdown.store(true, Ordering::Relaxed);
    }
}
