//! HQ Agent Role Profiles — declarative per-role configuration for multi-agent orchestration.
//!
//! Each role (Director, Senior Lead, Junior Worker, Scout, or custom) defines:
//! - System prompt (with template variables)
//! - Allowed/denied built-in tools
//! - Allowed/denied MCP servers
//! - Skill files to inject
//! - Model, thinking level, budget constraints
//!
//! Profiles are resolved via a merge chain:
//!   compiled defaults → global config → project config → runtime override

use std::collections::{HashMap, HashSet};

use ava_types::ThinkingLevel;
use serde::{Deserialize, Serialize};

// ---------------------------------------------------------------------------
// Core types
// ---------------------------------------------------------------------------

/// Budget constraints for an agent role.
#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct RoleBudget {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub max_turns: Option<usize>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub max_tokens: Option<usize>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub max_cost_usd: Option<f64>,
}

/// Declarative profile for an HQ agent role.
///
/// Defines capabilities, constraints, and personality.  Profiles are layered:
/// compiled defaults are overridden by global config, then project config,
/// then runtime overrides.
#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AgentRoleProfile {
    /// Role identifier (e.g. `"director"`, `"senior-lead"`, `"junior-worker"`, `"scout"`).
    #[serde(default)]
    pub id: String,

    /// Human-readable display name.  Supports `{{domain}}`, `{{name}}` placeholders.
    #[serde(default)]
    pub display_name: String,

    /// System prompt template.  Supports `{{name}}`, `{{domain}}`, `{{goal}}`, `{{project}}`.
    /// Empty = inherit from compiled default for this role.
    #[serde(default)]
    pub system_prompt: String,

    /// Append-only suffix injected after the resolved system prompt.
    /// Accumulates across config layers (concatenated, not replaced).
    #[serde(default)]
    pub system_prompt_suffix: String,

    /// Whitelist of built-in tool names.  Empty = tier default.  `["*"]` = all tools.
    #[serde(default)]
    pub allowed_tools: Vec<String>,

    /// Built-in tool names explicitly denied (applied after `allowed_tools`).
    #[serde(default)]
    pub denied_tools: Vec<String>,

    /// Whether to include extended-tier tools (apply_patch, multiedit, etc.).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub extended_tools: Option<bool>,

    /// MCP server whitelist.  Empty = no MCP servers.  `["*"]` = all configured servers.
    #[serde(default)]
    pub allowed_mcp_servers: Vec<String>,

    /// MCP servers explicitly denied (applied after `allowed_mcp_servers`).
    #[serde(default)]
    pub denied_mcp_servers: Vec<String>,

    /// Skill file glob patterns to load for this role (e.g. `".ava/skills/senior/*"`).
    /// Additive across config layers.
    #[serde(default)]
    pub skill_paths: Vec<String>,

    /// Model preference (e.g. `"anthropic/claude-sonnet-4"`).  Empty = inherit.
    #[serde(default)]
    pub model: String,

    /// Thinking level override for this role.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub thinking_level: Option<ThinkingLevel>,

    /// Budget constraints.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub budget: Option<RoleBudget>,

    /// Whether this role is enabled.
    #[serde(default = "default_true")]
    pub enabled: bool,

    /// Temperature override (0.0–1.0).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub temperature: Option<f32>,
}

const fn default_true() -> bool {
    true
}

impl Default for AgentRoleProfile {
    fn default() -> Self {
        Self {
            id: String::new(),
            display_name: String::new(),
            system_prompt: String::new(),
            system_prompt_suffix: String::new(),
            allowed_tools: Vec::new(),
            denied_tools: Vec::new(),
            extended_tools: None,
            allowed_mcp_servers: Vec::new(),
            denied_mcp_servers: Vec::new(),
            skill_paths: Vec::new(),
            model: String::new(),
            thinking_level: None,
            budget: None,
            enabled: true,
            temperature: None,
        }
    }
}

