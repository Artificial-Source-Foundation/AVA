//! AgentStack configuration and result types.

use std::path::PathBuf;
use std::sync::Arc;

use ava_llm::provider::LLMProvider;
use ava_types::Session;

pub struct AgentStackConfig {
    pub data_dir: PathBuf,
    pub provider: Option<String>,
    pub model: Option<String>,
    pub max_turns: usize,
    pub max_budget_usd: f64,
    pub yolo: bool,
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
            data_dir: dirs::home_dir().unwrap_or_default().join(".ava"),
            provider: None,
            model: None,
            max_turns: 0,
            max_budget_usd: 0.0,
            yolo: false,
            injected_provider: None,
            working_dir: None,
            compaction_threshold_pct: 80,
            auto_compact: true,
        }
    }
}
