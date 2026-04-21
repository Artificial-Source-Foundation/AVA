//! AgentStack configuration and result types.

use std::path::PathBuf;
use std::sync::Arc;

use ava_llm::provider::LLMProvider;
use ava_types::Session;

use crate::system_prompt::BenchmarkPromptOverride;

pub struct AgentStackConfig {
    pub data_dir: PathBuf,
    pub config_dir: Option<PathBuf>,
    pub provider: Option<String>,
    pub model: Option<String>,
    pub max_turns: usize,
    pub max_budget_usd: f64,
    pub yolo: bool,
    /// When true, keep non-interactive runs risk-aware by routing dangerous
    /// approval-worthy actions through the approval bridge instead of using the
    /// interactive yolo short-circuit. Safe work still proceeds automatically.
    pub non_interactive_approvals: bool,
    pub injected_provider: Option<Arc<dyn LLMProvider>>,
    /// Override the working directory for the agent. When set, the agent uses
    /// this path instead of `std::env::current_dir()` for project-root detection,
    /// MCP config lookup, custom tool discovery, and codebase indexing. Useful for
    /// benchmarks and sandboxed runs that should not touch the real project.
    pub working_dir: Option<PathBuf>,
    /// Compaction threshold as a percentage (50–95, default 80). Converted to a
    /// fraction (0.50–0.95) when passed to `CondenserConfig`.
    pub compaction_threshold_pct: u8,
    /// When false, automatic context compaction is disabled entirely.
    pub auto_compact: bool,
    /// When false, skip loading project instruction files into the system prompt.
    pub include_project_instructions: bool,
    /// When false, skip eager background codebase indexing at startup.
    pub eager_codebase_indexing: bool,
    /// When false, skip probing the system for installed external CLI agents.
    pub discover_cli_agents: bool,
    /// Benchmark-only prompt override for family forcing or one-off prompt note files.
    pub benchmark_prompt_override: Option<BenchmarkPromptOverride>,
}

impl AgentStackConfig {
    pub fn for_tui(
        data_dir: PathBuf,
        provider: Option<String>,
        model: Option<String>,
        max_turns: usize,
        max_budget_usd: f64,
        yolo: bool,
        include_project_instructions: bool,
        eager_codebase_indexing: bool,
    ) -> Self {
        Self {
            data_dir,
            config_dir: None,
            provider,
            model,
            max_turns,
            max_budget_usd,
            yolo,
            non_interactive_approvals: false,
            include_project_instructions,
            eager_codebase_indexing,
            discover_cli_agents: false,
            ..Self::default()
        }
    }

    pub fn for_headless(
        data_dir: PathBuf,
        provider: Option<String>,
        model: Option<String>,
        max_turns: usize,
        max_budget_usd: f64,
        yolo: bool,
        include_project_instructions: bool,
        eager_codebase_indexing: bool,
    ) -> Self {
        Self {
            data_dir,
            config_dir: None,
            provider,
            model,
            max_turns,
            max_budget_usd,
            yolo,
            non_interactive_approvals: true,
            include_project_instructions,
            eager_codebase_indexing,
            discover_cli_agents: false,
            ..Self::default()
        }
    }

    pub fn for_review(
        data_dir: PathBuf,
        provider: Option<String>,
        model: Option<String>,
        max_turns: usize,
    ) -> Self {
        Self {
            data_dir,
            config_dir: None,
            provider,
            model,
            max_turns,
            yolo: true,
            non_interactive_approvals: false,
            include_project_instructions: false,
            eager_codebase_indexing: false,
            discover_cli_agents: false,
            ..Self::default()
        }
    }

    pub fn for_benchmark(
        data_dir: PathBuf,
        provider: String,
        model: String,
        max_turns: usize,
        working_dir: PathBuf,
        benchmark_prompt_override: Option<BenchmarkPromptOverride>,
    ) -> Self {
        Self {
            data_dir,
            config_dir: None,
            provider: Some(provider),
            model: Some(model),
            max_turns,
            yolo: true,
            non_interactive_approvals: true,
            working_dir: Some(working_dir),
            include_project_instructions: false,
            eager_codebase_indexing: false,
            discover_cli_agents: false,
            benchmark_prompt_override,
            ..Self::default()
        }
    }