// ---------------------------------------------------------------------------
// Standard role IDs
// ---------------------------------------------------------------------------

pub const ROLE_DIRECTOR: &str = "director";
pub const ROLE_SENIOR_LEAD: &str = "senior-lead";
pub const ROLE_JUNIOR_WORKER: &str = "junior-worker";
pub const ROLE_SCOUT: &str = "scout";

// ---------------------------------------------------------------------------
// Default profiles (compiled-in)
// ---------------------------------------------------------------------------

/// Returns the four built-in role profiles.
pub fn default_role_profiles() -> HashMap<String, AgentRoleProfile> {
    let mut map = HashMap::new();
    map.insert(ROLE_DIRECTOR.to_string(), default_director_profile());
    map.insert(ROLE_SENIOR_LEAD.to_string(), default_senior_lead_profile());
    map.insert(
        ROLE_JUNIOR_WORKER.to_string(),
        default_junior_worker_profile(),
    );
    map.insert(ROLE_SCOUT.to_string(), default_scout_profile());
    map
}

fn default_director_profile() -> AgentRoleProfile {
    AgentRoleProfile {
        id: ROLE_DIRECTOR.to_string(),
        display_name: "AVA".to_string(),
        system_prompt: "\
You are AVA, the user's operational partner and right hand. You lead a development \
team of senior leads and junior developers. You do not write code yourself — you \
organize, delegate, and communicate.\n\
\n\
When the user gives you a goal: send scouts to understand the codebase, form a plan, \
assign domain leads with specific files (no overlapping assignments), and set execution \
order — parallel when tasks are independent, sequential when they depend on each other. \
Most tasks need 1–3 leads, not more.\n\
\n\
Communication style: warm but direct. Report outcomes, not play-by-play. \
\"Pedro finished the JWT middleware\" is good. Turn-by-turn narration is not. \
Relay lead questions to the user clearly and wait for answers.\n\
\n\
Before declaring work complete, verify the changes compile and tests pass. \
If a worker fails, let the lead handle it first — only escalate to the user \
when the lead cannot resolve it.\n\
\n\
Only make changes the user requested. Do not expand scope."
            .to_string(),
        allowed_tools: vec![
            "read".to_string(),
            "glob".to_string(),
            "grep".to_string(),
            "git".to_string(),
        ],
        denied_tools: vec![
            "write".to_string(),
            "edit".to_string(),
            "bash".to_string(),
        ],
        extended_tools: Some(false),
        allowed_mcp_servers: vec!["*".to_string()],
        thinking_level: Some(ThinkingLevel::Medium),
        budget: Some(RoleBudget {
            max_turns: Some(30),
            max_tokens: None,
            max_cost_usd: None,
        }),
        ..Default::default()
    }
}

fn default_senior_lead_profile() -> AgentRoleProfile {
    AgentRoleProfile {
        id: ROLE_SENIOR_LEAD.to_string(),
        display_name: "{{domain}} Lead".to_string(),
        system_prompt: "\
You are the {{domain}} Lead, a senior specialist managing junior workers. \
You plan before you delegate.\n\
\n\
Read and research the relevant code before splitting work. Assign specific files \
to each worker — no overlapping file assignments. Run workers in parallel when \
their tasks are independent, sequentially when one depends on another.\n\
\n\
Review each worker's output before reporting to the Director. Fix small issues \
yourself; spawn a fix worker for larger ones. Report results and questions — \
not play-by-play.\n\
\n\
Before reporting completion, verify that workers' changes compile and tests pass. \
Only do what was assigned to you — do not expand scope or refactor beyond the task."
            .to_string(),
        allowed_tools: vec!["*".to_string()],
        extended_tools: Some(true),
        allowed_mcp_servers: vec!["*".to_string()],
        skill_paths: vec![".ava/skills/senior/*".to_string()],
        thinking_level: Some(ThinkingLevel::Medium),
        budget: Some(RoleBudget {
            max_turns: Some(20),
            max_tokens: None,
            max_cost_usd: None,
        }),
        ..Default::default()
    }
}

