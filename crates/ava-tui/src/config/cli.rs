use clap::{Parser, Subcommand, ValueEnum};
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

    /// Maximum agent turns (0 = unlimited)
    #[arg(long, default_value_t = 0)]
    pub max_turns: usize,

    /// Maximum budget in USD (0 = unlimited)
    #[arg(long, default_value_t = 0.0)]
    pub max_budget_usd: f64,

    /// Auto-approve all tools (except Critical)
    #[arg(long, alias = "yolo")]
    pub auto_approve: bool,

    /// Theme name
    #[arg(long, default_value = "default")]
    pub theme: String,

    /// Force headless mode (no TUI)
    #[arg(long)]
    pub headless: bool,

    /// Output JSON events (for scripting/piping)
    #[arg(long)]
    pub json: bool,

    /// Use multi-agent Director mode (Praxis) instead of single AgentStack
    #[arg(long, alias = "praxis")]
    pub multi_agent: bool,

    /// Run a workflow pipeline (plan-code-review, code-review, plan-code)
    #[arg(long)]
    pub workflow: Option<String>,

    /// Attach image files to the prompt (repeatable). Supported: png, jpg, jpeg, gif, webp
    #[arg(long)]
    pub image: Vec<String>,

    /// Enable continuous voice input (requires --features voice)
    #[arg(long)]
    pub voice: bool,

    /// Run model benchmarks instead of normal operation
    #[arg(long)]
    pub benchmark: bool,

    /// Models to benchmark in "provider:model,provider:model" format
    #[arg(long)]
    pub models: Option<String>,

    /// LLM judge models for benchmark evaluation in "provider:model,provider:model" format
    #[arg(long)]
    pub judges: Option<String>,

    /// Benchmark suite filter: speed, standard, frontier, all (default: all)
    #[arg(long, default_value = "all")]
    pub suite: String,

    /// Run harnessed-pair benchmark (SOTA director + fast worker)
    #[arg(long)]
    pub harness: bool,

    /// Director model spec for harness benchmark (e.g. "openrouter:anthropic/claude-opus-4.6")
    #[arg(long)]
    pub director: Option<String>,

    /// Worker model spec for harness benchmark (e.g. "inception:mercury-2")
    #[arg(long)]
    pub worker: Option<String>,

    /// Import Aider Polyglot benchmark tasks from a local repo path
    #[arg(long)]
    pub import_polyglot: Option<String>,

    /// Follow-up messages to run after the main task completes (Tier 2, repeatable)
    #[arg(long = "follow-up")]
    pub follow_up: Vec<String>,

    /// Post-complete messages to run after everything (Tier 3, auto-assigns groups)
    #[arg(long)]
    pub later: Vec<String>,

    /// Post-complete messages with explicit group numbers: --later-group 1 "review code"
    #[arg(long = "later-group", num_args = 2, value_names = ["GROUP", "MESSAGE"])]
    pub later_group: Vec<String>,

    #[command(subcommand)]
    pub command: Option<Command>,
}

#[derive(Debug, Clone, Subcommand)]
pub enum Command {
    /// Review code changes using an LLM agent
    Review(ReviewArgs),
    /// Manage provider authentication (OAuth, API keys)
    Auth {
        #[command(subcommand)]
        action: AuthCommand,
    },
}

#[derive(Debug, Clone, Subcommand)]
pub enum AuthCommand {
    /// Sign in to a provider (opens browser for OAuth, prompts for API key)
    Login {
        /// Provider ID (e.g., openai, copilot, anthropic)
        provider: String,
    },
    /// Remove credentials for a provider
    Logout {
        /// Provider ID
        provider: String,
    },
    /// List all configured providers with status
    List,
    /// Test connection to a provider
    Test {
        /// Provider ID
        provider: String,
    },
}

#[derive(Debug, Clone, Parser)]
pub struct ReviewArgs {
    /// Review staged changes (git diff --staged)
    #[arg(long)]
    pub staged: bool,

