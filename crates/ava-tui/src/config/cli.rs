use clap::Parser;
use tracing::debug;

#[derive(Debug, Clone, Parser)]
#[command(name = "ava", about = "AVA - AI coding assistant")]
pub struct CliArgs {
    /// Goal to execute immediately
    pub goal: Option<String>,

    /// Resume last session
    #[arg(short = 'c', long = "continue")]
    pub resume: bool,

    /// Resume a specific session id
    #[arg(long)]
    pub session: Option<String>,

    /// Model to use
    #[arg(long, short)]
    pub model: Option<String>,

    /// Provider to use
    #[arg(long)]
    pub provider: Option<String>,

    /// Maximum agent turns
    #[arg(long, default_value_t = 20)]
    pub max_turns: usize,

    /// Auto-approve all tools
    #[arg(long)]
    pub yolo: bool,

    /// Theme name
    #[arg(long, default_value = "default")]
    pub theme: String,

    /// Force headless mode (no TUI)
    #[arg(long)]
    pub headless: bool,

    /// Output JSON events (for scripting/piping)
    #[arg(long)]
    pub json: bool,

    /// Use multi-agent Commander mode instead of single AgentStack
    #[arg(long)]
    pub multi_agent: bool,
}

impl CliArgs {
    /// Resolve provider and model from CLI flags or config file (~/.ava/config.yaml).
    ///
    /// CLI flags take precedence. If neither is set, loads from the config file.
    /// Returns `(provider, model)` where either may still be `None` if unconfigured.
    pub async fn resolve_provider_model(
        &self,
    ) -> color_eyre::Result<(Option<String>, Option<String>)> {
        // CLI flags take precedence
        if self.provider.is_some() || self.model.is_some() {
            return Ok((self.provider.clone(), self.model.clone()));
        }

        // Try loading from config file
        let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");
        let config_path = data_dir.join("config.yaml");

        if config_path.exists() {
            let content = tokio::fs::read_to_string(&config_path).await?;
            if let Ok(config) = serde_yaml::from_str::<ava_config::Config>(&content) {
                let provider = if config.llm.provider != "openai" {
                    debug!(provider = %config.llm.provider, "Loaded provider from config file");
                    Some(config.llm.provider)
                } else {
                    // Check if the user explicitly set "openai" vs it being the default
                    // by looking at the raw YAML
                    if let Ok(value) = serde_yaml::from_str::<serde_json::Value>(&content) {
                        let has_provider = value
                            .get("llm")
                            .and_then(|l| l.get("provider"))
                            .is_some();
                        if has_provider {
                            debug!("Loaded provider 'openai' from config file");
                            Some(config.llm.provider)
                        } else {
                            None
                        }
                    } else {
                        None
                    }
                };
                let model = if config.llm.model != "gpt-4" {
                    debug!(model = %config.llm.model, "Loaded model from config file");
                    Some(config.llm.model)
                } else if let Ok(value) = serde_yaml::from_str::<serde_json::Value>(&content) {
                    let has_model = value.get("llm").and_then(|l| l.get("model")).is_some();
                    if has_model {
                        debug!("Loaded model 'gpt-4' from config file");
                        Some(config.llm.model)
                    } else {
                        None
                    }
                } else {
                    None
                };
                if provider.is_some() {
                    return Ok((provider, model));
                }
            }
        }

        Ok((None, None))
    }
}

/// Error message shown when no provider is configured anywhere.
pub const NO_PROVIDER_ERROR: &str = "\
No provider configured. Set defaults in ~/.ava/config.yaml or use --provider/--model flags.

Example ~/.ava/config.yaml:
  llm:
    provider: openrouter
    model: anthropic/claude-sonnet-4

Or run with flags:
  ava --provider openrouter --model anthropic/claude-sonnet-4 \"your goal\"";