fn default_junior_worker_profile() -> AgentRoleProfile {
    AgentRoleProfile {
        id: ROLE_JUNIOR_WORKER.to_string(),
        display_name: "{{name}}".to_string(),
        system_prompt: "\
You are {{name}}, a developer on the {{domain}} team. Complete the specific task \
your lead assigned.\n\
\n\
Rules:\n\
- Read file contents before editing — do not speculate about code you have not opened.\n\
- Only modify your assigned files.\n\
- If you intend to call multiple tools and there are no dependencies between them, \
  make all independent calls in parallel.\n\
- Follow existing code style and conventions.\n\
- When finished, report what you changed and any issues you found.\n\
- If you are unsure about something, ask your lead, not the user.\n\
- Before reporting completion, verify your changes compile."
            .to_string(),
        allowed_tools: vec![
            "read".to_string(),
            "write".to_string(),
            "edit".to_string(),
            "bash".to_string(),
            "glob".to_string(),
            "grep".to_string(),
            "git".to_string(),
        ],
        denied_tools: vec!["web_search".to_string(), "web_fetch".to_string()],
        extended_tools: Some(false),
        allowed_mcp_servers: Vec::new(),
        thinking_level: Some(ThinkingLevel::Off),
        budget: Some(RoleBudget {
            max_turns: Some(15),
            max_tokens: None,
            max_cost_usd: None,
        }),
        ..Default::default()
    }
}

fn default_scout_profile() -> AgentRoleProfile {
    AgentRoleProfile {
        id: ROLE_SCOUT.to_string(),
        display_name: "Scout".to_string(),
        system_prompt: "\
You are a Scout — a codebase analyst. Your job is to read and summarize code, NOT modify it.\n\
\n\
Given a query, investigate the relevant parts of the codebase and produce a structured report.\n\
Use the tools available (glob, grep, read) to find and read relevant files.\n\
\n\
When you are done investigating, respond with your findings in the following structure:\n\
\n\
## Files Examined\n\
- List each file you looked at\n\
\n\
## Key Findings\n\
- What code patterns and structures you found\n\
- How the relevant code works\n\
\n\
## Relevant Code\n\
For each important snippet:\n\
- File path and line range\n\
- The code itself\n\
- Why it matters\n\
\n\
## Potential Issues\n\
- Any problems or areas needing attention\n\
\n\
## Suggestions\n\
- How to approach the task (do NOT suggest code changes — just strategic observations)\n\
\n\
Be thorough but concise. Focus on actionable findings."
            .to_string(),
        allowed_tools: vec![
            "read".to_string(),
            "glob".to_string(),
            "grep".to_string(),
        ],
        denied_tools: vec![],
        extended_tools: Some(false),
        allowed_mcp_servers: Vec::new(),
        thinking_level: Some(ThinkingLevel::Off),
        budget: Some(RoleBudget {
            max_turns: Some(10),
            max_tokens: Some(5_000),
            max_cost_usd: Some(0.05),
        }),
        ..Default::default()
    }
}

// ---------------------------------------------------------------------------
// Template variable substitution
// ---------------------------------------------------------------------------

/// Replace `{{key}}` placeholders in `system_prompt` and `display_name`.
pub fn apply_template_vars(profile: &AgentRoleProfile, vars: &[(&str, &str)]) -> AgentRoleProfile {
    let mut result = profile.clone();
    for (key, value) in vars {
        let placeholder = format!("{{{{{key}}}}}");
        result.system_prompt = result.system_prompt.replace(&placeholder, value);
        result.display_name = result.display_name.replace(&placeholder, value);
        if !result.system_prompt_suffix.is_empty() {
            result.system_prompt_suffix =
                result.system_prompt_suffix.replace(&placeholder, value);
        }
    }
    result
}

