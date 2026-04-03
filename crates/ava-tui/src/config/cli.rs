use clap::{Parser, Subcommand, ValueEnum};
use tracing::debug;

#[derive(Debug, Clone, Parser)]
#[command(
    name = "ava",
    version,
    about = "AI coding assistant for terminal, browser, and desktop",
    long_about = "AVA is an AI coding assistant that can run as an interactive TUI, a headless CLI agent, a browser-backed web server, or a desktop app backend.",
    after_help = "Examples:\n  ava\n      Open the terminal UI\n\n  ava \"fix the failing test\"\n      Run one task headlessly\n\n  ava --provider openrouter --model anthropic/claude-sonnet-4 \"review this crate\"\n      Run with an explicit provider and model\n\n  ava --connect openrouter\n      Connect a provider interactively\n\n  ava serve --port 8080\n      Start the browser/web server\n\n  ava auth login openrouter\n      Connect a provider\n\n  ava review --staged\n      Review staged git changes",
    next_line_help = true
)]
pub struct CliArgs {
    /// Goal to execute immediately
    #[arg(value_name = "GOAL")]
    pub goal: Option<String>,

    /// Resume last session
    #[arg(short = 'c', long = "continue")]
    #[arg(help_heading = "Session")]
    pub resume: bool,

    /// Resume a specific session id
    #[arg(long)]
    #[arg(help_heading = "Session")]
    pub session: Option<String>,

    /// Model to use
    #[arg(long, short)]
    #[arg(help_heading = "Model Selection")]
    pub model: Option<String>,

    /// Provider to use
    #[arg(long)]
    #[arg(help_heading = "Model Selection")]
    pub provider: Option<String>,

    /// Connect a provider interactively and exit
    #[arg(long, value_name = "PROVIDER")]
    #[arg(help_heading = "Project Setup")]
    pub connect: Option<String>,

    /// Maximum agent turns (0 = unlimited)
    #[arg(long, default_value_t = 0)]
    #[arg(help_heading = "Execution")]
    pub max_turns: usize,

    /// Maximum budget in USD (0 = unlimited)
    #[arg(long, default_value_t = 0.0)]
    #[arg(help_heading = "Execution")]
    pub max_budget_usd: f64,

    /// Auto-approve all tools (except Critical)
    #[arg(long, alias = "yolo")]
    #[arg(help_heading = "Automation")]
    pub auto_approve: bool,

    /// Theme name
    #[arg(long, default_value = "default")]
    #[arg(help_heading = "Interactive UI")]
    pub theme: String,

    /// Increase log verbosity. Use -v for info, -vv for debug, -vvv for trace.
    #[arg(long, short = 'v', action = clap::ArgAction::Count)]
    #[arg(help_heading = "Debugging")]
    pub verbose: u8,

    /// Force headless mode (no TUI)
    #[arg(long)]
    #[arg(help_heading = "Execution Modes")]
    pub headless: bool,

    /// Reduce startup overhead by skipping project instructions and eager codebase indexing
    #[arg(long)]
    #[arg(help_heading = "Execution")]
    pub fast: bool,

    /// Output JSON events (for scripting/piping)
    #[arg(long)]
    #[arg(help_heading = "Execution Modes")]
    pub json: bool,

    /// Watch files for `ava:` comment directives
    #[arg(long)]
    #[arg(help_heading = "Automation")]
    pub watch: bool,

    /// Additional watch paths (repeatable)
    #[arg(long = "watch-path")]
    #[arg(help_heading = "Automation")]
    pub watch_path: Vec<String>,

    /// Trust the current project and enable local AVA config/plugins
    #[arg(long)]
    #[arg(help_heading = "Project Setup")]
    pub trust: bool,

    /// Thinking/reasoning effort level: off, low, medium, high, xhigh
    #[arg(long, default_value = "off")]
    #[arg(help_heading = "Execution")]
    pub thinking: String,

    /// Disable automatic update checks on startup
    #[arg(long)]
    #[arg(help_heading = "Project Setup")]
    pub no_update_check: bool,

    /// Run as ACP (Agent Client Protocol) server on stdio for IDE integration
    #[arg(long)]
    #[arg(help_heading = "Execution Modes")]
    pub acp_server: bool,

    /// Attach image files to the prompt (repeatable). Supported: png, jpg, jpeg, gif, webp
    #[arg(long)]
    #[arg(help_heading = "Input")]
    pub image: Vec<String>,

    /// Force a review pass after the agent completes
    #[arg(long)]
    #[arg(help_heading = "Automation")]
    pub review: bool,

    /// Enable continuous voice input (requires --features voice)
    #[arg(long)]
    #[arg(help_heading = "Input")]
    pub voice: bool,

