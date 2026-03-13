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
    SpecCreated {
        spec_id: Uuid,
        title: String,
    },
    SpecStatusChanged {
        spec_id: Uuid,
        from: String,
        to: String,
    },
    SpecWorkflowStarted {
        spec_id: Uuid,
        workflow_name: String,
    },
    SpecWorkflowCompleted {
        spec_id: Uuid,
        workflow_name: String,
        turns: usize,
    },
    ArtifactCreated {
        artifact_id: Uuid,
        kind: String,
        producer: String,
        title: String,
    },
    PeerMessageSent {
        message_id: Uuid,
        from_worker: Uuid,
        to_worker: Uuid,
        kind: String,
    },
    ConflictDetected {
        workers: (Uuid, Uuid),
        overlapping_files: Vec<String>,
    },
    AcpRequestHandled {
        method: String,
        success: bool,
    },
}
