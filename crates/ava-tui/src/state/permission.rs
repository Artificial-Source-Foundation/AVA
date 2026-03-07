use ava_types::ToolCall;
use std::collections::{HashSet, VecDeque};
use tokio::sync::oneshot;

#[derive(Debug, Clone)]
pub enum ToolApproval {
    Allowed,
    AllowedForSession,
    Rejected(Option<String>),
}

#[derive(Debug)]
pub struct ApprovalRequest {
    pub call: ToolCall,
    pub approve_tx: oneshot::Sender<ToolApproval>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ApprovalStage {
    Preview,
    ActionSelect,
    RejectionReason,
}

#[derive(Debug)]
pub struct PermissionState {
    pub queue: VecDeque<ApprovalRequest>,
    pub current_stage: ApprovalStage,
    pub session_approved: HashSet<String>,
    pub yolo_mode: bool,
    pub rejection_input: String,
}

impl Default for PermissionState {
    fn default() -> Self {
        Self {
            queue: VecDeque::new(),
            current_stage: ApprovalStage::Preview,
            session_approved: HashSet::new(),
            yolo_mode: false,
            rejection_input: String::new(),
        }
    }
}

impl PermissionState {
    pub fn enqueue(&mut self, request: ApprovalRequest) {
        self.queue.push_back(request);
    }

    pub fn approve_current_once(&mut self) {
        if let Some(req) = self.queue.pop_front() {
            let _ = req.approve_tx.send(ToolApproval::Allowed);
        }
        self.reset();
    }

    pub fn approve_current_for_session(&mut self) {
        if let Some(req) = self.queue.pop_front() {
            self.session_approved.insert(req.call.name.clone());
            let _ = req.approve_tx.send(ToolApproval::AllowedForSession);
        }
        self.reset();
    }

    pub fn reject_current(&mut self) {
        if let Some(req) = self.queue.pop_front() {
            let reason = if self.rejection_input.trim().is_empty() {
                None
            } else {
                Some(self.rejection_input.trim().to_string())
            };
            let _ = req.approve_tx.send(ToolApproval::Rejected(reason));
        }
        self.reset();
    }

    fn reset(&mut self) {
        self.current_stage = ApprovalStage::Preview;
        self.rejection_input.clear();
    }
}
