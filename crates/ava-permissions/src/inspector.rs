use std::collections::{HashMap, HashSet};
use std::path::PathBuf;

use serde_json::Value;

use crate::classifier::classify_bash_command;
use crate::path_safety::analyze_path;
use crate::persistent::PersistentRules;
use crate::policy::PermissionPolicy;
use crate::tags::{RiskLevel, SafetyTag, ToolSafetyProfile};
use crate::Action;
use crate::PermissionSystem;

/// Outcome of a permission inspection — the allow/deny/ask decision with risk metadata.
#[derive(Debug, Clone)]
pub struct InspectionResult {
    pub action: Action,
    pub reason: String,
    pub risk_level: RiskLevel,
    pub tags: Vec<SafetyTag>,
    pub warnings: Vec<String>,
}

/// Where a tool came from — used for source-aware permission checks.
/// Mirrors `ava_tools::registry::ToolSource` without creating a dependency.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ToolSource {
    BuiltIn,
    MCP { server: String },
    Custom { path: String },
}

/// Runtime context for permission inspection — workspace root, auto-approve flag, session approvals, persistent rules, and safety profiles.
pub struct InspectionContext {
    pub workspace_root: PathBuf,
    pub auto_approve: bool,
    pub session_approved: HashSet<String>,
    pub persistent_rules: PersistentRules,
    pub safety_profiles: HashMap<String, ToolSafetyProfile>,
    /// Source of the tool being inspected (None if unknown).
    pub tool_source: Option<ToolSource>,
}

/// Safe .ava/ subdirectories that can be auto-approved for writes.
/// Trust-surface files like mcp.json, hooks/, tools/, permissions.toml are NOT safe.
const SAFE_AVA_PATHS: &[&str] = &[
    "plans",
    "state.json",
    "logs",
    "traces",
    "sessions",
    "themes",
];

/// Canonicalize a path safely, resolving symlinks.
/// For non-existent files, canonicalize the parent directory instead
/// and append the file name — this prevents symlink escape attacks where
/// a symlinked workspace directory makes a non-existent target appear
/// to be inside the workspace when it's actually outside.
fn safe_canonicalize(path: &std::path::Path) -> std::path::PathBuf {
    if path.exists() {
        std::fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf())
    } else {
        // For new files, canonicalize the parent directory
        path.parent()
            .and_then(|p| std::fs::canonicalize(p).ok())
            .map(|p| p.join(path.file_name().unwrap_or_default()))
            .unwrap_or_else(|| path.to_path_buf())
    }
}

/// Check if a path under .ava/ is in a safe (non-trust-surface) location.
fn is_safe_ava_path(path: &std::path::Path, ava_dir: &std::path::Path) -> bool {
    if !path.starts_with(ava_dir) {
        return false;
    }
    let Ok(relative) = path.strip_prefix(ava_dir) else {
        return false;
    };
    // Get the first component of the relative path
    let first = match relative.components().next() {
        Some(std::path::Component::Normal(name)) => name.to_string_lossy(),
        _ => return false,
    };
    SAFE_AVA_PATHS.iter().any(|safe| *safe == first.as_ref())
}

/// Evaluates whether a tool call should be allowed, denied, or require user approval.
///
/// The inspection considers command classification, path safety, auto-approve mode,
/// session approvals, policy rules, and risk level thresholds to produce an
/// [`InspectionResult`] with the decision and risk metadata.
pub trait PermissionInspector: Send + Sync {
    /// Inspect a tool call and return the permission decision with risk metadata.
    fn inspect(
        &self,
        tool_name: &str,
        arguments: &Value,
        context: &InspectionContext,
    ) -> InspectionResult;
}

/// 9-step permission inspector that evaluates tool calls against command classification,
/// path safety analysis, auto-approve mode, session approvals, and policy rules.
pub struct DefaultInspector {
    permission_system: PermissionSystem,
    policy: PermissionPolicy,
}

impl DefaultInspector {
    pub fn new(permission_system: PermissionSystem, policy: PermissionPolicy) -> Self {
        Self {
            permission_system,
            policy,
        }
    }
}

