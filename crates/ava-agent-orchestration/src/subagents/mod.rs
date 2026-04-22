pub mod catalog;
pub mod config;
pub mod effective;

pub use catalog::{
    apply_runtime_profile_to_registry, build_subagent_system_prompt, builtin_subagent_ids,
    default_external_allowed_tools, external_permission_mode, runtime_guidance,
    runtime_profile_for, tool_visibility_profile, SubAgentRuntimeProfile, MAX_AGENT_DEPTH,
};
pub use config::parse_model_spec;
pub use effective::{effective_subagent_definitions, EffectiveSubagentDefinition};
