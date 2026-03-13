//! Hardcoded fallback models for when fetch + cache both fail.

use std::collections::HashMap;

use super::types::{CatalogModel, ModelCatalog};

/// Curated coding-focused models per model provider prefix.
/// The prefix (e.g., "anthropic") matches the model ID prefix in models.dev
/// (e.g., "anthropic/claude-sonnet-4.6"). Only whitelisted models with
/// `tool_call: true` are shown. Update this list when new flagship models launch.
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
            "gpt-4.1",
            "codex-mini-latest",
        ],
    ),
    (
        "copilot",
        &[
            "claude-sonnet-4",
            "claude-sonnet-4.5",
            "claude-sonnet-4.6",
            "claude-opus-4.5",
            "claude-opus-4.6",
            "claude-haiku-4.5",
            "gpt-4.1",
            "gpt-4o",
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
/// Generated from the compiled-in model registry (single source of truth).
pub fn fallback_catalog() -> ModelCatalog {
    let reg = super::registry::registry();
    let mut providers: HashMap<String, Vec<CatalogModel>> = HashMap::new();
    for model in &reg.models {
        providers
            .entry(model.provider.clone())
            .or_default()
            // Subscription providers (0.0/0.0 cost) get None/None so they
            // don't display "free" — they're subscription-included, not free.
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
    ModelCatalog {
        providers,
        fetched_at: 0,
    }
}