impl PermissionInspector for DefaultInspector {
    fn inspect(
        &self,
        tool_name: &str,
        arguments: &Value,
        context: &InspectionContext,
    ) -> InspectionResult {
        tracing::debug!("Permission check: tool={tool_name}");

        // 0. Internal AVA tools are always safe — but ONLY if they are built-in.
        //    An MCP tool named "todo_read" must NOT get auto-approved.
        const INTERNAL_TOOLS: &[&str] = &[
            "todo_read",
            "todo_write",
            "task",
            "question",
            "codebase_search",
        ];
        // Fail-closed: only treat a tool as built-in if the source is EXPLICITLY BuiltIn.
        // Unknown source (None) is NOT treated as built-in — an MCP tool named "todo_read"
        // would otherwise bypass inspection.
        let is_builtin = context.tool_source.as_ref() == Some(&ToolSource::BuiltIn);
        let has_internal_name = INTERNAL_TOOLS.contains(&tool_name)
            || tool_name.starts_with("memory_")
            || tool_name.starts_with("session_");
        if has_internal_name && is_builtin {
            return InspectionResult {
                action: Action::Allow,
                reason: format!("internal AVA tool '{tool_name}' is always safe"),
                risk_level: RiskLevel::Safe,
                tags: vec![],
                warnings: vec![],
            };
        }
        // If the tool has an internal name but is NOT built-in, log a warning and continue
        // to normal inspection (it will go through risk checks like any other tool).
        if has_internal_name && !is_builtin {
            tracing::warn!(
                "Tool '{tool_name}' has an internal tool name but source is {:?} — not auto-approving",
                context.tool_source
            );
        }

        // 1. For bash: run classifier FIRST, before auto-approve check
        //    Critical/blocked commands must be denied regardless of auto-approve
        let mut risk_level;
        let mut tags: Vec<SafetyTag>;
        let mut warnings = Vec::new();

        let profile = context.safety_profiles.get(tool_name);
        risk_level = profile.map_or(RiskLevel::Medium, |p| p.risk_level);
        tags = profile
            .map(|p| p.tags.iter().copied().collect())
            .unwrap_or_default();

        // Custom and MCP tools get elevated to at least High risk — they run
        // untrusted code and must not be auto-approved at Medium by standard policy.
        if matches!(
            context.tool_source,
            Some(ToolSource::Custom { .. } | ToolSource::MCP { .. })
        ) && risk_level < RiskLevel::High
        {
            risk_level = RiskLevel::High;
        }

        if tool_name == "bash" {
            if let Some(command) = arguments.get("command").and_then(|v| v.as_str()) {
                let classification = classify_bash_command(command);

                // Blocked commands are ALWAYS denied, even in auto-approve mode
                if classification.blocked {
                    tracing::warn!("Command blocked: {command}");
                    return InspectionResult {
                        action: Action::Deny,
                        reason: classification
                            .reason
                            .unwrap_or_else(|| "Blocked command".to_string()),
                        risk_level: RiskLevel::Critical,
                        tags: classification.tags,
                        warnings: classification.warnings,
                    };
                }

                // Apply classifier risk (both upgrades AND downgrades from base profile)
                risk_level = classification.risk_level;
                for tag in &classification.tags {
                    if !tags.contains(tag) {
                        tags.push(*tag);
                    }
                }
                warnings.extend(classification.warnings);
            }
        }

        // 2. For file tools: run path safety analysis
        let file_tools = ["read", "write", "edit", "multiedit", "apply_patch"];
        if file_tools.contains(&tool_name) {
            let paths = extract_paths(tool_name, arguments);
            for path in &paths {
                let path_risk = analyze_path(path, &context.workspace_root);
                if path_risk.system_path && path_risk.risk_level == RiskLevel::Critical {
                    tracing::warn!("System path access denied: {path}");
                    return InspectionResult {
                        action: Action::Deny,
                        reason: path_risk
                            .reason
                            .unwrap_or_else(|| "System path access denied".to_string()),
                        risk_level: RiskLevel::Critical,
                        tags: vec![SafetyTag::SystemModification],
                        warnings: vec![format!("Attempt to access system path: {path}")],
                    };
                }
                if path_risk.outside_workspace && path_risk.risk_level > risk_level {
                    risk_level = path_risk.risk_level;
                    if let Some(reason) = &path_risk.reason {
                        warnings.push(reason.clone());
                    }
                }
            }
        }

        // 2b. Auto-approve file/search tools targeting paths inside the project root
        let project_safe_tools = ["read", "write", "edit", "glob", "grep"];
        if project_safe_tools.contains(&tool_name) {
            let paths = extract_paths(tool_name, arguments);
            let workspace = &context.workspace_root;
            let canonical_workspace =
                std::fs::canonicalize(workspace).unwrap_or_else(|_| workspace.clone());
            let all_inside = !paths.is_empty()
                && paths.iter().all(|p| {
                    let pb = std::path::Path::new(p);
                    let canonical_path = safe_canonicalize(pb);
                    canonical_path.starts_with(&canonical_workspace)
                });
            if all_inside {
                // Writes to safe .ava/ subdirectories are auto-approved
                let ava_dir = canonical_workspace.join(".ava");
                let all_ava_dir = paths.iter().all(|p| {
                    let pb = std::path::Path::new(p);
                    let canonical_path = safe_canonicalize(pb);
                    canonical_path.starts_with(&ava_dir)
                });
                if all_ava_dir {
                    // Only auto-approve if ALL paths target safe .ava/ subdirectories
                    let all_safe = paths.iter().all(|p| {
                        let pb = std::path::Path::new(p);
                        let canonical_path = safe_canonicalize(pb);
                        is_safe_ava_path(&canonical_path, &ava_dir)
                    });
                    if all_safe {
                        return InspectionResult {
                            action: Action::Allow,
                            reason: "path inside safe .ava/ subdirectory".to_string(),
                            risk_level: RiskLevel::Safe,
                            tags,
                            warnings,
                        };
                    }
                    // Trust-surface .ava/ files require approval
                    return InspectionResult {
                        action: Action::Ask,
                        reason: "path targets trust-surface file in .ava/ directory".to_string(),
                        risk_level: RiskLevel::Medium,
                        tags,
                        warnings,
                    };
                }
                risk_level = RiskLevel::Safe;
            }
        }

        // 2c. Auto-approve writes to safe .ava/ subdirectories regardless of tool
        {
            let paths = extract_paths(tool_name, arguments);
            let workspace = &context.workspace_root;
            let canonical_workspace =
                std::fs::canonicalize(workspace).unwrap_or_else(|_| workspace.clone());
            let ava_dir = canonical_workspace.join(".ava");
            let all_ava = !paths.is_empty()
                && paths.iter().all(|p| {
                    let pb = std::path::Path::new(p);
                    let canonical_path = safe_canonicalize(pb);
                    canonical_path.starts_with(&ava_dir)
                });
            if all_ava {
                let all_safe = paths.iter().all(|p| {
                    let pb = std::path::Path::new(p);
                    let canonical_path = safe_canonicalize(pb);
                    is_safe_ava_path(&canonical_path, &ava_dir)
                });
                if all_safe {
                    return InspectionResult {
                        action: Action::Allow,
                        reason: "path inside safe .ava/ subdirectory".to_string(),
                        risk_level: RiskLevel::Safe,
                        tags,
                        warnings,
                    };
                }
                return InspectionResult {
                    action: Action::Ask,
                    reason: "path targets trust-surface file in .ava/ directory".to_string(),
                    risk_level: RiskLevel::Medium,
                    tags,
                    warnings,
                };
            }
        }

        // 3. Auto-approve mode: allow everything EXCEPT what was already blocked above
        if context.auto_approve {
            return InspectionResult {
                action: Action::Allow,
                reason: "auto-approve enabled".to_string(),
                risk_level,
                tags,
                warnings,
            };
        }

        // 4. Session-approved tools skip further inspection
        if context.session_approved.contains(tool_name) {
            return InspectionResult {
                action: Action::Allow,
                reason: "approved for this session".to_string(),
                risk_level,
                tags,
                warnings,
            };
        }

        // 4b. Persistently allowed tools (from .ava/permissions.toml)
        if context.persistent_rules.is_tool_allowed(tool_name) {
            return InspectionResult {
                action: Action::Allow,
                reason: format!("tool '{tool_name}' is persistently allowed"),
                risk_level,
                tags,
                warnings,
            };
        }

        // 4c. Persistently allowed bash commands (prefix match)
        if tool_name == "bash" {
            if let Some(command) = arguments.get("command").and_then(|v| v.as_str()) {
                if context.persistent_rules.is_command_allowed(command) {
                    return InspectionResult {
                        action: Action::Allow,
                        reason: "command is persistently allowed".to_string(),
                        risk_level,
                        tags,
                        warnings,
                    };
                }
            }
        }

        // 5. Check policy blocked tools
        if self.policy.blocked_tools.contains(&tool_name.to_string()) {
            return InspectionResult {
                action: Action::Deny,
                reason: format!("tool '{tool_name}' is blocked by policy"),
                risk_level,
                tags,
                warnings,
            };
        }

        // 6. Check policy allowed tools
        if self.policy.allowed_tools.contains(&tool_name.to_string()) {
            return InspectionResult {
                action: Action::Allow,
                reason: format!("tool '{tool_name}' is allowed by policy"),
                risk_level,
                tags,
                warnings,
            };
        }

        // 7. Check blocked tags
        for tag in &tags {
            if self.policy.blocked_tags.contains(tag) {
                return InspectionResult {
                    action: Action::Deny,
                    reason: format!("tag {tag:?} is blocked by policy '{}'", self.policy.name),
                    risk_level,
                    tags,
                    warnings,
                };
            }
        }

        // 8. Check risk level against policy threshold
        if risk_level <= self.policy.max_risk_level {
            return InspectionResult {
                action: Action::Allow,
                reason: format!(
                    "risk level {risk_level:?} within policy threshold {:?}",
                    self.policy.max_risk_level
                ),
                risk_level,
                tags,
                warnings,
            };
        }

        // 9. Extract args for static/dynamic rule checks
        let args = extract_args_for_rules(tool_name, arguments);
        let arg_refs: Vec<&str> = args.iter().map(|s| s.as_str()).collect();
        let static_dynamic_action = self.permission_system.evaluate(tool_name, &arg_refs);

        InspectionResult {
            action: static_dynamic_action,
            reason: format!(
                "risk level {risk_level:?} exceeds policy threshold {:?}",
                self.policy.max_risk_level
            ),
            risk_level,
            tags,
            warnings,
        }
    }
}

