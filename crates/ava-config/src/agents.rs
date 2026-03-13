//! Sub-agent configuration system.
//!
//! Loads agent settings from TOML files:
//! - `~/.ava/agents.toml` — global defaults (all projects)
//! - `.ava/agents.toml` — project-level overrides
//!
//! Project-level settings override global settings.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::Path;

/// Top-level agents configuration parsed from `agents.toml`.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct AgentsConfig {
    /// Default settings applied to all sub-agents.
    #[serde(default)]
    pub defaults: AgentDefaults,
    /// Per-agent overrides keyed by agent name (e.g. "review", "task").
    #[serde(default)]
    pub agents: HashMap<String, AgentOverride>,
}

/// Default settings that apply to all sub-agents unless overridden.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AgentDefaults {
    /// Default model for sub-agents (e.g. "anthropic/claude-haiku-4.5").
    pub model: Option<String>,
    /// Default maximum turns for sub-agent execution.
    pub max_turns: Option<usize>,
    /// Whether sub-agents are enabled by default.
    #[serde(default = "default_true")]
    pub enabled: bool,
}

/// Per-agent override. `None` fields inherit from defaults.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct AgentOverride {
    /// Override enabled state. `None` inherits from defaults.
    pub enabled: Option<bool>,
    /// Override model. `None` inherits from defaults.
    pub model: Option<String>,
    /// Override max turns. `None` inherits from defaults.
    pub max_turns: Option<usize>,
    /// Custom system prompt for this agent type.
    pub prompt: Option<String>,
    /// Provider for this agent. Default uses AVA's native agent loop.
    /// Set to "claude-code" to use Claude Code as the runtime.
    #[serde(default)]
    pub provider: Option<String>,
    /// Allowed tools when using claude-code provider (CC's tools, not AVA's).
    #[serde(default)]
    pub allowed_tools: Option<Vec<String>>,
    /// Max budget in USD for claude-code provider.
    #[serde(default)]
    pub max_budget_usd: Option<f64>,
}

/// Fully resolved configuration for a specific agent, after merging
/// defaults with any agent-specific overrides.
#[derive(Debug, Clone, PartialEq)]
pub struct ResolvedAgent {
    pub enabled: bool,
    pub model: Option<String>,
    pub max_turns: Option<usize>,
    pub prompt: Option<String>,
    /// Provider for this agent (e.g. "claude-code"). `None` means native AVA agent loop.
    pub provider: Option<String>,
    /// Allowed tools when using an external provider like claude-code.
    pub allowed_tools: Option<Vec<String>>,
    /// Max budget in USD for external provider agents.
    pub max_budget_usd: Option<f64>,
}

fn default_true() -> bool {
    true
}

impl Default for AgentDefaults {
    fn default() -> Self {
        Self {
            model: None,
            max_turns: None,
            enabled: true,
        }
    }
}

impl AgentsConfig {
    /// Load agents configuration by merging global and project-level TOML files.
    ///
    /// - `global_path`: e.g. `~/.ava/agents.toml`
    /// - `project_path`: e.g. `.ava/agents.toml` (relative to project root)
    ///
    /// If neither file exists, returns a default config.
    /// Project-level settings override global settings.
    pub fn load(global_path: &Path, project_path: &Path) -> Self {
        let global = Self::load_file(global_path);
        let project = Self::load_file(project_path);

        match (global, project) {
            (None, None) => Self::default(),
            (Some(g), None) => g,
            (None, Some(p)) => p,
            (Some(g), Some(p)) => Self::merge(g, p),
        }
    }

    /// Load and parse a single TOML file, returning `None` if the file
    /// doesn't exist or fails to parse.
    fn load_file(path: &Path) -> Option<Self> {
        let content = std::fs::read_to_string(path).ok()?;
        match toml::from_str(&content) {
            Ok(config) => Some(config),
            Err(e) => {
                tracing::warn!("Failed to parse {}: {}", path.display(), e);
                None
            }
        }
    }

    /// Merge project config on top of global config.
    /// Project values take precedence; global values fill gaps.
    fn merge(global: Self, project: Self) -> Self {
        let defaults = AgentDefaults {
            model: project.defaults.model.or(global.defaults.model),
            max_turns: project.defaults.max_turns.or(global.defaults.max_turns),
            // Project explicitly sets enabled; only use global if project didn't specify.
            // Since `enabled` always deserializes (has a default), project wins.
            enabled: project.defaults.enabled,
        };

        // Start with global agents, then overlay project agents.
        let mut agents = global.agents;
        for (name, project_override) in project.agents {
            let entry = agents.entry(name).or_default();
            if project_override.enabled.is_some() {
                entry.enabled = project_override.enabled;
            }
            if project_override.model.is_some() {
                entry.model = project_override.model;
            }
            if project_override.max_turns.is_some() {
                entry.max_turns = project_override.max_turns;
            }
            if project_override.prompt.is_some() {
                entry.prompt = project_override.prompt;
            }
            if project_override.provider.is_some() {
                entry.provider = project_override.provider;
            }
            if project_override.allowed_tools.is_some() {
                entry.allowed_tools = project_override.allowed_tools;
            }
            if project_override.max_budget_usd.is_some() {
                entry.max_budget_usd = project_override.max_budget_usd;
            }
        }

        Self { defaults, agents }
    }

