use std::collections::HashSet;
use std::path::Path;

/// Persistent permission rules saved to `.ava/permissions.toml`.
///
/// These survive across sessions — when a user selects "Allow always" in the
/// approval dock, the rule is written here and auto-loaded on next startup.
#[derive(Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct PersistentRules {
    /// Tools that are always allowed (e.g., "bash", "task").
    #[serde(default)]
    pub allowed_tools: HashSet<String>,
    /// Specific bash commands that are always allowed (e.g., "cargo test").
    #[serde(default)]
    pub allowed_commands: HashSet<String>,
}

impl PersistentRules {
    /// Load persistent rules from `.ava/permissions.toml` under the given workspace root.
    /// Returns default (empty) rules if the file doesn't exist or can't be parsed.
    pub fn load(workspace_root: &Path) -> Self {
        let path = workspace_root.join(".ava/permissions.toml");
        std::fs::read_to_string(&path)
            .ok()
            .and_then(|s| toml::from_str(&s).ok())
            .unwrap_or_default()
    }

    /// Save persistent rules to `.ava/permissions.toml` under the given workspace root.
    pub fn save(&self, workspace_root: &Path) -> std::io::Result<()> {
        let path = workspace_root.join(".ava/permissions.toml");
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let content = toml::to_string_pretty(self).map_err(std::io::Error::other)?;
        std::fs::write(&path, content)
    }

    /// Mark a tool as always allowed.
    pub fn allow_tool(&mut self, tool: &str) {
        self.allowed_tools.insert(tool.to_string());
    }

    /// Mark a specific bash command prefix as always allowed.
    pub fn allow_command(&mut self, command: &str) {
        self.allowed_commands.insert(command.to_string());
    }

    /// Check whether a tool is persistently allowed.
    pub fn is_tool_allowed(&self, tool: &str) -> bool {
        self.allowed_tools.contains(tool)
    }

    /// Check whether a bash command matches any persistently allowed command prefix.
    pub fn is_command_allowed(&self, command: &str) -> bool {
        self.allowed_commands
            .iter()
            .any(|c| command.starts_with(c.as_str()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    #[test]
    fn load_missing_file_returns_default() {
        let rules = PersistentRules::load(&PathBuf::from("/nonexistent/path"));
        assert!(rules.allowed_tools.is_empty());
        assert!(rules.allowed_commands.is_empty());
    }

    #[test]
    fn allow_tool_and_check() {
        let mut rules = PersistentRules::default();
        assert!(!rules.is_tool_allowed("bash"));
        rules.allow_tool("bash");
        assert!(rules.is_tool_allowed("bash"));
    }

    #[test]
    fn allow_command_prefix_matching() {
        let mut rules = PersistentRules::default();
        rules.allow_command("cargo test");
        assert!(rules.is_command_allowed("cargo test --workspace"));
        assert!(rules.is_command_allowed("cargo test"));
        assert!(!rules.is_command_allowed("cargo build"));
    }

    #[test]
    fn roundtrip_save_load() {
        let dir = std::env::temp_dir().join("ava_persistent_rules_test");
        let _ = std::fs::remove_dir_all(&dir);

        let mut rules = PersistentRules::default();
        rules.allow_tool("bash");
        rules.allow_tool("task");
        rules.allow_command("cargo test");
        rules.allow_command("npm run");
        rules.save(&dir).expect("save should succeed");

        let loaded = PersistentRules::load(&dir);
        assert!(loaded.is_tool_allowed("bash"));
        assert!(loaded.is_tool_allowed("task"));
        assert!(loaded.is_command_allowed("cargo test --workspace"));
        assert!(loaded.is_command_allowed("npm run build"));
        assert!(!loaded.is_tool_allowed("write"));

        let _ = std::fs::remove_dir_all(&dir);
    }
}
