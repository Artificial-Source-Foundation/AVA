use ava_acp::DiscoveredAgent;
use ava_config::model_catalog::ModelCatalog;
use ava_config::CredentialStore;

use crate::widgets::select_list::{ItemStatus, SelectItem, SelectListState};

#[derive(Debug, Clone, Default)]
pub struct ModelValue {
    pub display: String,
    pub provider: String,
    pub model: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ModelSection {
    Recent,
    Copilot,
    Anthropic,
    OpenAI,
    OpenRouter,
    Gemini,
    Alibaba,
    ZAI,
    Kimi,
    MiniMax,
    Ollama,
    CLIAgents,
}

impl ModelSection {
    pub fn label(&self) -> String {
        match self {
            Self::Recent => "Recent".to_string(),
            Self::Copilot => "Copilot".to_string(),
            Self::Anthropic => "Anthropic".to_string(),
            Self::OpenAI => "OpenAI".to_string(),
            Self::OpenRouter => "OpenRouter".to_string(),
            Self::Gemini => "Gemini".to_string(),
            Self::Alibaba => "Alibaba".to_string(),
            Self::ZAI => "ZAI / ZhipuAI".to_string(),
            Self::Kimi => "Kimi".to_string(),
            Self::MiniMax => "MiniMax".to_string(),
            Self::Ollama => "Ollama".to_string(),
            Self::CLIAgents => "CLI Agents".to_string(),
        }
    }
}

#[derive(Debug, Default)]
pub struct ModelSelectorState {
    pub list: SelectListState<ModelValue>,
    pub current_model_key: String,
}

impl ModelSelectorState {
    /// Build model list from dynamic catalog + configured providers + recent models.
    pub fn from_catalog(
        catalog: &ModelCatalog,
        credentials: &CredentialStore,
        recent: &[String],
        current_model: &str,
        current_provider: &str,
    ) -> Self {
        Self::from_catalog_with_cli(
            catalog,
            credentials,
            recent,
            current_model,
            current_provider,
            &[],
        )
    }

    /// Build model list including discovered CLI agents.
    pub fn from_catalog_with_cli(
        catalog: &ModelCatalog,
        credentials: &CredentialStore,
        recent: &[String],
        current_model: &str,
        current_provider: &str,
        cli_agents: &[DiscoveredAgent],
    ) -> Self {
        let items = build_select_items(
            catalog,
            credentials,
            recent,
            current_model,
            current_provider,
            cli_agents,
        );
        Self {
            list: SelectListState::new(items),
            current_model_key: format!("{current_provider}/{current_model}"),
        }
    }

    /// Legacy: build from credentials only (uses fallback catalog).
    pub fn from_credentials(
        credentials: &CredentialStore,
        recent: &[String],
        current_model: &str,
        current_provider: &str,
    ) -> Self {
        let catalog = ava_config::fallback_catalog();
        Self::from_catalog(
            &catalog,
            credentials,
            recent,
            current_model,
            current_provider,
        )
    }

