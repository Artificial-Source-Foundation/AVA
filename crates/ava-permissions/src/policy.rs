use serde::{Deserialize, Serialize};

use crate::tags::{RiskLevel, SafetyTag};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PermissionPolicy {
    pub name: String,
    pub max_risk_level: RiskLevel,
    pub blocked_tags: Vec<SafetyTag>,
    pub allowed_tools: Vec<String>,
    pub blocked_tools: Vec<String>,
}

impl PermissionPolicy {
    /// Allow everything except Critical risk operations.
    pub fn permissive() -> Self {
        Self {
            name: "permissive".to_string(),
            max_risk_level: RiskLevel::High,
            blocked_tags: vec![],
            allowed_tools: vec![],
            blocked_tools: vec![],
        }
    }

    /// Allow Safe+Low+Medium automatically, ask for High+, deny Critical.
    pub fn standard() -> Self {
        Self {
            name: "standard".to_string(),
            max_risk_level: RiskLevel::Medium,
            blocked_tags: vec![SafetyTag::Destructive],
            allowed_tools: vec![],
            blocked_tools: vec![],
        }
    }

    /// Ask for everything except ReadOnly operations.
    pub fn strict() -> Self {
        Self {
            name: "strict".to_string(),
            max_risk_level: RiskLevel::Safe,
            blocked_tags: vec![SafetyTag::Destructive, SafetyTag::Privileged],
            allowed_tools: vec![],
            blocked_tools: vec![],
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn permissive_allows_up_to_high() {
        let policy = PermissionPolicy::permissive();
        assert_eq!(policy.max_risk_level, RiskLevel::High);
        assert!(policy.blocked_tags.is_empty());
    }

    #[test]
    fn standard_allows_up_to_medium() {
        let policy = PermissionPolicy::standard();
        assert_eq!(policy.max_risk_level, RiskLevel::Medium);
        assert!(policy.blocked_tags.contains(&SafetyTag::Destructive));
    }

    #[test]
    fn strict_allows_only_safe() {
        let policy = PermissionPolicy::strict();
        assert_eq!(policy.max_risk_level, RiskLevel::Safe);
        assert!(policy.blocked_tags.contains(&SafetyTag::Destructive));
        assert!(policy.blocked_tags.contains(&SafetyTag::Privileged));
    }

    #[test]
    fn policy_serialization_roundtrip() {
        let policy = PermissionPolicy::standard();
        let json = serde_json::to_string(&policy).unwrap();
        let deserialized: PermissionPolicy = serde_json::from_str(&json).unwrap();
        assert_eq!(deserialized.name, "standard");
        assert_eq!(deserialized.max_risk_level, RiskLevel::Medium);
    }
}
