//! Bundled skills and commands system.
//!
//! Skills are reusable agent workflow recipes (debug, verify, review, etc.)
//! that can be invoked by name or alias. Each skill has a prompt template
//! and an execution context (inline or fork).

pub mod bundled;
pub mod skill_def;
pub mod skill_registry;

pub use bundled::register_bundled_skills;
pub use skill_def::{SkillContext, SkillDefinition};
pub use skill_registry::SkillRegistry;

/// Format the available skills as a human-readable listing suitable for
/// injection into the agent's turn context or display in the TUI.
pub fn get_skill_listing(registry: &SkillRegistry) -> String {
    let skills = registry.list();
    if skills.is_empty() {
        return "No skills available.".to_string();
    }

    let mut out = String::from("Available skills:\n");
    for skill in skills {
        let aliases = if skill.aliases.is_empty() {
            String::new()
        } else {
            format!(" (aliases: {})", skill.aliases.join(", "))
        };
        let mode = match skill.context {
            SkillContext::Inline => "inline",
            SkillContext::Fork => "fork",
        };
        out.push_str(&format!(
            "  /skill {} — {} [{}]{}\n",
            skill.name, skill.description, mode, aliases
        ));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn skill_listing_format() {
        let mut registry = SkillRegistry::new();
        register_bundled_skills(&mut registry);

        let listing = get_skill_listing(&registry);
        assert!(listing.contains("Available skills:"));
        assert!(listing.contains("/skill debug"));
        assert!(listing.contains("/skill verify"));
        assert!(listing.contains("[inline]"));
        assert!(listing.contains("[fork]"));
        assert!(listing.contains("(aliases:"));
    }

    #[test]
    fn empty_registry_listing() {
        let registry = SkillRegistry::new();
        let listing = get_skill_listing(&registry);
        assert_eq!(listing, "No skills available.");
    }
}
