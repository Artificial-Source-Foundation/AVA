use ava_config::CredentialStore;
use nucleo::pattern::{CaseMatching, Normalization, Pattern};
use nucleo::Matcher;

#[derive(Debug, Clone)]
pub struct ModelOption {
    pub display: String,
    pub provider: String,
    pub model: String,
    /// Cost string for display (e.g. "$3/$15", "free")
    pub cost: String,
    /// Section header this model belongs to
    pub section: ModelSection,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ModelSection {
    Recent,
    Anthropic,
    OpenAI,
    OpenRouter,
    Gemini,
    Ollama,
    NotConfigured(String),
}

impl ModelSection {
    pub fn label(&self) -> String {
        match self {
            Self::Recent => "Recent".to_string(),
            Self::Anthropic => "Anthropic".to_string(),
            Self::OpenAI => "OpenAI".to_string(),
            Self::OpenRouter => "OpenRouter".to_string(),
            Self::Gemini => "Gemini".to_string(),
            Self::Ollama => "Ollama (local)".to_string(),
            Self::NotConfigured(name) => format!("{name} (not configured)"),
        }
    }
}

#[derive(Debug)]
pub struct ModelSelectorState {
    pub query: String,
    pub selected: usize,
    pub models: Vec<ModelOption>,
}

impl Default for ModelSelectorState {
    fn default() -> Self {
        Self {
            query: String::new(),
            selected: 0,
            models: fallback_models(),
        }
    }
}

impl ModelSelectorState {
    /// Build model list from configured providers and recent models.
    pub fn from_credentials(credentials: &CredentialStore, recent: &[String]) -> Self {
        let models = build_model_list(credentials, recent);
        Self {
            query: String::new(),
            selected: 0,
            models,
        }
    }

    pub fn filtered(&self) -> Vec<&ModelOption> {
        if self.query.trim().is_empty() {
            return self.models.iter().collect();
        }
        let mut matcher = Matcher::new(nucleo::Config::DEFAULT);
        let needle = Pattern::parse(
            &self.query,
            CaseMatching::Ignore,
            Normalization::Smart,
        );
        let mut items: Vec<_> = self
            .models
            .iter()
            .filter_map(|item| {
                let mut buf = Vec::new();
                let haystack = nucleo::Utf32Str::new(&item.display, &mut buf);
                needle.score(haystack, &mut matcher).map(|score| (score, item))
            })
            .collect();
        items.sort_by(|a, b| b.0.cmp(&a.0));
        items.into_iter().map(|(_, item)| item).collect()
    }

