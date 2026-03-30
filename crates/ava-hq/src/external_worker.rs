//! ExternalWorker — delegates task execution to an external agent via ACP transport.
//!
//! Instead of running AVA's internal AgentLoop, an ExternalWorker uses an
//! `AgentTransport` to communicate with an external agent (Claude Code, Codex, etc.).
//! Results are mapped back to AVA's Session format for seamless HQ session merging.

use ava_acp::transport::AgentTransport;
use ava_acp::{
    attach_delegation_record, protocol::AgentMessage, AgentQuery, ContentBlock,
    ExternalRunDescriptor, ExternalSessionMapper, PermissionMode,
};
use ava_types::{DelegationRecord, Message, Result, Role, Session};
use futures::StreamExt;
use tokio::sync::mpsc;
use tokio::time::{timeout, Duration};
use uuid::Uuid;

use crate::events::HqEvent;
use crate::{Budget, Task};

const EXTERNAL_WORKER_IDLE_TIMEOUT_SECS: u64 = 90;

/// Role determines tool scoping and timeout for external workers.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AgentRole {
    /// Full tool access, longer timeout.
    Engineer,
    /// Read-only tools, shorter timeout.
    Reviewer,
    /// Limited tools, shortest timeout.
    Subagent,
}

/// A worker that delegates to an external agent via ACP transport.
#[allow(dead_code)]
pub struct ExternalWorker {
    pub(crate) id: Uuid,
    pub(crate) lead: String,
    pub(crate) transport: Box<dyn AgentTransport>,
    pub(crate) budget: Budget,
    pub(crate) task: Task,
    pub(crate) agent_name: String,
    pub(crate) cwd: String,
    pub(crate) role: AgentRole,
    pub(crate) system_prompt: Option<String>,
}

impl ExternalWorker {
    pub fn new(
        lead: String,
        transport: Box<dyn AgentTransport>,
        budget: Budget,
        task: Task,
        cwd: String,
        role: AgentRole,
        system_prompt: Option<String>,
    ) -> Self {
        let agent_name = transport.name().to_string();
        Self {
            id: Uuid::new_v4(),
            lead,
            transport,
            budget,
            task,
            agent_name,
            cwd,
            role,
            system_prompt,
        }
    }

    pub fn id(&self) -> Uuid {
        self.id
    }

    pub fn lead(&self) -> &str {
        &self.lead
    }

    pub fn budget(&self) -> &Budget {
        &self.budget
    }

    pub fn task(&self) -> &Task {
        &self.task
    }

    /// Build an `AgentQuery` from the worker's task and configuration.
    #[allow(dead_code)]
    fn build_query(&self) -> AgentQuery {
        let permission_mode = match self.role {
            AgentRole::Engineer => Some(PermissionMode::AcceptEdits),
            AgentRole::Reviewer => Some(PermissionMode::Plan),
            AgentRole::Subagent => Some(PermissionMode::Default),
        };

        let allowed_tools = match self.role {
            AgentRole::Engineer => None, // Full access
            AgentRole::Reviewer => Some(vec!["Read".into(), "Glob".into(), "Grep".into()]),
            AgentRole::Subagent => Some(vec![
                "Read".into(),
                "Glob".into(),
                "Grep".into(),
                "WebSearch".into(),
                "WebFetch".into(),
            ]),
        };

        AgentQuery {
            prompt: self.task.description.clone(),
            system_prompt: self.system_prompt.clone(),
            working_directory: Some(self.cwd.clone()),
            max_turns: Some(self.budget.max_turns),
            permission_mode,
            allowed_tools,
            disallowed_tools: None,
            session_id: None,
            resume: false,
            model: None,
            max_budget_usd: Some(self.budget.max_cost_usd),
        }
    }
}