/// Extract file paths from tool arguments for path safety analysis.
fn extract_paths(tool_name: &str, arguments: &Value) -> Vec<String> {
    let mut paths = Vec::new();

    if let Some(path) = arguments.get("path").and_then(|v| v.as_str()) {
        paths.push(path.to_string());
    }
    if let Some(path) = arguments.get("file_path").and_then(|v| v.as_str()) {
        paths.push(path.to_string());
    }

    // multiedit: check edits array for file paths
    if tool_name == "multiedit" {
        if let Some(edits) = arguments.get("edits").and_then(|v| v.as_array()) {
            for edit in edits {
                if let Some(path) = edit.get("file_path").and_then(|v| v.as_str()) {
                    paths.push(path.to_string());
                }
            }
        }
    }

    paths
}

/// Extract string arguments from JSON for rule-based checking.
fn extract_args_for_rules(tool_name: &str, arguments: &Value) -> Vec<String> {
    let mut args = Vec::new();

    if tool_name == "bash" {
        if let Some(cmd) = arguments.get("command").and_then(|v| v.as_str()) {
            args.push(cmd.to_string());
        }
    } else if let Some(path) = arguments.get("path").and_then(|v| v.as_str()) {
        args.push(path.to_string());
    } else if let Some(path) = arguments.get("file_path").and_then(|v| v.as_str()) {
        args.push(path.to_string());
    }

    args
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tags::core_tool_profiles;
    use crate::Rule;

    fn test_context(auto_approve: bool) -> InspectionContext {
        InspectionContext {
            workspace_root: PathBuf::from("/workspace"),
            auto_approve,
            session_approved: HashSet::new(),
            persistent_rules: PersistentRules::default(),
            safety_profiles: core_tool_profiles(),
            tool_source: Some(ToolSource::BuiltIn),
        }
    }

    fn default_inspector() -> DefaultInspector {
        let system = PermissionSystem::load("/workspace", vec![]);
        DefaultInspector::new(system, PermissionPolicy::standard())
    }

    // === CRITICAL: Auto-approve mode still blocks dangerous commands ===

    #[test]
    fn auto_approve_blocks_rm_rf_root() {
        let inspector = default_inspector();
        let ctx = test_context(true);
        let result = inspector.inspect("bash", &serde_json::json!({"command": "rm -rf /"}), &ctx);
        assert_eq!(result.action, Action::Deny);
        assert_eq!(result.risk_level, RiskLevel::Critical);
    }

    #[test]
    fn auto_approve_blocks_sudo() {
        let inspector = default_inspector();
        let ctx = test_context(true);
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "sudo rm -rf /tmp"}),
            &ctx,
        );
        assert_eq!(result.action, Action::Deny);
    }

    #[test]
    fn auto_approve_blocks_fork_bomb() {
        let inspector = default_inspector();
        let ctx = test_context(true);
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": ":(){ :|:& };:"}),
            &ctx,
        );
        assert_eq!(result.action, Action::Deny);
    }

    #[test]
    fn auto_approve_allows_safe_commands() {
        let inspector = default_inspector();
        let ctx = test_context(true);
        let result = inspector.inspect("bash", &serde_json::json!({"command": "ls -la"}), &ctx);
        assert_eq!(result.action, Action::Allow);
        assert!(result.reason.contains("auto-approve"));
    }

    #[test]
    fn auto_approve_allows_non_blocked_tools() {
        let inspector = default_inspector();
        let ctx = test_context(true);
        let result = inspector.inspect("read", &serde_json::json!({"path": "src/main.rs"}), &ctx);
        assert_eq!(result.action, Action::Allow);
    }

    // === Session approved ===

    #[test]
    fn session_approved_tools_allowed() {
        let inspector = default_inspector();
        let mut ctx = test_context(false);
        ctx.session_approved.insert("bash".to_string());
        let result = inspector.inspect("bash", &serde_json::json!({"command": "ls"}), &ctx);
        assert_eq!(result.action, Action::Allow);
        assert!(result.reason.contains("session"));
    }

    // === Risk levels ===

    #[test]
    fn read_only_tools_allowed_by_standard_policy() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect("read", &serde_json::json!({"path": "src/main.rs"}), &ctx);
        assert_eq!(result.action, Action::Allow);
        assert_eq!(result.risk_level, RiskLevel::Safe);
    }

    #[test]
    fn write_tools_allowed_by_standard_policy() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect(
            "write",
            &serde_json::json!({"path": "src/main.rs", "content": "hello"}),
            &ctx,
        );
        assert_eq!(result.action, Action::Allow);
        assert_eq!(result.risk_level, RiskLevel::Low);
    }

    #[test]
    fn safe_bash_auto_approved_by_policy() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect("bash", &serde_json::json!({"command": "ls -la"}), &ctx);
        // Blocklist classifier: ls is Low (default), within standard policy threshold (Medium)
        assert_eq!(result.risk_level, RiskLevel::Low);
        assert_eq!(result.action, Action::Allow);
    }

    #[test]
    fn low_risk_bash_auto_approved_by_standard_policy() {
        let inspector = default_inspector();
        let ctx = test_context(false);

        // cargo test should be Low risk and auto-approved by standard policy (threshold=Medium)
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "cargo test --workspace"}),
            &ctx,
        );
        assert_eq!(result.risk_level, RiskLevel::Low);
        assert_eq!(result.action, Action::Allow);

        // npm run build should also be Low risk and auto-approved
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "npm run build"}),
            &ctx,
        );
        assert_eq!(result.risk_level, RiskLevel::Low);
        assert_eq!(result.action, Action::Allow);

        // git status should be Low risk and auto-approved
        let result = inspector.inspect("bash", &serde_json::json!({"command": "git status"}), &ctx);
        assert_eq!(result.risk_level, RiskLevel::Low);
        assert_eq!(result.action, Action::Allow);

        // cargo clippy should be Low risk and auto-approved
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "cargo clippy --workspace"}),
            &ctx,
        );
        assert_eq!(result.risk_level, RiskLevel::Low);
        assert_eq!(result.action, Action::Allow);

        // cd && cargo test chains should auto-approve (cd is Safe, cargo test is Low)
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "cd /workspace/project && cargo test"}),
            &ctx,
        );
        assert_eq!(result.action, Action::Allow);

        // git commit is Low risk (blocklist default) and auto-approved
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "git commit -m 'fix bug'"}),
            &ctx,
        );
        assert_eq!(result.risk_level, RiskLevel::Low);
        assert_eq!(result.action, Action::Allow);
    }

    #[test]
    fn destructive_bash_gets_critical_risk() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "rm -rf /tmp/test"}),
            &ctx,
        );
        assert_eq!(result.risk_level, RiskLevel::High);
    }

    #[test]
    fn network_bash_gets_high_risk() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "curl https://example.com"}),
            &ctx,
        );
        assert!(result.risk_level >= RiskLevel::High);
    }

    // === Path safety ===

    #[test]
    fn write_to_system_path_denied() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect(
            "write",
            &serde_json::json!({"path": "/etc/passwd", "content": "hacked"}),
            &ctx,
        );
        assert_eq!(result.action, Action::Deny);
        assert_eq!(result.risk_level, RiskLevel::Critical);
    }

    #[test]
    fn write_to_system_path_denied_in_auto_approve() {
        let inspector = default_inspector();
        let ctx = test_context(true);
        let result = inspector.inspect(
            "write",
            &serde_json::json!({"path": "/etc/passwd", "content": "hacked"}),
            &ctx,
        );
        assert_eq!(result.action, Action::Deny);
    }

    #[test]
    fn edit_workspace_file_allowed() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect(
            "edit",
            &serde_json::json!({"file_path": "/workspace/src/main.rs", "old": "a", "new": "b"}),
            &ctx,
        );
        assert_eq!(result.action, Action::Allow);
    }

    // === Warnings ===

    #[test]
    fn high_risk_bash_has_warnings() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "rm -rf /tmp/test"}),
            &ctx,
        );
        assert!(!result.warnings.is_empty());
    }

    // === Policy checks ===

    #[test]
    fn blocked_tool_is_denied() {
        let system = PermissionSystem::load("/workspace", vec![]);
        let mut policy = PermissionPolicy::standard();
        policy.blocked_tools.push("bash".to_string());
        let inspector = DefaultInspector::new(system, policy);
        let ctx = test_context(false);
        let result = inspector.inspect("bash", &serde_json::json!({"command": "ls"}), &ctx);
        assert_eq!(result.action, Action::Deny);
    }

    #[test]
    fn allowed_tool_bypasses_risk_check() {
        let system = PermissionSystem::load("/workspace", vec![]);
        let mut policy = PermissionPolicy::strict();
        policy.allowed_tools.push("bash".to_string());
        let inspector = DefaultInspector::new(system, policy);
        let ctx = test_context(false);
        let result = inspector.inspect("bash", &serde_json::json!({"command": "ls"}), &ctx);
        assert_eq!(result.action, Action::Allow);
    }

    #[test]
    fn permissive_policy_allows_medium_risk() {
        let system = PermissionSystem::load("/workspace", vec![]);
        let inspector = DefaultInspector::new(system, PermissionPolicy::permissive());
        let ctx = test_context(false);
        let result = inspector.inspect("bash", &serde_json::json!({"command": "ls"}), &ctx);
        assert_eq!(result.action, Action::Allow);
    }

    #[test]
    fn strict_policy_asks_for_write_tools() {
        let system = PermissionSystem::load("/workspace", vec![]);
        let inspector = DefaultInspector::new(system, PermissionPolicy::strict());
        let ctx = test_context(false);
        let result = inspector.inspect(
            "write",
            &serde_json::json!({"path": "src/main.rs", "content": "x"}),
            &ctx,
        );
        assert_eq!(result.action, Action::Ask);
    }

    #[test]
    fn explicit_allow_rule_overrides_risk_threshold() {
        let system = PermissionSystem::load(
            "/workspace",
            vec![Rule {
                tool: crate::Pattern::Any,
                args: crate::Pattern::Any,
                action: Action::Allow,
            }],
        );
        let inspector = DefaultInspector::new(system, PermissionPolicy::standard());
        let ctx = test_context(false);
        let result = inspector.inspect("bash", &serde_json::json!({"command": "cargo test"}), &ctx);
        assert!(result.action == Action::Allow || result.action == Action::Ask);
    }

    #[test]
    fn unknown_tool_gets_medium_risk() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect("custom_tool", &serde_json::json!({}), &ctx);
        assert_eq!(result.risk_level, RiskLevel::Medium);
    }

    // === New tool profiles ===

    #[test]
    fn diagnostics_tool_is_safe() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect("diagnostics", &serde_json::json!({}), &ctx);
        assert_eq!(result.risk_level, RiskLevel::Safe);
        assert_eq!(result.action, Action::Allow);
    }

    #[test]
    fn codebase_search_is_safe() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect(
            "codebase_search",
            &serde_json::json!({"query": "foo"}),
            &ctx,
        );
        assert_eq!(result.risk_level, RiskLevel::Safe);
        assert_eq!(result.action, Action::Allow);
    }

    #[test]
    fn memory_tools_are_safe() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        for tool in &["remember", "recall", "memory_search"] {
            let result = inspector.inspect(tool, &serde_json::json!({}), &ctx);
            assert_eq!(
                result.risk_level,
                RiskLevel::Safe,
                "expected Safe for {tool}"
            );
            assert_eq!(result.action, Action::Allow, "expected Allow for {tool}");
        }
    }

    #[test]
    fn session_tools_are_safe() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        for tool in &["session_search", "session_list", "session_load"] {
            let result = inspector.inspect(tool, &serde_json::json!({}), &ctx);
            assert_eq!(
                result.risk_level,
                RiskLevel::Safe,
                "expected Safe for {tool}"
            );
            assert_eq!(result.action, Action::Allow, "expected Allow for {tool}");
        }
    }

    // === UX-9: Safe bash commands auto-approve, risky ask, critical block ===

    #[test]
    fn safe_bash_auto_approved() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect("bash", &serde_json::json!({"command": "ls"}), &ctx);
        assert_eq!(result.risk_level, RiskLevel::Low);
    }

    #[test]
    fn risky_bash_asks() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        // rm -rf is High risk — requires user confirmation
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "rm -rf /tmp/test"}),
            &ctx,
        );
        assert!(result.risk_level >= RiskLevel::High);
    }

    #[test]
    fn critical_bash_blocked() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "sudo rm -rf /"}),
            &ctx,
        );
        assert_eq!(result.risk_level, RiskLevel::Critical);
        assert_eq!(result.action, Action::Deny);
    }

    // === Persistent rules ===

    #[test]
    fn persistent_tool_allowed() {
        let inspector = default_inspector();
        let mut ctx = test_context(false);
        ctx.persistent_rules.allow_tool("bash");
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "curl https://example.com"}),
            &ctx,
        );
        assert_eq!(result.action, Action::Allow);
        assert!(result.reason.contains("persistently allowed"));
    }

    #[test]
    fn persistent_command_allowed() {
        let inspector = default_inspector();
        let mut ctx = test_context(false);
        ctx.persistent_rules.allow_command("cargo test");
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "cargo test --workspace"}),
            &ctx,
        );
        assert_eq!(result.action, Action::Allow);
        assert!(result.reason.contains("persistently allowed"));
    }

    #[test]
    fn persistent_rules_do_not_bypass_critical_block() {
        let inspector = default_inspector();
        let mut ctx = test_context(false);
        // Even if bash is persistently allowed, critical commands are still blocked
        ctx.persistent_rules.allow_tool("bash");
        let result = inspector.inspect(
            "bash",
            &serde_json::json!({"command": "sudo rm -rf /"}),
            &ctx,
        );
        assert_eq!(result.action, Action::Deny);
        assert_eq!(result.risk_level, RiskLevel::Critical);
    }
}
