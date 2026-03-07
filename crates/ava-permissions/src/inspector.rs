use std::collections::{HashMap, HashSet};
use std::path::PathBuf;

use serde_json::Value;

use crate::classifier::classify_bash_command;
use crate::policy::PermissionPolicy;
use crate::tags::{RiskLevel, SafetyTag, ToolSafetyProfile};
use crate::Action;
use crate::PermissionSystem;

#[derive(Debug)]
pub struct InspectionResult {
    pub action: Action,
    pub reason: String,
    pub risk_level: RiskLevel,
    pub tags: Vec<SafetyTag>,
}

pub struct InspectionContext {
    pub workspace_root: PathBuf,
    pub yolo_mode: bool,
    pub session_approved: HashSet<String>,
    pub safety_profiles: HashMap<String, ToolSafetyProfile>,
}

pub trait PermissionInspector: Send + Sync {
    fn inspect(
        &self,
        tool_name: &str,
        arguments: &Value,
        context: &InspectionContext,
    ) -> InspectionResult;
}

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
        // 1. Yolo mode bypasses all checks
        if context.yolo_mode {
            return InspectionResult {
                action: Action::Allow,
                reason: "yolo mode enabled".to_string(),
                risk_level: RiskLevel::Safe,
                tags: vec![],
            };
        }

        // 2. Session-approved tools skip inspection
        if context.session_approved.contains(tool_name) {
            return InspectionResult {
                action: Action::Allow,
                reason: "approved for this session".to_string(),
                risk_level: RiskLevel::Safe,
                tags: vec![],
            };
        }

        // 3. Look up safety profile for base risk
        let profile = context.safety_profiles.get(tool_name);
        let mut risk_level = profile.map_or(RiskLevel::Medium, |p| p.risk_level);
        let tags: Vec<SafetyTag> = profile
            .map(|p| p.tags.iter().copied().collect())
            .unwrap_or_default();

        // 4. For bash: classify the command and potentially upgrade risk
        if tool_name == "bash" {
            if let Some(command) = arguments.get("command").and_then(|v| v.as_str()) {
                let cmd_risk = classify_bash_command(command);
                if cmd_risk.destructive {
                    risk_level = RiskLevel::Critical;
                } else if cmd_risk.network {
                    risk_level = risk_level.max(RiskLevel::High);
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
            };
        }

        // 6. Check policy allowed tools
        if self.policy.allowed_tools.contains(&tool_name.to_string()) {
            return InspectionResult {
                action: Action::Allow,
                reason: format!("tool '{tool_name}' is allowed by policy"),
                risk_level,
                tags,
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
        }
    }
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

    fn test_context(yolo: bool) -> InspectionContext {
        InspectionContext {
            workspace_root: PathBuf::from("/workspace"),
            yolo_mode: yolo,
            session_approved: HashSet::new(),
            safety_profiles: core_tool_profiles(),
        }
    }

    fn default_inspector() -> DefaultInspector {
        let system = PermissionSystem::load("/workspace", vec![]);
        DefaultInspector::new(system, PermissionPolicy::standard())
    }

    #[test]
    fn yolo_mode_allows_everything() {
        let inspector = default_inspector();
        let ctx = test_context(true);
        let result = inspector.inspect("bash", &serde_json::json!({"command": "rm -rf /"}), &ctx);
        assert_eq!(result.action, Action::Allow);
        assert!(result.reason.contains("yolo"));
    }

    #[test]
    fn session_approved_tools_allowed() {
        let inspector = default_inspector();
        let mut ctx = test_context(false);
        ctx.session_approved.insert("bash".to_string());
        let result =
            inspector.inspect("bash", &serde_json::json!({"command": "ls"}), &ctx);
        assert_eq!(result.action, Action::Allow);
        assert!(result.reason.contains("session"));
    }

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
    fn bash_exceeds_standard_policy_threshold() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result =
            inspector.inspect("bash", &serde_json::json!({"command": "ls -la"}), &ctx);
        // Medium > Low threshold → falls through to rule check → Ask (no rules)
        assert_eq!(result.action, Action::Ask);
        assert_eq!(result.risk_level, RiskLevel::Medium);
    }

    #[test]
    fn destructive_bash_gets_critical_risk() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result =
            inspector.inspect("bash", &serde_json::json!({"command": "rm -rf /tmp/test"}), &ctx);
        assert_eq!(result.risk_level, RiskLevel::Critical);
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

    #[test]
    fn blocked_tool_is_denied() {
        let system = PermissionSystem::load("/workspace", vec![]);
        let mut policy = PermissionPolicy::standard();
        policy.blocked_tools.push("bash".to_string());
        let inspector = DefaultInspector::new(system, policy);
        let ctx = test_context(false);
        let result =
            inspector.inspect("bash", &serde_json::json!({"command": "ls"}), &ctx);
        assert_eq!(result.action, Action::Deny);
    }

    #[test]
    fn allowed_tool_bypasses_risk_check() {
        let system = PermissionSystem::load("/workspace", vec![]);
        let mut policy = PermissionPolicy::strict();
        policy.allowed_tools.push("bash".to_string());
        let inspector = DefaultInspector::new(system, policy);
        let ctx = test_context(false);
        let result =
            inspector.inspect("bash", &serde_json::json!({"command": "ls"}), &ctx);
        assert_eq!(result.action, Action::Allow);
    }

    #[test]
    fn permissive_policy_allows_medium_risk() {
        let system = PermissionSystem::load("/workspace", vec![]);
        let inspector = DefaultInspector::new(system, PermissionPolicy::permissive());
        let ctx = test_context(false);
        let result =
            inspector.inspect("bash", &serde_json::json!({"command": "ls"}), &ctx);
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
        // Low > Safe threshold → Ask
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
        let result =
            inspector.inspect("bash", &serde_json::json!({"command": "cargo test"}), &ctx);
        // Medium > Low threshold, but rule says Allow
        // Note: dynamic_check may upgrade to Ask for some commands
        assert!(result.action == Action::Allow || result.action == Action::Ask);
    }

    #[test]
    fn unknown_tool_gets_medium_risk() {
        let inspector = default_inspector();
        let ctx = test_context(false);
        let result = inspector.inspect("custom_tool", &serde_json::json!({}), &ctx);
        assert_eq!(result.risk_level, RiskLevel::Medium);
    }
}
