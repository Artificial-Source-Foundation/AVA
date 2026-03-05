#[path = "loop.rs"]
pub mod agent_loop;
pub mod llm_trait;
pub mod reflection;

/// Reflection loop primitives for error analysis and auto-fix retries.
pub use reflection::{ErrorKind, ReflectionAgent, ReflectionLoop, ToolExecutor, ToolResult};
pub use {agent_loop::*, llm_trait::LLMProvider};

/// Returns whether the agent crate is reachable and responsive.
pub fn healthcheck() -> bool {
    true
}

#[cfg(test)]
mod tests {
    use super::healthcheck;

    #[test]
    fn healthcheck_returns_true() {
        assert!(healthcheck());
    }
}
