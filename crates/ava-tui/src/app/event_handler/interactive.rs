use super::*;
use ava_control_plane::interactive::InteractiveRequestKind;
use ava_tools::core::plan::PlanRequest;
use ava_tools::core::question::QuestionRequest;
use ava_tools::permission_middleware::{ApprovalRequest, ToolApproval};
use ava_types::PlanDecision;

impl App {
    pub(crate) async fn receive_question_request(
        &mut self,
        req: QuestionRequest,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        self.supersede_pending_question_requests(req.run_id.as_deref(), app_tx.clone())
            .await;

        let handle = self
            .pending_question_reply
            .register_with_run_id(req.reply, req.run_id.clone())
            .await;

        let question = QuestionState {
            request_id: handle.request_id.clone(),
            run_id: handle.run_id.clone(),
            question: req.question,
            options: req.options,
            selected: 0,
            input: String::new(),
        };
        let request_id = handle.request_id.clone();
        let run_id = handle.run_id.clone();
        if self.show_or_queue_question(question) {
            self.spawn_question_timeout(request_id, run_id, app_tx);
        }
    }

    pub(crate) async fn receive_tool_approval_request(
        &mut self,
        req: ApprovalRequest,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let handle = self
            .pending_approval_reply
            .register_with_run_id(req.reply, req.run_id.clone())
            .await;

        let inspection = Some(crate::state::permission::InspectionInfo {
            risk_level: req.inspection.risk_level,
            tags: req.inspection.tags,
            warnings: req.inspection.warnings,
        });
        self.state
            .permission
            .enqueue(crate::state::permission::ApprovalRequest {
                request_id: handle.request_id.clone(),
                run_id: handle.run_id.clone(),
                call: req.call,
                inspection,
            });
        if self.show_or_queue_approval(handle.request_id.clone()) {
            self.spawn_approval_timeout(handle.request_id, handle.run_id, app_tx);
        }
    }

    pub(crate) async fn receive_plan_request(
        &mut self,
        req: PlanRequest,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        self.supersede_pending_plan_requests(req.run_id.as_deref(), app_tx.clone())
            .await;

        let handle = self
            .pending_plan_reply
            .register_with_run_id(req.reply, req.run_id.clone())
            .await;

        let plan = crate::state::plan_approval::PlanApprovalState::new(
            handle.request_id.clone(),
            handle.run_id.clone(),
            req.plan,
        );
        let request_id = handle.request_id.clone();
        let run_id = handle.run_id.clone();
        if self.show_or_queue_plan(plan) {
            self.spawn_plan_timeout(request_id, run_id, app_tx);
        }
    }

    pub(crate) fn cancel_foreground_interactive_requests(
        &self,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        reason: &'static str,
    ) {
        let Some(run_id) = self.foreground_run_id else {
            return;
        };

        self.cancel_pending_interactive_requests(app_tx, reason, Some(run_id.to_string()));
    }

    pub(crate) fn resolve_question_request(
        &self,
        request_id: String,
        run_id: Option<String>,
        answer: String,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let pending = self.pending_question_reply.clone();
        tokio::spawn(async move {
            match pending.resolve(Some(&request_id)).await {
                Ok(reply) => {
                    let run_id = reply.handle.run_id.clone();
                    let request_id = reply.handle.request_id.clone();
                    let _ = reply.reply.send(answer);
                    let _ = app_tx.send(AppEvent::InteractiveRequestCleared {
                        request_id,
                        request_kind: reply.handle.kind,
                        timed_out: false,
                        run_id,
                    });
                }
                Err(err) => {
                    tracing::warn!(?err, request_id, "failed to resolve question request");
                    let _ = app_tx.send(AppEvent::InteractiveRequestCleared {
                        request_id,
                        request_kind: InteractiveRequestKind::Question,
                        timed_out: false,
                        run_id,
                    });
                }
            }
        });
    }

