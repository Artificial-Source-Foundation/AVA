use super::*;
use crate::state::plan_approval::PlanApprovalStage;
use ava_types::PlanDecision;

impl App {
    pub(crate) fn handle_plan_approval_key(
        &mut self,
        key: crossterm::event::KeyEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) -> bool {
        let Some(ref mut pa) = self.state.plan_approval else {
            self.state.active_modal = None;
            return false;
        };

        match pa.stage {
            PlanApprovalStage::ActionSelect => match key.code {
                KeyCode::Char('e') => {
                    if let Some(ref mut pa) = self.state.plan_approval.take() {
                        pa.resolve(PlanDecision::Approved);
                    }
                    self.state.active_modal = None;
                    self.state
                        .agent
                        .set_mode(crate::state::agent::AgentMode::Code, Some(app_tx));
                    self.set_status("Plan approved — executing", StatusLevel::Info);
                }
                KeyCode::Char('r') => {
                    pa.stage = PlanApprovalStage::RejectionFeedback;
                }
                KeyCode::Char('f') => {
                    if let Some(ref mut pa) = self.state.plan_approval.take() {
                        pa.resolve(PlanDecision::Rejected {
                            feedback: "Please refine the plan based on additional analysis."
                                .to_string(),
                        });
                    }
                    self.state.active_modal = None;
                    self.set_status("Plan sent back for refinement", StatusLevel::Info);
                }
                KeyCode::Esc => {
                    if let Some(ref mut pa) = self.state.plan_approval.take() {
                        pa.resolve(PlanDecision::Rejected {
                            feedback: "Cancelled by user".to_string(),
                        });
                    }
                    self.state.active_modal = None;
                }
                _ => {}
            },
            PlanApprovalStage::RejectionFeedback => match key.code {
                KeyCode::Enter => {
                    let feedback = self
                        .state
                        .plan_approval
                        .as_ref()
                        .map(|pa| pa.feedback_input.clone())
                        .unwrap_or_default();
                    let feedback = if feedback.trim().is_empty() {
                        "Rejected by user".to_string()
                    } else {
                        feedback
                    };
                    if let Some(ref mut pa) = self.state.plan_approval.take() {
                        pa.resolve(PlanDecision::Rejected { feedback });
                    }
                    self.state.active_modal = None;
                }
                KeyCode::Esc => {
                    if let Some(ref mut pa) = self.state.plan_approval {
                        pa.stage = PlanApprovalStage::ActionSelect;
                        pa.feedback_input.clear();
                    }
                }
                KeyCode::Char(ch) => {
                    if let Some(ref mut pa) = self.state.plan_approval {
                        pa.feedback_input.push(ch);
                    }
                }
                KeyCode::Backspace => {
                    if let Some(ref mut pa) = self.state.plan_approval {
                        pa.feedback_input.pop();
                    }
                }
                _ => {}
            },
        }
        false
    }
}