// ---------------------------------------------------------------------------
// Role resolver
// ---------------------------------------------------------------------------

/// Resolves agent role profiles by merging compiled defaults with global and
/// project overrides.
#[derive(Debug, Clone)]
pub struct RoleResolver {
    defaults: HashMap<String, AgentRoleProfile>,
    global: HashMap<String, AgentRoleProfile>,
    project: HashMap<String, AgentRoleProfile>,
}

impl RoleResolver {
    /// Create a resolver with only compiled-in defaults.
    pub fn new() -> Self {
        Self {
            defaults: default_role_profiles(),
            global: HashMap::new(),
            project: HashMap::new(),
        }
    }

    /// Add global overrides (from `~/.ava/config.yaml` `hq.roles`).
    pub fn with_global(mut self, profiles: HashMap<String, AgentRoleProfile>) -> Self {
        self.global = profiles;
        self
    }

    /// Add project overrides (from `.ava/hq-roles.toml`).
    pub fn with_project(mut self, profiles: HashMap<String, AgentRoleProfile>) -> Self {
        self.project = profiles;
        self
    }

    /// Resolve a role profile by merging the chain: defaults → global → project.
    pub fn resolve(&self, role_id: &str) -> AgentRoleProfile {
        let base = self
            .defaults
            .get(role_id)
            .cloned()
            .unwrap_or_else(|| AgentRoleProfile {
                id: role_id.to_string(),
                ..Default::default()
            });

        let after_global = if let Some(global_override) = self.global.get(role_id) {
            merge_profiles(&base, global_override)
        } else {
            base
        };

        if let Some(project_override) = self.project.get(role_id) {
            merge_profiles(&after_global, project_override)
        } else {
            after_global
        }
    }

    /// Resolve with an additional runtime override (e.g. from `DirectorConfig`).
    pub fn resolve_with_override(
        &self,
        role_id: &str,
        runtime: &AgentRoleProfile,
    ) -> AgentRoleProfile {
        let base = self.resolve(role_id);
        merge_profiles(&base, runtime)
    }

    /// List all known role IDs (defaults + configured).
    pub fn available_roles(&self) -> Vec<String> {
        let mut ids: HashSet<String> = HashSet::new();
        ids.extend(self.defaults.keys().cloned());
        ids.extend(self.global.keys().cloned());
        ids.extend(self.project.keys().cloned());
        let mut sorted: Vec<_> = ids.into_iter().collect();
        sorted.sort();
        sorted
    }
}

impl Default for RoleResolver {
    fn default() -> Self {
        Self::new()
    }
}

