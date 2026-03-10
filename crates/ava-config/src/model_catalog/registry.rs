//! Compiled-in model registry with metadata, pricing, and name normalization.
//!
//! The registry is embedded at compile time via `include_str!("registry.json")`.
//! It provides a single source of truth for model capabilities, pricing, and aliases.

use serde::Deserialize;

static REGISTRY_JSON: &str = include_str!("registry.json");

#[derive(Debug, Clone, Deserialize)]
pub struct ModelRegistry {
    pub models: Vec<RegisteredModel>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct RegisteredModel {
    pub id: String,
    pub provider: String,
    pub name: String,
    #[serde(default)]
    pub aliases: Vec<String>,
    pub capabilities: ModelCapabilities,
    pub limits: ModelLimits,
    pub cost: ModelCost,
}

#[derive(Debug, Clone, Deserialize)]
pub struct ModelCapabilities {
    pub tool_call: bool,
    #[serde(default)]
    pub vision: bool,
    #[serde(default)]
    pub reasoning: bool,
    #[serde(default)]
    pub streaming: bool,
}

#[derive(Debug, Clone, Deserialize)]
pub struct ModelLimits {
    pub context_window: usize,
    pub max_output: Option<usize>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct ModelCost {
    pub input_per_million: f64,
    pub output_per_million: f64,
    #[serde(default)]
    pub cache_read_per_million: Option<f64>,
    #[serde(default)]
    pub cache_write_per_million: Option<f64>,
}

impl ModelRegistry {
    pub fn load() -> Self {
        serde_json::from_str(REGISTRY_JSON)
            .expect("registry.json is embedded at compile time and must be valid")
    }

    /// Look up by exact ID or alias (case-insensitive).
    pub fn find(&self, query: &str) -> Option<&RegisteredModel> {
        let q = query.to_lowercase();
        self.models.iter().find(|m| {
            m.id.to_lowercase() == q || m.aliases.iter().any(|a| a.to_lowercase() == q)
        })
    }

    /// Look up for a specific provider.
    pub fn find_for_provider(&self, provider: &str, model: &str) -> Option<&RegisteredModel> {
        let q = model.to_lowercase();
        self.models.iter().find(|m| {
            m.provider == provider
                && (m.id.to_lowercase() == q
                    || m.aliases.iter().any(|a| a.to_lowercase() == q))
        })
    }

    /// Get (input, output) pricing in USD per million tokens.
    /// When multiple providers list the same model ID (e.g. copilot mirrors),
    /// prefer the entry with non-zero pricing for accurate cost estimation.
    pub fn pricing(&self, model: &str) -> Option<(f64, f64)> {
        let q = model.to_lowercase();
        let matches: Vec<_> = self
            .models
            .iter()
            .filter(|m| {
                m.id.to_lowercase() == q || m.aliases.iter().any(|a| a.to_lowercase() == q)
            })
            .collect();

        // Prefer non-zero pricing (real provider) over free-tier mirrors
        matches
            .iter()
            .find(|m| m.cost.input_per_million > 0.0 || m.cost.output_per_million > 0.0)
            .or(matches.first())
            .map(|m| (m.cost.input_per_million, m.cost.output_per_million))
    }

    /// All models for a provider.
    pub fn models_for_provider(&self, provider: &str) -> Vec<&RegisteredModel> {
        self.models
            .iter()
            .filter(|m| m.provider == provider)
            .collect()
    }

    /// Fuzzy normalize a model name to canonical ID.
    /// Handles: aliases, missing dashes/dots, word reordering.
    pub fn normalize(&self, query: &str) -> Option<String> {
        if let Some(m) = self.find(query) {
            return Some(m.id.clone());
        }
        let normalized = query.replace(['.', '-', '_'], "").to_lowercase();
        self.models
            .iter()
            .find(|m| {
                m.id.replace(['.', '-', '_'], "").to_lowercase() == normalized
                    || m.aliases
                        .iter()
                        .any(|a| a.replace(['.', '-', '_'], "").to_lowercase() == normalized)
            })
            .map(|m| m.id.clone())
    }
}

/// Global lazy-initialized registry.
pub fn registry() -> &'static ModelRegistry {
    use std::sync::OnceLock;
    static INSTANCE: OnceLock<ModelRegistry> = OnceLock::new();
    INSTANCE.get_or_init(ModelRegistry::load)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn registry_loads_successfully() {
        let reg = ModelRegistry::load();
        assert!(!reg.models.is_empty());
    }

    #[test]
    fn registry_find_by_id() {
        let reg = ModelRegistry::load();
        let m = reg.find("claude-opus-4.6").unwrap();
        assert_eq!(m.provider, "anthropic");
        assert!(m.capabilities.tool_call);
    }

    #[test]
    fn registry_find_by_alias() {
        let reg = ModelRegistry::load();
        assert!(reg.find("opus").is_some());
        assert!(reg.find("sonnet").is_some());
    }

    #[test]
    fn normalize_aliases() {
        let reg = ModelRegistry::load();
        assert_eq!(
            reg.normalize("opus"),
            Some("claude-opus-4.6".to_string())
        );
    }

    #[test]
    fn normalize_fuzzy() {
        let reg = ModelRegistry::load();
        assert_eq!(
            reg.normalize("gpt-4o"),
            Some("gpt-4o".to_string())
        );
        assert_eq!(
            reg.normalize("claude-opus-4-6"),
            Some("claude-opus-4.6".to_string())
        );
    }

    #[test]
    fn normalize_unknown_returns_none() {
        let reg = ModelRegistry::load();
        assert_eq!(reg.normalize("totally-unknown"), None);
    }

    #[test]
    fn pricing_returns_correct_values() {
        let reg = ModelRegistry::load();
        let (inp, out) = reg.pricing("claude-opus-4.6").unwrap();
        assert_eq!(inp, 15.0);
        assert_eq!(out, 75.0);
    }

    #[test]
    fn coding_plan_models_are_free() {
        let reg = ModelRegistry::load();
        for provider in [
            "zai-coding-plan",
            "zhipuai-coding-plan",
            "kimi-for-coding",
            "minimax-coding-plan",
            "minimax-cn-coding-plan",
            "copilot",
        ] {
            for m in reg.models_for_provider(provider) {
                assert_eq!(
                    m.cost.input_per_million, 0.0,
                    "Expected free for {}/{}",
                    provider, m.id
                );
                assert_eq!(
                    m.cost.output_per_million, 0.0,
                    "Expected free for {}/{}",
                    provider, m.id
                );
            }
        }
    }

    #[test]
    fn find_for_provider_works() {
        let reg = ModelRegistry::load();
        let m = reg
            .find_for_provider("anthropic", "claude-haiku-4.5")
            .unwrap();
        assert_eq!(m.provider, "anthropic");
        assert_eq!(m.id, "claude-haiku-4.5");
    }

    #[test]
    fn global_registry_singleton() {
        let r1 = registry();
        let r2 = registry();
        assert_eq!(r1.models.len(), r2.models.len());
    }
}