    /// Run model benchmarks instead of normal operation
    #[arg(long)]
    #[arg(help_heading = "Benchmarks")]
    pub benchmark: bool,

    /// Models to benchmark in "provider:model,provider:model" format
    #[arg(long)]
    #[arg(help_heading = "Benchmarks")]
    pub models: Option<String>,

    /// LLM judge models for benchmark evaluation in "provider:model,provider:model" format
    #[arg(long)]
    #[arg(help_heading = "Benchmarks")]
    pub judges: Option<String>,

    /// Benchmark suite filter: speed, standard, frontier, all (default: all)
    #[arg(long, default_value = "all")]
    #[arg(help_heading = "Benchmarks")]
    pub suite: String,

    /// Benchmark language filter: rust, python, js, go (comma-separated, default: all)
    #[arg(long)]
    #[arg(help_heading = "Benchmarks")]
    pub language: Option<String>,

    /// Benchmark task filter: comma-separated task names or substrings
    #[arg(long)]
    #[arg(help_heading = "Benchmarks")]
    pub task_filter: Option<String>,

    /// Run harnessed-pair benchmark (SOTA director + fast worker)
    #[arg(long)]
    #[arg(help_heading = "Benchmarks")]
    pub harness: bool,

    /// Director model spec for harness benchmark (e.g. "openrouter:anthropic/claude-opus-4.6")
    #[arg(long)]
    #[arg(help_heading = "Benchmarks")]
    pub director: Option<String>,

    /// Worker model spec for harness benchmark (e.g. "inception:mercury-2")
    #[arg(long)]
    #[arg(help_heading = "Benchmarks")]
    pub worker: Option<String>,

    /// Import Aider Polyglot benchmark tasks from a local repo path
    #[arg(long)]
    #[arg(help_heading = "Benchmarks")]
    pub import_polyglot: Option<String>,

    /// Follow-up messages to run after the main task completes
    #[arg(long = "follow-up")]
    #[arg(help_heading = "Automation")]
    pub follow_up: Vec<String>,

    /// Post-complete messages to run after everything else
    #[arg(long)]
    #[arg(help_heading = "Automation")]
    pub later: Vec<String>,

    /// Post-complete message with an explicit group number
    #[arg(long = "later-group", num_args = 2, value_names = ["GROUP", "MESSAGE"])]
    #[arg(help_heading = "Automation")]
    pub later_group: Vec<String>,

    #[command(subcommand)]
    pub command: Option<Command>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RuntimeLeanSettings {
    pub include_project_instructions: bool,
    pub eager_codebase_indexing: bool,
    /// Informational only; actual behavior is expressed via the other fields.
    pub auto_lean: bool,
}

impl CliArgs {
    fn qualifies_for_auto_lean(&self) -> bool {
        let Some(goal) = self.goal.as_deref() else {
            return false;
        };

        if !self.headless
            || self.resume
            || self.session.is_some()
            || self.command.is_some()
            || self.watch
            || self.voice
            || self.benchmark
            || self.harness
            || !self.image.is_empty()
            || !self.follow_up.is_empty()
            || !self.later.is_empty()
            || !self.later_group.is_empty()
        {
            return false;
        }

        let trimmed = goal.trim();
        // `max_turns == 0` means "use the normal default cap" in CLI UX; simple
        // one-shot prompts should still benefit from the auto-lean path.
        trimmed.len() <= 180
            && trimmed.lines().count() <= 3
            && (self.max_turns == 0 || self.max_turns <= 4)
    }

