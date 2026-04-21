//! Sub-agent configuration system.
//!
//! Loads agent settings from TOML files:
//! - `$XDG_CONFIG_HOME/ava/subagents.toml` — global defaults (all projects)
//! - `.ava/subagents.toml` — project-level overrides
//! - Legacy compatibility input: `agents.toml` in the same locations
//!
//! Project-level settings override global settings.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::Path;

pub const SUBAGENTS_CONFIG_FILE: &str = "subagents.toml";
pub const LEGACY_AGENTS_CONFIG_FILE: &str = "agents.toml";

/// Top-level subagent configuration parsed from `subagents.toml`
/// (with legacy `agents.toml` compatibility).
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct AgentsConfig {
    /// Default settings applied to all sub-agents.
    #[serde(default)]
    pub defaults: AgentDefaults,
    /// Per-agent overrides keyed by agent name (e.g. "review", "task").
    ///
    /// Accepts both `[agents.<id>]` and `[subagents.<id>]` TOML tables.
    #[serde(default, alias = "subagents")]
    pub agents: HashMap<String, AgentOverride>,
}

/// Default settings that apply to all sub-agents unless overridden.
#[derive(Debug, Clone, Serialize)]
pub struct AgentDefaults {
    /// Default model for sub-agents (e.g. "anthropic/claude-haiku-4.5").
    pub model: Option<String>,
    /// Default maximum turns for sub-agent execution.
    pub max_turns: Option<usize>,
    /// Whether sub-agents are enabled by default.
    #[serde(default = "default_true")]
    pub enabled: bool,
    /// Whether `enabled` was explicitly present in the parsed TOML.
    ///
    /// This is used only during layered merge so omitted project defaults do
    /// not accidentally override global defaults.
    #[serde(skip)]
    pub enabled_explicit: bool,
}

#[derive(Debug, Deserialize)]
struct AgentDefaultsRaw {
    model: Option<String>,
    max_turns: Option<usize>,
    enabled: Option<bool>,
}

impl<'de> Deserialize<'de> for AgentDefaults {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        let raw = AgentDefaultsRaw::deserialize(deserializer)?;
        Ok(Self {
            model: raw.model,
            max_turns: raw.max_turns,
            enabled: raw.enabled.unwrap_or(true),
            enabled_explicit: raw.enabled.is_some(),
        })
    }
}

/// Per-agent override. `None` fields inherit from defaults.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct AgentOverride {
    /// Human-readable description for this subagent profile.
    #[serde(default)]
    pub description: Option<String>,
    /// Override enabled state. `None` inherits from defaults.
    pub enabled: Option<bool>,
    /// Override model. `None` inherits from defaults.
    pub model: Option<String>,
    /// Override max turns. `None` inherits from defaults.
    pub max_turns: Option<usize>,
    /// Custom system prompt for this agent type.
    pub prompt: Option<String>,
    /// Temperature override for this agent (0.0–1.0).
    #[serde(default)]
    pub temperature: Option<f32>,
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
    pub description: Option<String>,
    pub enabled: bool,
    pub model: Option<String>,
    pub max_turns: Option<usize>,
    pub prompt: Option<String>,
    /// Temperature for this agent (0.0–1.0). `None` uses the provider default.
    pub temperature: Option<f32>,
    /// Provider for this agent (e.g. "claude-code"). `None` means native AVA agent loop.
    pub provider: Option<String>,
    /// Allowed tools when using an external provider like claude-code.
    pub allowed_tools: Option<Vec<String>>,
    /// Max budget in USD for external provider agents.
    pub max_budget_usd: Option<f64>,
}

impl Default for AgentDefaults {
    fn default() -> Self {
        Self {
            model: None,
            max_turns: None,
            enabled: true,
            enabled_explicit: false,
        }
    }
}

