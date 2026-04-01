//! System prompts for HQ multi-agent roles.
//!
//! The canonical prompts now live in [`ava_config::hq_roles`] as part of
//! [`AgentRoleProfile`] default profiles.  The functions here are thin wrappers
//! that resolve the default profile and apply template variables, preserving
//! the existing public API for callers that don't yet use the profile system.

use ava_config::{
    apply_template_vars, default_role_profiles, ROLE_DIRECTOR, ROLE_JUNIOR_WORKER, ROLE_SCOUT,
    ROLE_SENIOR_LEAD,
};

use crate::Domain;

/// Returns a human-readable label for a domain (e.g. "Backend", "QA").
pub fn domain_label(domain: &Domain) -> &'static str {
    match domain {
        Domain::Frontend => "Frontend",
        Domain::Backend => "Backend",
        Domain::QA => "QA",
        Domain::Research => "Research",
        Domain::Debug => "Debug",
        Domain::Fullstack => "Fullstack",
        Domain::DevOps => "DevOps",
    }
}

/// System prompt for the Director agent (AVA).
pub fn director_system_prompt() -> String {
    let profiles = default_role_profiles();
    profiles[ROLE_DIRECTOR].system_prompt.clone()
}

/// System prompt for a Lead agent in the given domain.
pub fn lead_system_prompt(domain: &str) -> String {
    let profiles = default_role_profiles();
    let profile = apply_template_vars(&profiles[ROLE_SENIOR_LEAD], &[("domain", domain)]);
    profile.system_prompt
}

/// System prompt for a Worker agent.
pub fn worker_system_prompt(name: &str, domain: &str) -> String {
    let profiles = default_role_profiles();
    let profile = apply_template_vars(
        &profiles[ROLE_JUNIOR_WORKER],
        &[("name", name), ("domain", domain)],
    );
    profile.system_prompt
}

/// System prompt for a Scout agent.
pub fn scout_system_prompt() -> String {
    let profiles = default_role_profiles();
    profiles[ROLE_SCOUT].system_prompt.clone()
}

/// Convenience: generate a lead system prompt from a [`Domain`].
pub fn lead_system_prompt_for_domain(domain: &Domain) -> String {
    lead_system_prompt(domain_label(domain))
}

/// Convenience: generate a worker system prompt from a name and [`Domain`].
pub fn worker_system_prompt_for_domain(name: &str, domain: &Domain) -> String {
    worker_system_prompt(name, domain_label(domain))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn director_prompt_is_ava() {
        let prompt = director_system_prompt();
        assert!(!prompt.is_empty());
        assert!(prompt.contains("AVA"));
        assert!(prompt.contains("operational partner"));
        assert!(prompt.contains("delegate"));
    }

    #[test]
    fn lead_prompt_includes_domain() {
        let prompt = lead_system_prompt("Backend");
        assert!(prompt.contains("Backend Lead"));
        assert!(prompt.contains("senior specialist"));
    }

    #[test]
    fn lead_prompt_for_domain_variant() {
        let prompt = lead_system_prompt_for_domain(&Domain::QA);
        assert!(prompt.contains("QA Lead"));
    }

    #[test]
    fn worker_prompt_includes_name_and_domain() {
        let prompt = worker_system_prompt("Pedro", "Backend");
        assert!(prompt.contains("Pedro"));
        assert!(prompt.contains("Backend team"));
        assert!(prompt.contains("assigned"));
    }

    #[test]
    fn worker_prompt_for_domain_variant() {
        let prompt = worker_system_prompt_for_domain("Sofia", &Domain::Frontend);
        assert!(prompt.contains("Sofia"));
        assert!(prompt.contains("Frontend team"));
    }

    #[test]
    fn scout_prompt_is_read_only() {
        let prompt = scout_system_prompt();
        assert!(prompt.contains("Scout"));
        assert!(prompt.contains("NOT modify"));
    }

    #[test]
    fn all_domains_have_labels() {
        let domains = [
            Domain::Frontend,
            Domain::Backend,
            Domain::QA,
            Domain::Research,
            Domain::Debug,
            Domain::Fullstack,
            Domain::DevOps,
        ];
        for domain in &domains {
            let label = domain_label(domain);
            assert!(!label.is_empty());
            let prompt = lead_system_prompt_for_domain(domain);
            assert!(prompt.contains(label));
        }
    }
}