    /// Resolve the effective configuration for a named agent by merging
    /// the defaults with any agent-specific overrides.
    pub fn get_agent(&self, name: &str) -> ResolvedAgent {
        match self.agents.get(name) {
            Some(over) => ResolvedAgent {
                enabled: over.enabled.unwrap_or(self.defaults.enabled),
                model: over.model.clone().or_else(|| self.defaults.model.clone()),
                max_turns: over.max_turns.or(self.defaults.max_turns),
                prompt: over.prompt.clone(),
                provider: over.provider.clone(),
                allowed_tools: over.allowed_tools.clone(),
                max_budget_usd: over.max_budget_usd,
            },
            None => ResolvedAgent {
                enabled: self.defaults.enabled,
                model: self.defaults.model.clone(),
                max_turns: self.defaults.max_turns,
                prompt: None,
                provider: None,
                allowed_tools: None,
                max_budget_usd: None,
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    /// Helper: write a TOML string to a temp file and return its path.
    fn write_toml(dir: &Path, filename: &str, content: &str) -> std::path::PathBuf {
        let path = dir.join(filename);
        std::fs::write(&path, content).unwrap();
        path
    }

    #[test]
    fn test_empty_config() {
        let tmp = TempDir::new().unwrap();
        let global = tmp.path().join("global.toml");
        let project = tmp.path().join("project.toml");

        let config = AgentsConfig::load(&global, &project);

        assert!(config.defaults.enabled);
        assert!(config.defaults.model.is_none());
        assert!(config.defaults.max_turns.is_none());
        assert!(config.agents.is_empty());
    }

    #[test]
    fn test_load_global_only() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[defaults]
model = "anthropic/claude-haiku-4.5"
max_turns = 10
"#,
        );
        let project = tmp.path().join("project.toml");

        let config = AgentsConfig::load(&global, &project);

        assert_eq!(
            config.defaults.model.as_deref(),
            Some("anthropic/claude-haiku-4.5")
        );
        assert_eq!(config.defaults.max_turns, Some(10));
        assert!(config.defaults.enabled);
    }

    #[test]
    fn test_project_overrides_global() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[defaults]
model = "anthropic/claude-haiku-4.5"
max_turns = 10
"#,
        );
        let project = write_toml(
            tmp.path(),
            "project.toml",
            r#"
[defaults]
model = "anthropic/claude-sonnet-4"
"#,
        );

        let config = AgentsConfig::load(&global, &project);

        // Project model overrides global.
        assert_eq!(
            config.defaults.model.as_deref(),
            Some("anthropic/claude-sonnet-4")
        );
        // Global max_turns is preserved since project didn't set it.
        assert_eq!(config.defaults.max_turns, Some(10));
    }

    #[test]
    fn test_get_agent_merges_defaults() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[defaults]
model = "anthropic/claude-haiku-4.5"
max_turns = 10

[agents.review]
max_turns = 15
"#,
        );
        let project = tmp.path().join("project.toml");

        let config = AgentsConfig::load(&global, &project);
        let review = config.get_agent("review");

        // max_turns from agent override
        assert_eq!(review.max_turns, Some(15));
        // model inherited from defaults
        assert_eq!(review.model.as_deref(), Some("anthropic/claude-haiku-4.5"));
        // enabled inherited from defaults
        assert!(review.enabled);
        // no custom prompt
        assert!(review.prompt.is_none());
    }

    #[test]
    fn test_get_agent_override() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[defaults]
model = "anthropic/claude-haiku-4.5"
max_turns = 10

[agents.review]
model = "anthropic/claude-sonnet-4"
max_turns = 20
prompt = "You are a reviewer."
"#,
        );
        let project = tmp.path().join("project.toml");

        let config = AgentsConfig::load(&global, &project);
        let review = config.get_agent("review");

        assert_eq!(review.model.as_deref(), Some("anthropic/claude-sonnet-4"));
        assert_eq!(review.max_turns, Some(20));
        assert_eq!(review.prompt.as_deref(), Some("You are a reviewer."));
        assert!(review.enabled);
    }

    #[test]
    fn test_disabled_agent() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[agents.review]
enabled = false
model = "anthropic/claude-sonnet-4"
"#,
        );
        let project = tmp.path().join("project.toml");

        let config = AgentsConfig::load(&global, &project);
        let review = config.get_agent("review");

        assert!(!review.enabled);
        assert_eq!(review.model.as_deref(), Some("anthropic/claude-sonnet-4"));
    }

    #[test]
    fn test_toml_parsing() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[defaults]