impl AgentsConfig {
    /// Load subagent configuration using new `subagents.toml` paths with
    /// explicit legacy `agents.toml` compatibility fallback.
    ///
    /// For each scope (global/project), `subagents.toml` is preferred when both
    /// files are present. Legacy `agents.toml` remains read-only compatibility
    /// input and should not be used as a write target.
    pub fn load_with_compat(
        global_subagents_path: &Path,
        global_legacy_agents_path: &Path,
        project_subagents_path: &Path,
        project_legacy_agents_path: &Path,
    ) -> Self {
        let global =
            Self::load_preferred_scope(global_subagents_path, global_legacy_agents_path, "global");
        let project = Self::load_preferred_scope(
            project_subagents_path,
            project_legacy_agents_path,
            "project",
        );

        match (global, project) {
            (None, None) => Self::default(),
            (Some(g), None) => g,
            (None, Some(p)) => p,
            (Some(g), Some(p)) => Self::merge(g, p),
        }
    }

    /// Load agents configuration by merging global and project-level TOML files.
    ///
    /// - `global_path`: e.g. `$XDG_CONFIG_HOME/ava/subagents.toml`
    /// - `project_path`: e.g. `.ava/subagents.toml` (relative to project root)
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

    fn load_preferred_scope(
        subagents_path: &Path,
        legacy_agents_path: &Path,
        scope_label: &str,
    ) -> Option<Self> {
        let subagents_exists = subagents_path.exists();
        let legacy_exists = legacy_agents_path.exists();

        if subagents_exists {
            if legacy_exists {
                tracing::info!(
                    scope = scope_label,
                    preferred = %subagents_path.display(),
                    ignored_legacy = %legacy_agents_path.display(),
                    "Both subagent config files exist in scope; preferring subagents.toml"
                );
            }
            return Self::load_file(subagents_path);
        }

        if legacy_exists {
            tracing::warn!(
                scope = scope_label,
                legacy = %legacy_agents_path.display(),
                "Loading legacy agents.toml compatibility input; migrate to subagents.toml"
            );
            return Self::load_file(legacy_agents_path);
        }

        None
    }