/// Run an external agent worker, streaming events and returning a Session.
#[allow(dead_code)]
pub(crate) async fn run_external_worker(
    worker: &ExternalWorker,
    event_tx: mpsc::UnboundedSender<HqEvent>,
) -> Result<Session> {
    let _ = event_tx.send(HqEvent::ExternalWorkerStarted {
        worker_id: worker.id,
        lead: worker.lead.clone(),
        agent_name: worker.agent_name.clone(),
        task_description: worker.task.description.clone(),
    });

    let query = worker.build_query();
    let mut stream = worker.transport.query(query).await?;

    let worker_id = worker.id;
    let mut event_count = 0usize;
    let mut success = true;
    let started_at = std::time::Instant::now();
    let mut mapper = ExternalSessionMapper::new(ExternalRunDescriptor {
        provider: Some(worker.agent_name.clone()),
        agent_name: Some(worker.agent_name.clone()),
        model: None,
        cwd: Some(worker.cwd.clone()),
        resume_attempted: false,
    });

    while let Some(msg) = timeout(
        Duration::from_secs(EXTERNAL_WORKER_IDLE_TIMEOUT_SECS),
        stream.next(),
    )
    .await
    .map_err(|_| {
        ava_types::AvaError::ToolError(format!(
            "External worker '{}' timed out waiting for output",
            worker.agent_name
        ))
    })? {
        event_count += 1;
        match &msg {
            AgentMessage::Assistant { content, .. } => {
                for block in content {
                    match block {
                        ContentBlock::Text { text } => {
                            let _ = event_tx.send(HqEvent::ExternalWorkerText {
                                worker_id,
                                content: text.clone(),
                            });
                        }
                        ContentBlock::ToolUse { name, .. } => {
                            let _ = event_tx.send(HqEvent::ExternalWorkerToolUse {
                                worker_id,
                                tool_name: name.clone(),
                            });
                        }
                        ContentBlock::Thinking { thinking } => {
                            let _ = event_tx.send(HqEvent::ExternalWorkerThinking {
                                worker_id,
                                content: thinking.clone(),
                            });
                        }
                        _ => {}
                    }
                }
            }
            AgentMessage::Error { message, .. } => {
                success = false;
                let _ = event_tx.send(HqEvent::ExternalWorkerFailed {
                    worker_id,
                    error: message.clone(),
                });
            }
            _ => {}
        }
        mapper.apply(msg)?;
    }

    let mut session = mapper.into_session();
    let session_id = session
        .metadata
        .get("externalSessionId")
        .and_then(|value| value.as_str())
        .map(ToString::to_string);
    let cost_usd = session
        .metadata
        .get("externalCostUsd")
        .and_then(|value| value.as_f64());

    // Emit completion event
    let _ = event_tx.send(HqEvent::ExternalWorkerCompleted {
        worker_id,
        success,
        session_id: session_id.clone(),
        cost_usd,
        turns: event_count,
    });

    // Convert to AVA Session
    session.metadata["externalAgent"] = serde_json::Value::String(worker.agent_name.clone());
    session.metadata["lead"] = serde_json::Value::String(worker.lead.clone());
    session.add_message(Message::new(Role::User, worker.task.description.clone()));
    let delegation_record = DelegationRecord {
        agent_type: Some(format!("hq-{:?}", worker.role).to_lowercase()),
        provider: Some(worker.agent_name.clone()),
        parent_session_id: None,
        child_session_id: Some(session.id.to_string()),
        external_session_id: session_id,
        policy_reason: Some("hq external worker".to_string()),
        policy_version: Some("v1".to_string()),
        latency_ms: Some(started_at.elapsed().as_millis() as u64),
        resumed: false,
        input_tokens: Some(session.token_usage.input_tokens),
        output_tokens: Some(session.token_usage.output_tokens),
        cost_usd,
        outcome: Some(if success { "success" } else { "error" }.to_string()),
    };
    attach_delegation_record(&mut session, &delegation_record);

    Ok(session)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::TaskType;

    fn test_task() -> Task {
        Task {
            description: "Fix the bug".to_string(),
            task_type: TaskType::CodeGeneration,
            files: vec!["src/main.rs".to_string()],
        }
    }

    // Mock transport for testing
    struct MockTransport;

    #[async_trait::async_trait]
    impl AgentTransport for MockTransport {
        async fn query(
            &self,
            _query: AgentQuery,
        ) -> Result<ava_acp::transport::AgentMessageStream> {
            let stream = futures::stream::iter(vec![
                AgentMessage::Assistant {
                    content: vec![ContentBlock::Text {
                        text: "Fixed!".into(),
                    }],
                    session_id: None,
                },
                AgentMessage::Result {
                    result: "done".into(),
                    details: Default::default(),
                },
            ]);
            Ok(Box::pin(stream))
        }

        fn name(&self) -> &str {
            "mock-agent"
        }
    }

    #[test]
    fn external_worker_creation() {
        let transport = Box::new(MockTransport);
        let budget = Budget::new(100_000, 20, 5.0);

        let worker = ExternalWorker::new(
            "Backend Lead".to_string(),
            transport,
            budget,
            test_task(),
            "/tmp/project".to_string(),
            AgentRole::Engineer,
            None,
        );

        assert_eq!(worker.lead(), "Backend Lead");
        assert_eq!(worker.agent_name, "mock-agent");
        assert_eq!(worker.task().description, "Fix the bug");
        assert_eq!(worker.budget().max_turns, 20);
    }

    #[tokio::test]
    async fn run_external_worker_streams_events() {
        let transport = Box::new(MockTransport);
        let budget = Budget::new(100_000, 10, 2.0);
        let worker = ExternalWorker::new(
            "QA Lead".to_string(),
            transport,
            budget,
            test_task(),
            "/tmp".to_string(),
            AgentRole::Reviewer,
            None,
        );

        let (tx, mut rx) = mpsc::unbounded_channel();
        let session = run_external_worker(&worker, tx).await.unwrap();

        // Collect events
        let mut events = Vec::new();
        while let Ok(event) = rx.try_recv() {
            events.push(event);
        }

        // Should have started + text + completed
        assert!(events.len() >= 2);
        assert!(matches!(&events[0], HqEvent::ExternalWorkerStarted { .. }));

        // Session keeps the user prompt plus structured external assistant output.
        assert!(session.messages.len() >= 2);
        assert!(session.metadata.get("delegation").is_some());
    }
}
