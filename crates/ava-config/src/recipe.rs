//! Recipe system — load reusable agent configurations from TOML files.
//!
//! Recipes define pre-packaged agent invocations with a prompt, model,
//! max turns, and tool selections.

use serde::{Deserialize, Serialize};
use std::path::Path;

/// A recipe defining a reusable agent configuration.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Recipe {
    /// Recipe name (e.g., "code-review").
    pub name: String,
    /// Human-readable description of what the recipe does.
    #[serde(default)]
    pub description: String,
    /// The system prompt or user prompt to send to the agent.
    pub prompt: String,
    /// Override model for this recipe (e.g., "anthropic/claude-sonnet-4").
    #[serde(default)]
    pub model: Option<String>,
    /// Maximum number of agent turns.
    #[serde(default)]
    pub max_turns: Option<u32>,
    /// List of tool names to enable (empty = use defaults).
    #[serde(default)]
    pub tools: Vec<String>,
}

/// Load a recipe from a TOML file.
pub fn load_recipe(path: &Path) -> Result<Recipe, String> {
    let content = std::fs::read_to_string(path)
        .map_err(|e| format!("Failed to read {}: {}", path.display(), e))?;
    toml::from_str(&content).map_err(|e| format!("Failed to parse {}: {}", path.display(), e))
}

/// List all recipes in a directory by reading `.toml` files.
///
/// Skips files that fail to parse and logs a warning.
pub fn list_recipes(dir: &Path) -> Vec<Recipe> {
    let mut recipes = Vec::new();

    let Ok(entries) = std::fs::read_dir(dir) else {
        return recipes;
    };

    for entry in entries.flatten() {
        let path = entry.path();
        let is_toml = path
            .extension()
            .and_then(|e| e.to_str())
            .map(|e| e == "toml")
            .unwrap_or(false);

        if path.is_file() && is_toml {
            match load_recipe(&path) {
                Ok(recipe) => recipes.push(recipe),
                Err(e) => {
                    tracing::warn!("Skipping recipe {}: {}", path.display(), e);
                }
            }
        }
    }

    recipes.sort_by(|a, b| a.name.cmp(&b.name));
    recipes
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn load_recipe_from_toml() {
        let dir = TempDir::new().unwrap();
        let path = dir.path().join("review.toml");
        std::fs::write(
            &path,
            r#"
name = "code-review"
description = "Review code for issues"
prompt = "Review the following code for bugs and style issues."
model = "anthropic/claude-sonnet-4"
max_turns = 5
tools = ["read", "grep", "glob"]
"#,
        )
        .unwrap();

        let recipe = load_recipe(&path).unwrap();
        assert_eq!(recipe.name, "code-review");
        assert_eq!(recipe.tools.len(), 3);
        assert_eq!(recipe.max_turns, Some(5));
    }

    #[test]
    fn load_recipe_missing_file_returns_error() {
        let result = load_recipe(Path::new("/nonexistent/recipe.toml"));
        assert!(result.is_err());
    }

    #[test]
    fn list_recipes_from_directory() {
        let dir = TempDir::new().unwrap();

        std::fs::write(
            dir.path().join("a_recipe.toml"),
            r#"
name = "alpha"
prompt = "Do alpha things"
"#,
        )
        .unwrap();

        std::fs::write(
            dir.path().join("b_recipe.toml"),
            r#"
name = "beta"
prompt = "Do beta things"
"#,
        )
        .unwrap();

        // Non-TOML file should be ignored
        std::fs::write(dir.path().join("readme.md"), "# Recipes").unwrap();

        let recipes = list_recipes(dir.path());
        assert_eq!(recipes.len(), 2);
        assert_eq!(recipes[0].name, "alpha");
        assert_eq!(recipes[1].name, "beta");
    }
}