    /// Merge project config on top of global config.
    /// Project values take precedence; global values fill gaps.
    fn merge(global: Self, project: Self) -> Self {
        let defaults = AgentDefaults {
            model: project.defaults.model.or(global.defaults.model),
            max_turns: project.defaults.max_turns.or(global.defaults.max_turns),
            // Presence-aware merge: only apply project `enabled` when explicitly set.
            enabled: if project.defaults.enabled_explicit {
                project.defaults.enabled
            } else {
                global.defaults.enabled
            },
            enabled_explicit: project.defaults.enabled_explicit || global.defaults.enabled_explicit,
        };

        // Start with global agents, then overlay project agents.
        let mut agents = global.agents;
        for (name, project_override) in project.agents {
            let entry = agents.entry(name).or_default();
            if project_override.description.is_some() {
                entry.description = project_override.description;
            }
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
            if project_override.temperature.is_some() {
                entry.temperature = project_override.temperature;
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
    ///
    /// Resolution order: explicit agent override > defaults section > predefined template.
    /// If no override exists and a predefined template matches `name`, the template
    /// fields are used as a fallback (but `defaults` section values still take priority
    /// for `model` and `max_turns`).
    pub fn get_agent(&self, name: &str) -> ResolvedAgent {
        match self.agents.get(name) {
            Some(over) => ResolvedAgent {
                description: over.description.clone(),
                enabled: over.enabled.unwrap_or(self.defaults.enabled),
                model: over.model.clone().or_else(|| self.defaults.model.clone()),
                max_turns: over.max_turns.or(self.defaults.max_turns),
                prompt: over.prompt.clone(),
                temperature: over.temperature,
                provider: over.provider.clone(),
                allowed_tools: over.allowed_tools.clone(),
                max_budget_usd: over.max_budget_usd,
            },
            None => {
                // Fall back to predefined template if one exists for this name.
                let template = default_agents().remove(name);
                match template {
                    Some(tmpl) => ResolvedAgent {
                        description: tmpl.description,
                        enabled: self.defaults.enabled,
                        model: self.defaults.model.clone().or(tmpl.model),
                        max_turns: self.defaults.max_turns.or(tmpl.max_turns),
                        prompt: tmpl.prompt,
                        temperature: tmpl.temperature,
                        provider: tmpl.provider,
                        allowed_tools: tmpl.allowed_tools,
                        max_budget_usd: tmpl.max_budget_usd,
                    },
                    None => ResolvedAgent {
                        description: None,
                        enabled: self.defaults.enabled,
                        model: self.defaults.model.clone(),
                        max_turns: self.defaults.max_turns,
                        prompt: None,
                        temperature: None,
                        provider: None,
                        allowed_tools: None,
                        max_budget_usd: None,
                    },
                }
            }
        }
    }

    /// List all available agent names: explicitly configured agents merged
    /// with predefined template names.
    pub fn available_agents(&self) -> Vec<String> {
        let mut names: Vec<String> = self.agents.keys().cloned().collect();
        for key in default_agents().keys() {
            if !names.contains(key) {
                names.push(key.clone());
            }
        }
        names.sort();
        names
    }
}

/// Predefined agent templates that serve as fallbacks when no explicit
/// configuration exists in `subagents.toml` (or legacy `agents.toml`). Users can override any field.
pub fn default_agents() -> HashMap<String, AgentOverride> {
    let mut agents = HashMap::new();

    agents.insert(
        "build".into(),
        AgentOverride {
            description: Some("Build-and-test specialist for compile and CI failures.".into()),
            prompt: Some(
                "You are a build and test specialist. Focus on running tests, \
                 fixing compilation errors, and ensuring code quality. \
                 Prefer targeted fixes over broad refactors."
                    .into(),
            ),
            max_turns: Some(20),
            ..Default::default()
        },
    );

    agents.insert(
        "plan".into(),
        AgentOverride {
            description: Some(
                "Planning-focused architect for structure-first task breakdown.".into(),
            ),
            prompt: Some(
                "You are an architect and planner. Analyze requirements, \
                 design solutions, and create detailed implementation plans. \
                 Do not write code — focus on structure and strategy."
                    .into(),
            ),
            max_turns: Some(10),
            temperature: Some(0.3),
            ..Default::default()
        },
    );

    agents.insert(
        "explore".into(),
        AgentOverride {
            description: Some("Read-first explorer for quick repo reconnaissance.".into()),
            prompt: Some(
                "You are a fast codebase explorer. Answer questions quickly \
                 using read, glob, and grep. Be concise and direct."
                    .into(),
            ),
            max_turns: Some(5),
            ..Default::default()
        },
    );

    agents.insert(
        "review".into(),
        AgentOverride {
            description: Some(
                "Targeted reviewer for bugs, security, and performance issues.".into(),
            ),
            prompt: Some(
                "You are a code review specialist. Examine changes for bugs, \
                 security issues, performance problems, and style violations. \
                 Provide actionable feedback with specific line references."
                    .into(),
            ),
            max_turns: Some(15),
            temperature: Some(0.2),
            ..Default::default()
        },
    );

    agents.insert(
        "general".into(),
        AgentOverride {
            description: Some(
                "General-purpose coding helper for delegated implementation work.".into(),
            ),
            prompt: Some(
                "You are a general coding helper. Execute delegated tasks end-to-end, \
                 keep changes scoped, and summarize outcomes clearly."
                    .into(),
            ),
            max_turns: Some(12),
            ..Default::default()
        },
    );

    // Canonical default alias used by the `subagent` tool when no explicit
    // agent type is provided.
    agents.insert(
        "subagent".into(),
        AgentOverride {
            description: Some("Default delegated helper alias (same intent as general).".into()),
            prompt: Some(
                "You are the default delegated sub-agent. Execute delegated tasks end-to-end, \
                 keep changes scoped, and summarize outcomes clearly."
                    .into(),
            ),
            max_turns: Some(12),
            ..Default::default()
        },
    );

    agents.insert(
        "task".into(),
        AgentOverride {
            description: Some(
                "Focused execution worker for delegated implementation slices.".into(),
            ),
            prompt: Some(
                "You are a focused sub-agent. Complete the assigned task \
                 efficiently and report results clearly."
                    .into(),
            ),
            max_turns: Some(10),
            ..Default::default()
        },
    );

    agents.insert(
        "scout".into(),
        AgentOverride {
            description: Some("Low-cost scout for quick read-only investigation.".into()),
            prompt: Some(
                "You are a lightweight scout agent. Quickly investigate the codebase \
                 using read-only tools and produce concise summaries. Be fast and cheap."
                    .into(),
            ),
            max_turns: Some(5),
            ..Default::default()
        },
    );

    agents.insert(
        "worker".into(),
        AgentOverride {
            description: Some("Execution-heavy worker for larger delegated coding tasks.".into()),
            prompt: Some(
                "You are a focused worker agent. Execute the assigned coding task \
                 efficiently using available tools. Focus on correctness and completeness."
                    .into(),
            ),
            max_turns: Some(15),
            ..Default::default()
        },
    );

    agents
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
    fn test_merge_preserves_global_defaults_enabled_when_project_has_no_defaults() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[defaults]
enabled = false
"#,
        );
        let project = write_toml(tmp.path(), "project.toml", "");

        let config = AgentsConfig::load(&global, &project);

        assert!(!config.defaults.enabled);
        let review = config.get_agent("review");
        assert!(!review.enabled);
    }

    #[test]
    fn test_merge_preserves_global_defaults_enabled_when_project_defaults_omit_enabled() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[defaults]
enabled = false
max_turns = 7
"#,
        );
        let project = write_toml(
            tmp.path(),
            "project.toml",
            r#"
[defaults]
model = "openai/gpt-5.3-codex"
"#,
        );

