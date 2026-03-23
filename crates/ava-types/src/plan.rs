//! Plan types shared between ava-tools (plan tool) and ava-tui (plan display).

use std::sync::{Arc, RwLock};

/// What kind of work a plan step performs.
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PlanAction {
    /// Read and understand code/documentation.
    Research,
    /// Write or modify code.
    Implement,
    /// Write or run tests.
    Test,
    /// Review code for correctness, style, security.
    Review,
}

impl std::fmt::Display for PlanAction {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Research => write!(f, "research"),
            Self::Implement => write!(f, "implement"),
            Self::Test => write!(f, "test"),
            Self::Review => write!(f, "review"),
        }
    }
}

/// A single step in an execution plan.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct PlanStep {
    /// Unique step identifier (e.g. "1", "2a").
    pub id: String,
    /// Human-readable description of what this step does.
    pub description: String,
    /// Files this step will read or modify.
    #[serde(default)]
    pub files: Vec<String>,
    /// The kind of work this step performs.
    pub action: PlanAction,
    /// IDs of steps that must complete before this one starts.
    #[serde(default)]
    pub depends_on: Vec<String>,
}

/// A structured execution plan proposed by the agent.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct Plan {
    /// Ordered list of steps.
    pub steps: Vec<PlanStep>,
    /// Short summary of the entire plan.
    pub summary: String,
    /// Estimated number of agent turns to complete the plan.
    #[serde(default)]
    pub estimated_turns: Option<u32>,
    /// Short memorable codename for the plan (e.g., "Phoenix", "Refactor-Auth").
    /// Auto-generated from summary if not provided by the agent.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub codename: Option<String>,
}

/// The user's decision on a proposed plan.
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case", tag = "decision")]
pub enum PlanDecision {
    /// User approved the plan as-is.
    Approved,
    /// User rejected the plan, optionally with feedback.
    Rejected { feedback: String },
    /// User modified the plan and returned the updated version.
    Modified { plan: Plan, feedback: String },
}

impl std::fmt::Display for PlanDecision {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Approved => write!(f, "approved"),
            Self::Rejected { .. } => write!(f, "rejected"),
            Self::Modified { .. } => write!(f, "modified"),
        }
    }
}

// Need PartialEq for PlanDecision, so we need it on Plan and PlanStep too
impl PartialEq for Plan {
    fn eq(&self, other: &Self) -> bool {
        self.summary == other.summary
            && self.codename == other.codename
            && self.estimated_turns == other.estimated_turns
            && self.steps.len() == other.steps.len()
            && self
                .steps
                .iter()
                .zip(other.steps.iter())
                .all(|(a, b)| a.id == b.id && a.description == b.description)
    }
}

impl Eq for Plan {}

/// Shared plan state accessible by both the tool implementations and the TUI.
///
/// Uses `std::sync::RwLock` (not tokio) for synchronous TUI render access.
#[derive(Debug, Clone, Default)]
pub struct PlanState {
    current: Arc<RwLock<Option<Plan>>>,
}

impl PlanState {
    pub fn new() -> Self {
        Self::default()
    }

    /// Set the current plan (replaces any existing plan).
    pub fn set(&self, plan: Plan) {
        *self.current.write().expect("PlanState poisoned") = Some(plan);
    }

    /// Get a snapshot of the current plan.
    pub fn get(&self) -> Option<Plan> {
        self.current.read().expect("PlanState poisoned").clone()
    }

    /// Clear the current plan.
    pub fn clear(&self) {
        *self.current.write().expect("PlanState poisoned") = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn plan_action_display() {
        assert_eq!(PlanAction::Research.to_string(), "research");
        assert_eq!(PlanAction::Implement.to_string(), "implement");
        assert_eq!(PlanAction::Test.to_string(), "test");
        assert_eq!(PlanAction::Review.to_string(), "review");
    }

    #[test]
    fn plan_decision_display() {
        assert_eq!(PlanDecision::Approved.to_string(), "approved");
        assert_eq!(
            PlanDecision::Rejected {
                feedback: "bad".into()
            }
            .to_string(),
            "rejected"
        );
    }

    #[test]
    fn plan_serde_roundtrip() {
        let plan = Plan {
            steps: vec![PlanStep {
                id: "1".into(),
                description: "Read auth module".into(),
                files: vec!["src/auth/mod.rs".into()],
                action: PlanAction::Research,
                depends_on: vec![],
            }],
            summary: "Research auth patterns".into(),
            estimated_turns: Some(5),
            codename: None,
        };

        let json = serde_json::to_string(&plan).unwrap();
        let deserialized: Plan = serde_json::from_str(&json).unwrap();
        assert_eq!(deserialized.summary, "Research auth patterns");
        assert_eq!(deserialized.steps.len(), 1);
        assert_eq!(deserialized.steps[0].action, PlanAction::Research);
    }

    #[test]
    fn plan_decision_serde_roundtrip() {
        let approved = PlanDecision::Approved;
        let json = serde_json::to_string(&approved).unwrap();
        let deserialized: PlanDecision = serde_json::from_str(&json).unwrap();
        assert_eq!(deserialized, PlanDecision::Approved);

        let rejected = PlanDecision::Rejected {
            feedback: "needs more tests".into(),
        };
        let json = serde_json::to_string(&rejected).unwrap();
        let deserialized: PlanDecision = serde_json::from_str(&json).unwrap();
        assert_eq!(
            deserialized,
            PlanDecision::Rejected {
                feedback: "needs more tests".into()
            }
        );
    }

    #[test]
    fn plan_state_set_and_get() {
        let state = PlanState::new();
        assert!(state.get().is_none());

        state.set(Plan {
            steps: vec![],
            summary: "Test plan".into(),
            estimated_turns: None,
            codename: None,
        });

        let plan = state.get().unwrap();
        assert_eq!(plan.summary, "Test plan");
    }

    #[test]
    fn plan_state_clear() {
        let state = PlanState::new();
        state.set(Plan {
            steps: vec![],
            summary: "Will be cleared".into(),
            estimated_turns: None,
            codename: None,
        });
        assert!(state.get().is_some());

        state.clear();
        assert!(state.get().is_none());
    }

    #[test]
    fn plan_state_shared_across_clones() {
        let state1 = PlanState::new();
        let state2 = state1.clone();

        state1.set(Plan {
            steps: vec![],
            summary: "Shared plan".into(),
            estimated_turns: Some(10),
            codename: None,
        });

        let plan = state2.get().unwrap();
        assert_eq!(plan.summary, "Shared plan");
    }
}