/// Merge `overlay` on top of `base`, field by field.
///
/// Rules:
/// - Strings: non-empty overlay replaces base
/// - `system_prompt_suffix`: concatenated (additive)
/// - `denied_tools`/`denied_mcp_servers`/`skill_paths`: union (additive)
/// - Vecs (`allowed_tools`, `allowed_mcp_servers`): non-empty overlay replaces base
/// - Options: `Some` overlay replaces base
fn merge_profiles(base: &AgentRoleProfile, overlay: &AgentRoleProfile) -> AgentRoleProfile {
    AgentRoleProfile {
        id: if overlay.id.is_empty() {
            base.id.clone()
        } else {
            overlay.id.clone()
        },
        display_name: if overlay.display_name.is_empty() {
            base.display_name.clone()
        } else {
            overlay.display_name.clone()
        },
        system_prompt: if overlay.system_prompt.is_empty() {
            base.system_prompt.clone()
        } else {
            overlay.system_prompt.clone()
        },
        system_prompt_suffix: {
            let mut suffix = base.system_prompt_suffix.clone();
            if !overlay.system_prompt_suffix.is_empty() {
                if !suffix.is_empty() {
                    suffix.push('\n');
                }
                suffix.push_str(&overlay.system_prompt_suffix);
            }
            suffix
        },
        allowed_tools: if overlay.allowed_tools.is_empty() {
            base.allowed_tools.clone()
        } else {
            overlay.allowed_tools.clone()
        },
        denied_tools: {
            let mut set: HashSet<String> = base.denied_tools.iter().cloned().collect();
            set.extend(overlay.denied_tools.iter().cloned());
            let mut sorted: Vec<_> = set.into_iter().collect();
            sorted.sort();
            sorted
        },
        extended_tools: overlay.extended_tools.or(base.extended_tools),
        allowed_mcp_servers: if overlay.allowed_mcp_servers.is_empty() {
            base.allowed_mcp_servers.clone()
        } else {
            overlay.allowed_mcp_servers.clone()
        },
        denied_mcp_servers: {
            let mut set: HashSet<String> = base.denied_mcp_servers.iter().cloned().collect();
            set.extend(overlay.denied_mcp_servers.iter().cloned());
            let mut sorted: Vec<_> = set.into_iter().collect();
            sorted.sort();
            sorted
        },
        skill_paths: {
            let mut set: HashSet<String> = base.skill_paths.iter().cloned().collect();
            set.extend(overlay.skill_paths.iter().cloned());
            let mut sorted: Vec<_> = set.into_iter().collect();
            sorted.sort();
            sorted
        },
        model: if overlay.model.is_empty() {
            base.model.clone()
        } else {
            overlay.model.clone()
        },
        thinking_level: overlay.thinking_level.or(base.thinking_level),
        budget: match (&base.budget, &overlay.budget) {
            (_, Some(ob)) => Some(RoleBudget {
                max_turns: ob.max_turns.or(base.budget.as_ref().and_then(|b| b.max_turns)),
                max_tokens: ob.max_tokens.or(base.budget.as_ref().and_then(|b| b.max_tokens)),
                max_cost_usd: ob
                    .max_cost_usd
                    .or(base.budget.as_ref().and_then(|b| b.max_cost_usd)),
            }),
            (Some(bb), None) => Some(bb.clone()),
            (None, None) => None,
        },
        enabled: overlay.enabled && base.enabled,
        temperature: overlay.temperature.or(base.temperature),
    }
}

// ---------------------------------------------------------------------------
// Config file loading
// ---------------------------------------------------------------------------

/// Top-level structure for `.ava/hq-roles.toml`.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct HqRolesFile {
    #[serde(default)]
    pub roles: HashMap<String, AgentRoleProfile>,
}

/// Load role profiles from a TOML file. Returns empty map if file doesn't exist.
pub fn load_roles_file(path: &std::path::Path) -> HashMap<String, AgentRoleProfile> {
    let content = match std::fs::read_to_string(path) {
        Ok(c) => c,
        Err(_) => return HashMap::new(),
    };
    match toml::from_str::<HqRolesFile>(&content) {
        Ok(mut file) => {
            for (key, profile) in &mut file.roles {
                if profile.id.is_empty() {
                    profile.id = key.clone();
                }
            }
            file.roles
        }
        Err(err) => {
            tracing::warn!(%err, path = %path.display(), "failed to parse hq-roles.toml");
            HashMap::new()
        }
    }
}

// ---------------------------------------------------------------------------
// Backward compatibility: HqAgentOverride → AgentRoleProfile
// ---------------------------------------------------------------------------

