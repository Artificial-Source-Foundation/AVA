//! AVA HQ — multi-agent orchestration with domain-specific leads.
//!
//! This crate implements the director pattern for coordinating multiple agents:
//! - Domain-specific leads (Frontend, Backend, QA, etc.)
//! - Worker spawning and task delegation
//! - Event streaming and coordination
//!
//! Hierarchy: User (CEO) -> Director -> Leads -> Workers

use serde::{Deserialize, Serialize};

pub mod acp;
pub mod acp_handler;
pub mod acp_transport;
pub mod artifact;
pub mod artifact_store;
pub mod board;
pub mod colors;
pub mod conflict;
pub mod director;
pub mod events;
pub mod file_mailbox;
pub mod lead;
pub mod mailbox;
pub mod memory;
pub mod plan;
pub mod prompts;
pub mod review;
pub mod routing;
pub mod scout;
pub mod send_message;
pub mod spec;
pub mod spec_workflow;
pub mod worker;
pub mod workflow;

pub mod external_worker;
pub mod role_tools;

pub use acp::{AcpError, AcpMethod, AcpRequest, AcpResponse};
pub use acp_handler::AcpHandler;
pub use acp_transport::InProcessAcpTransport;
pub use artifact::{Artifact, ArtifactKind};
pub use artifact_store::{ArtifactStore, FileArtifactStore};
pub use board::{Board, BoardMember, BoardOpinion, BoardPersonality, BoardResult, BoardVote};
pub use colors::AgentColorManager;
pub use conflict::{ConflictDetector, ConflictReport, WorkerIntent};
pub use director::{Director, DirectorConfig};
pub use events::HqEvent;
pub use external_worker::ExternalWorker;
pub use file_mailbox::MailboxMessage;
pub use lead::Lead;
pub use mailbox::{Mailbox, PeerMessage, PeerMessageKind};
pub use memory::{bootstrap_hq_memory, HqMemoryBootstrapOptions, HqMemoryBootstrapResult};
pub use plan::{ExecutionGroup, HqPlan, HqTask, PlannerConfig, TaskComplexity};
pub use prompts::{
    director_system_prompt, lead_system_prompt, lead_system_prompt_for_domain,
    worker_system_prompt, worker_system_prompt_for_domain,
};
pub use review::{DiffMode, ReviewContext, ReviewResult, ReviewVerdict, Severity};
pub use role_tools::{build_registry_for_role, compute_disabled_mcp_servers, ALL_BUILTIN_TOOLS};
pub use routing::{domain_to_task_type, topological_sort};
pub use scout::{CodeSnippet, Scout, ScoutReport};
pub use send_message::SendMessageTool;
pub use spec::{SpecDocument, SpecStatus, SpecStore, SpecTask};
pub use spec_workflow::build_spec_goal;
pub use worker::Worker;
pub use workflow::{Phase, PhaseRole, Workflow, WorkflowExecutor};

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Hash)]
pub enum Domain {
    Frontend,
    Backend,
    QA,
    Research,
    Debug,
    Fullstack,
    DevOps,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Budget {
    pub max_tokens: usize,
    pub max_turns: usize,
    pub max_cost_usd: f64,
}

impl Budget {
    pub fn new(max_tokens: usize, max_turns: usize, max_cost_usd: f64) -> Self {
        Self {
            max_tokens,
            max_turns,
            max_cost_usd,
        }
    }

    pub fn interactive(max_turns: usize, max_budget_usd: f64) -> Self {
        Self::new(
            128_000,
            if max_turns == 0 { 200 } else { max_turns },
            if max_budget_usd > 0.0 {
                max_budget_usd
            } else {
                10.0
            },
        )
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Task {
    pub description: String,
    pub task_type: TaskType,
    pub files: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum TaskType {
    Planning,
    CodeGeneration,
    Testing,
    Review,
    Research,
    Debug,
    Chat,
    Simple,
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_task(id: &str, deps: &[&str]) -> HqTask {
        HqTask {
            id: id.to_string(),
            description: format!("Task {id}"),
            domain: Domain::Backend,
            complexity: TaskComplexity::Simple,
            dependencies: deps.iter().map(|s| s.to_string()).collect(),
            budget: Budget::new(10_000, 10, 1.0),
            files_hint: vec![],
        }
    }

    #[test]
    fn topological_sort_empty() {
        let waves = topological_sort(&[]).unwrap();
        assert!(waves.is_empty());
    }

    #[test]
    fn topological_sort_no_deps() {
        let tasks = vec![
            make_task("t1", &[]),
            make_task("t2", &[]),
            make_task("t3", &[]),
        ];
        let waves = topological_sort(&tasks).unwrap();
        assert_eq!(
            waves.len(),
            1,
            "all tasks with no deps should be in one wave"
        );
        assert_eq!(waves[0].len(), 3);
    }

    #[test]
    fn topological_sort_linear_chain() {
        let tasks = vec![
            make_task("t1", &[]),
            make_task("t2", &["t1"]),
            make_task("t3", &["t2"]),
        ];
        let waves = topological_sort(&tasks).unwrap();
        assert_eq!(waves.len(), 3);
        assert_eq!(waves[0][0].id, "t1");
        assert_eq!(waves[1][0].id, "t2");
        assert_eq!(waves[2][0].id, "t3");
    }

    #[test]
    fn topological_sort_diamond() {
        // t1 -> t2, t1 -> t3, t2+t3 -> t4
        let tasks = vec![
            make_task("t1", &[]),
            make_task("t2", &["t1"]),
            make_task("t3", &["t1"]),
            make_task("t4", &["t2", "t3"]),
        ];
        let waves = topological_sort(&tasks).unwrap();
        assert_eq!(waves.len(), 3);
        assert_eq!(waves[0].len(), 1); // t1
        assert_eq!(waves[1].len(), 2); // t2, t3 in parallel
        assert_eq!(waves[2].len(), 1); // t4
        assert_eq!(waves[2][0].id, "t4");
    }

    #[test]
    fn topological_sort_cycle_detected() {
        let tasks = vec![make_task("t1", &["t2"]), make_task("t2", &["t1"])];
        let result = topological_sort(&tasks);
        assert!(result.is_err());
        let err = result.unwrap_err().to_string();
        assert!(err.contains("cycle"), "error should mention cycle: {err}");
    }

    #[test]
    fn topological_sort_unknown_dep() {
        let tasks = vec![make_task("t1", &["t99"])];
        let result = topological_sort(&tasks);
        assert!(result.is_err());
        let err = result.unwrap_err().to_string();
        assert!(
            err.contains("unknown task id"),
            "error should mention unknown: {err}"
        );
    }

    #[test]
    fn topological_sort_mixed_waves() {
        // t1, t2 no deps; t3 depends on t1; t4 depends on t1 and t2
        let tasks = vec![
            make_task("t1", &[]),
            make_task("t2", &[]),
            make_task("t3", &["t1"]),
            make_task("t4", &["t1", "t2"]),
        ];
        let waves = topological_sort(&tasks).unwrap();
        assert_eq!(waves.len(), 2);
        assert_eq!(waves[0].len(), 2); // t1, t2
        assert_eq!(waves[1].len(), 2); // t3, t4
    }
}
