use ava_config::AgentsConfig;

use super::{runtime_profile_for, SubAgentRuntimeProfile};

/// Backend-owned effective subagent definition used for runtime introspection
/// and future adapter-facing list/read APIs.
#[derive(Debug, Clone, PartialEq)]
pub struct EffectiveSubagentDefinition {
    pub id: String,
    pub description: Option<String>,
    pub enabled: bool,
    pub model: Option<String>,
    pub max_turns: Option<usize>,
    pub temperature: Option<f32>,
    pub provider: Option<String>,
    pub allowed_tools: Option<Vec<String>>,
    pub max_budget_usd: Option<f64>,
    pub runtime_profile: SubAgentRuntimeProfile,
}

pub fn effective_subagent_definitions(config: &AgentsConfig) -> Vec<EffectiveSubagentDefinition> {
    config
        .available_agents()
        .into_iter()
        .map(|id| {
            let resolved = config.get_agent(&id);
            EffectiveSubagentDefinition {
                runtime_profile: runtime_profile_for(&id),
                id,
                description: resolved.description,
                enabled: resolved.enabled,
                model: resolved.model,
                max_turns: resolved.max_turns,
                temperature: resolved.temperature,
                provider: resolved.provider,
                allowed_tools: resolved.allowed_tools,
                max_budget_usd: resolved.max_budget_usd,
            }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn effective_catalog_includes_builtins_and_runtime_profiles() {
        let config = AgentsConfig::default();
        let defs = effective_subagent_definitions(&config);

        assert!(defs.iter().any(|def| def.id == "general" && def.enabled));
        assert!(defs.iter().any(|def| def.id == "subagent" && def.enabled));
        assert!(defs.iter().any(|def| {
            def.id == "review" && def.runtime_profile == SubAgentRuntimeProfile::ReadOnly
        }));
    }
}
