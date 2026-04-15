//! AVA Agent — core agent execution loop with tool calling and stuck detection.
//!
//! This crate implements the main agent loop that:
//! - Sends messages to LLM providers via `ava-llm`
//! - Parses and executes tool calls via `ava-tools`
//! - Detects stuck states and terminates gracefully

pub mod agent_loop;
pub(crate) mod budget;
pub mod cache_sharing;
pub mod continuation;
pub mod control_plane;
pub mod dream;
pub mod error_hints;
pub mod instruction_resolver;
pub mod instructions;
pub mod llm_trait;
pub(crate) mod memory_enrichment;
pub mod message_queue;
pub mod reflection;
pub mod routing;
pub mod session_logger;
pub mod stack;
pub mod streaming_diff;
pub mod stuck;
pub mod system_prompt;
pub mod trace;
pub mod turn_diff;

pub use instructions::{
    contextual_instructions_for_file, discover_runtime_skills, discover_runtime_skills_from_root,
    load_project_instructions, load_startup_project_instructions_with_config,
    matching_rule_instructions_for_file, trim_instructions_to_budget, RuntimeSkill,
    RuntimeSkillDiscovery, RuntimeSkillScope,
};
/// Reflection loop primitives for error analysis and auto-fix retries.
pub use reflection::{ErrorKind, ReflectionAgent, ReflectionLoop, ToolExecutor, ToolResult};
pub use {
    agent_loop::*,
    control_plane::commands::{
        canonical_command_specs, command_spec, command_spec_by_name, queue_command_from_alias,
        queue_command_from_tier, queue_command_label, queue_message_tier, CommandFamily,
        CommandSpec, CompletionMode, ControlPlaneCommand, CorrelationIdKey,
        CorrelationIdRequirements, ResponseEnvelope, TerminalClosureSignal,
    },
    control_plane::interactive::{
        canonical_interactive_timeout_policy, InteractiveRequestHandle, InteractiveRequestKind,
        InteractiveRequestPhase, InteractiveRequestStore, InteractiveTimeoutPolicy,
        ResolveInteractiveRequestError, TerminalInteractiveRequest,
        DEFAULT_INTERACTIVE_REQUEST_TIMEOUT,
    },
    llm_trait::{LLMProvider, LLMResponse},
};

#[cfg(test)]
pub(crate) mod tests {
    use std::sync::Arc;

    /// Create a mock LLM provider for stuck detection tests.
    pub fn mock_llm() -> Arc<dyn crate::llm_trait::LLMProvider> {
        Arc::new(ava_llm::providers::mock::MockProvider::new("mock", vec![]))
    }
}
