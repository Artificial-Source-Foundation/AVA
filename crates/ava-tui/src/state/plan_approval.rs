use ava_types::{Plan, PlanDecision};
use tokio::sync::oneshot;

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
    /// The proposed plan.
    pub plan: Plan,
    /// Current stage of the approval flow.
    pub stage: PlanApprovalStage,
    /// Rejection feedback text input.
    pub feedback_input: String,
    /// Channel to send the decision back to the agent.
    pub reply: Option<oneshot::Sender<PlanDecision>>,
}

impl PlanApprovalState {
    pub fn new(plan: Plan, reply: oneshot::Sender<PlanDecision>) -> Self {
        Self {
            plan,
            stage: PlanApprovalStage::ActionSelect,
            feedback_input: String::new(),
            reply: Some(reply),
        }
    }

    /// Resolve the plan with the given decision and consume the reply channel.
    pub fn resolve(&mut self, decision: PlanDecision) {
        if let Some(reply) = self.reply.take() {
            let _ = reply.send(decision);
        }
    }
}
