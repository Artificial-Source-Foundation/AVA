use nucleo::pattern::{CaseMatching, Normalization, Pattern};
use nucleo::Matcher;

#[derive(Debug, Clone)]
pub struct ModelOption {
    pub display: String,
    pub provider: String,
    pub model: String,
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
            models: default_models(),
        }
    }
}

impl ModelSelectorState {
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

fn default_models() -> Vec<ModelOption> {
    vec![
        ModelOption {
            display: "claude-sonnet-4".to_string(),
            provider: "openrouter".to_string(),
            model: "anthropic/claude-sonnet-4".to_string(),
        },
        ModelOption {
            display: "claude-haiku-4-5".to_string(),
            provider: "openrouter".to_string(),
            model: "anthropic/claude-haiku-4-5".to_string(),
        },
        ModelOption {
            display: "gpt-4o".to_string(),
            provider: "openrouter".to_string(),
            model: "openai/gpt-4o".to_string(),
        },
        ModelOption {
            display: "gpt-4o-mini".to_string(),
            provider: "openrouter".to_string(),
            model: "openai/gpt-4o-mini".to_string(),
        },
        ModelOption {
            display: "kimi-k2.5".to_string(),
            provider: "openrouter".to_string(),
            model: "moonshotai/kimi-k2.5".to_string(),
        },
    ]
}