    pub fn runtime_lean_settings(&self) -> RuntimeLeanSettings {
        if self.fast {
            return RuntimeLeanSettings {
                include_project_instructions: false,
                eager_codebase_indexing: false,
                auto_lean: false,
            };
        }

        if self.qualifies_for_auto_lean() {
            return RuntimeLeanSettings {
                include_project_instructions: true,
                eager_codebase_indexing: false,
                auto_lean: true,
            };
        }

        RuntimeLeanSettings {
            include_project_instructions: true,
            eager_codebase_indexing: true,
            auto_lean: false,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn base_cli() -> CliArgs {
        CliArgs {
            goal: Some("Reply exactly with ok".to_string()),
            resume: false,
            session: None,
            model: None,
            provider: None,
            connect: None,
            max_turns: 3,
            max_budget_usd: 0.0,
            auto_approve: false,
            theme: "default".to_string(),
            verbose: 0,
            headless: true,
            fast: false,
            json: false,
            watch: false,
            watch_path: Vec::new(),
            trust: false,
            thinking: "off".to_string(),
            no_update_check: false,
            acp_server: false,
            image: Vec::new(),
            review: false,
            voice: false,
            benchmark: false,
            models: None,
            judges: None,
            suite: "all".to_string(),
            language: None,
            task_filter: None,
            harness: false,
            director: None,
            worker: None,
            import_polyglot: None,
            follow_up: Vec::new(),
            later: Vec::new(),
            later_group: Vec::new(),
            command: None,
        }
    }

    #[test]
    fn auto_lean_enables_for_simple_headless_goals() {
        let cli = base_cli();
        let settings = cli.runtime_lean_settings();
        assert!(settings.auto_lean);
        assert!(settings.include_project_instructions);
        assert!(!settings.eager_codebase_indexing);
    }

    #[test]
    fn explicit_fast_still_overrides_auto_lean() {
        let mut cli = base_cli();
        cli.fast = true;
        let settings = cli.runtime_lean_settings();
        assert!(!settings.auto_lean);
        assert!(!settings.include_project_instructions);
        assert!(!settings.eager_codebase_indexing);
    }

    #[test]
    fn complex_goal_keeps_full_runtime_defaults() {
        let mut cli = base_cli();
        cli.goal = Some(
            "Inspect the auth flow, update the Rust backend, run the tests, and summarize the API changes in detail."
                .to_string(),
        );
        cli.max_turns = 8;
        let settings = cli.runtime_lean_settings();
        assert!(!settings.auto_lean);
        assert!(settings.include_project_instructions);
        assert!(settings.eager_codebase_indexing);
    }
}

#[derive(Debug, Clone, Subcommand)]
#[command(
    about = "Top-level AVA commands",
    after_help = "Examples:\n  ava auth login openrouter\n  ava plugin list\n  ava hq init\n  ava serve --port 8080"
)]
pub enum Command {
    /// Review code changes using an LLM agent
    Review(ReviewArgs),
    /// Manage provider authentication
    Auth {
        #[command(subcommand)]
        action: AuthCommand,
    },
    /// Manage power plugins
    Plugin {
        #[command(subcommand)]
        action: PluginCommand,
    },
    /// HQ setup and utilities
    Hq {
        #[command(subcommand)]
        action: HqCommand,
    },
    /// Check for and install updates
    #[command(after_help = "Example:\n  ava update")]
    Update,
    /// Check for and install updates (alias)
    #[command(name = "self-update")]
    #[command(after_help = "Example:\n  ava self-update")]
    SelfUpdate,
    /// Start the AVA web server for browser mode
    #[command(
        after_help = "Examples:\n  ava serve\n  ava serve --port 3000\n  ava serve --host 127.0.0.1 --port 8080"
    )]
    Serve {
        /// Port to listen on
        #[arg(long, default_value_t = 8080)]
        port: u16,
        /// Host/IP to bind to
        #[arg(long, default_value = "0.0.0.0")]
        host: String,
    },
}

#[derive(Debug, Clone, Subcommand)]
#[command(
    about = "HQ setup and utilities",
    long_about = "Initialize and manage HQ project state for AVA's Director-led multi-agent mode.",
    after_help = "Examples:\n  ava hq init\n  ava hq init --force\n  ava hq init --director-model openrouter:anthropic/claude-opus-4.1"
)]
pub enum HqCommand {
    /// Initialize `.ava/HQ/` memory for the current project
    #[command(
        after_help = "Examples:\n  ava hq init\n  ava hq init --force\n  ava hq init --director-model openrouter:anthropic/claude-opus-4.1"
    )]
    Init {
        /// Preferred Director model to record in HQ memory
        #[arg(long)]
        director_model: Option<String>,
        /// Overwrite existing HQ memory files
        #[arg(long)]
        force: bool,
    },
}

#[derive(Debug, Clone, Subcommand)]
#[command(
    about = "Manage power plugins",
    after_help = "Examples:\n  ava plugin list\n  ava plugin add ./my-plugin\n  ava plugin add @scope/ava-plugin\n  ava plugin init my-plugin --lang typescript"
)]
pub enum PluginCommand {
    /// List installed plugins
    #[command(after_help = "Example:\n  ava plugin list")]
    List,
    /// Install a plugin from a local path or npm package
    #[command(
        after_help = "Examples:\n  ava plugin add ./my-plugin\n  ava plugin add @scope/ava-plugin"
    )]
    Add {
        /// Local path to plugin directory, or npm package name
        source: String,
    },
    /// Remove an installed plugin
    #[command(after_help = "Example:\n  ava plugin remove my-plugin")]
    Remove {
        /// Plugin name
        name: String,
    },
    /// Show details for an installed plugin
    #[command(after_help = "Example:\n  ava plugin info my-plugin")]
    Info {
        /// Plugin name
        name: String,
    },
    /// Scaffold a new plugin project
    #[command(
        after_help = "Examples:\n  ava plugin init my-plugin\n  ava plugin init my-plugin --lang python"
    )]
    Init {
        /// Plugin name
        name: String,
        /// Language: typescript, python, or shell
        #[arg(long, default_value = "typescript")]
        lang: String,
    },
}

