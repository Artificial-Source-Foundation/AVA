mod reflection;

/// Reflection loop primitives for error analysis and auto-fix retries.
pub use reflection::{ErrorKind, ReflectionAgent, ReflectionLoop, ToolExecutor, ToolResult};

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
