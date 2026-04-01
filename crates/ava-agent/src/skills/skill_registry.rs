//! Skill registry for discovering and looking up skills by name or alias.

use std::collections::HashMap;

use super::skill_def::SkillDefinition;

/// Registry of available skills, supporting lookup by name and alias.
#[derive(Debug, Default)]
pub struct SkillRegistry {
    skills: HashMap<String, SkillDefinition>,
    /// Maps aliases to canonical skill names for fast lookup.
    alias_index: HashMap<String, String>,
}

impl SkillRegistry {
    /// Create an empty skill registry.
    pub fn new() -> Self {
        Self::default()
    }

    /// Register a skill definition. Overwrites any existing skill with the same name.
    pub fn register(&mut self, skill: SkillDefinition) {
        // Remove old alias entries if overwriting
        if let Some(old) = self.skills.get(&skill.name) {
            for alias in &old.aliases {
                self.alias_index.remove(&alias.to_lowercase());
            }
        }

        // Index aliases
        for alias in &skill.aliases {
            self.alias_index
                .insert(alias.to_lowercase(), skill.name.clone());
        }

        self.skills.insert(skill.name.clone(), skill);
    }

    /// Look up a skill by exact name or alias.
    pub fn get(&self, name: &str) -> Option<&SkillDefinition> {
        let lower = name.to_lowercase();
        // Try direct name first
        if let Some(skill) = self.skills.get(&lower) {
            return Some(skill);
        }
        // Check case-insensitive name match
        for skill in self.skills.values() {
            if skill.name.to_lowercase() == lower {
                return Some(skill);
            }
        }
        // Try alias
        if let Some(canonical) = self.alias_index.get(&lower) {
            return self.skills.get(canonical);
        }
        None
    }

    /// List all registered skills sorted by name.
    pub fn list(&self) -> Vec<&SkillDefinition> {
        let mut skills: Vec<&SkillDefinition> = self.skills.values().collect();
        skills.sort_by(|a, b| a.name.cmp(&b.name));
        skills
    }

    /// Search for skills matching a query string (checks name, aliases, description).
    pub fn search(&self, query: &str) -> Vec<&SkillDefinition> {
        let q = query.to_lowercase();
        let mut results: Vec<&SkillDefinition> = self
            .skills
            .values()
            .filter(|s| {
                s.name.to_lowercase().contains(&q)
                    || s.description.to_lowercase().contains(&q)
                    || s.aliases.iter().any(|a| a.to_lowercase().contains(&q))
            })
            .collect();
        results.sort_by(|a, b| a.name.cmp(&b.name));
        results
    }

    /// Number of registered skills.
    pub fn len(&self) -> usize {
        self.skills.len()
    }

    /// Whether the registry is empty.
    pub fn is_empty(&self) -> bool {
        self.skills.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::skills::skill_def::SkillContext;

    fn make_skill(name: &str, aliases: &[&str]) -> SkillDefinition {
        SkillDefinition {
            name: name.to_string(),
            description: format!("{name} skill"),
            aliases: aliases.iter().map(|s| s.to_string()).collect(),
            allowed_tools: vec![],
            model: None,
            context: SkillContext::Inline,
            prompt_template: format!("Run {name}: {{args}}"),
        }
    }

    #[test]
    fn register_and_get() {
        let mut reg = SkillRegistry::new();
        reg.register(make_skill("debug", &["dbg"]));

        assert!(reg.get("debug").is_some());
        assert_eq!(reg.get("debug").unwrap().name, "debug");
    }

    #[test]
    fn lookup_by_alias() {
        let mut reg = SkillRegistry::new();
        reg.register(make_skill("debug", &["dbg", "troubleshoot"]));

        assert!(reg.get("dbg").is_some());
        assert_eq!(reg.get("dbg").unwrap().name, "debug");
        assert!(reg.get("troubleshoot").is_some());
    }

    #[test]
    fn lookup_case_insensitive() {
        let mut reg = SkillRegistry::new();
        reg.register(make_skill("debug", &["dbg"]));

        assert!(reg.get("Debug").is_some());
        assert!(reg.get("DBG").is_some());
    }

    #[test]
    fn list_sorted() {
        let mut reg = SkillRegistry::new();
        reg.register(make_skill("verify", &[]));
        reg.register(make_skill("debug", &[]));
        reg.register(make_skill("review", &[]));

        let names: Vec<&str> = reg.list().iter().map(|s| s.name.as_str()).collect();
        assert_eq!(names, vec!["debug", "review", "verify"]);
    }

    #[test]
    fn search_by_name_and_description() {
        let mut reg = SkillRegistry::new();
        reg.register(make_skill("debug", &[]));
        reg.register(make_skill("verify", &[]));

        let results = reg.search("debug");
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].name, "debug");

        // Search in description
        let results = reg.search("skill");
        assert_eq!(results.len(), 2);
    }

    #[test]
    fn unknown_skill_returns_none() {
        let reg = SkillRegistry::new();
        assert!(reg.get("nonexistent").is_none());
    }
}
