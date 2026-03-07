pub mod browser;
pub mod core;
pub mod edit;
pub mod git;
pub mod mcp_bridge;
pub mod monitor;
pub mod permission_middleware;
pub mod registry;

pub fn healthcheck() -> bool {
    true
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn healthcheck_returns_true() {
        assert!(healthcheck());
    }
}
