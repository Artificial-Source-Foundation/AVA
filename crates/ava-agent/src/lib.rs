//! AVA Agent — core agent execution loop with tool calling and stuck detection.
//!
//! This crate implements the main agent loop that:
//! - Sends messages to LLM providers via `ava-llm`
//! - Parses and executes tool calls via `ava-tools`
//! - Detects stuck states and terminates gracefully

pub mod agent_loop;
pub(crate) mod budget;
pub mod instruction_resolver;
pub mod instructions;
pub mod llm_trait;
pub(crate) mod memory_enrichment;
pub mod message_queue;
pub mod reflection;
pub mod reviewer;
pub mod routing;
pub mod stack;
pub mod stuck;
pub mod system_prompt;
pub mod trace;
pub mod turn_diff;

pub use instructions::{
    contextual_instructions_for_file, load_project_instructions, trim_instructions_to_budget,
};
/// Reflection loop primitives for error analysis and auto-fix retries.
pub use reflection::{ErrorKind, ReflectionAgent, ReflectionLoop, ToolExecutor, ToolResult};
pub use {
    agent_loop::*,
    llm_trait::{LLMProvider, LLMResponse},
};

#[cfg(test)]
pub(crate) mod tests {
    use std::sync::Arc;

    /// Create a mock LLM provider for stuck detection tests.
    pub fn mock_llm() -> Arc<dyn crate::llm_trait::LLMProvider> {
        Arc::new(ava_llm::providers::MockProvider::new("mock", vec![]))
    }
}
