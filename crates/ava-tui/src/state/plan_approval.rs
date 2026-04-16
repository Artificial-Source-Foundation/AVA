use ava_types::Plan;

/// Stages of the plan approval flow.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum PlanApprovalStage {
    /// Showing plan summary, waiting for user choice.
    #[default]
    ActionSelect,
    /// User is typing rejection feedback.
    RejectionFeedback,
}

/// State for an active plan approval request.
pub struct PlanApprovalState {
    /// Correlation ID owned by the shared interactive lifecycle store.
    pub request_id: String,
    /// Optional run correlation for the originating agent run.
    pub run_id: Option<String>,
    /// The proposed plan.
    pub plan: Plan,
    /// Current stage of the approval flow.
    pub stage: PlanApprovalStage,
    /// Rejection feedback text input.
    pub feedback_input: String,
}

impl PlanApprovalState {
    pub fn new(request_id: String, run_id: Option<String>, plan: Plan) -> Self {
        Self {
            request_id,
            run_id,
            plan,
            stage: PlanApprovalStage::ActionSelect,
            feedback_input: String::new(),
        }
    }
}
