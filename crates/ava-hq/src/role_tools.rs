//! Per-role tool registry and MCP server filtering.
//!
//! Builds a [`ToolRegistry`] tailored to an [`AgentRoleProfile`], ensuring
//! that each HQ agent tier only sees the tools and MCP servers it should.

use std::collections::HashSet;
use std::sync::Arc;

use ava_config::AgentRoleProfile;
use ava_platform::Platform;
use ava_tools::core::{
    bash, edit, file_backup, git_read, glob, grep, hashline, read, web_fetch, web_search, write,
};
use ava_tools::registry::ToolRegistry;

/// All built-in tool names that can be selectively registered.
pub const ALL_BUILTIN_TOOLS: &[&str] = &[
    "read",
    "write",
    "edit",
    "bash",
    "glob",
    "grep",
    "web_fetch",
    "web_search",
    "git",
];

/// Build a [`ToolRegistry`] filtered to match a role profile.
///
/// Only tools in `profile.allowed_tools` (minus `profile.denied_tools`) are
/// registered.  `["*"]` in `allowed_tools` means all built-in tools.
/// An empty `allowed_tools` also means all (tier default).
pub fn build_registry_for_role(
    profile: &AgentRoleProfile,
    platform: Arc<dyn Platform>,
) -> (ToolRegistry, file_backup::FileBackupSession) {
    let mut registry = ToolRegistry::new();
    let hashline_cache = hashline::new_cache();
    let backup_session = file_backup::new_backup_session();

    let allowed = &profile.allowed_tools;
    let denied: HashSet<&str> = profile.denied_tools.iter().map(|s| s.as_str()).collect();

    let allow_all = allowed.is_empty() || allowed.iter().any(|t| t == "*");

    let should_include = |name: &str| -> bool {
        if denied.contains(name) {
            return false;
        }
        allow_all || allowed.iter().any(|t| t == name)
    };

    if should_include("read") {
        registry.register(read::ReadTool::new(
            platform.clone(),
            hashline_cache.clone(),
        ));
    }
    if should_include("write") {
        registry.register(write::WriteTool::with_backup_session(
            platform.clone(),
            backup_session.clone(),
        ));
    }
    if should_include("edit") {
        registry.register(edit::EditTool::with_backup_session(
            platform.clone(),
            hashline_cache,
            backup_session.clone(),
        ));
    }
    if should_include("bash") {
        registry.register(bash::BashTool::new(platform.clone()));
    }
    if should_include("glob") {
        registry.register(glob::GlobTool::new());
    }
    if should_include("grep") {
        registry.register(grep::GrepTool::new());
    }
    if should_include("web_fetch") {
        registry.register(web_fetch::WebFetchTool::new());
    }
    if should_include("web_search") {
        registry.register(web_search::WebSearchTool::new());
    }
    if should_include("git") {
        registry.register(git_read::GitReadTool::new());
    }

    (registry, backup_session)
}

