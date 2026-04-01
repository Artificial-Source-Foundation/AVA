//! Skill definition types for the bundled skills system.

use serde::{Deserialize, Serialize};

/// How a skill executes when invoked.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SkillContext {
    /// Expand the prompt inline in the current conversation — the agent
    /// continues with the skill's prompt as additional context.
    Inline,
    /// Fork a sub-agent that runs the skill's prompt independently,
    /// returning the result to the parent conversation.
    Fork,
}

/// A skill definition describing a reusable agent workflow/recipe.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SkillDefinition {
    /// Unique skill name (e.g. "debug", "verify").
    pub name: String,
    /// Human-readable description shown in listings and help.
    pub description: String,
    /// Alternative names that resolve to this skill.
    pub aliases: Vec<String>,
    /// Tool names this skill is allowed to use (empty = all tools).
    pub allowed_tools: Vec<String>,
    /// Preferred model override (empty = use current model).
    pub model: Option<String>,
    /// How the skill executes.
    pub context: SkillContext,
    /// Prompt template injected when the skill is invoked.
    /// May contain `{args}` placeholder for user-supplied arguments.
    pub prompt_template: String,
}

impl SkillDefinition {
    /// Expand the prompt template with the given arguments.
    pub fn expand_prompt(&self, args: &str) -> String {
        self.prompt_template.replace("{args}", args)
    }

    /// Check whether this skill matches a given name or alias.
    pub fn matches(&self, query: &str) -> bool {
        let q = query.to_lowercase();
        self.name.to_lowercase() == q || self.aliases.iter().any(|a| a.to_lowercase() == q)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn expand_prompt_replaces_args() {
        let skill = SkillDefinition {
            name: "test".to_string(),
            description: "test skill".to_string(),
            aliases: vec![],
            allowed_tools: vec![],
            model: None,
            context: SkillContext::Inline,
            prompt_template: "Do {args} carefully.".to_string(),
        };

        assert_eq!(skill.expand_prompt("the thing"), "Do the thing carefully.");
    }

    #[test]
    fn matches_name_and_alias() {
        let skill = SkillDefinition {
            name: "debug".to_string(),
            description: "debugging".to_string(),
            aliases: vec!["dbg".to_string(), "troubleshoot".to_string()],
            allowed_tools: vec![],
            model: None,
            context: SkillContext::Inline,
            prompt_template: String::new(),
        };

        assert!(skill.matches("debug"));
        assert!(skill.matches("Debug"));
        assert!(skill.matches("dbg"));
        assert!(skill.matches("troubleshoot"));
        assert!(!skill.matches("unknown"));
    }
}
