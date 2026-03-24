use serde::Serialize;
use serde_json::Value;
use tauri::{Emitter, Window};

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TodoItemPayload {
    pub content: String,
    pub status: String,
    pub priority: String,
}

#[derive(Clone, Serialize)]
#[serde(tag = "type")]
pub enum AgentEvent {
    #[serde(rename = "token")]
    Token { content: String },
    #[serde(rename = "thinking")]
    Thinking { content: String },
    #[serde(rename = "tool_call")]
    ToolCall { name: String, args: Value },
    #[serde(rename = "tool_result")]
    ToolResult { content: String, is_error: bool },
    #[serde(rename = "progress")]
    Progress { message: String },
    #[serde(rename = "complete")]
    Complete { session: Value },
    #[serde(rename = "error")]
    Error { message: String },
    #[serde(rename = "todo_update")]
    TodoUpdate { todos: Vec<TodoItemPayload> },
    #[serde(rename = "token_usage")]
    TokenUsage {
        input_tokens: usize,
        output_tokens: usize,
        cost_usd: f64,
    },
    #[serde(rename = "budget_warning")]
    BudgetWarning {
        threshold_percent: u8,
        current_cost_usd: f64,
        max_budget_usd: f64,
    },
    #[serde(rename = "approval_request")]
    ApprovalRequest {
        id: String,
        tool_name: String,
        args: Value,
        risk_level: String,
        reason: String,
        warnings: Vec<String>,
    },
    #[serde(rename = "question_request")]
    QuestionRequest {
        id: String,
        question: String,
        options: Vec<String>,
    },

    // ── Praxis multi-agent events ──────────────────────────────────────
    #[serde(rename = "praxis_worker_started")]
    PraxisWorkerStarted {
        worker_id: String,
        lead: String,
        task: String,
    },
    #[serde(rename = "praxis_worker_progress")]
    PraxisWorkerProgress {
        worker_id: String,
        turn: usize,
        max_turns: usize,
    },
    #[serde(rename = "praxis_worker_token")]
    PraxisWorkerToken { worker_id: String, token: String },
    #[serde(rename = "praxis_worker_completed")]
    PraxisWorkerCompleted {
        worker_id: String,
        success: bool,
        turns: usize,
    },
    #[serde(rename = "praxis_worker_failed")]
    PraxisWorkerFailed { worker_id: String, error: String },
    #[serde(rename = "praxis_all_complete")]
    PraxisAllComplete {
        total_workers: usize,
        succeeded: usize,
        failed: usize,
    },
    #[serde(rename = "praxis_summary")]
    PraxisSummary {
        total_workers: usize,
        succeeded: usize,
        failed: usize,
        total_turns: usize,
    },
    #[serde(rename = "praxis_phase_started")]
    PraxisPhaseStarted {
        phase_index: usize,
        phase_count: usize,
        phase_name: String,
        role: String,
    },
    #[serde(rename = "praxis_phase_completed")]
    PraxisPhaseCompleted {
        phase_index: usize,
        phase_name: String,
        turns: usize,
        output_preview: String,
    },
    #[serde(rename = "praxis_spec_created")]
    PraxisSpecCreated { spec_id: String, title: String },
    #[serde(rename = "praxis_artifact_created")]
    PraxisArtifactCreated {
        artifact_id: String,
        kind: String,
        producer: String,
        title: String,
    },
    #[serde(rename = "praxis_conflict_detected")]
    PraxisConflictDetected {
        workers: (String, String),
        overlapping_files: Vec<String>,
    },

    #[serde(rename = "plan_created")]
    PlanCreated { plan: PlanPayload },
}

/// Flattened plan data for the desktop frontend.
/// We avoid sending the full `PraxisPlan` (which includes Budget, Domain
/// enums, etc.) and instead project into a frontend-friendly shape.
#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PlanPayload {
    pub summary: String,
    pub steps: Vec<PlanStepPayload>,
    pub estimated_turns: usize,
}

