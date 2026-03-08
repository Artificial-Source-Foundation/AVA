use std::collections::{HashMap, HashSet};

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum SafetyTag {
    ReadOnly,
    WriteFile,
    DeleteFile,
    ExecuteCommand,
    NetworkAccess,
    SystemModification,
    Destructive,
    Privileged,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
pub enum RiskLevel {
    Safe,
    Low,
    Medium,
    High,
    Critical,
}

#[derive(Debug, Clone)]
pub struct ToolSafetyProfile {
    pub tool_name: String,
    pub tags: HashSet<SafetyTag>,
    pub risk_level: RiskLevel,
    pub description: String,
}

impl ToolSafetyProfile {
    pub fn new(
        tool_name: impl Into<String>,
        tags: impl IntoIterator<Item = SafetyTag>,
        risk_level: RiskLevel,
        description: impl Into<String>,
    ) -> Self {
        Self {
            tool_name: tool_name.into(),
            tags: tags.into_iter().collect(),
            risk_level,
            description: description.into(),
        }
    }
}

/// Returns default safety profiles for all registered tools (18 total).
pub fn core_tool_profiles() -> HashMap<String, ToolSafetyProfile> {
    let profiles = vec![
        // Original 6 core tools
        ToolSafetyProfile::new(
            "read",
            [SafetyTag::ReadOnly],
            RiskLevel::Safe,
            "Reads file contents",
        ),
        ToolSafetyProfile::new(
            "glob",
            [SafetyTag::ReadOnly],
            RiskLevel::Safe,
            "Searches for files by pattern",
        ),
        ToolSafetyProfile::new(
            "grep",
            [SafetyTag::ReadOnly],
            RiskLevel::Safe,
            "Searches file contents",
        ),
        ToolSafetyProfile::new(
            "write",
            [SafetyTag::WriteFile],
            RiskLevel::Low,
            "Creates or overwrites files",
        ),
        ToolSafetyProfile::new(
            "edit",
            [SafetyTag::WriteFile],
            RiskLevel::Low,
            "Edits existing files",
        ),
        ToolSafetyProfile::new(
            "bash",
            [SafetyTag::ExecuteCommand],
            RiskLevel::Medium,
            "Executes shell commands",
        ),
        // Extended tools
        ToolSafetyProfile::new(
            "multiedit",
            [SafetyTag::WriteFile],
            RiskLevel::Low,
            "Edits multiple files atomically",
        ),
        ToolSafetyProfile::new(
            "apply_patch",
            [SafetyTag::WriteFile],
            RiskLevel::Medium,
            "Applies unified diff patches to files",
        ),
        ToolSafetyProfile::new(
            "test_runner",
            [SafetyTag::ExecuteCommand],
            RiskLevel::Low,
            "Runs test suites",
        ),
        ToolSafetyProfile::new(
            "lint",
            [SafetyTag::ExecuteCommand],
            RiskLevel::Low,
            "Runs linters and formatters",
        ),
        ToolSafetyProfile::new(
            "diagnostics",
            [SafetyTag::ReadOnly],
            RiskLevel::Safe,
            "Retrieves LSP diagnostics",
        ),
        ToolSafetyProfile::new(
            "codebase_search",
            [SafetyTag::ReadOnly],
            RiskLevel::Safe,
            "Searches codebase index",
        ),
        // Memory tools
        ToolSafetyProfile::new(
            "remember",
            [SafetyTag::WriteFile],
            RiskLevel::Safe,
            "Stores a memory entry",
        ),
        ToolSafetyProfile::new(
            "recall",
            [SafetyTag::ReadOnly],
            RiskLevel::Safe,
            "Recalls stored memories",
        ),
        ToolSafetyProfile::new(
            "memory_search",
            [SafetyTag::ReadOnly],
            RiskLevel::Safe,
            "Searches stored memories",
        ),
        // Session tools
        ToolSafetyProfile::new(
            "session_search",
            [SafetyTag::ReadOnly],
            RiskLevel::Safe,
            "Searches session history",
        ),
        ToolSafetyProfile::new(
            "session_list",
            [SafetyTag::ReadOnly],
            RiskLevel::Safe,
            "Lists available sessions",
        ),
        ToolSafetyProfile::new(
            "session_load",
            [SafetyTag::ReadOnly],
            RiskLevel::Safe,
            "Loads a previous session",
        ),
    ];

    profiles
        .into_iter()
        .map(|p| (p.tool_name.clone(), p))
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn core_profiles_have_expected_risk_levels() {
        let profiles = core_tool_profiles();

        assert_eq!(profiles["read"].risk_level, RiskLevel::Safe);
        assert_eq!(profiles["glob"].risk_level, RiskLevel::Safe);
        assert_eq!(profiles["grep"].risk_level, RiskLevel::Safe);
        assert_eq!(profiles["write"].risk_level, RiskLevel::Low);
        assert_eq!(profiles["edit"].risk_level, RiskLevel::Low);
        assert_eq!(profiles["bash"].risk_level, RiskLevel::Medium);
    }

    #[test]
    fn core_profiles_have_expected_tags() {
        let profiles = core_tool_profiles();

        assert!(profiles["read"].tags.contains(&SafetyTag::ReadOnly));
        assert!(profiles["write"].tags.contains(&SafetyTag::WriteFile));
        assert!(profiles["bash"].tags.contains(&SafetyTag::ExecuteCommand));
    }

    #[test]
    fn all_core_tools_present() {
        let profiles = core_tool_profiles();
        assert_eq!(profiles.len(), 18);
        let expected = [
            "read", "glob", "grep", "write", "edit", "bash",
            "multiedit", "apply_patch", "test_runner", "lint",
            "diagnostics", "codebase_search",
            "remember", "recall", "memory_search",
            "session_search", "session_list", "session_load",
        ];
        for name in &expected {
            assert!(profiles.contains_key(*name), "missing profile for {name}");
        }
    }

    #[test]
    fn risk_level_ordering() {
        assert!(RiskLevel::Safe < RiskLevel::Low);
        assert!(RiskLevel::Low < RiskLevel::Medium);
        assert!(RiskLevel::Medium < RiskLevel::High);
        assert!(RiskLevel::High < RiskLevel::Critical);
    }

    #[test]
    fn safety_tag_serialization_roundtrip() {
        let tag = SafetyTag::Destructive;
        let json = serde_json::to_string(&tag).unwrap();
        let deserialized: SafetyTag = serde_json::from_str(&json).unwrap();
        assert_eq!(tag, deserialized);
    }
}
