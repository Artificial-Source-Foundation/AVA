pub mod browser;
pub mod edit;
pub mod git;

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
