use std::collections::{HashMap, HashSet};
use std::path::PathBuf;

use serde_json::Value;

use crate::classifier::classify_bash_command;
use crate::path_safety::analyze_path;
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

/// Runtime context for permission inspection — workspace root, auto-approve flag, session approvals, and safety profiles.
pub struct InspectionContext {
    pub workspace_root: PathBuf,
    pub auto_approve: bool,
    pub session_approved: HashSet<String>,
    pub safety_profiles: HashMap<String, ToolSafetyProfile>,
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

        if tool_name == "bash" {
            if let Some(command) = arguments.get("command").and_then(|v| v.as_str()) {
                let classification = classify_bash_command(command);

                // Blocked commands are ALWAYS denied, even in auto-approve mode
                if classification.blocked {
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
            safety_profiles: core_tool_profiles(),
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
        // Classifier downgrades to Safe, which is within standard policy threshold (Low)
        assert_eq!(result.risk_level, RiskLevel::Safe);
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
        assert_eq!(result.risk_level, RiskLevel::Safe);
    }

    #[test]
    fn risky_bash_asks() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect("bash", &serde_json::json!({"command": "rm foo.txt"}), &ctx);
        assert!(result.risk_level >= RiskLevel::Medium);
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
}
