//! Repo-owned model catalog for the curated AVA provider/model set.
//!
//! The catalog is compiled into the binary and updated manually in-repo.
//! There is no runtime network fetch for model metadata.

mod fallback;
pub mod registry;
mod types;

pub use fallback::fallback_catalog;
pub use types::{CatalogModel, CatalogState, ModelCatalog};

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn cost_display_formatting() {
        let model = CatalogModel {
            id: "test".to_string(),
            name: "Test".to_string(),
            provider_id: "test".to_string(),
            tool_call: true,
            cost_input: Some(3.0),
            cost_output: Some(15.0),
            context_window: None,
            max_output: None,
        };
        assert_eq!(model.cost_display(), "$3/$15");

        let free = CatalogModel {
            cost_input: Some(0.0),
            cost_output: Some(0.0),
            ..model.clone()
        };
        assert_eq!(free.cost_display(), "free");

        let cheap = CatalogModel {
            cost_input: Some(0.15),
            cost_output: Some(0.60),
            ..model.clone()
        };
        assert_eq!(cheap.cost_display(), "$0.15/$0.6");
    }

    #[test]
    fn ava_provider_mapping() {
        let google = CatalogModel {
            id: "gemini-2.5-pro".to_string(),
            name: "Gemini 2.5 Pro".to_string(),
            provider_id: "google".to_string(),
            tool_call: true,
            cost_input: None,
            cost_output: None,
            context_window: None,
            max_output: None,
        };
        assert_eq!(google.ava_provider(), "gemini");

        let anthropic = CatalogModel {
            provider_id: "anthropic".to_string(),
            ..google.clone()
        };
        assert_eq!(anthropic.ava_provider(), "anthropic");
    }

    #[test]
    fn fallback_catalog_has_models() {
        let catalog = fallback_catalog();
        assert!(!catalog.is_empty());
        assert!(!catalog.models_for("anthropic").is_empty());
        assert!(!catalog.models_for("openai").is_empty());
        assert!(!catalog.models_for("gemini").is_empty());
    }

    #[test]
    fn fallback_catalog_starts_unfetched() {
        let catalog = ModelCatalog::default();
        assert_eq!(catalog.fetched_at, 0);
    }

    #[test]
    fn api_model_id_anthropic() {
        let model = CatalogModel {
            id: "claude-sonnet-4.6".to_string(),
            name: "Claude Sonnet 4.6".to_string(),
            provider_id: "anthropic".to_string(),
            tool_call: true,
            cost_input: None,
            cost_output: None,
            context_window: None,
            max_output: None,
        };
        assert_eq!(model.api_model_id("anthropic"), "claude-sonnet-4-6");

        let opus = CatalogModel {
            id: "claude-opus-4.6".to_string(),
            ..model.clone()
        };
        assert_eq!(opus.api_model_id("anthropic"), "claude-opus-4-6");

        let haiku = CatalogModel {
            id: "claude-haiku-4.5".to_string(),
            ..model.clone()
        };
        assert_eq!(haiku.api_model_id("anthropic"), "claude-haiku-4-5-20251001");

        let sonnet45 = CatalogModel {
            id: "claude-sonnet-4.5".to_string(),
            ..model.clone()
        };
        assert_eq!(
            sonnet45.api_model_id("anthropic"),
            "claude-sonnet-4-20250514"
        );
    }

    #[test]
    fn api_model_id_openrouter() {
        let model = CatalogModel {
            id: "claude-sonnet-4.6".to_string(),
            name: "Claude Sonnet 4.6".to_string(),
            provider_id: "anthropic".to_string(),
            tool_call: true,
            cost_input: None,
            cost_output: None,
            context_window: None,
            max_output: None,
        };
        assert_eq!(
            model.api_model_id("openrouter"),
            "anthropic/claude-sonnet-4.6"
        );
    }

    #[test]
    fn api_model_id_passthrough() {
        let model = CatalogModel {
            id: "gpt-5.2-codex".to_string(),
            name: "GPT-5.2 Codex".to_string(),
            provider_id: "openai".to_string(),
            tool_call: true,
            cost_input: None,
            cost_output: None,
            context_window: None,
            max_output: None,
        };
        // OpenAI and other providers: pass through as-is
        assert_eq!(model.api_model_id("openai"), "gpt-5.2-codex");
        assert_eq!(model.api_model_id("gemini"), "gpt-5.2-codex");
    }

    #[test]
    fn google_maps_to_gemini() {
        let catalog = fallback_catalog();
        let gemini_models = catalog.models_for("gemini");
        assert!(!gemini_models.is_empty());
        assert_eq!(gemini_models[0].ava_provider(), "gemini");
    }

    #[test]
    fn curated_models_match_fallback_catalog() {
        use fallback::CURATED_MODELS;

        let catalog = fallback_catalog();
        for &(provider, models) in CURATED_MODELS {
            let catalog_models = catalog.models_for(provider);
            assert!(
                !catalog_models.is_empty(),
                "Provider {provider} in CURATED_MODELS but has no models in fallback_catalog()"
            );
            for &model_id in models {
                assert!(
                    catalog_models.iter().any(|m| m.id == model_id),
                    "Model {model_id} in CURATED_MODELS[{provider}] but not in fallback_catalog()"
                );
            }
        }
    }

    #[test]
    fn fallback_catalog_covers_all_coding_plan_providers() {
        let catalog = fallback_catalog();
        let coding_plan_providers = [
            "alibaba",
            "alibaba-cn",
            "zai-coding-plan",
            "zhipuai-coding-plan",
            "kimi-for-coding",
            "minimax-coding-plan",
            "minimax-cn-coding-plan",
        ];
        for provider in coding_plan_providers {
            let models = catalog.models_for(provider);
            assert!(
                !models.is_empty(),
                "Coding plan provider {provider} has no models in fallback_catalog()"
            );
        }
    }
}