    pub(crate) fn resolve_tool_approval_request(
        &self,
        request_id: String,
        run_id: Option<String>,
        approval: ToolApproval,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let pending = self.pending_approval_reply.clone();
        tokio::spawn(async move {
            match pending.resolve(Some(&request_id)).await {
                Ok(reply) => {
                    let run_id = reply.handle.run_id.clone();
                    let request_id = reply.handle.request_id.clone();
                    let _ = reply.reply.send(approval);
                    let _ = app_tx.send(AppEvent::InteractiveRequestCleared {
                        request_id,
                        request_kind: reply.handle.kind,
                        timed_out: false,
                        run_id,
                    });
                }
                Err(err) => {
                    tracing::warn!(?err, request_id, "failed to resolve approval request");
                    let _ = app_tx.send(AppEvent::InteractiveRequestCleared {
                        request_id,
                        request_kind: InteractiveRequestKind::Approval,
                        timed_out: false,
                        run_id,
                    });
                }
            }
        });
    }

    pub(crate) fn resolve_plan_request(
        &self,
        request_id: String,
        run_id: Option<String>,
        decision: PlanDecision,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let pending = self.pending_plan_reply.clone();
        tokio::spawn(async move {
            match pending.resolve(Some(&request_id)).await {
                Ok(reply) => {
                    let run_id = reply.handle.run_id.clone();
                    let request_id = reply.handle.request_id.clone();
                    let _ = reply.reply.send(decision);
                    let _ = app_tx.send(AppEvent::InteractiveRequestCleared {
                        request_id,
                        request_kind: reply.handle.kind,
                        timed_out: false,
                        run_id,
                    });
                }
                Err(err) => {
                    tracing::warn!(?err, request_id, "failed to resolve plan request");
                    let _ = app_tx.send(AppEvent::InteractiveRequestCleared {
                        request_id,
                        request_kind: InteractiveRequestKind::Plan,
                        timed_out: false,
                        run_id,
                    });
                }
            }
        });
    }

    pub(crate) fn cancel_pending_interactive_requests(
        &self,
        app_tx: mpsc::UnboundedSender<AppEvent>,
        reason: &'static str,
        run_id_filter: Option<String>,
    ) {
        let pending_approval = self.pending_approval_reply.clone();
        let approval_tx = app_tx.clone();
        let approval_run_id = run_id_filter.clone();
        tokio::spawn(async move {
            loop {
                let cancelled = match approval_run_id.as_deref() {
                    Some(run_id) => pending_approval.cancel_pending_for_run(run_id).await,
                    None => pending_approval.cancel_pending().await,
                };
                let Some(cancelled) = cancelled else {
                    break;
                };
                let request_id = cancelled.handle.request_id.clone();
                let run_id = cancelled.handle.run_id.clone();
                let _ = cancelled
                    .reply
                    .send(ToolApproval::Rejected(Some(reason.to_string())));
                let _ = approval_tx.send(AppEvent::InteractiveRequestCleared {
                    request_id,
                    request_kind: cancelled.handle.kind,
                    timed_out: false,
                    run_id,
                });
            }
        });

        let pending_question = self.pending_question_reply.clone();
        let question_tx = app_tx.clone();
        let question_run_id = run_id_filter.clone();
        tokio::spawn(async move {
            loop {
                let cancelled = match question_run_id.as_deref() {
                    Some(run_id) => pending_question.cancel_pending_for_run(run_id).await,
                    None => pending_question.cancel_pending().await,
                };
                let Some(cancelled) = cancelled else {
                    break;
                };
                let request_id = cancelled.handle.request_id.clone();
                let run_id = cancelled.handle.run_id.clone();
                let _ = cancelled.reply.send(String::new());
                let _ = question_tx.send(AppEvent::InteractiveRequestCleared {
                    request_id,
                    request_kind: cancelled.handle.kind,
                    timed_out: false,
                    run_id,
                });
            }
        });

        let pending_plan = self.pending_plan_reply.clone();
        let plan_run_id = run_id_filter;
        tokio::spawn(async move {
            loop {
                let cancelled = match plan_run_id.as_deref() {
                    Some(run_id) => pending_plan.cancel_pending_for_run(run_id).await,
                    None => pending_plan.cancel_pending().await,
                };
                let Some(cancelled) = cancelled else {
                    break;
                };
                let request_id = cancelled.handle.request_id.clone();
                let run_id = cancelled.handle.run_id.clone();
                let _ = cancelled.reply.send(PlanDecision::Rejected {
                    feedback: reason.to_string(),
                });
                let _ = app_tx.send(AppEvent::InteractiveRequestCleared {
                    request_id,
                    request_kind: cancelled.handle.kind,
                    timed_out: false,
                    run_id,
                });
            }
        });
    }

