use std::collections::HashMap;

use serde::{Deserialize, Serialize};

/// Maximum thinking budget: 100K tokens. Prevents runaway token usage from
/// misconfigured budgets.
const MAX_THINKING_BUDGET: u32 = 100_000;

/// Clamp a thinking budget to the allowed maximum.
pub fn validate_budget(budget: u32) -> u32 {
    budget.min(MAX_THINKING_BUDGET)
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq, Eq)]
pub struct ThinkingBudgetConfig {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub default: Option<u32>,
    #[serde(default, skip_serializing_if = "HashMap::is_empty")]
    pub providers: HashMap<String, ProviderThinkingBudgetConfig>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq, Eq)]
pub struct ProviderThinkingBudgetConfig {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub default: Option<u32>,
    #[serde(default, skip_serializing_if = "HashMap::is_empty")]
    pub models: HashMap<String, u32>,
}

impl ThinkingBudgetConfig {
    pub fn resolve(&self, provider: &str, model: &str) -> Option<u32> {
        let provider_key = provider.to_ascii_lowercase();
        let model_key = model.to_ascii_lowercase();

        self.providers
            .get(&provider_key)
            .and_then(|provider_cfg| {
                provider_cfg
                    .models
                    .get(&model_key)
                    .copied()
                    .or(provider_cfg.default)
            })
            .or(self.default)
            .filter(|budget| *budget > 0)
            .map(validate_budget)
    }

    pub fn normalize_keys(&mut self) {
        self.providers = std::mem::take(&mut self.providers)
            .into_iter()
            .map(|(provider, mut config)| {
                config.models = config
                    .models
                    .into_iter()
                    .map(|(model, budget)| (model.to_ascii_lowercase(), budget))
                    .collect();
                (provider.to_ascii_lowercase(), config)
            })
            .collect();
    }
}

#[cfg(test)]
mod tests {
    use super::{ProviderThinkingBudgetConfig, ThinkingBudgetConfig};
    use std::collections::HashMap;

    #[test]
    fn resolves_model_then_provider_then_global_budget() {
        let mut config = ThinkingBudgetConfig {
            default: Some(1200),
            providers: HashMap::from([(
                "gemini".to_string(),
                ProviderThinkingBudgetConfig {
                    default: Some(3200),
                    models: HashMap::from([("gemini-2.5-pro".to_string(), 6400)]),
                },
            )]),
        };
        config.normalize_keys();

        assert_eq!(config.resolve("gemini", "gemini-2.5-pro"), Some(6400));
        assert_eq!(config.resolve("gemini", "gemini-2.5-flash"), Some(3200));
        assert_eq!(config.resolve("openai", "gpt-5.4"), Some(1200));
    }

    #[test]
    fn resolve_ignores_zero_budgets() {
        let mut config = ThinkingBudgetConfig {
            default: Some(0),
            providers: HashMap::from([(
                "OpenAI".to_string(),
                ProviderThinkingBudgetConfig {
                    default: Some(0),
                    models: HashMap::from([("GPT-5.4".to_string(), 0)]),
                },
            )]),
        };
        config.normalize_keys();

        assert_eq!(config.resolve("openai", "gpt-5.4"), None);
        assert_eq!(config.resolve("openai", "gpt-5.3"), None);
    }

    #[test]
    fn normalize_keys_makes_resolution_case_insensitive() {
        let mut config = ThinkingBudgetConfig {
            default: None,
            providers: HashMap::from([(
                "Gemini".to_string(),
                ProviderThinkingBudgetConfig {
                    default: None,
                    models: HashMap::from([("Gemini-2.5-Pro".to_string(), 8192)]),
                },
            )]),
        };

        config.normalize_keys();

        assert_eq!(config.resolve("GEMINI", "GEMINI-2.5-PRO"), Some(8192));
    }

    #[test]
    fn resolve_clamps_excessive_budget() {
        let mut config = ThinkingBudgetConfig {
            default: Some(500_000),
            providers: HashMap::new(),
        };
        config.normalize_keys();

        // Should be clamped to MAX_THINKING_BUDGET (100_000)
        assert_eq!(config.resolve("any", "any"), Some(100_000));
    }
}
