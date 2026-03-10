//! Hardcoded fallback models for when fetch + cache both fail.

use std::collections::HashMap;

use super::types::{CatalogModel, ModelCatalog};

/// Curated coding-focused models per model provider prefix.
/// The prefix (e.g., "anthropic") matches the model ID prefix in models.dev
/// (e.g., "anthropic/claude-sonnet-4.6"). Only whitelisted models with
/// `tool_call: true` are shown. Update this list when new flagship models launch.
pub(crate) const CURATED_MODELS: &[(&str, &[&str])] = &[
    ("anthropic", &[
        "claude-opus-4.6",
        "claude-sonnet-4.6",
        "claude-sonnet-4.5",
        "claude-haiku-4.5",
    ]),
    ("openai", &[
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
    ]),
    ("copilot", &[
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
    ]),
    ("google", &[
        "gemini-2.5-pro",
        "gemini-2.5-flash",
        "gemini-3-pro-preview",
        "gemini-3-flash-preview",
    ]),
    // Coding plan providers
    ("zai-coding-plan", &[
        "glm-4.7",
        "glm-4.6",
        "glm-4.5",
        "glm-4.5-flash",
        "glm-4.7-flash",
    ]),
    ("zhipuai-coding-plan", &[
        "glm-4.7",
        "glm-4.6",
        "glm-4.5",
        "glm-4.5-flash",
        "glm-4.7-flash",
    ]),
    ("alibaba", &[
        "qwen3.5-plus",
        "qwen3-max-2026-01-23",
        "qwen3-coder-next",
        "qwen3-coder-plus",
        "MiniMax-M2.5",
        "glm-5",
        "glm-4.7",
        "kimi-k2.5",
    ]),
    ("alibaba-cn", &[
        "qwen3.5-plus",
        "qwen3-max-2026-01-23",
        "qwen3-coder-next",
        "qwen3-coder-plus",
        "MiniMax-M2.5",
        "glm-5",
        "glm-4.7",
        "kimi-k2.5",
    ]),
    ("kimi-for-coding", &[
        "k2p5",
        "kimi-k2-thinking",
    ]),
    ("minimax-coding-plan", &[
        "MiniMax-M2",
        "MiniMax-M2.1",
    ]),
    ("minimax-cn-coding-plan", &[
        "MiniMax-M2",
        "MiniMax-M2.1",
    ]),
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
                cost_input: if model.cost.input_per_million == 0.0 && model.cost.output_per_million == 0.0 {
                    None
                } else {
                    Some(model.cost.input_per_million)
                },
                cost_output: if model.cost.input_per_million == 0.0 && model.cost.output_per_million == 0.0 {
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

/// Legacy hardcoded fallback — kept as dead code reference, replaced by registry-generated version above.
#[allow(dead_code)]
fn _legacy_fallback_catalog() -> ModelCatalog {
    let mut providers = HashMap::new();

    providers.insert(
        "anthropic".to_string(),
        vec![
            CatalogModel {
                id: "claude-opus-4.6".to_string(),
                name: "Claude Opus 4.6".to_string(),
                provider_id: "anthropic".to_string(),
                tool_call: true,
                cost_input: Some(15.0),
                cost_output: Some(75.0),
                context_window: Some(200_000),
                max_output: Some(32_000),
            },
            CatalogModel {
                id: "claude-sonnet-4.6".to_string(),
                name: "Claude Sonnet 4.6".to_string(),
                provider_id: "anthropic".to_string(),
                tool_call: true,
                cost_input: Some(3.0),
                cost_output: Some(15.0),
                context_window: Some(200_000),
                max_output: Some(64_000),
            },
            CatalogModel {
                id: "claude-sonnet-4.5".to_string(),
                name: "Claude Sonnet 4.5".to_string(),
                provider_id: "anthropic".to_string(),
                tool_call: true,
                cost_input: Some(3.0),
                cost_output: Some(15.0),
                context_window: Some(200_000),
                max_output: Some(16_000),
            },
            CatalogModel {
                id: "claude-haiku-4.5".to_string(),
                name: "Claude Haiku 4.5".to_string(),
                provider_id: "anthropic".to_string(),
                tool_call: true,
                cost_input: Some(1.0),
                cost_output: Some(5.0),
                context_window: Some(200_000),
                max_output: Some(8_192),
            },
        ],
    );

    providers.insert(
        "openai".to_string(),
        vec![
            CatalogModel {
                id: "gpt-5.3-codex".to_string(),
                name: "GPT-5.3 Codex".to_string(),
                provider_id: "openai".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5.3-codex-spark".to_string(),
                name: "GPT-5.3 Codex Spark".to_string(),
                provider_id: "openai".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5.2-pro".to_string(),
                name: "GPT-5.2 Pro".to_string(),
                provider_id: "openai".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5.2-codex".to_string(),
                name: "GPT-5.2 Codex".to_string(),
                provider_id: "openai".to_string(),
                tool_call: true,
                cost_input: Some(2.0),
                cost_output: Some(8.0),
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5.2".to_string(),
                name: "GPT-5.2".to_string(),
                provider_id: "openai".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5.1-codex-max".to_string(),
                name: "GPT-5.1 Codex Max".to_string(),
                provider_id: "openai".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5.1-codex".to_string(),
                name: "GPT-5.1 Codex".to_string(),
                provider_id: "openai".to_string(),
                tool_call: true,
                cost_input: Some(1.5),
                cost_output: Some(6.0),
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5.1-codex-mini".to_string(),
                name: "GPT-5.1 Codex Mini".to_string(),
                provider_id: "openai".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5.1".to_string(),
                name: "GPT-5.1".to_string(),
                provider_id: "openai".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5-codex".to_string(),
                name: "GPT-5 Codex".to_string(),
                provider_id: "openai".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "codex-mini-latest".to_string(),
                name: "Codex Mini".to_string(),
                provider_id: "openai".to_string(),
                tool_call: true,
                cost_input: Some(1.5),
                cost_output: Some(6.0),
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5".to_string(),
                name: "GPT-5".to_string(),
                provider_id: "openai".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-4.1".to_string(),
                name: "GPT-4.1".to_string(),
                provider_id: "openai".to_string(),
                tool_call: true,
                cost_input: Some(2.0),
                cost_output: Some(8.0),
                context_window: Some(1_000_000),
                max_output: Some(32_768),
            },
        ],
    );

    providers.insert(
        "copilot".to_string(),
        vec![
            CatalogModel {
                id: "claude-sonnet-4.6".to_string(),
                name: "Claude Sonnet 4.6".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(64_000),
            },
            CatalogModel {
                id: "claude-sonnet-4.5".to_string(),
                name: "Claude Sonnet 4.5".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(16_000),
            },
            CatalogModel {
                id: "claude-sonnet-4".to_string(),
                name: "Claude Sonnet 4".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(16_000),
            },
            CatalogModel {
                id: "claude-opus-4.6".to_string(),
                name: "Claude Opus 4.6".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(32_000),
            },
            CatalogModel {
                id: "claude-opus-4.5".to_string(),
                name: "Claude Opus 4.5".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(32_000),
            },
            CatalogModel {
                id: "claude-haiku-4.5".to_string(),
                name: "Claude Haiku 4.5".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(8_192),
            },
            CatalogModel {
                id: "gpt-4.1".to_string(),
                name: "GPT-4.1".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(1_000_000),
                max_output: Some(32_768),
            },
            CatalogModel {
                id: "gpt-4o".to_string(),
                name: "GPT-4o".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(128_000),
                max_output: Some(16_384),
            },
            CatalogModel {
                id: "gpt-5".to_string(),
                name: "GPT-5".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5-mini".to_string(),
                name: "GPT-5 Mini".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5.1".to_string(),
                name: "GPT-5.1".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5.1-codex".to_string(),
                name: "GPT-5.1 Codex".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5.1-codex-max".to_string(),
                name: "GPT-5.1 Codex Max".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gpt-5.2".to_string(),
                name: "GPT-5.2".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
            CatalogModel {
                id: "gemini-2.5-pro".to_string(),
                name: "Gemini 2.5 Pro".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(128_000),
                max_output: Some(65_536),
            },
            CatalogModel {
                id: "o3-mini".to_string(),
                name: "o3-mini".to_string(),
                provider_id: "copilot".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(200_000),
                max_output: Some(100_000),
            },
        ],
    );

    providers.insert(
        "google".to_string(),
        vec![
            CatalogModel {
                id: "gemini-2.5-pro".to_string(),
                name: "Gemini 2.5 Pro".to_string(),
                provider_id: "google".to_string(),
                tool_call: true,
                cost_input: Some(1.25),
                cost_output: Some(10.0),
                context_window: Some(1_000_000),
                max_output: Some(65_536),
            },
            CatalogModel {
                id: "gemini-2.5-flash".to_string(),
                name: "Gemini 2.5 Flash".to_string(),
                provider_id: "google".to_string(),
                tool_call: true,
                cost_input: Some(0.15),
                cost_output: Some(0.60),
                context_window: Some(1_000_000),
                max_output: Some(65_536),
            },
            CatalogModel {
                id: "gemini-3-pro-preview".to_string(),
                name: "Gemini 3 Pro Preview".to_string(),
                provider_id: "google".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(1_000_000),
                max_output: Some(65_536),
            },
            CatalogModel {
                id: "gemini-3-flash-preview".to_string(),
                name: "Gemini 3 Flash Preview".to_string(),
                provider_id: "google".to_string(),
                tool_call: true,
                cost_input: None,
                cost_output: None,
                context_window: Some(1_000_000),
                max_output: Some(65_536),
            },
        ],
    );

    // Coding plan providers (subscription)
    let glm_models = vec![
        CatalogModel {
            id: "glm-4.7".to_string(),
            name: "GLM-4.7".to_string(),
            provider_id: "zai-coding-plan".to_string(),
            tool_call: true,
            cost_input: Some(0.0),
            cost_output: Some(0.0),
            context_window: Some(204_800),
            max_output: Some(131_072),
        },
        CatalogModel {
            id: "glm-4.6".to_string(),
            name: "GLM-4.6".to_string(),
            provider_id: "zai-coding-plan".to_string(),
            tool_call: true,
            cost_input: Some(0.0),
            cost_output: Some(0.0),
            context_window: Some(204_800),
            max_output: Some(131_072),
        },
        CatalogModel {
            id: "glm-4.5".to_string(),
            name: "GLM-4.5".to_string(),
            provider_id: "zai-coding-plan".to_string(),
            tool_call: true,
            cost_input: Some(0.0),
            cost_output: Some(0.0),
            context_window: Some(131_072),
            max_output: Some(98_304),
        },
        CatalogModel {
            id: "glm-4.5-flash".to_string(),
            name: "GLM-4.5 Flash".to_string(),
            provider_id: "zai-coding-plan".to_string(),
            tool_call: true,
            cost_input: Some(0.0),
            cost_output: Some(0.0),
            context_window: Some(131_072),
            max_output: Some(98_304),
        },
        CatalogModel {
            id: "glm-4.7-flash".to_string(),
            name: "GLM-4.7 Flash".to_string(),
            provider_id: "zai-coding-plan".to_string(),
            tool_call: true,
            cost_input: Some(0.0),
            cost_output: Some(0.0),
            context_window: Some(204_800),
            max_output: Some(131_072),
        },
    ];
    providers.insert("zai-coding-plan".to_string(), glm_models.clone());
    providers.insert(
        "zhipuai-coding-plan".to_string(),
        glm_models
            .into_iter()
            .map(|m| CatalogModel {
                provider_id: "zhipuai-coding-plan".to_string(),
                ..m
            })
            .collect(),
    );

    providers.insert(
        "kimi-for-coding".to_string(),
        vec![
            CatalogModel {
                id: "k2p5".to_string(),
                name: "Kimi K2.5".to_string(),
                provider_id: "kimi-for-coding".to_string(),
                tool_call: true,
                cost_input: Some(0.0),
                cost_output: Some(0.0),
                context_window: Some(262_144),
                max_output: Some(32_768),
            },
            CatalogModel {
                id: "kimi-k2-thinking".to_string(),
                name: "Kimi K2 Thinking".to_string(),
                provider_id: "kimi-for-coding".to_string(),
                tool_call: true,
                cost_input: Some(0.0),
                cost_output: Some(0.0),
                context_window: Some(262_144),
                max_output: Some(32_768),
            },
        ],
    );

    let minimax_models = vec![
        CatalogModel {
            id: "MiniMax-M2".to_string(),
            name: "MiniMax M2".to_string(),
            provider_id: "minimax-coding-plan".to_string(),
            tool_call: true,
            cost_input: Some(0.0),
            cost_output: Some(0.0),
            context_window: Some(196_608),
            max_output: Some(128_000),
        },
        CatalogModel {
            id: "MiniMax-M2.1".to_string(),
            name: "MiniMax M2.1".to_string(),
            provider_id: "minimax-coding-plan".to_string(),
            tool_call: true,
            cost_input: Some(0.0),
            cost_output: Some(0.0),
            context_window: Some(204_800),
            max_output: Some(131_072),
        },
    ];
    providers.insert("minimax-coding-plan".to_string(), minimax_models.clone());
    providers.insert(
        "minimax-cn-coding-plan".to_string(),
        minimax_models
            .into_iter()
            .map(|m| CatalogModel {
                provider_id: "minimax-cn-coding-plan".to_string(),
                ..m
            })
            .collect(),
    );

    providers.insert(
        "alibaba".to_string(),
        vec![
            CatalogModel {
                id: "qwen3-coder-flash".to_string(),
                name: "Qwen3 Coder Flash".to_string(),
                provider_id: "alibaba".to_string(),
                tool_call: true,
                cost_input: Some(0.0),
                cost_output: Some(0.0),
                context_window: Some(1_000_000),
                max_output: None,
            },
            CatalogModel {
                id: "qwen-turbo".to_string(),
                name: "Qwen Turbo".to_string(),
                provider_id: "alibaba".to_string(),
                tool_call: true,
                cost_input: Some(0.0),
                cost_output: Some(0.0),
                context_window: Some(1_000_000),
                max_output: None,
            },
            CatalogModel {
                id: "qwen-vl-max".to_string(),
                name: "Qwen VL Max".to_string(),
                provider_id: "alibaba".to_string(),
                tool_call: true,
                cost_input: Some(0.0),
                cost_output: Some(0.0),
                context_window: Some(131_072),
                max_output: None,
            },
        ],
    );

    providers.insert(
        "alibaba-cn".to_string(),
        vec![
            CatalogModel {
                id: "qwen3-coder-flash".to_string(),
                name: "Qwen3 Coder Flash".to_string(),
                provider_id: "alibaba-cn".to_string(),
                tool_call: true,
                cost_input: Some(0.0),
                cost_output: Some(0.0),
                context_window: Some(1_000_000),
                max_output: None,
            },
            CatalogModel {
                id: "qwen-turbo".to_string(),
                name: "Qwen Turbo".to_string(),
                provider_id: "alibaba-cn".to_string(),
                tool_call: true,
                cost_input: Some(0.0),
                cost_output: Some(0.0),
                context_window: Some(1_000_000),
                max_output: None,
            },
            CatalogModel {
                id: "qwen-vl-max".to_string(),
                name: "Qwen VL Max".to_string(),
                provider_id: "alibaba-cn".to_string(),
                tool_call: true,
                cost_input: Some(0.0),
                cost_output: Some(0.0),
                context_window: Some(131_072),
                max_output: None,
            },
            CatalogModel {
                id: "deepseek-r1".to_string(),
                name: "DeepSeek R1".to_string(),
                provider_id: "alibaba-cn".to_string(),
                tool_call: true,
                cost_input: Some(0.0),
                cost_output: Some(0.0),
                context_window: Some(131_072),
                max_output: None,
            },
            CatalogModel {
                id: "deepseek-v3".to_string(),
                name: "DeepSeek V3".to_string(),
                provider_id: "alibaba-cn".to_string(),
                tool_call: true,
                cost_input: Some(0.0),
                cost_output: Some(0.0),
                context_window: Some(131_072),
                max_output: None,
            },
        ],
    );

    ModelCatalog {
        providers,
        fetched_at: 0,
    }
}