    pub fn for_web(data_dir: PathBuf) -> Self {
        Self {
            data_dir,
            config_dir: None,
            discover_cli_agents: false,
            ..Self::default()
        }
    }

    pub fn for_desktop(data_dir: PathBuf) -> Self {
        Self {
            data_dir,
            config_dir: None,
            discover_cli_agents: false,
            ..Self::default()
        }
    }

    pub fn for_background_isolation(
        data_dir: PathBuf,
        provider: Option<String>,
        model: Option<String>,
        max_turns: usize,
        max_budget_usd: f64,
        working_dir: PathBuf,
    ) -> Self {
        Self {
            data_dir,
            config_dir: None,
            provider,
            model,
            max_turns,
            max_budget_usd,
            yolo: false,
            non_interactive_approvals: false,
            working_dir: Some(working_dir),
            include_project_instructions: true,
            eager_codebase_indexing: true,
            discover_cli_agents: true,
            ..Self::default()
        }
    }
}

#[derive(Debug)]
pub struct AgentRunResult {
    pub success: bool,
    pub turns: usize,
    pub session: Session,
}

impl Default for AgentStackConfig {
    fn default() -> Self {
        Self {
            data_dir: ava_config::data_dir().unwrap_or_else(|_| PathBuf::from(".")),
            config_dir: None,
            provider: None,
            model: None,
            max_turns: 0,
            max_budget_usd: 0.0,
            yolo: false,
            non_interactive_approvals: false,
            injected_provider: None,
            working_dir: None,
            compaction_threshold_pct: 80,
            auto_compact: true,
            include_project_instructions: true,
            eager_codebase_indexing: true,
            discover_cli_agents: true,
            benchmark_prompt_override: None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::AgentStackConfig;
    use std::path::PathBuf;

    #[test]
    fn tui_preset_disables_expensive_startup_work() {
        let cfg = AgentStackConfig::for_tui(
            PathBuf::from("/tmp/ava"),
            None,
            None,
            10,
            5.0,
            false,
            true,
            false,
        );
        assert!(!cfg.eager_codebase_indexing);
        assert!(!cfg.discover_cli_agents);
        assert!(cfg.include_project_instructions);
    }

    #[test]
    fn headless_preset_disables_cli_agent_discovery() {
        let cfg = AgentStackConfig::for_headless(
            PathBuf::from("/tmp/ava"),
            None,
            None,
            10,
            5.0,
            true,
            false,
            false,
        );
        assert!(!cfg.discover_cli_agents);
        assert!(!cfg.include_project_instructions);
        assert!(!cfg.eager_codebase_indexing);
        assert!(cfg.non_interactive_approvals);
    }

    #[test]
    fn review_preset_is_lean_and_yolo() {
        let cfg = AgentStackConfig::for_review(
            PathBuf::from("/tmp/ava"),
            Some("openai".to_string()),
            Some("gpt-5".to_string()),
            5,
        );
        assert!(cfg.yolo);
        assert!(!cfg.include_project_instructions);
        assert!(!cfg.eager_codebase_indexing);
        assert!(!cfg.discover_cli_agents);
    }

    #[test]
    fn benchmark_preset_is_lean_and_scoped() {
        let cfg = AgentStackConfig::for_benchmark(
            PathBuf::from("/tmp/ava"),
            "openai".to_string(),
            "gpt-5".to_string(),
            3,
            PathBuf::from("/tmp/worktree"),
            None,
        );
        assert!(cfg.yolo);
        assert!(cfg.non_interactive_approvals);
        assert_eq!(cfg.working_dir, Some(PathBuf::from("/tmp/worktree")));
        assert!(!cfg.include_project_instructions);
        assert!(!cfg.eager_codebase_indexing);
        assert!(!cfg.discover_cli_agents);
    }
}
