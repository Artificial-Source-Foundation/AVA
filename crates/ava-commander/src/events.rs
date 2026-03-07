use serde::Serialize;
use uuid::Uuid;

#[derive(Debug, Clone, Serialize)]
pub enum CommanderEvent {
    WorkerStarted {
        worker_id: Uuid,
        lead: String,
        task_description: String,
    },
    WorkerProgress {
        worker_id: Uuid,
        turn: usize,
        max_turns: usize,
    },
    WorkerToken {
        worker_id: Uuid,
        token: String,
    },
    WorkerCompleted {
        worker_id: Uuid,
        success: bool,
        turns: usize,
    },
    WorkerFailed {
        worker_id: Uuid,
        error: String,
    },
    AllComplete {
        total_workers: usize,
        succeeded: usize,
        failed: usize,
    },
    Summary {
        total_workers: usize,
        succeeded: usize,
        failed: usize,
        total_turns: usize,
    },
}
