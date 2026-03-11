use serde::Serialize;
use uuid::Uuid;

#[derive(Debug, Clone, Serialize)]
pub enum PraxisEvent {
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
    // Workflow events
    PhaseStarted {
        phase_index: usize,
        phase_count: usize,
        phase_name: String,
        role: String,
    },
    PhaseCompleted {
        phase_index: usize,
        phase_name: String,
        turns: usize,
        output_preview: String,
    },
    IterationStarted {
        iteration: usize,
        max_iterations: usize,
    },
    WorkflowComplete {
        phases_completed: usize,
        total_phases: usize,
        iterations: usize,
        total_turns: usize,
    },
}
