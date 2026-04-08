//! Hardcoded fallback models for when fetch + cache both fail.

use std::collections::HashMap;

use super::types::{CatalogModel, ModelCatalog};

/// Curated coding-focused models per provider.
/// Update this list when new flagship models launch or when the supported core set changes.
#[cfg(test)]
pub(crate) const CURATED_MODELS: &[(&str, &[&str])] = &[
    (
        "anthropic",
        &[
            "claude-opus-4.6",
            "claude-sonnet-4.6",
            "claude-sonnet-4.5",
            "claude-haiku-4.5",
        ],
    ),
    (
        "openai",
        &[
            "gpt-5.3-codex",
            "gpt-5.3-codex-spark",
            "gpt-5.2-pro",
            "gpt-5.2-codex",
            "gpt-5.2",
            "gpt-5.1-codex-max",
            "gpt-5.1-codex",
            "gpt-5.1-codex-mini",
            "gpt-5.1",
            "gpt-5-codex",
            "gpt-5",
            "codex-mini-latest",
        ],
    ),
    (
        "copilot",
        &[
            "claude-sonnet-4.5",
            "claude-sonnet-4.6",
            "claude-opus-4.5",
            "claude-opus-4.6",
            "claude-haiku-4.5",
            "gpt-5",
            "gpt-5-mini",
            "gpt-5.1",
            "gpt-5.1-codex",
            "gpt-5.1-codex-max",
            "gpt-5.2",
            "gemini-2.5-pro",
            "o3-mini",
        ],
    ),
    (
        "google",
        &[
            "gemini-2.5-pro",
            "gemini-2.5-flash",
            "gemini-3-pro-preview",
            "gemini-3-flash-preview",
        ],
    ),
    // Coding plan providers
    (
        "zai-coding-plan",
        &[
            "glm-4.7",
            "glm-4.6",
            "glm-4.5",
            "glm-4.5-flash",
            "glm-4.7-flash",
        ],
    ),
    (
        "zhipuai-coding-plan",
        &[
            "glm-4.7",
            "glm-4.6",
            "glm-4.5",
            "glm-4.5-flash",
            "glm-4.7-flash",
        ],
    ),
    (
        "alibaba",
        &[
            "qwen3.5-plus",
            "qwen3-max-2026-01-23",
            "qwen3-coder-next",
            "qwen3-coder-plus",
            "MiniMax-M2.5",
            "glm-5",
            "glm-4.7",
            "kimi-k2.5",
        ],
    ),
    (
        "alibaba-cn",
        &[
            "qwen3.5-plus",
            "qwen3-max-2026-01-23",
            "qwen3-coder-next",
            "qwen3-coder-plus",
            "MiniMax-M2.5",
            "glm-5",
            "glm-4.7",
            "kimi-k2.5",
        ],
    ),
    ("kimi-for-coding", &["k2p5", "kimi-k2-thinking"]),
    ("minimax-coding-plan", &["MiniMax-M2", "MiniMax-M2.1"]),
    ("minimax-cn-coding-plan", &["MiniMax-M2", "MiniMax-M2.1"]),
];

/// Hardcoded fallback models for when fetch + cache both fail.
///
/// Combines the compiled-in registry (for models with owned metadata)
/// with hardcoded entries for subscription/coding-plan providers that are not
/// carried in `registry.json` yet.
pub fn fallback_catalog() -> ModelCatalog {
    let reg = super::registry::registry();
    let mut providers: HashMap<String, Vec<CatalogModel>> = HashMap::new();

    // 1. Load models from the compiled-in registry (essential paid models)
    for model in &reg.models {
        providers
            .entry(model.provider.clone())
            .or_default()
            .push(CatalogModel {
                id: model.id.clone(),
                name: model.name.clone(),
                provider_id: model.provider.clone(),
                tool_call: model.capabilities.tool_call,
                cost_input: if model.cost.input_per_million == 0.0
                    && model.cost.output_per_million == 0.0
                {
                    None
                } else {
                    Some(model.cost.input_per_million)
                },
                cost_output: if model.cost.input_per_million == 0.0
                    && model.cost.output_per_million == 0.0
                {
                    None
                } else {
                    Some(model.cost.output_per_million)
                },
                context_window: Some(model.limits.context_window as u64),
                max_output: model.limits.max_output.map(|v| v as u64),
            });
    }

    // 2. Add subscription/coding-plan providers not in registry.json.
    //    These are free-tier or subscription-included models that don't need
    //    pricing data in the registry.
    add_subscription_models(&mut providers);

    ModelCatalog {
        providers,
        fetched_at: 0,
    }
}

