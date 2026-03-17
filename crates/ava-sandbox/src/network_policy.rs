//! Network access policy for sandboxed commands (BG2-11 foundation).
//!
//! Domain-level allow/deny lists for controlling outbound network access
//! from tool processes. This is the policy layer — enforcement happens
//! via proxy env vars injected into subprocess environments.

use std::collections::HashSet;

/// Network access policy controlling which domains tool processes can reach.
#[derive(Debug, Clone)]
pub struct NetworkPolicy {
    /// Default action when a domain doesn't match any rule.
    pub default_action: NetworkAction,
    /// Domains explicitly allowed (e.g., "api.github.com", "*.npmjs.org").
    pub allow_list: HashSet<String>,
    /// Domains explicitly blocked (e.g., "evil.com").
    pub deny_list: HashSet<String>,
    /// Domains the user has been asked about and approved this session.
    approved_domains: HashSet<String>,
}

/// What to do with a network request to a given domain.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NetworkAction {
    /// Allow the connection.
    Allow,
    /// Block the connection.
    Deny,
    /// Ask the user for approval.
    Ask,
}

impl Default for NetworkPolicy {
    fn default() -> Self {
        let mut allow_list = HashSet::new();
        // Common dev infrastructure always allowed
        for domain in &[
            "github.com",
            "api.github.com",
            "registry.npmjs.org",
            "crates.io",
            "pypi.org",
            "rubygems.org",
            "pkg.go.dev",
            "localhost",
            "127.0.0.1",
            "::1",
        ] {
            allow_list.insert(domain.to_string());
        }

        Self {
            default_action: NetworkAction::Ask,
            allow_list,
            deny_list: HashSet::new(),
            approved_domains: HashSet::new(),
        }
    }
}

impl NetworkPolicy {
    /// Create a permissive policy that allows all network access.
    pub fn permissive() -> Self {
        Self {
            default_action: NetworkAction::Allow,
            allow_list: HashSet::new(),
            deny_list: HashSet::new(),
            approved_domains: HashSet::new(),
        }
    }

    /// Create a restrictive policy that blocks all network access.
    pub fn restrictive() -> Self {
        Self {
            default_action: NetworkAction::Deny,
            allow_list: HashSet::new(),
            deny_list: HashSet::new(),
            approved_domains: HashSet::new(),
        }
    }

    /// Check whether a domain is allowed, denied, or needs asking.
    pub fn check_domain(&self, domain: &str) -> NetworkAction {
        let normalized = domain.to_lowercase();

        // Explicit deny takes highest priority
        if self.matches_list(&normalized, &self.deny_list) {
            return NetworkAction::Deny;
        }

        // Explicit allow
        if self.matches_list(&normalized, &self.allow_list) {
            return NetworkAction::Allow;
        }

        // Session-approved domains
        if self.approved_domains.contains(&normalized) {
            return NetworkAction::Allow;
        }

        self.default_action
    }

    /// Record that the user approved a domain for this session.
    pub fn approve_domain(&mut self, domain: &str) {
        self.approved_domains.insert(domain.to_lowercase());
    }

    /// Add a domain to the allow list.
    pub fn allow_domain(&mut self, domain: &str) {
        self.allow_list.insert(domain.to_lowercase());
    }

    /// Add a domain to the deny list.
    pub fn deny_domain(&mut self, domain: &str) {
        self.deny_list.insert(domain.to_lowercase());
    }

    /// Check if a domain matches any pattern in the given set.
    /// Supports wildcard prefixes: "*.example.com" matches "sub.example.com".
    fn matches_list(&self, domain: &str, list: &HashSet<String>) -> bool {
        // Direct match
        if list.contains(domain) {
            return true;
        }

        // Wildcard match: "*.example.com" matches "sub.example.com"
        for pattern in list {
            if let Some(suffix) = pattern.strip_prefix("*.") {
                if domain.ends_with(suffix) && domain.len() > suffix.len() {
                    return true;
                }
            }
        }

        false
    }

    /// Generate environment variables to inject into a subprocess
    /// for proxy-based network enforcement.
    pub fn to_env_vars(&self, proxy_addr: &str) -> Vec<(String, String)> {
        vec![
            ("http_proxy".to_string(), proxy_addr.to_string()),
            ("https_proxy".to_string(), proxy_addr.to_string()),
            ("HTTP_PROXY".to_string(), proxy_addr.to_string()),
            ("HTTPS_PROXY".to_string(), proxy_addr.to_string()),
            // no_proxy for always-allowed local addresses
            (
                "no_proxy".to_string(),
                "localhost,127.0.0.1,::1".to_string(),
            ),
            (
                "NO_PROXY".to_string(),
                "localhost,127.0.0.1,::1".to_string(),
            ),
        ]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_allows_common_dev_domains() {
        let policy = NetworkPolicy::default();
        assert_eq!(policy.check_domain("github.com"), NetworkAction::Allow);
        assert_eq!(
            policy.check_domain("registry.npmjs.org"),
            NetworkAction::Allow
        );
        assert_eq!(policy.check_domain("crates.io"), NetworkAction::Allow);
        assert_eq!(policy.check_domain("localhost"), NetworkAction::Allow);
    }

    #[test]
    fn default_asks_for_unknown_domains() {
        let policy = NetworkPolicy::default();
        assert_eq!(
            policy.check_domain("unknown-server.com"),
            NetworkAction::Ask
        );
    }

    #[test]
    fn deny_list_takes_priority() {
        let mut policy = NetworkPolicy::default();
        policy.allow_domain("example.com");
        policy.deny_domain("example.com");
        assert_eq!(policy.check_domain("example.com"), NetworkAction::Deny);
    }

    #[test]
    fn wildcard_matching() {
        let mut policy = NetworkPolicy::default();
        policy.allow_domain("*.example.com");
        assert_eq!(policy.check_domain("sub.example.com"), NetworkAction::Allow);
        assert_eq!(
            policy.check_domain("deep.sub.example.com"),
            NetworkAction::Allow
        );
        // The root domain itself should not match the wildcard
        assert_ne!(policy.check_domain("example.com"), NetworkAction::Allow);
    }

    #[test]
    fn session_approval_persists() {
        let mut policy = NetworkPolicy::default();
        assert_eq!(policy.check_domain("new-api.com"), NetworkAction::Ask);
        policy.approve_domain("new-api.com");
        assert_eq!(policy.check_domain("new-api.com"), NetworkAction::Allow);
    }

    #[test]
    fn permissive_allows_everything() {
        let policy = NetworkPolicy::permissive();
        assert_eq!(policy.check_domain("anything.com"), NetworkAction::Allow);
    }

    #[test]
    fn restrictive_blocks_everything() {
        let policy = NetworkPolicy::restrictive();
        assert_eq!(policy.check_domain("anything.com"), NetworkAction::Deny);
    }

    #[test]
    fn case_insensitive() {
        let mut policy = NetworkPolicy::default();
        policy.allow_domain("API.Example.COM");
        assert_eq!(policy.check_domain("api.example.com"), NetworkAction::Allow);
    }

    #[test]
    fn env_vars_for_proxy() {
        let policy = NetworkPolicy::default();
        let vars = policy.to_env_vars("http://127.0.0.1:8080");
        assert_eq!(vars.len(), 6);
        assert!(vars
            .iter()
            .any(|(k, v)| k == "http_proxy" && v == "http://127.0.0.1:8080"));
        assert!(vars
            .iter()
            .any(|(k, v)| k == "no_proxy" && v.contains("localhost")));
    }
}
