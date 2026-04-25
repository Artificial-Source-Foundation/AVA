use super::*;

impl App {
    pub(crate) fn handle_tool_approval_key(
        &mut self,
        key: crossterm::event::KeyEvent,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) -> bool {
        use crate::state::permission::ApprovalStage;
        use ava_tools::permission_middleware::ToolApproval;

        if self.state.permission.queue.is_empty() {
            self.state.active_modal = None;
            return false;
        }
        let approval_width = self.state.permission.preview_content_width;

        match self.state.permission.current_stage {
            ApprovalStage::Preview => {
                self.state.permission.current_stage = ApprovalStage::ActionSelect;
            }
            ApprovalStage::ActionSelect => {
                match key.code {
                    KeyCode::Char('a') => {
                        if !self.state.permission.queue.front().is_some_and(|request| {
                            request.preview_complete_for_width(approval_width)
                        }) {
                            self.set_status(
                                "Approval disabled because payload preview is truncated",
                                StatusLevel::Warn,
                            );
                            return false;
                        }
                        if let Some(req) = self.state.permission.approve_current_once() {
                            self.resolve_tool_approval_request(
                                req.request_id,
                                req.run_id,
                                ToolApproval::Allowed,
                                app_tx.clone(),
                            );
                        }
                    }
                    KeyCode::Char('s') => {
                        if !self.state.permission.queue.front().is_some_and(|request| {
                            request.preview_complete_for_width(approval_width)
                        }) {
                            self.set_status(
                                "Session approval disabled because payload preview is truncated",
                                StatusLevel::Warn,
                            );
                            return false;
                        }
                        if let Some(req) = self.state.permission.approve_current_for_session() {
                            self.resolve_tool_approval_request(
                                req.request_id,
                                req.run_id,
                                ToolApproval::AllowedForSession,
                                app_tx.clone(),
                            );
                        }
                    }
                    KeyCode::Char('r') => {
                        self.state.permission.current_stage = ApprovalStage::RejectionReason;
                    }
                    KeyCode::Char('y') => {
                        if !self.state.permission.queue.front().is_some_and(|request| {
                            request.preview_complete_for_width(approval_width)
                        }) {
                            self.set_status(
                                "Auto-approve disabled because payload preview is truncated",
                                StatusLevel::Warn,
                            );
                            return false;
                        }
                        self.state.permission.permission_level =
                            crate::state::permission::PermissionLevel::AutoApprove;
                        while self.state.permission.queue.front().is_some_and(|request| {
                            request.preview_complete_for_width(approval_width)
                        }) {
                            let Some(req) = self.state.permission.approve_current_once() else {
                                break;
                            };
                            self.resolve_tool_approval_request(
                                req.request_id,
                                req.run_id,
                                ToolApproval::Allowed,
                                app_tx.clone(),
                            );
                        }
                        self.set_status("Auto-approve enabled", StatusLevel::Info);
                    }
                    KeyCode::Esc => {
                        if let Some((req, reason)) = self.state.permission.reject_current() {
                            self.resolve_tool_approval_request(
                                req.request_id,
                                req.run_id,
                                ToolApproval::Rejected(reason),
                                app_tx.clone(),
                            );
                        }
                    }
                    _ => {}
                }
            }
            ApprovalStage::RejectionReason => match key.code {
                KeyCode::Enter => {
                    if let Some((req, reason)) = self.state.permission.reject_current() {
                        self.resolve_tool_approval_request(
                            req.request_id,
                            req.run_id,
                            ToolApproval::Rejected(reason),
                            app_tx.clone(),
                        );
                    }
                }
                KeyCode::Esc => {
                    self.state.permission.current_stage = ApprovalStage::ActionSelect;
                    self.state.permission.rejection_input.clear();
                }
                KeyCode::Char(ch) => {
                    self.state.permission.rejection_input.push(ch);
                }
                KeyCode::Backspace => {
                    self.state.permission.rejection_input.pop();
                }
                _ => {}
            },
        }
        false
    }
}