/// Compute the set of MCP server names that should be disabled for a role.
///
/// - `allowed_mcp_servers = []` → all servers disabled (no MCP access)
/// - `allowed_mcp_servers = ["*"]` → no servers disabled (full MCP access)
/// - `allowed_mcp_servers = ["docs", "search"]` → only those two enabled
/// - `denied_mcp_servers` is always applied on top (union exclusion)
pub fn compute_disabled_mcp_servers(
    profile: &AgentRoleProfile,
    all_server_names: &[String],
) -> HashSet<String> {
    let allowed = &profile.allowed_mcp_servers;
    let denied: HashSet<&str> = profile
        .denied_mcp_servers
        .iter()
        .map(|s| s.as_str())
        .collect();

    // Empty allowed = no MCP servers at all
    if allowed.is_empty() {
        return all_server_names.iter().cloned().collect();
    }

    let allow_all = allowed.iter().any(|s| s == "*");

    let mut disabled = HashSet::new();
    for name in all_server_names {
        let included = allow_all || allowed.iter().any(|a| a == name);
        let excluded = denied.contains(name.as_str());
        if !included || excluded {
            disabled.insert(name.clone());
        }
    }
    disabled
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use ava_config::{
        default_role_profiles, AgentRoleProfile, ROLE_DIRECTOR, ROLE_JUNIOR_WORKER, ROLE_SCOUT,
        ROLE_SENIOR_LEAD,
    };
    use ava_platform::StandardPlatform;

    #[test]
    fn junior_worker_gets_limited_tools() {
        let profiles = default_role_profiles();
        let junior = &profiles[ROLE_JUNIOR_WORKER];
        let platform = Arc::new(StandardPlatform);
        let (registry, _backup) = build_registry_for_role(junior, platform);
        let names = registry.tool_names();

        assert!(names.iter().any(|n| n == "read"));
        assert!(names.iter().any(|n| n == "write"));
        assert!(names.iter().any(|n| n == "edit"));
        assert!(names.iter().any(|n| n == "bash"));
        assert!(names.iter().any(|n| n == "glob"));
        assert!(names.iter().any(|n| n == "grep"));
        assert!(names.iter().any(|n| n == "git"));

        // Denied tools must NOT be present
        assert!(
            !names.iter().any(|n| n == "web_search"),
            "junior must not have web_search"
        );
        assert!(
            !names.iter().any(|n| n == "web_fetch"),
            "junior must not have web_fetch"
        );
    }

    #[test]
    fn senior_lead_gets_all_tools() {
        let profiles = default_role_profiles();
        let senior = &profiles[ROLE_SENIOR_LEAD];
        let platform = Arc::new(StandardPlatform);
        let (registry, _backup) = build_registry_for_role(senior, platform);
        let names = registry.tool_names();

        assert_eq!(names.len(), 9, "senior should get all 9 built-in tools");
        assert!(names.iter().any(|n| n == "web_search"));
        assert!(names.iter().any(|n| n == "web_fetch"));
    }

    #[test]
    fn scout_gets_read_only_tools() {
        let profiles = default_role_profiles();
        let scout = &profiles[ROLE_SCOUT];
        let platform = Arc::new(StandardPlatform);
        let (registry, _backup) = build_registry_for_role(scout, platform);
        let names = registry.tool_names();

        assert_eq!(names.len(), 3, "scout should only get 3 tools");
        assert!(names.iter().any(|n| n == "read"));
        assert!(names.iter().any(|n| n == "glob"));
        assert!(names.iter().any(|n| n == "grep"));
    }

    #[test]
    fn director_gets_read_only_plus_git() {
        let profiles = default_role_profiles();
        let director = &profiles[ROLE_DIRECTOR];
        let platform = Arc::new(StandardPlatform);
        let (registry, _backup) = build_registry_for_role(director, platform);
        let names = registry.tool_names();

        assert!(names.iter().any(|n| n == "read"));
        assert!(names.iter().any(|n| n == "glob"));
        assert!(names.iter().any(|n| n == "grep"));
        assert!(names.iter().any(|n| n == "git"));
        assert!(
            !names.iter().any(|n| n == "write"),
            "director must not have write"
        );
        assert!(
            !names.iter().any(|n| n == "edit"),
            "director must not have edit"
        );
        assert!(
            !names.iter().any(|n| n == "bash"),
            "director must not have bash"
        );
    }

    #[test]
    fn custom_profile_with_explicit_tools() {
        let profile = AgentRoleProfile {
            allowed_tools: vec!["read".to_string(), "grep".to_string()],
            ..Default::default()
        };
        let platform = Arc::new(StandardPlatform);
        let (registry, _backup) = build_registry_for_role(&profile, platform);
        let names = registry.tool_names();

        assert_eq!(names.len(), 2);
        assert!(names.iter().any(|n| n == "read"));
        assert!(names.iter().any(|n| n == "grep"));
    }

    #[test]
    fn mcp_disabled_when_empty_allowed() {
        let profile = AgentRoleProfile {
            allowed_mcp_servers: vec![], // no MCP
            ..Default::default()
        };
        let all_servers = vec!["docs".to_string(), "search".to_string(), "ci".to_string()];
        let disabled = compute_disabled_mcp_servers(&profile, &all_servers);
        assert_eq!(disabled.len(), 3, "all servers should be disabled");
    }

    #[test]
    fn mcp_all_enabled_with_star() {
        let profile = AgentRoleProfile {
            allowed_mcp_servers: vec!["*".to_string()],
            ..Default::default()
        };
        let all_servers = vec!["docs".to_string(), "search".to_string()];
        let disabled = compute_disabled_mcp_servers(&profile, &all_servers);
        assert!(disabled.is_empty(), "no servers should be disabled with *");
    }

    #[test]
    fn mcp_selective_allow() {
        let profile = AgentRoleProfile {
            allowed_mcp_servers: vec!["docs".to_string()],
            ..Default::default()
        };
        let all_servers = vec!["docs".to_string(), "search".to_string(), "ci".to_string()];
        let disabled = compute_disabled_mcp_servers(&profile, &all_servers);
        assert!(!disabled.contains("docs"));
        assert!(disabled.contains("search"));
        assert!(disabled.contains("ci"));
    }

    #[test]
    fn mcp_denied_overrides_allowed() {
        let profile = AgentRoleProfile {
            allowed_mcp_servers: vec!["*".to_string()],
            denied_mcp_servers: vec!["expensive-research".to_string()],
            ..Default::default()
        };
        let all_servers = vec!["docs".to_string(), "expensive-research".to_string()];
        let disabled = compute_disabled_mcp_servers(&profile, &all_servers);
        assert!(!disabled.contains("docs"));
        assert!(disabled.contains("expensive-research"));
    }
}