#[derive(Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PlanStepPayload {
    pub id: String,
    pub description: String,
    pub files: Vec<String>,
    pub action: String,
    pub depends_on: Vec<String>,
}

pub struct EventEmitter {
    window: Window,
}

impl EventEmitter {
    pub fn new(window: Window) -> Self {
        Self { window }
    }

    pub fn emit_token(&self, token: &str) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::Token {
                    content: token.to_string(),
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_tool_call(&self, name: &str, args: Value) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::ToolCall {
                    name: name.to_string(),
                    args,
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_tool_result(&self, content: &str, is_error: bool) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::ToolResult {
                    content: content.to_string(),
                    is_error,
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_progress(&self, message: &str) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::Progress {
                    message: message.to_string(),
                },
            )
            .map_err(|e| e.to_string())
    }

    pub fn emit_complete(&self, session: Value) -> Result<(), String> {
        self.window
            .emit("agent-event", AgentEvent::Complete { session })
            .map_err(|e| e.to_string())
    }

    pub fn emit_error(&self, message: &str) -> Result<(), String> {
        self.window
            .emit(
                "agent-event",
                AgentEvent::Error {
                    message: message.to_string(),
                },
            )
            .map_err(|e| e.to_string())
    }
}

/// Convert an `ava_agent::AgentEvent` to a Tauri `AgentEvent` payload.
/// Returns `None` for events that have no direct desktop representation
/// (e.g. `ToolStats`, `SubAgentComplete`).
pub fn from_backend_event(event: &ava_agent::agent_loop::AgentEvent) -> Option<AgentEvent> {
    use ava_agent::agent_loop::AgentEvent as BE;
    match event {
        BE::Token(content) => Some(AgentEvent::Token {
            content: content.clone(),
        }),
        BE::Thinking(content) => Some(AgentEvent::Thinking {
            content: content.clone(),
        }),
        BE::ToolCall(tc) => Some(AgentEvent::ToolCall {
            name: tc.name.clone(),
            args: tc.arguments.clone(),
        }),
        BE::ToolResult(tr) => Some(AgentEvent::ToolResult {
            content: tr.content.clone(),
            is_error: tr.is_error,
        }),
        BE::Progress(msg) => Some(AgentEvent::Progress {
            message: msg.clone(),
        }),
        BE::Complete(session) => {
            let session_json = serde_json::to_value(session).unwrap_or_default();
            Some(AgentEvent::Complete {
                session: session_json,
            })
        }
        BE::Error(msg) => Some(AgentEvent::Error {
            message: msg.clone(),
        }),
        BE::TokenUsage {
            input_tokens,
            output_tokens,
            cost_usd,
        } => Some(AgentEvent::TokenUsage {
            input_tokens: *input_tokens,
            output_tokens: *output_tokens,
            cost_usd: *cost_usd,
        }),
        BE::BudgetWarning {
            threshold_percent,
            current_cost_usd,
            max_budget_usd,
        } => Some(AgentEvent::BudgetWarning {
            threshold_percent: *threshold_percent,
            current_cost_usd: *current_cost_usd,
            max_budget_usd: *max_budget_usd,
        }),
        // ToolStats and SubAgentComplete don't have a direct frontend representation yet.
        _ => None,
    }
}

/// Emit a backend `AgentEvent` to all Tauri windows via the app handle.
pub fn emit_backend_event<R: tauri::Runtime>(
    handle: &tauri::AppHandle<R>,
    event: &ava_agent::agent_loop::AgentEvent,
) {
    if let Some(payload) = from_backend_event(event) {
        let _ = handle.emit("agent-event", payload);
    }
}

/// Convert a `PraxisEvent` to a Tauri `AgentEvent` payload.
pub fn from_praxis_event(event: &ava_praxis::PraxisEvent) -> Option<AgentEvent> {
    use ava_praxis::PraxisEvent as PE;
    match event {
        PE::WorkerStarted {
            worker_id,
            lead,
            task_description,
        } => Some(AgentEvent::PraxisWorkerStarted {
            worker_id: worker_id.to_string(),
            lead: lead.clone(),
            task: task_description.clone(),
        }),
        PE::WorkerProgress {
            worker_id,
            turn,
            max_turns,
        } => Some(AgentEvent::PraxisWorkerProgress {
            worker_id: worker_id.to_string(),
            turn: *turn,
            max_turns: *max_turns,
        }),
        PE::WorkerToken { worker_id, token } => Some(AgentEvent::PraxisWorkerToken {
            worker_id: worker_id.to_string(),
            token: token.clone(),
        }),
        PE::WorkerCompleted {
            worker_id,
            success,
            turns,
        } => Some(AgentEvent::PraxisWorkerCompleted {
            worker_id: worker_id.to_string(),
            success: *success,
            turns: *turns,
        }),
        PE::WorkerFailed { worker_id, error } => Some(AgentEvent::PraxisWorkerFailed {
            worker_id: worker_id.to_string(),
            error: error.clone(),
        }),
        PE::AllComplete {
            total_workers,
            succeeded,
            failed,
        } => Some(AgentEvent::PraxisAllComplete {
            total_workers: *total_workers,
            succeeded: *succeeded,
            failed: *failed,
        }),
        PE::Summary {
            total_workers,
            succeeded,
            failed,
            total_turns,
        } => Some(AgentEvent::PraxisSummary {
            total_workers: *total_workers,
            succeeded: *succeeded,
            failed: *failed,
            total_turns: *total_turns,
        }),
        PE::PhaseStarted {
            phase_index,
            phase_count,
            phase_name,
            role,
        } => Some(AgentEvent::PraxisPhaseStarted {
            phase_index: *phase_index,
            phase_count: *phase_count,
            phase_name: phase_name.clone(),
            role: role.clone(),
        }),
        PE::PhaseCompleted {
            phase_index,
            phase_name,
            turns,
            output_preview,
        } => Some(AgentEvent::PraxisPhaseCompleted {
            phase_index: *phase_index,
            phase_name: phase_name.clone(),
            turns: *turns,
            output_preview: output_preview.clone(),
        }),
        PE::SpecCreated { spec_id, title } => Some(AgentEvent::PraxisSpecCreated {
            spec_id: spec_id.to_string(),
            title: title.clone(),
        }),
        PE::ArtifactCreated {
            artifact_id,
            kind,
            producer,
            title,
        } => Some(AgentEvent::PraxisArtifactCreated {
            artifact_id: artifact_id.to_string(),
            kind: kind.clone(),
            producer: producer.clone(),
            title: title.clone(),
        }),
        PE::ConflictDetected {
            workers,
            overlapping_files,
        } => Some(AgentEvent::PraxisConflictDetected {
            workers: (workers.0.to_string(), workers.1.to_string()),
            overlapping_files: overlapping_files.clone(),
        }),
        PE::PlanCreated { plan } => {
            let domain_to_action = |d: &ava_praxis::Domain| -> String {
                match d {
                    ava_praxis::Domain::Research => "research".to_string(),
                    ava_praxis::Domain::QA => "test".to_string(),
                    ava_praxis::Domain::Debug => "review".to_string(),
                    _ => "implement".to_string(),
                }
            };
            let steps = plan
                .tasks
                .iter()
                .map(|t| PlanStepPayload {
                    id: t.id.clone(),
                    description: t.description.clone(),
                    files: t.files_hint.clone(),
                    action: domain_to_action(&t.domain),
                    depends_on: t.dependencies.clone(),
                })
                .collect();
            let estimated_turns: usize = plan.tasks.iter().map(|t| t.budget.max_turns).sum();
            Some(AgentEvent::PlanCreated {
                plan: PlanPayload {
                    summary: plan.goal.clone(),
                    steps,
                    estimated_turns,
                },
            })
        }
        // IterationStarted, WorkflowComplete, SpecStatusChanged, SpecWorkflowStarted,
        // SpecWorkflowCompleted, PeerMessageSent, AcpRequestHandled — no direct UI representation yet
        _ => None,
    }
}

/// Emit a `PraxisEvent` to all Tauri windows via the app handle.
pub fn emit_praxis_event<R: tauri::Runtime>(
    handle: &tauri::AppHandle<R>,
    event: &ava_praxis::PraxisEvent,
) {
    if let Some(payload) = from_praxis_event(event) {
        let _ = handle.emit("agent-event", payload);
    }
}

#[cfg(test)]
mod tests {
    use serde_json::json;