    /// Review a diff range (e.g. "main..HEAD", "abc123..def456")
    #[arg(long)]
    pub diff: Option<String>,

    /// Review a specific commit
    #[arg(long)]
    pub commit: Option<String>,

    /// Review working directory changes (unstaged)
    #[arg(long)]
    pub working: bool,

    /// Output format
    #[arg(long, value_enum, default_value = "text")]
    pub format: ReviewFormat,

    /// Focus area for the review
    #[arg(long, default_value = "all")]
    pub focus: String,

    /// Fail (exit 1) when issues at or above this severity are found
    #[arg(long, value_enum, default_value = "critical")]
    pub fail_on: FailOnSeverity,

    /// LLM provider to use
    #[arg(long)]
    pub provider: Option<String>,

    /// LLM model to use
    #[arg(long, short)]
    pub model: Option<String>,

    /// Maximum agent turns
    #[arg(long, default_value_t = 10)]
    pub max_turns: usize,
}

#[derive(Debug, Clone, Copy, ValueEnum)]
pub enum ReviewFormat {
    Text,
    Json,
    Markdown,
}

#[derive(Debug, Clone, Copy, ValueEnum)]
pub enum FailOnSeverity {
    Critical,
    Warning,
    Suggestion,
    Any,
}

impl FailOnSeverity {
    pub fn to_severity(self) -> ava_praxis::Severity {
        match self {
            Self::Critical => ava_praxis::Severity::Critical,
            Self::Warning => ava_praxis::Severity::Warning,
            Self::Suggestion => ava_praxis::Severity::Suggestion,
            Self::Any => ava_praxis::Severity::Nitpick,
        }
    }
}

/// Resolve provider and model from multiple sources.
/// Priority: explicit args > env vars > config file.
pub async fn resolve_provider_model(
    provider: Option<&str>,
    model: Option<&str>,
) -> color_eyre::Result<(Option<String>, Option<String>)> {
    // Explicit args take precedence
    if provider.is_some() || model.is_some() {
        return Ok((provider.map(String::from), model.map(String::from)));
    }

    // Env vars next
    let env_provider = std::env::var("AVA_PROVIDER").ok();
    let env_model = std::env::var("AVA_MODEL").ok();
    if env_provider.is_some() || env_model.is_some() {
        debug!(?env_provider, ?env_model, "Using provider/model from env vars");
        return Ok((env_provider, env_model));
    }

    // Check per-project state (`.ava/state.json`) for last used model
    let project_root = std::env::current_dir().unwrap_or_default();
    let project_state = ava_config::ProjectState::load(&project_root);
    if project_state.last_provider.is_some() {
        debug!(
            provider = ?project_state.last_provider,
            model = ?project_state.last_model,
            "Using last used provider/model from project state"
        );
        return Ok((project_state.last_provider, project_state.last_model));
    }

    // Try loading from global config file
    let data_dir = dirs::home_dir().unwrap_or_default().join(".ava");
    let config_path = data_dir.join("config.yaml");

    if config_path.exists() {
        let content = tokio::fs::read_to_string(&config_path).await?;
        if let Ok(config) = serde_yaml::from_str::<ava_config::Config>(&content) {
            // Fall back to explicit llm.provider/llm.model config
            let provider = if config.llm.provider != "openai" {
                debug!(provider = %config.llm.provider, "Loaded provider from config file");
                Some(config.llm.provider)
            } else if let Ok(value) = serde_yaml::from_str::<serde_json::Value>(&content) {
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

impl CliArgs {
    /// Resolve provider and model from CLI flags, env vars, or config file.
    pub async fn resolve_provider_model(
        &self,
    ) -> color_eyre::Result<(Option<String>, Option<String>)> {
        resolve_provider_model(self.provider.as_deref(), self.model.as_deref()).await
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