        let config = AgentsConfig::load(&global, &project);

        assert!(!config.defaults.enabled);
        assert_eq!(
            config.defaults.model.as_deref(),
            Some("openai/gpt-5.3-codex")
        );
        assert_eq!(config.defaults.max_turns, Some(7));
    }

    #[test]
    fn test_merge_preserves_global_defaults_enabled_with_project_agent_only_overrides() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[defaults]
enabled = false

[agents.review]
model = "anthropic/claude-sonnet-4"
"#,
        );
        let project = write_toml(
            tmp.path(),
            "project.toml",
            r#"
[agents.review]
max_turns = 21
"#,
        );

        let config = AgentsConfig::load(&global, &project);

        assert!(!config.defaults.enabled);
        let review = config.get_agent("review");
        assert!(!review.enabled);
        assert_eq!(review.model.as_deref(), Some("anthropic/claude-sonnet-4"));
        assert_eq!(review.max_turns, Some(21));
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

    #[test]
    fn test_default_agents_templates() {
        let templates = default_agents();
        assert!(templates.contains_key("build"));
        assert!(templates.contains_key("plan"));
        assert!(templates.contains_key("explore"));
        assert!(templates.contains_key("review"));
        assert!(templates.contains_key("general"));
        assert!(templates.contains_key("subagent"));
        assert!(templates.contains_key("task"));
        assert!(templates.contains_key("scout"));
        assert!(templates.contains_key("worker"));

        let plan = &templates["plan"];
        assert_eq!(plan.max_turns, Some(10));
        assert_eq!(plan.temperature, Some(0.3));
        assert!(plan.prompt.is_some());

        let explore = &templates["explore"];
        assert_eq!(explore.max_turns, Some(5));
    }

    #[test]
    fn test_fallback_to_template_when_no_config() {
        let tmp = TempDir::new().unwrap();
        let global = tmp.path().join("global.toml");
        let project = tmp.path().join("project.toml");

        // No config files — should fall back to predefined templates.
        let config = AgentsConfig::load(&global, &project);

        let build = config.get_agent("build");
        assert!(build.enabled);
        assert_eq!(build.max_turns, Some(20));
        assert!(build.prompt.is_some());
        assert!(build.prompt.as_deref().unwrap().contains("build"));

        let plan = config.get_agent("plan");
        assert_eq!(plan.max_turns, Some(10));
        assert_eq!(plan.temperature, Some(0.3));

        // Unknown agent with no template returns bare defaults.
        let unknown = config.get_agent("unknown");
        assert!(unknown.prompt.is_none());
        assert!(unknown.max_turns.is_none());
    }

    #[test]
    fn test_defaults_override_template() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[defaults]
