//! Network fetching and raw JSON parsing for the model catalog.

use std::collections::HashMap;
use std::time::{Duration, SystemTime};

use serde_json::Value;

use super::fallback::CURATED_MODELS;
use super::types::{CatalogModel, ModelCatalog};

const MODELS_DEV_URL: &str = "https://models.dev/api.json";
const FETCH_TIMEOUT: Duration = Duration::from_secs(10);

impl ModelCatalog {
    /// Fetch catalog from models.dev API.
    pub async fn fetch() -> Result<Self, String> {
        let client = reqwest::Client::builder()
            .timeout(FETCH_TIMEOUT)
            .user_agent("ava/2.1")
            .build()
            .map_err(|e| format!("HTTP client error: {e}"))?;

        let response = client
            .get(MODELS_DEV_URL)
            .send()
            .await
            .map_err(|e| format!("Failed to fetch models.dev: {e}"))?;

        if !response.status().is_success() {
            return Err(format!("models.dev returned {}", response.status()));
        }

        let raw: HashMap<String, Value> = response
            .json()
            .await
            .map_err(|e| format!("Failed to parse models.dev JSON: {e}"))?;

        let now = SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();

        let mut catalog = Self::from_raw(raw, now);
        // Merge fallback models for any whitelisted models not yet in the API
        catalog.merge_fallback();
        Ok(catalog)
    }

    /// Parse raw models.dev JSON into a filtered catalog.
    ///
    /// models.dev organizes models under *hosting* providers (zenmux, fastrouter,
    /// io-net, etc.), NOT under model creators. A model like "anthropic/claude-sonnet-4.6"
    /// appears under "zenmux" with ID "anthropic/claude-sonnet-4.6".
    ///
    /// We scan ALL hosting providers, extract models by their ID prefix
    /// (e.g., "anthropic/", "openai/", "google/"), deduplicate, and apply our
    /// curated whitelist. Only models with `tool_call: true` are included.
    pub(crate) fn from_raw(raw: HashMap<String, Value>, fetched_at: u64) -> Self {
        // Build a lookup: model_provider_prefix → whitelist
        let whitelist_map: HashMap<&str, &[&str]> = CURATED_MODELS.iter().copied().collect();

        // Collect models across all hosting providers, deduplicating by model ID
        let mut seen: std::collections::HashSet<String> = std::collections::HashSet::new();
        let mut providers: HashMap<String, Vec<CatalogModel>> = HashMap::new();

        for provider_data in raw.values() {
            let Some(models_obj) = provider_data.get("models").and_then(|v| v.as_object()) else {
                continue;
            };

            for (_model_key, model_data) in models_obj {
                let full_id = model_data
                    .get("id")
                    .and_then(|v| v.as_str())
                    .unwrap_or_default();

                // Extract model provider from ID prefix (e.g., "anthropic" from "anthropic/claude-...")
                let Some((model_provider, model_id)) = full_id.split_once('/') else {
                    continue;
                };

                // Check if this model provider is in our whitelist
                let Some(whitelist) = whitelist_map.get(model_provider) else {
                    continue;
                };

                // Only include whitelisted models
                if !whitelist.contains(&model_id) {
                    continue;
                }

                // Deduplicate (same model appears under multiple hosting providers)
                if !seen.insert(full_id.to_string()) {
                    continue;
                }

                // Skip deprecated/alpha models
                if let Some(status) = model_data.get("status").and_then(|v| v.as_str()) {
                    if status == "deprecated" || status == "alpha" {
                        continue;
                    }
                }

                let tool_call = model_data
                    .get("tool_call")
                    .and_then(|v| v.as_bool())
                    .unwrap_or(false);

                if !tool_call {
                    continue;
                }

                let name = model_data
                    .get("name")
                    .and_then(|v| v.as_str())
                    .unwrap_or(model_id);

                let cost = model_data.get("cost");
                let cost_input = cost.and_then(|c| c.get("input")).and_then(|v| v.as_f64());
                let cost_output = cost.and_then(|c| c.get("output")).and_then(|v| v.as_f64());

                let limit = model_data.get("limit");
                let context_window = limit
                    .and_then(|l| l.get("context"))
                    .and_then(|v| v.as_u64());
                let max_output = limit.and_then(|l| l.get("output")).and_then(|v| v.as_u64());

                providers
                    .entry(model_provider.to_string())
                    .or_default()
                    .push(CatalogModel {
                        id: model_id.to_string(),
                        name: name.to_string(),
                        provider_id: model_provider.to_string(),
                        tool_call,
                        cost_input,
                        cost_output,
                        context_window,
                        max_output,
                    });
            }
        }

        // Sort each provider's models by whitelist order
        for (provider_id, models) in &mut providers {
            if let Some(whitelist) = whitelist_map.get(provider_id.as_str()) {
                models.sort_by_key(|m| {
                    whitelist
                        .iter()
                        .position(|&w| w == m.id)
                        .unwrap_or(usize::MAX)
                });
            }
        }

        Self {
            providers,
            fetched_at,
        }
    }
}