    pub fn reset(&mut self) {
        self.query.clear();
        self.selected = 0;
    }
}

/// Curated model catalog per provider with costs.
struct CuratedModel {
    display: &'static str,
    model: &'static str,
    cost: &'static str,
}

fn anthropic_models() -> Vec<CuratedModel> {
    vec![
        CuratedModel { display: "claude-opus-4.6", model: "claude-opus-4-6", cost: "$15/$75" },
        CuratedModel { display: "claude-sonnet-4.6", model: "claude-sonnet-4-6", cost: "$3/$15" },
        CuratedModel { display: "claude-haiku-4.5", model: "claude-haiku-4-5", cost: "$1/$5" },
    ]
}

fn openai_models() -> Vec<CuratedModel> {
    vec![
        CuratedModel { display: "gpt-5.4", model: "gpt-5.4", cost: "$2.50/$15" },
        CuratedModel { display: "gpt-5.3-codex", model: "gpt-5.3-codex", cost: "$1.75/$7" },
        CuratedModel { display: "gpt-4o", model: "gpt-4o", cost: "$2.50/$10" },
        CuratedModel { display: "gpt-4o-mini", model: "gpt-4o-mini", cost: "$0.15/$0.60" },
    ]
}

fn gemini_models() -> Vec<CuratedModel> {
    vec![
        CuratedModel { display: "gemini-3-pro", model: "gemini-3-pro", cost: "$1.25/$5" },
        CuratedModel { display: "gemini-3-flash", model: "gemini-3-flash", cost: "$0.10/$0.40" },
        CuratedModel { display: "gemini-2.5-flash", model: "gemini-2.5-flash", cost: "$0.15/$0.60" },
    ]
}

fn openrouter_models() -> Vec<CuratedModel> {
    vec![
        CuratedModel { display: "anthropic/claude-sonnet-4.6", model: "anthropic/claude-sonnet-4-6", cost: "$3/$15" },
        CuratedModel { display: "anthropic/claude-haiku-4.5", model: "anthropic/claude-haiku-4-5", cost: "$1/$5" },
        CuratedModel { display: "openai/gpt-4o", model: "openai/gpt-4o", cost: "$2.50/$10" },
        CuratedModel { display: "openai/gpt-4o-mini", model: "openai/gpt-4o-mini", cost: "$0.15/$0.60" },
        CuratedModel { display: "google/gemini-3-flash-preview", model: "google/gemini-3-flash-preview", cost: "$0.10/$0.40" },
        CuratedModel { display: "moonshotai/kimi-k2.5", model: "moonshotai/kimi-k2.5", cost: "$0.14/$0.28" },
    ]
}

type ProviderEntry = (&'static str, ModelSection, fn() -> Vec<CuratedModel>);

fn build_model_list(credentials: &CredentialStore, recent: &[String]) -> Vec<ModelOption> {
    let mut models = Vec::new();

    // Recent models section
    for key in recent {
        if let Some((provider, model)) = key.split_once('/') {
            models.push(ModelOption {
                display: key.clone(),
                provider: provider.to_string(),
                model: model.to_string(),
                cost: String::new(),
                section: ModelSection::Recent,
            });
        }
    }

    // Provider sections
    let providers: &[ProviderEntry] = &[
        ("anthropic", ModelSection::Anthropic, anthropic_models),
        ("openai", ModelSection::OpenAI, openai_models),
        ("openrouter", ModelSection::OpenRouter, openrouter_models),
        ("gemini", ModelSection::Gemini, gemini_models),
    ];

    for &(provider_id, ref section, model_fn) in providers {
        let configured = credentials.get(provider_id).is_some();
        let section = if configured {
            section.clone()
        } else {
            ModelSection::NotConfigured(ava_config::provider_name(provider_id))
        };

        for cm in model_fn() {
            models.push(ModelOption {
                display: cm.display.to_string(),
                provider: provider_id.to_string(),
                model: cm.model.to_string(),
                cost: cm.cost.to_string(),
                section: section.clone(),
            });
        }
    }

    // Ollama is always "local" — no API key needed
    models.push(ModelOption {
        display: "llama3.3".to_string(),
        provider: "ollama".to_string(),
        model: "llama3.3".to_string(),
        cost: "free".to_string(),
        section: ModelSection::Ollama,
    });
    models.push(ModelOption {
        display: "codestral".to_string(),
        provider: "ollama".to_string(),
        model: "codestral".to_string(),
        cost: "free".to_string(),
        section: ModelSection::Ollama,
    });

    models
}

/// Fallback models when credentials can't be loaded.
fn fallback_models() -> Vec<ModelOption> {
    vec![
        ModelOption {
            display: "claude-sonnet-4".to_string(),
            provider: "openrouter".to_string(),
            model: "anthropic/claude-sonnet-4".to_string(),
            cost: "$3/$15".to_string(),
            section: ModelSection::OpenRouter,
        },
        ModelOption {
            display: "claude-haiku-4-5".to_string(),
            provider: "openrouter".to_string(),
            model: "anthropic/claude-haiku-4-5".to_string(),
            cost: "$1/$5".to_string(),
            section: ModelSection::OpenRouter,
        },
        ModelOption {
            display: "gpt-4o".to_string(),
            provider: "openrouter".to_string(),
            model: "openai/gpt-4o".to_string(),
            cost: "$2.50/$10".to_string(),
            section: ModelSection::OpenRouter,
        },
        ModelOption {
            display: "gpt-4o-mini".to_string(),
            provider: "openrouter".to_string(),
            model: "openai/gpt-4o-mini".to_string(),
            cost: "$0.15/$0.60".to_string(),
            section: ModelSection::OpenRouter,
        },
        ModelOption {
            display: "kimi-k2.5".to_string(),
            provider: "openrouter".to_string(),
            model: "moonshotai/kimi-k2.5".to_string(),
            cost: "$0.14/$0.28".to_string(),
            section: ModelSection::OpenRouter,
        },
    ]
}
