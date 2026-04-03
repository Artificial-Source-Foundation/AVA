//! On-demand LSP runtime scaffolding for AVA.
//!
//! The crate is intentionally minimal today so the workspace can resolve while
//! the LSP integration is built out incrementally.

/// Returns the current readiness state for the on-demand LSP runtime.
pub fn readiness() -> &'static str {
    "stub"
}

#[cfg(test)]
mod tests {
    use super::readiness;

    #[test]
    fn reports_stub_readiness() {
        assert_eq!(readiness(), "stub");
    }
}