#[derive(Debug, Clone, Subcommand)]
#[command(
    about = "Manage provider authentication",
    long_about = "Log in to providers, remove credentials, list configured providers, or test a provider connection.",
    after_help = "Examples:\n  ava auth login openrouter\n  ava auth logout openai\n  ava auth list\n  ava auth test copilot"
)]
pub enum AuthCommand {
    /// Sign in to a provider (opens browser for OAuth, prompts for API key)
    #[command(
        after_help = "Examples:\n  ava auth login openrouter\n  ava auth login openai\n  ava auth login copilot"
    )]
    Login {
        /// Provider ID (e.g., openai, copilot, anthropic)
        provider: String,
    },
    /// Remove credentials for a provider
    #[command(after_help = "Examples:\n  ava auth logout openai\n  ava auth logout openrouter")]
    Logout {
        /// Provider ID
        provider: String,
    },
    /// List all configured providers with status
    #[command(after_help = "Example:\n  ava auth list")]
    List,
    /// Test connection to a provider
    #[command(after_help = "Examples:\n  ava auth test openrouter\n  ava auth test anthropic")]
    Test {
        /// Provider ID
        provider: String,
    },
}

#[derive(Debug, Clone, Parser)]
#[command(
    about = "Review code changes with AVA's review agent",
    after_help = "Examples:\n  ava review --staged\n  ava review --diff main..HEAD\n  ava review --commit abc123 --format markdown"
)]
pub struct ReviewArgs {
    /// Review staged changes (git diff --staged)
    #[arg(help_heading = "Review Scope")]
    #[arg(long)]
    pub staged: bool,

    /// Review a diff range (e.g. "main..HEAD", "abc123..def456")
    #[arg(help_heading = "Review Scope")]
    #[arg(long)]
    pub diff: Option<String>,

    /// Review a specific commit
    #[arg(help_heading = "Review Scope")]
    #[arg(long)]
    pub commit: Option<String>,

    /// Review working directory changes (unstaged)
    #[arg(help_heading = "Review Scope")]
    #[arg(long)]
    pub working: bool,

    /// Output format
    #[arg(help_heading = "Review Output")]
    #[arg(long, value_enum, default_value = "text")]
    pub format: ReviewFormat,

    /// Focus area for the review
    #[arg(help_heading = "Review Output")]
    #[arg(long, default_value = "all")]
    pub focus: String,

    /// Fail (exit 1) when issues at or above this severity are found
    #[arg(help_heading = "Review Output")]
    #[arg(long, value_enum, default_value = "critical")]
    pub fail_on: FailOnSeverity,

    /// LLM provider to use
    #[arg(help_heading = "Model Selection")]
    #[arg(long)]
    pub provider: Option<String>,

    /// LLM model to use
    #[arg(help_heading = "Model Selection")]
    #[arg(long, short)]
    pub model: Option<String>,

    /// Maximum agent turns
    #[arg(help_heading = "Execution")]
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
    pub fn to_severity(self) -> ava_hq::Severity {
        match self {
            Self::Critical => ava_hq::Severity::Critical,
            Self::Warning => ava_hq::Severity::Warning,
            Self::Suggestion => ava_hq::Severity::Suggestion,
            Self::Any => ava_hq::Severity::Nitpick,
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
        debug!(
            ?env_provider,
            ?env_model,
            "Using provider/model from env vars"
        );
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
                let has_provider = value.get("llm").and_then(|l| l.get("provider")).is_some();
                if has_provider {
                    debug!("Loaded provider 'openai' from config file");
                    Some(config.llm.provider)
                } else {
                    None
                }
            } else {
                None
            };
            let model = if config.llm.model != "gpt-5.3-codex" {
                debug!(model = %config.llm.model, "Loaded model from config file");
                Some(config.llm.model)
            } else if let Ok(value) = serde_yaml::from_str::<serde_json::Value>(&content) {
                let has_model = value.get("llm").and_then(|l| l.get("model")).is_some();
                if has_model {
                    debug!("Loaded model 'gpt-5.3-codex' from config file");
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
No provider configured.

Quickest fix:
  ava --connect openrouter

Or use the auth subcommand directly:
  ava auth login openrouter

Or set defaults in ~/.ava/config.yaml or use --provider/--model flags.

Example ~/.ava/config.yaml:
  llm:
    provider: openrouter
    model: anthropic/claude-sonnet-4

Or run with flags:
  ava --provider openrouter --model anthropic/claude-sonnet-4 \"your goal\"";
