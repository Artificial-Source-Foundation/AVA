use crate::plan::PraxisPlan;
use serde::Serialize;
use uuid::Uuid;

#[derive(Debug, Clone, Serialize)]
pub enum PraxisEvent {
    /// Emitted when the Director produces a structured plan via LLM analysis.
    PlanCreated {
        plan: PraxisPlan,
    },
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
    // Board of Directors events
    /// Emitted when the Board of Directors is convened for multi-model consensus.
    BoardConvened {
        members: Vec<String>,
    },
    /// Emitted when a board member produces their opinion.
    BoardOpinion {
        member: String,
        vote: String,
        summary: String,
    },
    /// Emitted when the board reaches a consensus.
    BoardResult {
        consensus: String,
        vote_summary: String,
    },
    // Lead-managed execution events
    LeadExecutionStarted {
        lead: String,
        total_tasks: usize,
        total_waves: usize,
    },
    LeadWaveStarted {
        lead: String,
        wave_index: usize,
        task_count: usize,
    },
    LeadWaveCompleted {
        lead: String,
        wave_index: usize,
        succeeded: usize,
        failed: usize,
    },
    LeadReviewStarted {
        lead: String,
    },
    LeadReviewCompleted {
        lead: String,
        issues_found: usize,
    },
    LeadExecutionCompleted {
        lead: String,
        total_tasks: usize,
        succeeded: usize,
        failed: usize,
    },
    // External CLI agent worker events
    ExternalWorkerStarted {
        worker_id: Uuid,
        lead: String,
        agent_name: String,
        task_description: String,
    },
    ExternalWorkerText {
        worker_id: Uuid,
        content: String,
    },
    ExternalWorkerToolUse {
        worker_id: Uuid,
        tool_name: String,
    },
    ExternalWorkerThinking {
        worker_id: Uuid,
        content: String,
    },
    ExternalWorkerCompleted {
        worker_id: Uuid,
        success: bool,
        session_id: Option<String>,
        cost_usd: Option<f64>,
        turns: usize,
    },
    ExternalWorkerFailed {
        worker_id: Uuid,
        error: String,
    },
    // Scout events
    ScoutStarted {
        id: Uuid,
        query: String,
    },
    ScoutCompleted {
        id: Uuid,
        query: String,
        files_examined: usize,
        snippets_found: usize,
    },
    ScoutFailed {
        id: Uuid,
        query: String,
        error: String,
    },
}