/// Add subscription and coding-plan provider models that are not in registry.json.
/// These models have no cost (subscription-included) so they use None for pricing.
fn add_subscription_models(providers: &mut HashMap<String, Vec<CatalogModel>>) {
    // Copilot mirrors (free through GitHub subscription)
    add_models(
        providers,
        "copilot",
        &[
            (
                "claude-sonnet-4.5",
                "Claude Sonnet 4.5",
                200_000,
                Some(16_000),
            ),
            (
                "claude-sonnet-4.6",
                "Claude Sonnet 4.6",
                1_048_576,
                Some(64_000),
            ),
            ("claude-opus-4.5", "Claude Opus 4.5", 200_000, Some(32_000)),
            (
                "claude-opus-4.6",
                "Claude Opus 4.6",
                1_048_576,
                Some(32_000),
            ),
            ("claude-haiku-4.5", "Claude Haiku 4.5", 200_000, Some(8_192)),
            ("gpt-5", "GPT-5", 200_000, Some(100_000)),
            ("gpt-5-mini", "GPT-5 Mini", 131_072, Some(100_000)),
            ("gpt-5.1", "GPT-5.1", 200_000, Some(100_000)),
            ("gpt-5.1-codex", "GPT-5.1 Codex", 200_000, Some(100_000)),
            (
                "gpt-5.1-codex-max",
                "GPT-5.1 Codex Max",
                200_000,
                Some(100_000),
            ),
            ("gpt-5.2", "GPT-5.2", 200_000, Some(100_000)),
            ("gemini-2.5-pro", "Gemini 2.5 Pro", 1_000_000, Some(65_536)),
            ("o3-mini", "o3-mini", 200_000, Some(100_000)),
        ],
    );

    // OpenAI models not yet represented in the compiled registry.
    add_models(
        providers,
        "openai",
        &[
            ("gpt-5.3", "GPT-5.3", 200_000, Some(100_000)),
            (
                "gpt-5.3-codex-spark",
                "GPT-5.3 Codex Spark",
                200_000,
                Some(100_000),
            ),
            ("gpt-5.2-pro", "GPT-5.2 Pro", 200_000, Some(100_000)),
            ("gpt-5.2-codex", "GPT-5.2 Codex", 200_000, Some(100_000)),
            ("gpt-5.2", "GPT-5.2", 200_000, Some(100_000)),
            (
                "gpt-5.1-codex-max",
                "GPT-5.1 Codex Max",
                200_000,
                Some(100_000),
            ),
            ("gpt-5.1-codex", "GPT-5.1 Codex", 200_000, Some(100_000)),
            (
                "gpt-5.1-codex-mini",
                "GPT-5.1 Codex Mini",
                200_000,
                Some(100_000),
            ),
            ("gpt-5.1", "GPT-5.1", 200_000, Some(100_000)),
            ("gpt-5-codex", "GPT-5 Codex", 200_000, Some(100_000)),
            ("gpt-5", "GPT-5", 200_000, Some(100_000)),
            ("codex-mini-latest", "Codex Mini", 200_000, Some(100_000)),
        ],
    );

    // Google models not in the minimal registry
    add_models(
        providers,
        "google",
        &[
            (
                "gemini-3-pro-preview",
                "Gemini 3 Pro Preview",
                1_000_000,
                Some(65_536),
            ),
            (
                "gemini-3-flash-preview",
                "Gemini 3 Flash Preview",
                1_000_000,
                Some(65_536),
            ),
        ],
    );

    // ZAI Coding Plan
    add_models(
        providers,
        "zai-coding-plan",
        &[
            ("glm-4.7", "GLM-4.7", 204_800, Some(131_072)),
            ("glm-4.6", "GLM-4.6", 204_800, Some(131_072)),
            ("glm-4.5", "GLM-4.5", 131_072, Some(98_304)),
            ("glm-4.5-flash", "GLM-4.5 Flash", 131_072, Some(98_304)),
            ("glm-4.7-flash", "GLM-4.7 Flash", 204_800, Some(131_072)),
        ],
    );

    // ZhipuAI Coding Plan (same models as ZAI)
    add_models(
        providers,
        "zhipuai-coding-plan",
        &[
            ("glm-4.7", "GLM-4.7", 204_800, Some(131_072)),
            ("glm-4.6", "GLM-4.6", 204_800, Some(131_072)),
            ("glm-4.5", "GLM-4.5", 131_072, Some(98_304)),
            ("glm-4.5-flash", "GLM-4.5 Flash", 131_072, Some(98_304)),
            ("glm-4.7-flash", "GLM-4.7 Flash", 204_800, Some(131_072)),
        ],
    );

    // Alibaba Coding Plan
    add_models(
        providers,
        "alibaba",
        &[
            ("qwen3.5-plus", "Qwen3.5 Plus", 131_072, None),
            ("qwen3-max-2026-01-23", "Qwen3 Max", 131_072, None),
            ("qwen3-coder-next", "Qwen3 Coder Next", 131_072, None),
            ("qwen3-coder-plus", "Qwen3 Coder Plus", 131_072, None),
            ("MiniMax-M2.5", "MiniMax M2.5", 131_072, None),
            ("glm-5", "GLM-5", 131_072, None),
            ("glm-4.7", "GLM-4.7", 131_072, None),
            ("kimi-k2.5", "Kimi K2.5", 131_072, None),
        ],
    );

    // Alibaba CN (same models)
    add_models(
        providers,
        "alibaba-cn",
        &[
            ("qwen3.5-plus", "Qwen3.5 Plus", 131_072, None),
            ("qwen3-max-2026-01-23", "Qwen3 Max", 131_072, None),
            ("qwen3-coder-next", "Qwen3 Coder Next", 131_072, None),
            ("qwen3-coder-plus", "Qwen3 Coder Plus", 131_072, None),
            ("MiniMax-M2.5", "MiniMax M2.5", 131_072, None),
            ("glm-5", "GLM-5", 131_072, None),
            ("glm-4.7", "GLM-4.7", 131_072, None),
            ("kimi-k2.5", "Kimi K2.5", 131_072, None),
        ],
    );

    // Kimi for Coding
    add_models(
        providers,
        "kimi-for-coding",
        &[
            ("k2p5", "Kimi K2.5", 262_144, Some(32_768)),
            (
                "kimi-k2-thinking",
                "Kimi K2 Thinking",
                262_144,
                Some(32_768),
            ),
        ],
    );

    // MiniMax Coding Plan
    add_models(
        providers,
        "minimax-coding-plan",
        &[
            ("MiniMax-M2", "MiniMax M2", 196_608, Some(128_000)),
            ("MiniMax-M2.1", "MiniMax M2.1", 204_800, Some(131_072)),
        ],
    );

    // MiniMax CN Coding Plan
    add_models(
        providers,
        "minimax-cn-coding-plan",
        &[
            ("MiniMax-M2", "MiniMax M2", 196_608, Some(128_000)),
            ("MiniMax-M2.1", "MiniMax M2.1", 204_800, Some(131_072)),
        ],
    );
}

/// Helper: add models to a provider, skipping duplicates.
fn add_models(
    providers: &mut HashMap<String, Vec<CatalogModel>>,
    provider_id: &str,
    models: &[(&str, &str, u64, Option<u64>)],
) {
    let existing = providers.entry(provider_id.to_string()).or_default();
    for &(id, name, context_window, max_output) in models {
        if !existing.iter().any(|m| m.id == id) {
            existing.push(CatalogModel {
                id: id.to_string(),
                name: name.to_string(),
                provider_id: provider_id.to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(context_window),
                max_output,
            });
        }
    }
}