model = "anthropic/claude-haiku-4.5"
max_turns = 10
enabled = true

[agents.review]
enabled = true
model = "anthropic/claude-sonnet-4"
max_turns = 15
prompt = """
You are a code review specialist. Focus on:
- Security vulnerabilities
- Performance issues
- Code style violations
"""

[agents.task]
enabled = true
max_turns = 10
"#,
        );
        let project = tmp.path().join("project.toml");

        let config = AgentsConfig::load(&global, &project);

        // Defaults
        assert_eq!(
            config.defaults.model.as_deref(),
            Some("anthropic/claude-haiku-4.5")
        );
        assert_eq!(config.defaults.max_turns, Some(10));
        assert!(config.defaults.enabled);

        // Review agent
        let review = config.get_agent("review");
        assert!(review.enabled);
        assert_eq!(review.model.as_deref(), Some("anthropic/claude-sonnet-4"));
        assert_eq!(review.max_turns, Some(15));
        assert!(review.prompt.is_some());
        assert!(review.prompt.as_deref().unwrap().contains("Security"));

        // Task agent
        let task = config.get_agent("task");
        assert!(task.enabled);
        assert_eq!(task.max_turns, Some(10));
        // Model inherited from defaults
        assert_eq!(task.model.as_deref(), Some("anthropic/claude-haiku-4.5"));
        assert!(task.prompt.is_none());

        // Unknown agent inherits defaults
        let unknown = config.get_agent("unknown");
        assert!(unknown.enabled);
        assert_eq!(unknown.model.as_deref(), Some("anthropic/claude-haiku-4.5"));
        assert_eq!(unknown.max_turns, Some(10));
        assert!(unknown.prompt.is_none());
    }

    #[test]
    fn test_claude_code_provider() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[defaults]
model = "anthropic/claude-haiku-4.5"
max_turns = 10

[agents.code-reviewer]
provider = "claude-code"
prompt = "You are a security code reviewer."
allowed_tools = ["Read", "Grep", "Glob"]
max_turns = 15
max_budget_usd = 0.50
"#,
        );
        let project = tmp.path().join("project.toml");

        let config = AgentsConfig::load(&global, &project);
        let reviewer = config.get_agent("code-reviewer");

        assert!(reviewer.enabled);
        assert_eq!(reviewer.provider.as_deref(), Some("claude-code"));
        assert_eq!(
            reviewer.allowed_tools.as_deref(),
            Some(&["Read".to_string(), "Grep".to_string(), "Glob".to_string()][..])
        );
        assert_eq!(reviewer.max_turns, Some(15));
        assert_eq!(reviewer.max_budget_usd, Some(0.50));
        assert!(reviewer.prompt.as_deref().unwrap().contains("security"));
    }

    #[test]
    fn test_claude_code_provider_project_override() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[agents.refactorer]
provider = "claude-code"
allowed_tools = ["Read", "Edit", "Bash"]
max_budget_usd = 2.00
"#,
        );
        let project = write_toml(
            tmp.path(),
            "project.toml",
            r#"
[agents.refactorer]
max_budget_usd = 5.00
allowed_tools = ["Read", "Edit", "Bash", "Glob", "Grep"]
"#,
        );

        let config = AgentsConfig::load(&global, &project);
        let refactorer = config.get_agent("refactorer");

        // Provider preserved from global
        assert_eq!(refactorer.provider.as_deref(), Some("claude-code"));
        // Budget overridden by project
        assert_eq!(refactorer.max_budget_usd, Some(5.00));
        // Allowed tools overridden by project
        assert_eq!(refactorer.allowed_tools.as_ref().unwrap().len(), 5);
    }

    #[test]
    fn test_native_agent_has_no_provider() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[agents.task]
max_turns = 10
"#,
        );
        let project = tmp.path().join("project.toml");

        let config = AgentsConfig::load(&global, &project);
        let task = config.get_agent("task");

        assert!(task.provider.is_none());
        assert!(task.allowed_tools.is_none());
        assert!(task.max_budget_usd.is_none());
    }

    #[test]
    fn test_project_overrides_agent() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[agents.review]
model = "anthropic/claude-haiku-4.5"
max_turns = 10
prompt = "Global review prompt."
"#,
        );
        let project = write_toml(
            tmp.path(),
            "project.toml",
            r#"
[agents.review]
model = "anthropic/claude-sonnet-4"
"#,
        );

        let config = AgentsConfig::load(&global, &project);
        let review = config.get_agent("review");

        // Project overrides model
        assert_eq!(review.model.as_deref(), Some("anthropic/claude-sonnet-4"));
        // Global max_turns and prompt preserved
        assert_eq!(review.max_turns, Some(10));
        assert_eq!(review.prompt.as_deref(), Some("Global review prompt."));
    }
}
