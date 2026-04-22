use ava_acp::PermissionMode;
use ava_tools::registry::ToolRegistry;

/// Maximum nesting depth for sub-agent spawning. Prevents unbounded recursion
/// even if future refactors accidentally expose the task tool to sub-agents.
pub const MAX_AGENT_DEPTH: u32 = 3;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SubAgentRuntimeProfile {
    Full,
    ReadOnly,
}

/// Stable built-in subagent IDs that ship with backend defaults.
///
/// Canonical ownership lives in `ava_config::default_agents()`; this helper
/// projects those IDs for runtime/backend introspection surfaces.
pub fn builtin_subagent_ids() -> Vec<String> {
    let mut ids: Vec<String> = ava_config::default_agents().into_keys().collect();
    ids.sort();
    ids
}

pub fn runtime_profile_for(agent_type: &str) -> SubAgentRuntimeProfile {
    match agent_type {
        "plan" | "explore" | "scout" | "review" => SubAgentRuntimeProfile::ReadOnly,
        _ => SubAgentRuntimeProfile::Full,
    }
}

pub fn apply_runtime_profile_to_registry(
    registry: &mut ToolRegistry,
    profile: SubAgentRuntimeProfile,
) {
    if profile == SubAgentRuntimeProfile::ReadOnly {
        for tool in ["write", "edit", "bash", "web_fetch", "web_search"] {
            registry.unregister(tool);
        }
    }
}

pub fn tool_visibility_profile(
    profile: SubAgentRuntimeProfile,
) -> ava_agent::routing::ToolVisibilityProfile {
    match profile {
        SubAgentRuntimeProfile::Full => ava_agent::routing::ToolVisibilityProfile::Full,
        SubAgentRuntimeProfile::ReadOnly => ava_agent::routing::ToolVisibilityProfile::ReadOnly,
    }
}

pub fn runtime_guidance(profile: SubAgentRuntimeProfile) -> &'static str {
    match profile {
        SubAgentRuntimeProfile::Full => {
            "\n\n## Runtime limits\n- Stay focused on the delegated task. Keep changes narrow and summarize the result clearly.\n"
        }
        SubAgentRuntimeProfile::ReadOnly => {
            "\n\n## Runtime limits\n- You are running in read-only specialist mode. Do not edit files, run shell commands, or browse the web. Investigate with read, glob, grep, and git, then report back clearly.\n"
        }
    }
}

pub fn external_permission_mode(profile: SubAgentRuntimeProfile) -> PermissionMode {
    match profile {
        SubAgentRuntimeProfile::Full => PermissionMode::AcceptEdits,
        SubAgentRuntimeProfile::ReadOnly => PermissionMode::Plan,
    }
}

pub fn default_external_allowed_tools(profile: SubAgentRuntimeProfile) -> Option<Vec<String>> {
    match profile {
        SubAgentRuntimeProfile::Full => None,
        SubAgentRuntimeProfile::ReadOnly => Some(vec![
            "Read".to_string(),
            "Glob".to_string(),
            "Grep".to_string(),
        ]),
    }
}

pub fn build_subagent_system_prompt(agent_type: &str) -> String {
    format!(
        "You are the `{agent_type}` sub-agent of AVA, an AI coding assistant. You have been given a specific task \
         to complete autonomously. Work through it step by step using the available tools.\n\n\
         ## Rules\n\
         - Tool calls must use the tool's exact JSON parameter names. Examples: `read` requires `{{\"path\": \"...\"}}`, `glob` requires `{{\"pattern\": \"...\"}}`, and `grep` requires `{{\"pattern\": \"...\", \"path\": \"...\"}}`.\n\
         - Read files before modifying them.\n\
         - Prefer focused, local changes over broad rewrites.\n\
         - Be thorough but efficient -- you have a limited number of turns.\n\
         - If a tool call fails validation, correct the arguments on the next attempt instead of repeating the same invalid call.\n\
         - When your task is complete, provide a clear summary of what you did as your final response.\n\
         - Do NOT call attempt_completion -- simply respond with your final answer when done.\n"
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builtin_catalog_includes_general_profile() {
        assert!(builtin_subagent_ids().contains(&"general".to_string()));
    }
}
