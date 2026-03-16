use std::collections::HashSet;
use std::path::{Path, PathBuf};

/// Persistent permission rules saved to `~/.ava/permissions.toml` (user-global).
///
/// These survive across sessions — when a user selects "Allow always" in the
/// approval dock, the rule is written here and auto-loaded on next startup.
///
/// SEC: Allowlists are stored in the user-global path (`~/.ava/permissions.toml`)
/// to prevent malicious repositories from pre-populating allowlists via a
/// repo-local `.ava/permissions.toml`. Project-local rules (via `load_project`)
/// can only *restrict* (add blocked tools/commands), never expand permissions.
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
    /// Return the user-global permissions path: `~/.ava/permissions.toml`.
    fn global_path() -> PathBuf {
        dirs::home_dir()
            .unwrap_or_default()
            .join(".ava/permissions.toml")
    }

    /// Load persistent rules from `~/.ava/permissions.toml` (user-global).
    /// Returns default (empty) rules if the file doesn't exist or can't be parsed.
    ///
    /// SEC: This reads from the user's home directory, NOT from the repository,
    /// so a malicious repo cannot pre-populate allowlists.
    pub fn load() -> Self {
        Self::load_from(&Self::global_path())
    }

    /// Load rules from an arbitrary path (used internally and for testing).
    fn load_from(path: &Path) -> Self {
        std::fs::read_to_string(path)
            .ok()
            .and_then(|s| toml::from_str(&s).ok())
            .unwrap_or_default()
    }

    /// Load project-local rules from `.ava/permissions.toml` under the workspace root.
    ///
    /// SEC-4: Project-local rules can only *restrict* (add blocked tools/commands),
    /// never *expand* permissions (allow tools/commands). This prevents a malicious
    /// repository from shipping a `.ava/permissions.toml` that auto-approves
    /// dangerous tools.
    pub fn load_project(workspace_root: &Path) -> Self {
        let path = workspace_root.join(".ava/permissions.toml");
        let mut rules = Self::load_from(&path);
        // Project-local rules can only restrict, never expand permissions
        rules.allowed_tools.clear();
        rules.allowed_commands.clear();
        rules
    }

    /// Merge project-local restrictions into the user-global rules.
    /// This loads user-global allowlists and then overlays project-local blocklists.
    pub fn load_merged(workspace_root: &Path) -> Self {
        let mut global = Self::load();
        let project = Self::load_project(workspace_root);
        // Overlay project-local blocklists onto global rules
        global.blocked_tools.extend(project.blocked_tools);
        global.blocked_commands.extend(project.blocked_commands);
        global
    }

    /// Save persistent rules to `~/.ava/permissions.toml` (user-global).
    ///
    /// SEC: Always writes to the user's home directory, never to the repository.
    pub fn save(&self) -> std::io::Result<()> {
        let path = Self::global_path();
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let content = toml::to_string_pretty(self).map_err(std::io::Error::other)?;
        std::fs::write(&path, content)
    }

    /// Save persistent rules to a specific path (for testing).
    #[cfg(test)]
    pub fn save_to(&self, path: &Path) -> std::io::Result<()> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let content = toml::to_string_pretty(self).map_err(std::io::Error::other)?;
        std::fs::write(path, content)
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

    #[test]
    fn load_missing_file_returns_default() {
        let rules =
            PersistentRules::load_from(&PathBuf::from("/nonexistent/path/permissions.toml"));
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

        // Write to the project-local path
        let project_path = dir.join(".ava/permissions.toml");
        rules.save_to(&project_path).expect("save should succeed");

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
        let path = dir.join("permissions.toml");

        let mut rules = PersistentRules::default();
        rules.allow_tool("bash");
        rules.allow_tool("task");
        rules.allow_command("cargo test");
        rules.allow_command("npm run");
        rules.save_to(&path).expect("save should succeed");

        let loaded = PersistentRules::load_from(&path);
        assert!(loaded.is_tool_allowed("bash"));
        assert!(loaded.is_tool_allowed("task"));
        assert!(loaded.is_command_allowed("cargo test --workspace"));
        assert!(loaded.is_command_allowed("npm run build"));
        assert!(!loaded.is_tool_allowed("write"));

        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn merged_rules_combine_global_allows_and_project_blocks() {
        let dir = std::env::temp_dir().join("ava_persistent_merged_test");
        let _ = std::fs::remove_dir_all(&dir);

        // Write project-local blocklist
        let mut project_rules = PersistentRules::default();
        project_rules.blocked_tools.insert("bash".to_string());
        let project_path = dir.join(".ava/permissions.toml");
        project_rules
            .save_to(&project_path)
            .expect("save should succeed");

        // load_merged reads global + project blocks
        let merged = PersistentRules::load_merged(&dir);
        assert!(merged.is_tool_blocked("bash"));

        let _ = std::fs::remove_dir_all(&dir);
    }
}