    pub fn reset(&mut self) {
        self.list.reset();
    }
}

fn build_select_items(
    catalog: &ModelCatalog,
    credentials: &CredentialStore,
    recent: &[String],
    current_model: &str,
    current_provider: &str,
    cli_agents: &[DiscoveredAgent],
) -> Vec<SelectItem<ModelValue>> {
    let mut items = Vec::new();
    let current_key = format!("{current_provider}/{current_model}");

    // Recent models section
    for key in recent {
        if let Some((provider, model)) = key.split_once('/') {
            let is_current = *key == current_key;
            items.push(SelectItem {
                title: key.clone(),
                detail: String::new(),
                section: Some(ModelSection::Recent.label()),
                status: if is_current {
                    Some(ItemStatus::Active)
                } else {
                    None
                },
                value: ModelValue {
                    display: key.clone(),
                    provider: provider.to_string(),
                    model: model.to_string(),
                },
                enabled: true,
            });
        }
    }

    // Provider sections from catalog
    for &(_catalog_provider, ava_provider, section_fn) in PROVIDER_SECTIONS {
        if ava_provider == "openrouter" {
            let configured = credentials
                .get("openrouter")
                .is_some_and(|c| !c.api_key.trim().is_empty() || c.is_oauth_configured());
            if !configured {
                continue;
            }
            let section = section_fn();
            for provider_id in ["anthropic", "openai", "google"] {
                for cm in catalog.models_for(cm_provider(provider_id)) {
                    let or_display = format!("{}/{}", provider_id, cm.name);
                    let model_id = cm.api_model_id("openrouter");
                    let is_current = format!("openrouter/{model_id}") == current_key;
                    items.push(SelectItem {
                        title: or_display.clone(),
                        detail: model_detail(&cm.cost_display(), cm.context_window),
                        section: Some(section.label()),
                        status: if is_current {
                            Some(ItemStatus::Active)
                        } else if cm.cost_display() == "free" {
                            Some(ItemStatus::Info("free".to_string()))
                        } else {
                            None
                        },
                        value: ModelValue {
                            display: or_display,
                            provider: "openrouter".to_string(),
                            model: model_id,
                        },
                        enabled: true,
                    });
                }
            }
            continue;
        }

        let configured = credentials
            .get(ava_provider)
            .is_some_and(|c| !c.api_key.trim().is_empty() || c.is_oauth_configured());

        if !configured {
            continue;
        }

        let section = section_fn();
        let catalog_models = catalog.models_for(ava_provider);

        for cm in catalog_models {
            let model_id = cm.api_model_id(ava_provider);
            let is_current = format!("{ava_provider}/{model_id}") == current_key;
            items.push(SelectItem {
                title: cm.name.clone(),
                detail: model_detail(&cm.cost_display(), cm.context_window),
                section: Some(section.label()),
                status: if is_current {
                    Some(ItemStatus::Active)
                } else if cm.cost_display() == "free" {
                    Some(ItemStatus::Info("free".to_string()))
                } else {
                    None
                },
                value: ModelValue {
                    display: cm.name.clone(),
                    provider: ava_provider.to_string(),
                    model: model_id,
                },
                enabled: true,
            });
        }
    }

    // Ollama is always "local"
    for (name, cost) in [
        ("llama3.3", "free"),
        ("codestral", "free"),
        ("qwen2.5-coder", "free"),
        ("devstral", "free"),
    ] {
        let is_current = format!("ollama/{name}") == current_key;
        items.push(SelectItem {
            title: name.to_string(),
            detail: cost.to_string(),
            section: Some(ModelSection::Ollama.label()),
            status: if is_current {
                Some(ItemStatus::Active)
            } else {
                Some(ItemStatus::Info("free".to_string()))
            },
            value: ModelValue {
                display: name.to_string(),
                provider: "ollama".to_string(),
                model: name.to_string(),
            },
            enabled: true,
        });
    }

    // CLI Agents — discovered external coding agents
    for agent in cli_agents {
        let display = format!("{} (CLI)", capitalize_agent_name(&agent.name));
        let is_current = format!("cli/{}", agent.name) == current_key;
        items.push(SelectItem {
            title: display.clone(),
            detail: format!("v{}", agent.version),
            section: Some(ModelSection::CLIAgents.label()),
            status: if is_current {
                Some(ItemStatus::Active)
            } else {
                Some(ItemStatus::Info("installed".to_string()))
            },
            value: ModelValue {
                display,
                provider: "cli".to_string(),
                model: agent.name.clone(),
            },
            enabled: true,
        });
    }

    items
}

/// Capitalize a kebab-case agent name for display (e.g., "claude-code" → "Claude Code").
fn capitalize_agent_name(name: &str) -> String {
    name.split('-')
        .map(|word| {
            let mut chars = word.chars();
            match chars.next() {
                None => String::new(),
                Some(c) => c.to_uppercase().to_string() + chars.as_str(),
            }
        })
        .collect::<Vec<_>>()
        .join(" ")
}

type ProviderEntry = (&'static str, &'static str, fn() -> ModelSection);

const PROVIDER_SECTIONS: &[ProviderEntry] = &[
    // Core providers
    ("copilot", "copilot", || ModelSection::Copilot),
    ("anthropic", "anthropic", || ModelSection::Anthropic),
    ("openai", "openai", || ModelSection::OpenAI),
    ("openrouter", "openrouter", || ModelSection::OpenRouter),
    ("google", "gemini", || ModelSection::Gemini),
    // Coding plan providers (subscription)
    ("alibaba", "alibaba", || ModelSection::Alibaba),
    ("alibaba-cn", "alibaba-cn", || ModelSection::Alibaba),
    ("zai-coding-plan", "zai-coding-plan", || ModelSection::ZAI),
    ("zhipuai-coding-plan", "zhipuai-coding-plan", || {
        ModelSection::ZAI
    }),
    ("kimi-for-coding", "kimi-for-coding", || ModelSection::Kimi),
    ("minimax-coding-plan", "minimax-coding-plan", || {
        ModelSection::MiniMax
    }),
    ("minimax-cn-coding-plan", "minimax-cn-coding-plan", || {
        ModelSection::MiniMax
    }),
];

fn cm_provider(ava_provider: &str) -> &str {
    match ava_provider {
        "gemini" => "google",
        other => other,
    }
}

/// Format a context window size as a human-readable string (e.g., "200K", "1M").
fn format_context_window(tokens: u64) -> String {
    if tokens >= 1_000_000 {
        let m = tokens as f64 / 1_000_000.0;
        if m == m.floor() {
            format!("{}M", m as u64)
        } else {
            format!("{:.1}M", m)
        }
    } else if tokens >= 1_000 {
        let k = tokens as f64 / 1_000.0;
        if k == k.floor() {
            format!("{}K", k as u64)
        } else {
            format!("{:.1}K", k)
        }
    } else {
        format!("{tokens}")
    }
}

/// Build a detail string combining cost and context window.
fn model_detail(cost: &str, context_window: Option<u64>) -> String {
    let ctx = context_window.map(format_context_window);
    match (cost.is_empty(), ctx) {
        (false, Some(ctx)) => format!("{cost}  {ctx}"),
        (false, None) => cost.to_string(),
        (true, Some(ctx)) => ctx,
        (true, None) => String::new(),
    }
}
