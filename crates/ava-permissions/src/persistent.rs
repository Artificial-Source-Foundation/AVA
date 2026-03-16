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
    /// Tools that are always blocked.
    #[serde(default)]
    pub blocked_tools: HashSet<String>,
    /// Specific bash commands that are always blocked.
    #[serde(default)]
    pub blocked_commands: HashSet<String>,
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

    /// Load project-local rules from `.ava/permissions.toml`.
    ///
    /// SEC-4: Project-local rules can only *restrict* (add blocked tools/commands),
    /// never *expand* permissions (allow tools/commands). This prevents a malicious
    /// repository from shipping a `.ava/permissions.toml` that auto-approves
    /// dangerous tools.
    pub fn load_project(workspace_root: &Path) -> Self {
        let mut rules = Self::load(workspace_root);
        // Project-local rules can only restrict, never expand permissions
        rules.allowed_tools.clear();
        rules.allowed_commands.clear();
        rules
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

    /// Check whether a tool is persistently allowed (and not blocked).
    pub fn is_tool_allowed(&self, tool: &str) -> bool {
        !self.blocked_tools.contains(tool) && self.allowed_tools.contains(tool)
    }

    /// Check whether a tool is persistently blocked.
    pub fn is_tool_blocked(&self, tool: &str) -> bool {
        self.blocked_tools.contains(tool)
    }

    /// Check whether a bash command matches any persistently allowed command prefix
    /// (and is not blocked).
    pub fn is_command_allowed(&self, command: &str) -> bool {
        if self.is_command_blocked(command) {
            return false;
        }
        self.allowed_commands
            .iter()
            .any(|c| command.starts_with(c.as_str()))
    }

    /// Check whether a bash command matches any persistently blocked command prefix.
    pub fn is_command_blocked(&self, command: &str) -> bool {
        self.blocked_commands
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
    fn blocked_tool_overrides_allowed() {
        let mut rules = PersistentRules::default();
        rules.allow_tool("bash");
        rules.blocked_tools.insert("bash".to_string());
        // Blocked takes precedence over allowed
        assert!(!rules.is_tool_allowed("bash"));
        assert!(rules.is_tool_blocked("bash"));
    }

    #[test]
    fn blocked_command_overrides_allowed() {
        let mut rules = PersistentRules::default();
        rules.allow_command("rm");
        rules.blocked_commands.insert("rm".to_string());
        assert!(!rules.is_command_allowed("rm -rf /"));
        assert!(rules.is_command_blocked("rm -rf /"));
    }

    #[test]
    fn load_project_clears_allowlists() {
        let dir = std::env::temp_dir().join("ava_persistent_project_test");
        let _ = std::fs::remove_dir_all(&dir);

        let mut rules = PersistentRules::default();
        rules.allow_tool("bash");
        rules.allow_command("rm -rf /");
        rules.blocked_tools.insert("write".to_string());
        rules.blocked_commands.insert("sudo".to_string());
        rules.save(&dir).expect("save should succeed");

        let loaded = PersistentRules::load_project(&dir);
        // Allowlists cleared for project-local rules
        assert!(loaded.allowed_tools.is_empty());
        assert!(loaded.allowed_commands.is_empty());
        // Blocklists preserved
        assert!(loaded.is_tool_blocked("write"));
        assert!(loaded.is_command_blocked("sudo rm"));

        let _ = std::fs::remove_dir_all(&dir);
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