    pub(crate) fn handle_interactive_request_cleared(
        &mut self,
        request_id: &str,
        request_kind: InteractiveRequestKind,
        timed_out: bool,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        match request_kind {
            InteractiveRequestKind::Approval => {
                let is_active_approval_request = self
                    .state
                    .permission
                    .queue
                    .iter()
                    .any(|request| request.request_id == request_id);
                let cleared_active_request = self.state.active_modal
                    == Some(ModalType::ToolApproval)
                    && (!is_active_approval_request
                        || self
                            .state
                            .permission
                            .queue
                            .front()
                            .is_some_and(|request| request.request_id == request_id));

                if let Some(index) = self
                    .state
                    .permission
                    .queue
                    .iter()
                    .position(|request| request.request_id == request_id)
                {
                    self.state.permission.queue.remove(index);
                }
                let removed_hidden_request = self.remove_queued_interactive_modal(request_id);
                if cleared_active_request {
                    self.state.permission.reset_modal_state();
                    self.state.active_modal = None;
                    self.promote_next_queued_interactive_modal(app_tx);
                } else if removed_hidden_request && self.state.active_modal.is_none() {
                    self.promote_next_queued_interactive_modal(app_tx);
                }
                if timed_out {
                    self.set_status("Tool approval timed out".to_string(), StatusLevel::Warn);
                }
            }
            InteractiveRequestKind::Question => {
                if self
                    .state
                    .question
                    .as_ref()
                    .is_some_and(|question| question.request_id == request_id)
                {
                    self.state.question = None;
                    if self.state.active_modal == Some(ModalType::Question) {
                        self.state.active_modal = None;
                        self.promote_next_queued_interactive_modal(app_tx);
                    }
                } else {
                    self.remove_queued_interactive_modal(request_id);
                }
                if timed_out {
                    self.set_status("Question timed out".to_string(), StatusLevel::Warn);
                }
            }
            InteractiveRequestKind::Plan => {
                if self
                    .state
                    .plan_approval
                    .as_ref()
                    .is_some_and(|plan| plan.request_id == request_id)
                {
                    self.state.plan_approval = None;
                    if self.state.active_modal == Some(ModalType::PlanApproval) {
                        self.state.active_modal = None;
                        self.promote_next_queued_interactive_modal(app_tx);
                    }
                } else {
                    self.remove_queued_interactive_modal(request_id);
                }
                if timed_out {
                    self.set_status("Plan approval timed out".to_string(), StatusLevel::Warn);
                }
            }
        }
    }

    async fn supersede_pending_question_requests(
        &self,
        run_id_filter: Option<&str>,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        while let Some(cancelled) = match run_id_filter {
            Some(run_id) => {
                self.pending_question_reply
                    .cancel_pending_for_run(run_id)
                    .await
            }
            None => self.pending_question_reply.cancel_pending().await,
        } {
            let request_id = cancelled.handle.request_id.clone();
            let run_id = cancelled.handle.run_id.clone();
            let _ = cancelled.reply.send(String::new());
            let _ = app_tx.send(AppEvent::InteractiveRequestCleared {
                request_id,
                request_kind: cancelled.handle.kind,
                timed_out: false,
                run_id,
            });
        }
    }