model = "anthropic/claude-haiku-4.5"
max_turns = 50
"#,
        );
        let project = tmp.path().join("project.toml");

        let config = AgentsConfig::load(&global, &project);

        // "build" template has max_turns=20, but defaults section says 50.
        // Defaults section takes priority over template.
        let build = config.get_agent("build");
        assert_eq!(build.max_turns, Some(50));
        assert_eq!(build.model.as_deref(), Some("anthropic/claude-haiku-4.5"));
        // Prompt still comes from template.
        assert!(build.prompt.is_some());
    }

    #[test]
    fn test_temperature_in_toml() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[agents.creative]
model = "anthropic/claude-sonnet-4"
temperature = 0.9
prompt = "Be creative."
max_turns = 15
"#,
        );
        let project = tmp.path().join("project.toml");

        let config = AgentsConfig::load(&global, &project);
        let creative = config.get_agent("creative");

        assert_eq!(creative.temperature, Some(0.9));
        assert_eq!(creative.model.as_deref(), Some("anthropic/claude-sonnet-4"));
        assert_eq!(creative.prompt.as_deref(), Some("Be creative."));
    }

    #[test]
    fn test_temperature_project_override() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[agents.writer]
temperature = 0.5
prompt = "Write well."
"#,
        );
        let project = write_toml(
            tmp.path(),
            "project.toml",
            r#"
[agents.writer]
temperature = 0.8
"#,
        );

        let config = AgentsConfig::load(&global, &project);
        let writer = config.get_agent("writer");

        // Project overrides temperature
        assert_eq!(writer.temperature, Some(0.8));
        // Prompt preserved from global
        assert_eq!(writer.prompt.as_deref(), Some("Write well."));
    }

    #[test]
    fn test_available_agents() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[agents.custom-agent]