    use super::AgentEvent;

    #[test]
    fn serializes_with_expected_event_type_tag() {
        let token_event = AgentEvent::Token {
            content: "abc".to_string(),
        };
        let as_json = serde_json::to_value(token_event).expect("event to serialize");

        assert_eq!(as_json["type"], "token");
        assert_eq!(as_json["content"], "abc");

        let complete_event = AgentEvent::Complete {
            session: json!({ "id": "mock-session" }),
        };
        let complete_json = serde_json::to_value(complete_event).expect("event to serialize");

        assert_eq!(complete_json["type"], "complete");
        assert_eq!(complete_json["session"]["id"], "mock-session");
    }

    #[test]
    fn serializes_approval_request_event() {
        let event = AgentEvent::ApprovalRequest {
            id: "req-1".to_string(),
            tool_name: "bash".to_string(),
            args: json!({"command": "rm -rf /tmp/test"}),
            risk_level: "high".to_string(),
            reason: "destructive command".to_string(),
            warnings: vec!["uses rm -rf".to_string()],
        };
        let as_json = serde_json::to_value(event).expect("event to serialize");
        assert_eq!(as_json["type"], "approval_request");
        assert_eq!(as_json["tool_name"], "bash");
        assert_eq!(as_json["risk_level"], "high");
    }

