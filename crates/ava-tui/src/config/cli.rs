use clap::{Parser, Subcommand, ValueEnum};
use std::collections::HashMap;
use std::path::PathBuf;
use tracing::debug;

#[derive(Debug, Clone, Parser)]
#[command(name = "ava", about = "AVA - AI coding assistant")]
pub struct CliArgs {
    /// Goal to execute immediately
    pub goal: Option<String>,

    /// Override the working directory AVA should run in. Takes precedence over
    /// `AVA_WORKING_DIRECTORY` when both are set.
    #[arg(long, value_name = "PATH")]
    pub cwd: Option<PathBuf>,

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

    /// Startup primary-agent profile id (configured in config.yaml)
    #[arg(long, value_name = "ID")]
    pub agent: Option<String>,

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

    /// Increase log verbosity. Use -v for info, -vv for debug, -vvv for trace.
    #[arg(long, short = 'v', action = clap::ArgAction::Count)]
    pub verbose: u8,

    /// Force headless mode (no TUI)
    #[arg(long)]
    pub headless: bool,

    /// Reduce startup overhead by skipping project instructions and eager codebase indexing
    #[arg(long)]
    pub fast: bool,

    /// Output JSON events (for scripting/piping)
    #[arg(long)]
    pub json: bool,

    /// Watch files and trigger on `ava:` comment directives
    #[arg(long)]
    pub watch: bool,

    /// Additional paths to watch (repeatable, defaults to current directory)
    #[arg(long = "watch-path")]
    pub watch_path: Vec<String>,

    /// Trust the current project (allows loading all project-local config: .ava/mcp.json, .ava/hooks/, .ava/tools/, .ava/commands/, .ava/subagents.toml, .ava/skills/, AGENTS.md, .ava/rules/)
    #[arg(long)]
    pub trust: bool,

    /// Thinking/reasoning effort level: off, low, medium, high, xhigh
    #[arg(long, default_value = "off")]
    pub thinking: String,

    /// Disable automatic update checks on startup
    #[arg(long)]
    pub no_update_check: bool,

    /// Run as ACP (Agent Client Protocol) server on stdio for IDE integration
    #[arg(long)]
    pub acp_server: bool,

    /// Attach image files to the prompt (repeatable). Supported: png, jpg, jpeg, gif, webp
    #[arg(long)]
    pub image: Vec<String>,

    /// Force a code review pass after the agent completes (for CI pipelines).
    /// Without this flag, the agent decides when to self-review via subagent.
    #[arg(long)]
    pub review: bool,

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

    /// Benchmark suite filter: speed, standard, frontier, prompt_regression, all (default: all)
    #[arg(long, default_value = "all")]
    pub suite: String,

    /// Benchmark language filter: rust, python, js, go (comma-separated, default: all)
    #[arg(long)]
    pub language: Option<String>,

    /// Benchmark task filter: comma-separated task names or substrings
    #[arg(long)]
    pub task_filter: Option<String>,

    /// Import Aider Polyglot benchmark tasks from a local repo path
    #[arg(long)]
    pub import_polyglot: Option<String>,

    /// Force a prompt family for benchmark prompt assembly (e.g. gpt, claude, gemini, generic)
    #[arg(long)]
    pub prompt_family: Option<String>,

    #[arg(long)]
    pub prompt_variant: Option<String>,

    /// Load benchmark prompt note overrides from a local file
    #[arg(long)]
    pub prompt_file: Option<String>,

    #[arg(long)]
    pub prompt_version: Option<String>,

    #[arg(long)]
    pub prompt_hash: Option<String>,

    /// Repeat benchmark runs N times and emit an aggregate summary
    #[arg(long, default_value_t = 1)]
    pub repeat: usize,

    /// Optional benchmark seed recorded in report metadata
    #[arg(long)]
    pub seed: Option<u64>,

    /// Optional output path for benchmark JSON artifact
    #[arg(long)]
    pub benchmark_output: Option<String>,