max_turns = 5
"#,
        );
        let project = tmp.path().join("project.toml");

        let config = AgentsConfig::load(&global, &project);
        let agents = config.available_agents();

        // Should include all default templates + the custom agent.
        assert!(agents.contains(&"build".to_string()));
        assert!(agents.contains(&"plan".to_string()));
        assert!(agents.contains(&"explore".to_string()));
        assert!(agents.contains(&"review".to_string()));
        assert!(agents.contains(&"general".to_string()));
        assert!(agents.contains(&"subagent".to_string()));
        assert!(agents.contains(&"task".to_string()));
        assert!(agents.contains(&"scout".to_string()));
        assert!(agents.contains(&"worker".to_string()));
        assert!(agents.contains(&"custom-agent".to_string()));
    }

    #[test]
    fn test_load_with_compat_prefers_subagents_in_same_scope() {
        let tmp = TempDir::new().unwrap();
        let global_subagents = write_toml(
            tmp.path(),
            "subagents.toml",
            r#"
[defaults]
model = "openai/gpt-5.3-codex"

[subagents.review]
max_turns = 9
"#,
        );
        let global_legacy = write_toml(
            tmp.path(),
            "agents.toml",
            r#"
[defaults]
model = "anthropic/claude-haiku-4.5"

[agents.review]
max_turns = 3
"#,
        );

        let project_subagents = tmp.path().join("missing-project-subagents.toml");
        let project_legacy = tmp.path().join("missing-project-agents.toml");

        let config = AgentsConfig::load_with_compat(
            &global_subagents,
            &global_legacy,
            &project_subagents,
            &project_legacy,
        );

        let review = config.get_agent("review");
        assert_eq!(review.model.as_deref(), Some("openai/gpt-5.3-codex"));
        assert_eq!(review.max_turns, Some(9));
    }

    #[test]
    fn test_load_with_compat_uses_legacy_when_new_missing() {
        let tmp = TempDir::new().unwrap();
        let global_subagents = tmp.path().join("missing-subagents.toml");
        let global_legacy = write_toml(
            tmp.path(),
            "agents.toml",
            r#"
[defaults]
model = "anthropic/claude-sonnet-4"

[agents.general]
max_turns = 7
"#,
        );

        let project_subagents = tmp.path().join("missing-project-subagents.toml");
        let project_legacy = tmp.path().join("missing-project-agents.toml");

        let config = AgentsConfig::load_with_compat(
            &global_subagents,
            &global_legacy,
            &project_subagents,
            &project_legacy,
        );

        let general = config.get_agent("general");
        assert_eq!(general.model.as_deref(), Some("anthropic/claude-sonnet-4"));
        assert_eq!(general.max_turns, Some(7));
    }

    #[test]
    fn test_load_with_compat_prefers_project_subagents_over_project_legacy() {
        let tmp = TempDir::new().unwrap();
        let global_subagents = tmp.path().join("missing-global-subagents.toml");
        let global_legacy = tmp.path().join("missing-global-agents.toml");

        let project_subagents = write_toml(
            tmp.path(),
            "project-subagents.toml",
            r#"
[subagents.review]
max_turns = 11
"#,
        );
        let project_legacy = write_toml(
            tmp.path(),
            "project-agents.toml",
            r#"
[agents.review]
max_turns = 2
"#,
        );

        let config = AgentsConfig::load_with_compat(
            &global_subagents,
            &global_legacy,
            &project_subagents,
            &project_legacy,
        );

        let review = config.get_agent("review");
        assert_eq!(review.max_turns, Some(11));
    }

    #[test]
    fn test_load_with_compat_merges_global_legacy_with_project_subagents() {
        let tmp = TempDir::new().unwrap();
        let global_subagents = tmp.path().join("missing-subagents.toml");
        let global_legacy = write_toml(
            tmp.path(),
            "agents.toml",
            r#"
[defaults]
model = "anthropic/claude-haiku-4.5"
"#,
        );
        let project_subagents = write_toml(
            tmp.path(),
            "project-subagents.toml",
            r#"
[subagents.review]
max_turns = 13
"#,
        );
        let project_legacy = tmp.path().join("missing-project-agents.toml");

        let config = AgentsConfig::load_with_compat(
            &global_subagents,
            &global_legacy,
            &project_subagents,
            &project_legacy,
        );

        let review = config.get_agent("review");
        assert_eq!(review.model.as_deref(), Some("anthropic/claude-haiku-4.5"));
        assert_eq!(review.max_turns, Some(13));
    }

    #[test]
    fn test_subagents_table_alias_is_supported() {
        let tmp = TempDir::new().unwrap();
        let global = write_toml(
            tmp.path(),
            "global.toml",
            r#"
[defaults]
model = "openrouter/openai/gpt-4.1-mini"

[subagents.explore]
max_turns = 4
description = "Read-first exploration"
"#,
        );
        let project = tmp.path().join("project.toml");

        let config = AgentsConfig::load(&global, &project);
        let explore = config.get_agent("explore");
        assert_eq!(explore.max_turns, Some(4));
        assert_eq!(
            explore.description.as_deref(),
            Some("Read-first exploration")
        );
        assert_eq!(
            explore.model.as_deref(),
            Some("openrouter/openai/gpt-4.1-mini")
        );
    }
}
