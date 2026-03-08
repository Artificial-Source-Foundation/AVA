//! AVA Agent — core agent execution loop with tool calling and stuck detection.
//!
//! This crate implements the main agent loop that:
//! - Sends messages to LLM providers via `ava-llm`
//! - Parses and executes tool calls via `ava-tools`
//! - Detects stuck states and terminates gracefully

pub mod agent_loop;
pub mod llm_trait;
pub mod reflection;
pub mod stack;
pub mod stuck;
pub mod system_prompt;

/// Reflection loop primitives for error analysis and auto-fix retries.
pub use reflection::{ErrorKind, ReflectionAgent, ReflectionLoop, ToolExecutor, ToolResult};
pub use {agent_loop::*, llm_trait::{LLMProvider, LLMResponse}};

/// Returns whether the agent crate is reachable and responsive.
pub fn healthcheck() -> bool {
    true
}

#[cfg(test)]
pub(crate) mod tests {
    use super::healthcheck;
    use std::sync::Arc;

    #[test]
    fn healthcheck_returns_true() {
        assert!(healthcheck());
    }

    /// Create a mock LLM provider for stuck detection tests.
    pub fn mock_llm() -> Arc<dyn crate::llm_trait::LLMProvider> {
        Arc::new(ava_llm::providers::MockProvider::new("mock", vec![]))
    }
}