    #[test]
    fn serializes_plan_created_event() {
        let event = AgentEvent::PlanCreated {
            plan: super::PlanPayload {
                summary: "Build auth system".to_string(),
                steps: vec![
                    super::PlanStepPayload {
                        id: "t1".to_string(),
                        description: "Research OAuth patterns".to_string(),
                        files: vec!["docs/auth.md".to_string()],
                        action: "research".to_string(),
                        depends_on: vec![],
                    },
                    super::PlanStepPayload {
                        id: "t2".to_string(),
                        description: "Implement login".to_string(),
                        files: vec!["src/auth.rs".to_string()],
                        action: "implement".to_string(),
                        depends_on: vec!["t1".to_string()],
                    },
                ],
                estimated_turns: 15,
            },
        };
        let as_json = serde_json::to_value(event).expect("event to serialize");
        assert_eq!(as_json["type"], "plan_created");
        assert_eq!(as_json["plan"]["summary"], "Build auth system");
        assert_eq!(as_json["plan"]["steps"].as_array().unwrap().len(), 2);
        assert_eq!(as_json["plan"]["estimatedTurns"], 15);
        assert_eq!(as_json["plan"]["steps"][0]["action"], "research");
        assert_eq!(as_json["plan"]["steps"][1]["dependsOn"][0], "t1");
    }

    #[test]
    fn serializes_question_request_event() {
        let event = AgentEvent::QuestionRequest {
            id: "q-1".to_string(),
            question: "Which framework?".to_string(),
            options: vec!["React".to_string(), "SolidJS".to_string()],
        };
        let as_json = serde_json::to_value(event).expect("event to serialize");
        assert_eq!(as_json["type"], "question_request");
        assert_eq!(as_json["question"], "Which framework?");
    }
}
