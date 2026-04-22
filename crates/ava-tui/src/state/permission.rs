use ava_permissions::tags::{RiskLevel, SafetyTag};
pub use ava_tools::permission_middleware::ToolApproval;
use ava_types::ToolCall;
use std::collections::{HashSet, VecDeque};

/// Optional inspection result attached to approval requests for UI display.
#[derive(Debug, Clone)]
pub struct InspectionInfo {
    pub risk_level: RiskLevel,
    pub tags: Vec<SafetyTag>,
    pub warnings: Vec<String>,
}

#[derive(Debug)]
pub struct ApprovalRequest {
    pub request_id: String,
    pub run_id: Option<String>,
    pub call: ToolCall,
    pub inspection: Option<InspectionInfo>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ApprovalStage {
    Preview,
    ActionSelect,
    RejectionReason,
}

/// Permission level — controls tool auto-approval behavior.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum PermissionLevel {
    #[default]
    Standard, // Auto-approve reads+writes, ask for bash/commands
    AutoApprove, // Auto-approve everything, only block Critical
}

impl PermissionLevel {
    pub fn label(&self) -> &'static str {
        match self {
            Self::Standard => "standard",
            Self::AutoApprove => "auto-approve",
        }
    }

    pub fn toggle(&self) -> Self {
        match self {
            Self::Standard => Self::AutoApprove,
            Self::AutoApprove => Self::Standard,
        }
    }

    /// Whether this level auto-approves tool requests except Critical ones.
    pub fn is_auto_approve(&self) -> bool {
        matches!(self, Self::AutoApprove)
    }
}

#[derive(Debug)]
pub struct PermissionState {
    pub queue: VecDeque<ApprovalRequest>,
    pub current_stage: ApprovalStage,
    /// UI-only mirror for the current session's approvals.
    /// The authoritative set used by permission checks lives in `InspectionContext`.
    pub session_approved: HashSet<String>,
    pub permission_level: PermissionLevel,
    pub rejection_input: String,
}

impl Default for PermissionState {
    fn default() -> Self {
        Self {
            queue: VecDeque::new(),
            current_stage: ApprovalStage::Preview,
            session_approved: HashSet::new(),
            permission_level: PermissionLevel::Standard,
            rejection_input: String::new(),
        }
    }
}

impl PermissionState {
    pub fn enqueue(&mut self, request: ApprovalRequest) {
        self.queue.push_back(request);
    }

    pub fn approve_current_once(&mut self) -> Option<ApprovalRequest> {
        let request = self.queue.pop_front();
        self.reset_modal_state();
        request
    }

    pub fn approve_current_for_session(&mut self) -> Option<ApprovalRequest> {
        let request = self.queue.pop_front();
        if let Some(req) = request.as_ref() {
            self.session_approved.insert(req.call.name.clone());
        }
        self.reset_modal_state();
        request
    }

    pub fn reject_current(&mut self) -> Option<(ApprovalRequest, Option<String>)> {
        let request = self.queue.pop_front();
        let reason = if self.rejection_input.trim().is_empty() {
            None
        } else {
            Some(self.rejection_input.trim().to_string())
        };
        self.reset();
        request.map(|request| (request, reason))
    }

    pub fn reset_modal_state(&mut self) {
        self.reset();
    }

    fn reset(&mut self) {
        self.current_stage = ApprovalStage::Preview;
        self.rejection_input.clear();
    }
}
