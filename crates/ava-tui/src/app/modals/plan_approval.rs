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

        let mut resolution: Option<(String, Option<String>, PlanDecision)> = None;
        let mut approved = false;
        let mut refined = false;

        match pa.stage {
            PlanApprovalStage::ActionSelect => match key.code {
                KeyCode::Char('e') => {
                    resolution = Some((
                        pa.request_id.clone(),
                        pa.run_id.clone(),
                        PlanDecision::Approved,
                    ));
                    approved = true;
                }
                KeyCode::Char('r') => {
                    pa.stage = PlanApprovalStage::RejectionFeedback;
                }
                KeyCode::Char('f') => {
                    resolution = Some((
                        pa.request_id.clone(),
                        pa.run_id.clone(),
                        PlanDecision::Rejected {
                            feedback: "Please refine the plan based on additional analysis."
                                .to_string(),
                        },
                    ));
                    refined = true;
                }
                KeyCode::Esc => {
                    resolution = Some((
                        pa.request_id.clone(),
                        pa.run_id.clone(),
                        PlanDecision::Rejected {
                            feedback: "Cancelled by user".to_string(),
                        },
                    ));
                }
                _ => {}
            },
            PlanApprovalStage::RejectionFeedback => match key.code {
                KeyCode::Enter => {
                    let feedback = if pa.feedback_input.trim().is_empty() {
                        "Rejected by user".to_string()
                    } else {
                        pa.feedback_input.clone()
                    };
                    resolution = Some((
                        pa.request_id.clone(),
                        pa.run_id.clone(),
                        PlanDecision::Rejected { feedback },
                    ));
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

        if let Some((request_id, run_id, decision)) = resolution {
            self.state.plan_approval = None;
            self.state.active_modal = None;
            self.promote_next_queued_interactive_modal(app_tx.clone());
            self.resolve_plan_request(request_id, run_id, decision, app_tx.clone());
            if approved {
                self.state
                    .agent
                    .set_mode(crate::state::agent::AgentMode::Code, Some(app_tx));
                self.set_status("Plan approved — executing", StatusLevel::Info);
            } else if refined {
                self.set_status("Plan sent back for refinement", StatusLevel::Info);
            }
        }

        false
    }
}