    /// Compare two benchmark JSON reports (left side path)
    #[arg(
        long = "benchmark-compare-left-report",
        alias = "benchmark-compare-ava-report",
        alias = "benchmark-compare-prompt-a-report"
    )]
    pub benchmark_compare_left_report: Option<String>,

    /// Compare two benchmark JSON reports (right side path)
    #[arg(
        long = "benchmark-compare-right-report",
        alias = "benchmark-compare-opencode-report",
        alias = "benchmark-compare-prompt-b-report"
    )]
    pub benchmark_compare_right_report: Option<String>,

    /// Optional output path for comparison JSON artifact
    #[arg(long)]
    pub benchmark_compare_output: Option<String>,

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

        // Keep normal interactive TUI startup lean: avoid eager indexing at boot
        // while still including project instructions.
        if !self.headless {
            return RuntimeLeanSettings {
                include_project_instructions: true,
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

    fn setup_startup_resolver_config(config_yaml: &str) -> tempfile::TempDir {
        let temp = tempfile::tempdir().expect("tempdir");
        let config_root = temp.path().join("config-root");
        let config_dir = config_root.join("ava");
        std::fs::create_dir_all(&config_dir).expect("create config dir");
        std::fs::write(config_dir.join("config.yaml"), config_yaml).expect("write config");
        temp
    }

    fn base_cli() -> CliArgs {
        CliArgs {
            goal: Some("Reply exactly with ok".to_string()),
            cwd: None,
            resume: false,
            session: None,
            model: None,
            provider: None,
            agent: None,
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
            import_polyglot: None,
            prompt_family: None,
            prompt_variant: None,
            prompt_file: None,
            prompt_version: None,
            prompt_hash: None,
            repeat: 1,
            seed: None,
            benchmark_output: None,
            benchmark_compare_left_report: None,
            benchmark_compare_right_report: None,
            benchmark_compare_output: None,
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

    #[test]
    fn interactive_tui_startup_stays_lean_without_auto_lean() {
        let mut cli = base_cli();
        cli.headless = false;
        cli.goal = None;

        let settings = cli.runtime_lean_settings();
        assert!(!settings.auto_lean);
        assert!(settings.include_project_instructions);
        assert!(!settings.eager_codebase_indexing);
    }

    #[test]
    fn cli_parses_cwd_override() {
        let cli = CliArgs::parse_from(["ava", "--cwd", "/tmp/project", "--headless"]);
        assert_eq!(cli.cwd, Some(PathBuf::from("/tmp/project")));
    }

    #[test]
    fn cli_parses_agent_override() {
        let cli = CliArgs::parse_from(["ava", "--agent", "architect", "--headless"]);
        assert_eq!(cli.agent.as_deref(), Some("architect"));
    }

    #[tokio::test]
    async fn startup_selection_uses_agent_provider_model_when_only_agent_is_explicit() {
        let temp = setup_startup_resolver_config(
            r#"
primary_agents:
  architect:
    provider: openrouter
    model: anthropic/claude-sonnet-4
    prompt: You are the architect profile.
"#,
        );
        let workspace = temp.path().join("workspace");
        std::fs::create_dir_all(&workspace).expect("workspace dir");
        let config_root = temp.path().join("config-root");
        let config_path = config_root.join("ava").join("config.yaml");

        let startup = resolve_startup_selection_with_context(
            None,
            None,
            Some("architect"),
            None,
            None,
            config_path,
            workspace,
        )
        .await
        .expect("resolve startup selection");

        assert_eq!(startup.provider.as_deref(), Some("openrouter"));
        assert_eq!(startup.model.as_deref(), Some("anthropic/claude-sonnet-4"));
        assert_eq!(startup.primary_agent_id.as_deref(), Some("architect"));
        assert_eq!(
            startup.primary_agent_prompt.as_deref(),
            Some("You are the architect profile.")
        );
    }

    #[tokio::test]
    async fn startup_selection_preserves_agent_prompt_when_provider_model_flags_override() {
        let temp = setup_startup_resolver_config(
            r#"
primary_agents:
  architect:
    provider: openrouter
    model: anthropic/claude-sonnet-4
    prompt: You are the architect profile.
"#,
        );
        let workspace = temp.path().join("workspace");
        std::fs::create_dir_all(&workspace).expect("workspace dir");
        let config_root = temp.path().join("config-root");
        let config_path = config_root.join("ava").join("config.yaml");

        let startup = resolve_startup_selection_with_context(
            Some("openai"),
            Some("gpt-5.3-codex"),
            Some("architect"),
            None,
            None,
            config_path,
            workspace,
        )
        .await
        .expect("resolve startup selection");

        assert_eq!(startup.provider.as_deref(), Some("openai"));
        assert_eq!(startup.model.as_deref(), Some("gpt-5.3-codex"));
        assert_eq!(startup.primary_agent_id.as_deref(), Some("architect"));
        assert_eq!(
            startup.primary_agent_prompt.as_deref(),
            Some("You are the architect profile.")
        );
    }

    #[tokio::test]
    async fn startup_selection_explicit_agent_works_when_unrelated_config_sections_are_invalid() {
        let temp = setup_startup_resolver_config(
            r#"
llm: invalid-shape
primary_agents:
  architect:
    provider: openrouter
    model: anthropic/claude-sonnet-4
    prompt: You are the architect profile.
"#,
        );
        let workspace = temp.path().join("workspace");
        std::fs::create_dir_all(&workspace).expect("workspace dir");
        let config_root = temp.path().join("config-root");
        let config_path = config_root.join("ava").join("config.yaml");

        let startup = resolve_startup_selection_with_context(
            None,
            None,
            Some("architect"),
            None,
            None,
            config_path,
            workspace,
        )
        .await
        .expect("resolve startup selection");

        assert_eq!(startup.provider.as_deref(), Some("openrouter"));
        assert_eq!(startup.model.as_deref(), Some("anthropic/claude-sonnet-4"));
        assert_eq!(startup.primary_agent_id.as_deref(), Some("architect"));
        assert_eq!(
            startup.primary_agent_prompt.as_deref(),
            Some("You are the architect profile.")
        );
    }

    #[tokio::test]
    async fn startup_selection_explicit_agent_reports_actionable_yaml_parse_errors() {
        let temp = setup_startup_resolver_config(
            r#"
primary_agents:
  architect
    provider: openrouter
"#,
        );
        let workspace = temp.path().join("workspace");
        std::fs::create_dir_all(&workspace).expect("workspace dir");
        let config_root = temp.path().join("config-root");
        let config_path = config_root.join("ava").join("config.yaml");

        let err = resolve_startup_selection_with_context(
            None,
            None,
            Some("architect"),
            None,
            None,
            config_path.clone(),
            workspace,
        )
        .await
        .expect_err("expected parse failure");

        let message = format!("{err:#}");
        assert!(message.contains("could not be parsed for primary_agents"));
        assert!(message.contains(&config_path.display().to_string()));
    }
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
    /// Manage power plugins
    Plugin {
        #[command(subcommand)]
        action: PluginCommand,
    },
    /// Check for and install updates
    Update,
    /// Check for and install updates (alias)
    #[command(name = "self-update")]
    SelfUpdate,
    /// Start the AVA web server (HTTP API + WebSocket for agent events)
    Serve {
        /// Port to listen on
        #[arg(long, default_value_t = 8080)]
        port: u16,
        /// Host/IP to bind to
        #[arg(long, default_value = "127.0.0.1")]
        host: String,
        /// Control token for privileged HTTP routes and WebSocket access
        #[arg(long)]
        token: Option<String>,
        /// Insecure: allow any browser origin instead of loopback-only origins
        #[arg(long, default_value_t = false)]
        insecure_open_cors: bool,
    },
}

#[derive(Debug, Clone, Subcommand)]
pub enum PluginCommand {
    /// List installed plugins
    List,
    /// Install a plugin from a local path or npm package
    Add {
        /// Local path to plugin directory, or npm package name
        source: String,
    },
    /// Remove an installed plugin
    Remove {
        /// Plugin name
        name: String,
    },
    /// Show details for an installed plugin
    Info {
        /// Plugin name
        name: String,
    },
    /// Scaffold a new plugin project
    Init {
        /// Plugin name
        name: String,
        /// Language: typescript, python, or shell
        #[arg(long, default_value = "typescript")]
        lang: String,
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
    pub fn to_severity(self) -> ava_review::Severity {
        match self {
            Self::Critical => ava_review::Severity::Critical,
            Self::Warning => ava_review::Severity::Warning,
            Self::Suggestion => ava_review::Severity::Suggestion,
            Self::Any => ava_review::Severity::Nitpick,
        }
    }
}

/// Resolve provider and model from multiple sources.
/// Priority: explicit args > env vars > config file.
pub async fn resolve_provider_model(
    provider: Option<&str>,
    model: Option<&str>,
    agent: Option<&str>,
) -> color_eyre::Result<(Option<String>, Option<String>)> {
    let startup = resolve_startup_selection(provider, model, agent).await?;
    Ok((startup.provider, startup.model))
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StartupSelection {
    pub provider: Option<String>,
    pub model: Option<String>,
    pub primary_agent_id: Option<String>,
    pub primary_agent_prompt: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ConfiguredPrimaryAgent {
    pub id: String,
    pub provider: Option<String>,
    pub model: Option<String>,
    pub prompt: Option<String>,
    pub description: Option<String>,
}

#[derive(Debug, Clone, serde::Deserialize, Default)]
struct PrimaryAgentOnlyConfig {
    #[serde(default)]
    primary_agent: Option<String>,
    #[serde(default)]
    primary_agents: HashMap<String, ava_config::PrimaryAgentConfig>,
}

impl PrimaryAgentOnlyConfig {
    fn resolve_primary_agent(
        &self,
        explicit_id: Option<&str>,
    ) -> std::result::Result<Option<ava_config::ResolvedPrimaryAgent>, String> {
        let config = ava_config::Config {
            primary_agent: self.primary_agent.clone(),
            primary_agents: self.primary_agents.clone(),
            ..Default::default()
        };
        config.resolve_primary_agent(explicit_id)
    }

    fn configured_primary_agents(&self) -> Vec<ConfiguredPrimaryAgent> {
        let mut profiles = self
            .primary_agents
            .iter()
            .map(|(id, profile)| ConfiguredPrimaryAgent {
                id: id.clone(),
                provider: profile.provider.clone(),
                model: profile.model.clone(),
                prompt: profile.prompt.clone(),
                description: profile.description.clone(),
            })
            .collect::<Vec<_>>();
        profiles.sort_by(|left, right| left.id.cmp(&right.id));
        profiles
    }
}

pub async fn load_primary_agent_profiles() -> color_eyre::Result<Vec<ConfiguredPrimaryAgent>> {
    let config_path =
        ava_config::config_file_path().unwrap_or_else(|_| PathBuf::from("config.yaml"));
    if !config_path.exists() {
        return Ok(Vec::new());
    }

    let content = tokio::fs::read_to_string(&config_path).await?;
    let partial = serde_yaml::from_str::<PrimaryAgentOnlyConfig>(&content).map_err(|err| {
        color_eyre::eyre::eyre!(
            "{} could not be parsed for primary_agents: {err}",
            config_path.display()
        )
    })?;
    Ok(partial.configured_primary_agents())
}

/// Resolve startup provider/model + primary-agent metadata from multiple
/// sources while keeping existing provider/model precedence explicit.
///
/// Precedence:
/// 1. Explicit CLI provider/model flags (`--provider`, `--model`)
/// 2. Explicit CLI primary agent (`--agent`) provider/model
/// 3. Environment variables (`AVA_PROVIDER`, `AVA_MODEL`)
/// 4. Per-project state (`.ava/state.json`)
/// 5. Config default primary agent (`primary_agent` + `primary_agents.<id>`)
/// 6. Config `llm.provider` / `llm.model`
pub async fn resolve_startup_selection(
    provider: Option<&str>,
    model: Option<&str>,
    agent: Option<&str>,
) -> color_eyre::Result<StartupSelection> {
    let config_path =
        ava_config::config_file_path().unwrap_or_else(|_| PathBuf::from("config.yaml"));
    let env_provider = std::env::var("AVA_PROVIDER").ok();
    let env_model = std::env::var("AVA_MODEL").ok();
    let project_root = std::env::current_dir().unwrap_or_default();

    resolve_startup_selection_with_context(
        provider,
        model,
        agent,
        env_provider,
        env_model,
        config_path,
        project_root,
    )
    .await
}

async fn resolve_startup_selection_with_context(
    provider: Option<&str>,
    model: Option<&str>,
    agent: Option<&str>,
    env_provider: Option<String>,
    env_model: Option<String>,
    config_path: PathBuf,
    project_root: PathBuf,
) -> color_eyre::Result<StartupSelection> {
    let explicit_provider = provider.map(String::from);
    let explicit_model = model.map(String::from);

    let config_content = if config_path.exists() {
        Some(tokio::fs::read_to_string(&config_path).await?)
    } else {
        None
    };

    let loaded_config = if let Some(content) = config_content.as_deref() {
        match serde_yaml::from_str::<ava_config::Config>(content) {
            Ok(config) => {
                let raw = serde_yaml::from_str::<serde_json::Value>(content).ok();
                Some((config, raw))
            }
            Err(_) => None,
        }
    } else {
        None
    };

    let explicit_primary_agent = if let Some(agent_id) = agent {
        let content = config_content.as_deref().ok_or_else(|| {
            color_eyre::eyre::eyre!(
                "--agent '{agent_id}' was provided but config file was not found at {}",
                config_path.display()
            )
        })?;

        // Prefer full config deserialization when available.
        if let Some((config, _)) = loaded_config.as_ref() {
            config
                .resolve_primary_agent(Some(agent_id))
                .map_err(color_eyre::eyre::Report::msg)?
        } else {
            // Fall back to parsing only the primary-agent fields so unrelated
            // top-level config issues do not break explicit `--agent` startup.
            let partial = serde_yaml::from_str::<PrimaryAgentOnlyConfig>(content).map_err(|err| {
                color_eyre::eyre::eyre!(
                    "--agent '{agent_id}' was provided, but {} could not be parsed for primary_agents: {err}",
                    config_path.display()
                )
            })?;

            partial
                .resolve_primary_agent(Some(agent_id))
                .map_err(color_eyre::eyre::Report::msg)?
        }
    } else {
        None
    };

    // Explicit args take precedence
    if explicit_provider.is_some() || explicit_model.is_some() {
        return Ok(StartupSelection {
            provider: explicit_provider,
            model: explicit_model,
            primary_agent_id: explicit_primary_agent
                .as_ref()
                .map(|entry| entry.id.clone()),
            primary_agent_prompt: explicit_primary_agent.and_then(|entry| entry.prompt),
        });
    }

    // Explicit --agent next
    if let Some(primary) = explicit_primary_agent {
        return Ok(StartupSelection {
            provider: primary.provider,
            model: primary.model,
            primary_agent_id: Some(primary.id),
            primary_agent_prompt: primary.prompt,
        });
    }

    // Env vars next
    if env_provider.is_some() || env_model.is_some() {
        debug!(
            ?env_provider,
            ?env_model,
            "Using provider/model from env vars"
        );
        return Ok(StartupSelection {
            provider: env_provider,
            model: env_model,
            primary_agent_id: None,
            primary_agent_prompt: None,
        });
    }

    // Check per-project state (`.ava/state.json`) for last used model
    let project_state = ava_config::ProjectState::load(&project_root);
    if project_state.last_provider.is_some() {
        debug!(
            provider = ?project_state.last_provider,
            model = ?project_state.last_model,
            "Using last used provider/model from project state"
        );
        return Ok(StartupSelection {
            provider: project_state.last_provider,
            model: project_state.last_model,
            primary_agent_id: None,
            primary_agent_prompt: None,
        });
    }

    if let Some((config, raw)) = loaded_config {
        // Config default primary agent before llm fallback.
        if let Some(primary) = config
            .resolve_primary_agent(None)
            .map_err(color_eyre::eyre::Report::msg)?
        {
            return Ok(StartupSelection {
                provider: primary.provider,
                model: primary.model,
                primary_agent_id: Some(primary.id),
                primary_agent_prompt: primary.prompt,
            });
        }

        // Fall back to explicit llm.provider/llm.model config.
        let provider = if config.llm.provider != "openai" {
            debug!(provider = %config.llm.provider, "Loaded provider from config file");
            Some(config.llm.provider)
        } else if raw
            .as_ref()
            .and_then(|value| value.get("llm").and_then(|l| l.get("provider")))
            .is_some()
        {
            debug!("Loaded provider 'openai' from config file");
            Some(config.llm.provider)
        } else {
            None
        };
        let model = if config.llm.model != "gpt-5.3-codex" {
            debug!(model = %config.llm.model, "Loaded model from config file");
            Some(config.llm.model)
        } else if raw
            .as_ref()
            .and_then(|value| value.get("llm").and_then(|l| l.get("model")))
            .is_some()
        {
            debug!("Loaded model 'gpt-5.3-codex' from config file");
            Some(config.llm.model)
        } else {
            None
        };
        if provider.is_some() {
            return Ok(StartupSelection {
                provider,
                model,
                primary_agent_id: None,
                primary_agent_prompt: None,
            });
        }
    }

    Ok(StartupSelection {
        provider: None,
        model: None,
        primary_agent_id: None,
        primary_agent_prompt: None,
    })
}

impl CliArgs {
    /// Resolve provider and model from CLI flags, env vars, or config file.
    pub async fn resolve_provider_model(
        &self,
    ) -> color_eyre::Result<(Option<String>, Option<String>)> {
        resolve_provider_model(
            self.provider.as_deref(),
            self.model.as_deref(),
            self.agent.as_deref(),
        )
        .await
    }

    pub async fn resolve_startup_selection(&self) -> color_eyre::Result<StartupSelection> {
        resolve_startup_selection(
            self.provider.as_deref(),
            self.model.as_deref(),
            self.agent.as_deref(),
        )
        .await
    }
}

/// Error message shown when no provider is configured anywhere.
pub const NO_PROVIDER_ERROR: &str = "\
No provider configured. Set defaults in $XDG_CONFIG_HOME/ava/config.yaml or use --provider/--model flags.

Example $XDG_CONFIG_HOME/ava/config.yaml:
  llm:
    provider: openrouter
    model: anthropic/claude-sonnet-4

Or run with flags:
  ava --provider openrouter --model anthropic/claude-sonnet-4 \"your goal\"";