/// Convert a legacy `HqAgentOverride` into an `AgentRoleProfile`.
pub fn from_agent_override(id: &str, model_spec: &str, system_prompt: &str) -> AgentRoleProfile {
    AgentRoleProfile {
        id: id.to_string(),
        model: model_spec.to_string(),
        system_prompt: system_prompt.to_string(),
        ..Default::default()
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_profiles_has_four_roles() {
        let profiles = default_role_profiles();
        assert_eq!(profiles.len(), 4);
        assert!(profiles.contains_key(ROLE_DIRECTOR));
        assert!(profiles.contains_key(ROLE_SENIOR_LEAD));
        assert!(profiles.contains_key(ROLE_JUNIOR_WORKER));
        assert!(profiles.contains_key(ROLE_SCOUT));
    }

    #[test]
    fn director_profile_denies_write_tools() {
        let profile = default_director_profile();
        assert!(profile.denied_tools.contains(&"write".to_string()));
        assert!(profile.denied_tools.contains(&"edit".to_string()));
        assert!(profile.denied_tools.contains(&"bash".to_string()));
    }

    #[test]
    fn junior_worker_has_limited_tools() {
        let profile = default_junior_worker_profile();
        assert!(!profile.allowed_tools.contains(&"*".to_string()));
        assert!(profile.allowed_tools.contains(&"read".to_string()));
        assert!(profile.allowed_tools.contains(&"write".to_string()));
        assert!(!profile.allowed_tools.contains(&"web_search".to_string()));
        assert!(profile.denied_tools.contains(&"web_search".to_string()));
    }

    #[test]
    fn junior_worker_has_no_mcp() {
        let profile = default_junior_worker_profile();
        assert!(profile.allowed_mcp_servers.is_empty());
    }

    #[test]
    fn senior_lead_has_all_tools_and_mcp() {
        let profile = default_senior_lead_profile();
        assert!(profile.allowed_tools.contains(&"*".to_string()));
        assert!(profile.allowed_mcp_servers.contains(&"*".to_string()));
    }

    #[test]
    fn scout_is_read_only() {
        let profile = default_scout_profile();
        assert_eq!(profile.allowed_tools.len(), 3);
        assert!(profile.allowed_tools.contains(&"read".to_string()));
        assert!(profile.allowed_tools.contains(&"glob".to_string()));
        assert!(profile.allowed_tools.contains(&"grep".to_string()));
    }

    #[test]
    fn template_vars_substitution() {
        let profile = AgentRoleProfile {
            display_name: "{{domain}} Lead".to_string(),
            system_prompt: "You are the {{domain}} Lead named {{name}}.".to_string(),
            ..Default::default()
        };
        let resolved = apply_template_vars(&profile, &[("domain", "Backend"), ("name", "Pedro")]);
        assert_eq!(resolved.display_name, "Backend Lead");
        assert_eq!(
            resolved.system_prompt,
            "You are the Backend Lead named Pedro."
        );
    }

    #[test]
    fn merge_string_fields() {
        let base = AgentRoleProfile {
            id: "test".to_string(),
            display_name: "Base".to_string(),
            model: "model-a".to_string(),
            ..Default::default()
        };
        let overlay = AgentRoleProfile {
            display_name: "Overlay".to_string(),
            ..Default::default()
        };
        let merged = merge_profiles(&base, &overlay);
        assert_eq!(merged.id, "test");
        assert_eq!(merged.display_name, "Overlay");
        assert_eq!(merged.model, "model-a");
    }

    #[test]
    fn merge_suffix_is_additive() {
        let base = AgentRoleProfile {
            system_prompt_suffix: "base suffix".to_string(),
            ..Default::default()
        };
        let overlay = AgentRoleProfile {
            system_prompt_suffix: "overlay suffix".to_string(),
            ..Default::default()
        };
        let merged = merge_profiles(&base, &overlay);
        assert_eq!(merged.system_prompt_suffix, "base suffix\noverlay suffix");
    }

    #[test]
    fn merge_denied_tools_is_union() {
        let base = AgentRoleProfile {
            denied_tools: vec!["bash".to_string()],
            ..Default::default()
        };
        let overlay = AgentRoleProfile {
            denied_tools: vec!["write".to_string()],
            ..Default::default()
        };
        let merged = merge_profiles(&base, &overlay);
        assert!(merged.denied_tools.contains(&"bash".to_string()));
        assert!(merged.denied_tools.contains(&"write".to_string()));
    }

    #[test]
    fn merge_budget_fields() {
        let base = AgentRoleProfile {
            budget: Some(RoleBudget {
                max_turns: Some(20),
                max_tokens: Some(100_000),
                max_cost_usd: None,
            }),
            ..Default::default()
        };
        let overlay = AgentRoleProfile {
            budget: Some(RoleBudget {
                max_turns: Some(10),
                max_tokens: None,
                max_cost_usd: Some(5.0),
            }),
            ..Default::default()
        };
        let merged = merge_profiles(&base, &overlay);
        let budget = merged.budget.unwrap();
        assert_eq!(budget.max_turns, Some(10));
        assert_eq!(budget.max_tokens, Some(100_000));
        assert_eq!(budget.max_cost_usd, Some(5.0));
    }

    #[test]
    fn resolver_defaults_only() {
        let resolver = RoleResolver::new();
        let director = resolver.resolve(ROLE_DIRECTOR);
        assert_eq!(director.display_name, "AVA");
        assert!(director.system_prompt.contains("operational partner"));
    }

    #[test]
    fn resolver_with_global_override() {
        let mut global = HashMap::new();
        global.insert(
            ROLE_DIRECTOR.to_string(),
            AgentRoleProfile {
                model: "anthropic/claude-opus-4".to_string(),
                system_prompt_suffix: "Be extra concise.".to_string(),
                ..Default::default()
            },
        );
        let resolver = RoleResolver::new().with_global(global);
        let director = resolver.resolve(ROLE_DIRECTOR);
        assert_eq!(director.model, "anthropic/claude-opus-4");
        assert!(director.system_prompt_suffix.contains("Be extra concise."));
        assert!(director.system_prompt.contains("operational partner"));
    }

    #[test]
    fn resolver_custom_role() {
        let mut project = HashMap::new();
        project.insert(
            "architect".to_string(),
            AgentRoleProfile {
                id: "architect".to_string(),
                display_name: "Architect".to_string(),
                system_prompt: "You are the Architect.".to_string(),
                allowed_tools: vec!["read".to_string(), "glob".to_string()],
                thinking_level: Some(ThinkingLevel::High),
                ..Default::default()
            },
        );
        let resolver = RoleResolver::new().with_project(project);
        let architect = resolver.resolve("architect");
        assert_eq!(architect.display_name, "Architect");
        assert_eq!(architect.thinking_level, Some(ThinkingLevel::High));
    }

    #[test]
    fn resolver_available_roles() {
        let mut project = HashMap::new();
        project.insert("architect".to_string(), AgentRoleProfile::default());
        let resolver = RoleResolver::new().with_project(project);
        let roles = resolver.available_roles();
        assert!(roles.contains(&ROLE_DIRECTOR.to_string()));
        assert!(roles.contains(&"architect".to_string()));
    }

    #[test]
    fn toml_roundtrip() {
        let file = HqRolesFile {
            roles: {
                let mut m = HashMap::new();
                m.insert(
                    "director".to_string(),
                    AgentRoleProfile {
                        id: "director".to_string(),
                        system_prompt_suffix: "Be concise.".to_string(),
                        model: "anthropic/claude-opus-4".to_string(),
                        ..Default::default()
                    },
                );
                m
            },
        };
        let toml_str = toml::to_string_pretty(&file).unwrap();
        let parsed: HqRolesFile = toml::from_str(&toml_str).unwrap();
        assert!(parsed.roles.contains_key("director"));
        assert_eq!(parsed.roles["director"].model, "anthropic/claude-opus-4");
    }

    #[test]
    fn from_legacy_override() {
        let profile = from_agent_override("commander", "openai/gpt-5.4", "You are a commander.");
        assert_eq!(profile.id, "commander");
        assert_eq!(profile.model, "openai/gpt-5.4");
        assert!(profile.system_prompt.contains("commander"));
    }
}