    fn show_or_queue_approval(&mut self, request_id: String) -> bool {
        if self.should_show_interactive_modal(InteractiveRequestKind::Approval, None) {
            self.state.active_modal = Some(ModalType::ToolApproval);
            true
        } else {
            self.queued_interactive_modals
                .push_back(QueuedInteractiveModal::Approval(request_id));
            false
        }
    }

    fn show_or_queue_question(&mut self, question: QuestionState) -> bool {
        if self.should_show_interactive_modal(
            InteractiveRequestKind::Question,
            question.run_id.as_deref(),
        ) {
            self.state.question = Some(question);
            self.state.active_modal = Some(ModalType::Question);
            true
        } else {
            self.queued_interactive_modals
                .push_back(QueuedInteractiveModal::Question(question));
            false
        }
    }

    fn show_or_queue_plan(&mut self, plan: crate::state::plan_approval::PlanApprovalState) -> bool {
        if self.should_show_interactive_modal(InteractiveRequestKind::Plan, plan.run_id.as_deref())
        {
            self.state.plan_approval = Some(plan);
            self.state.active_modal = Some(ModalType::PlanApproval);
            true
        } else {
            self.queued_interactive_modals
                .push_back(QueuedInteractiveModal::PlanApproval(plan));
            false
        }
    }

    fn should_show_interactive_modal(
        &self,
        new_kind: InteractiveRequestKind,
        new_run_id: Option<&str>,
    ) -> bool {
        match self.visible_interactive_request_kind() {
            None => true,
            Some(kind) if kind != new_kind => false,
            Some(InteractiveRequestKind::Approval) => false,
            Some(InteractiveRequestKind::Question) => self
                .state
                .question
                .as_ref()
                .map(|question| question.run_id.as_deref() == new_run_id)
                .unwrap_or(true),
            Some(InteractiveRequestKind::Plan) => self
                .state
                .plan_approval
                .as_ref()
                .map(|plan| plan.run_id.as_deref() == new_run_id)
                .unwrap_or(true),
        }
    }

    fn visible_interactive_request_kind(&self) -> Option<InteractiveRequestKind> {
        match self.state.active_modal {
            Some(ModalType::ToolApproval) if !self.state.permission.queue.is_empty() => {
                Some(InteractiveRequestKind::Approval)
            }
            Some(ModalType::Question) if self.state.question.is_some() => {
                Some(InteractiveRequestKind::Question)
            }
            Some(ModalType::PlanApproval) if self.state.plan_approval.is_some() => {
                Some(InteractiveRequestKind::Plan)
            }
            _ => None,
        }
    }

    fn remove_queued_interactive_modal(&mut self, request_id: &str) -> bool {
        let Some(index) = self
            .queued_interactive_modals
            .iter()
            .position(|modal| match modal {
                QueuedInteractiveModal::Approval(queued_request_id) => {
                    queued_request_id == request_id
                }
                QueuedInteractiveModal::Question(question) => question.request_id == request_id,
                QueuedInteractiveModal::PlanApproval(plan) => plan.request_id == request_id,
            })
        else {
            return false;
        };

        self.queued_interactive_modals.remove(index);
        true
    }

