use crate::state::messages::{MessageKind, UiMessage};
use std::time::Instant;
use uuid::Uuid;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PraxisTaskStatus {
    Pending,
    Running,
    Completed,
    Failed,
}

impl std::fmt::Display for PraxisTaskStatus {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Pending => write!(f, "pending"),
            Self::Running => write!(f, "running"),
            Self::Completed => write!(f, "completed"),
            Self::Failed => write!(f, "failed"),
        }
    }
}

#[derive(Debug, Clone)]
pub struct PraxisWorkerState {
    pub worker_id: Uuid,
    pub lead: String,
    pub task_description: String,
    pub status: PraxisTaskStatus,
    pub turn: usize,
    pub max_turns: usize,
}

#[derive(Debug, Clone)]
pub struct PraxisTaskState {
    pub id: usize,
    pub goal: String,
    pub status: PraxisTaskStatus,
    pub started_at: Instant,
    pub completed_at: Option<Instant>,
    pub workers: Vec<PraxisWorkerState>,
    pub messages: Vec<UiMessage>,
    pub merged_messages: usize,
    pub cancel: Option<tokio_util::sync::CancellationToken>,
}

#[derive(Debug, Default, Clone)]
pub struct PraxisState {
    pub tasks: Vec<PraxisTaskState>,
    next_id: usize,
}

impl PraxisState {
    pub fn add_task(&mut self, goal: String) -> usize {
        self.next_id += 1;
        let id = self.next_id;
        self.tasks.push(PraxisTaskState {
            id,
            goal,
            status: PraxisTaskStatus::Pending,
            started_at: Instant::now(),
            completed_at: None,
            workers: Vec::new(),
            messages: vec![UiMessage::new(
                MessageKind::System,
                "Praxis task created. Waiting for workers...",
            )],
            merged_messages: 0,
            cancel: None,
        });
        id
    }

    pub fn task_mut(&mut self, id: usize) -> Option<&mut PraxisTaskState> {
        self.tasks.iter_mut().find(|task| task.id == id)
    }

    pub fn task(&self, id: usize) -> Option<&PraxisTaskState> {
        self.tasks.iter().find(|task| task.id == id)
    }
}
