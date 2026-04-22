//! AVA Agent — core agent execution loop with tool calling and stuck detection.
//!
//! This crate implements the main agent loop that:
//! - Sends messages to LLM providers via `ava-llm`
//! - Parses and executes tool calls via `ava-tools`
//! - Detects stuck states and terminates gracefully

pub mod agent_loop;
pub mod budget;
pub mod cache_sharing;
pub mod continuation;
pub mod control_plane;
pub mod dream;
pub mod error_hints;
pub mod instruction_resolver;
pub mod instructions;
pub mod llm_trait;
pub mod memory_enrichment;
pub mod message_queue;
pub mod reflection;
pub mod routing;
pub mod run_context;
pub mod session_logger;
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
    llm_trait::{LLMProvider, LLMResponse},
    run_context::AgentRunContext,
};

#[cfg(test)]
pub(crate) mod tests {
    use std::sync::Arc;

    /// Create a mock LLM provider for stuck detection tests.
    pub fn mock_llm() -> Arc<dyn crate::llm_trait::LLMProvider> {
        Arc::new(ava_llm::providers::mock::MockProvider::new("mock", vec![]))
    }
}