    pub(crate) fn promote_next_queued_interactive_modal(
        &mut self,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        if self.state.active_modal.is_some() {
            return;
        }

        while let Some(queued) = self.queued_interactive_modals.pop_front() {
            match queued {
                QueuedInteractiveModal::Approval(request_id) => {
                    let Some(request) = self
                        .state
                        .permission
                        .queue
                        .front()
                        .filter(|request| request.request_id == request_id)
                    else {
                        continue;
                    };

                    self.state.active_modal = Some(ModalType::ToolApproval);
                    self.spawn_approval_timeout(
                        request.request_id.clone(),
                        request.run_id.clone(),
                        app_tx.clone(),
                    );
                    return;
                }
                QueuedInteractiveModal::Question(question) => {
                    let request_id = question.request_id.clone();
                    let run_id = question.run_id.clone();
                    self.state.question = Some(question);
                    self.state.active_modal = Some(ModalType::Question);
                    self.spawn_question_timeout(request_id, run_id, app_tx.clone());
                    return;
                }
                QueuedInteractiveModal::PlanApproval(plan) => {
                    let request_id = plan.request_id.clone();
                    let run_id = plan.run_id.clone();
                    self.state.plan_approval = Some(plan);
                    self.state.active_modal = Some(ModalType::PlanApproval);
                    self.spawn_plan_timeout(request_id, run_id, app_tx.clone());
                    return;
                }
            }
        }

        if let Some(next_request) = self.state.permission.queue.front() {
            self.state.active_modal = Some(ModalType::ToolApproval);
            self.spawn_approval_timeout(
                next_request.request_id.clone(),
                next_request.run_id.clone(),
                app_tx,
            );
        }
    }

    async fn supersede_pending_plan_requests(
        &self,
        run_id_filter: Option<&str>,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        while let Some(cancelled) = match run_id_filter {
            Some(run_id) => self.pending_plan_reply.cancel_pending_for_run(run_id).await,
            None => self.pending_plan_reply.cancel_pending().await,
        } {
            let request_id = cancelled.handle.request_id.clone();
            let run_id = cancelled.handle.run_id.clone();
            let _ = cancelled.reply.send(PlanDecision::Rejected {
                feedback: "Superseded by a newer TUI plan request".to_string(),
            });
            let _ = app_tx.send(AppEvent::InteractiveRequestCleared {
                request_id,
                request_kind: cancelled.handle.kind,
                timed_out: false,
                run_id,
            });
        }
    }

    fn spawn_question_timeout(
        &self,
        request_id: String,
        _run_id: Option<String>,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let pending = self.pending_question_reply.clone();
        tokio::spawn(async move {
            tokio::time::sleep(pending.timeout()).await;
            if let Some(timed_out) = pending.timeout_request(&request_id).await {
                let request_id = timed_out.handle.request_id.clone();
                let run_id = timed_out.handle.run_id.clone();
                let _ = timed_out.reply.send(String::new());
                let _ = app_tx.send(AppEvent::InteractiveRequestCleared {
                    request_id,
                    request_kind: timed_out.handle.kind,
                    timed_out: true,
                    run_id,
                });
            }
        });
    }

    fn spawn_approval_timeout(
        &self,
        request_id: String,
        _run_id: Option<String>,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let pending = self.pending_approval_reply.clone();
        tokio::spawn(async move {
            tokio::time::sleep(pending.timeout()).await;
            if let Some(timed_out) = pending.timeout_request(&request_id).await {
                let request_id = timed_out.handle.request_id.clone();
                let run_id = timed_out.handle.run_id.clone();
                let _ = timed_out.reply.send(ToolApproval::Rejected(Some(
                    "Timed out waiting for user approval in TUI".to_string(),
                )));
                let _ = app_tx.send(AppEvent::InteractiveRequestCleared {
                    request_id,
                    request_kind: timed_out.handle.kind,
                    timed_out: true,
                    run_id,
                });
            }
        });
    }

    fn spawn_plan_timeout(
        &self,
        request_id: String,
        _run_id: Option<String>,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        let pending = self.pending_plan_reply.clone();
        tokio::spawn(async move {
            tokio::time::sleep(pending.timeout()).await;
            if let Some(timed_out) = pending.timeout_request(&request_id).await {
                let request_id = timed_out.handle.request_id.clone();
                let run_id = timed_out.handle.run_id.clone();
                let _ = timed_out.reply.send(PlanDecision::Rejected {
                    feedback: "Timed out waiting for plan response in TUI".to_string(),
                });
                let _ = app_tx.send(AppEvent::InteractiveRequestCleared {
                    request_id,
                    request_kind: timed_out.handle.kind,
                    timed_out: true,
                    run_id,
                });
            }
        });
    }
}
