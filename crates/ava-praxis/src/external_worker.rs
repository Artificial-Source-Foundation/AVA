//! ExternalWorker — delegates task execution to an external agent via ACP transport.
//!
//! Instead of running AVA's internal AgentLoop, an ExternalWorker uses an
//! `AgentTransport` to communicate with an external agent (Claude Code, Codex, etc.).
//! Results are mapped back to AVA's Session format for seamless Praxis session merging.

use ava_acp::protocol::{AgentMessage, AgentQuery, ContentBlock, PermissionMode};
use ava_acp::transport::AgentTransport;
use ava_types::{Message, Result, Role, Session};
use futures::StreamExt;
use tokio::sync::mpsc;
use uuid::Uuid;

use crate::events::PraxisEvent;
use crate::{Budget, Task};

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
        }
    }
}

/// Run an external agent worker, streaming events and returning a Session.
#[allow(dead_code)]
pub(crate) async fn run_external_worker(
    worker: &ExternalWorker,
    event_tx: mpsc::UnboundedSender<PraxisEvent>,
) -> Result<Session> {
    let _ = event_tx.send(PraxisEvent::ExternalWorkerStarted {
        worker_id: worker.id,
        lead: worker.lead.clone(),
        agent_name: worker.agent_name.clone(),
        task_description: worker.task.description.clone(),
    });

    let query = worker.build_query();
    let mut stream = worker.transport.query(query).await?;

    let worker_id = worker.id;
    let mut output = String::new();
    let mut session_id = None;
    let mut cost_usd = None;
    let mut event_count = 0usize;

    while let Some(msg) = stream.next().await {
        event_count += 1;
        match &msg {
            AgentMessage::Assistant { content, .. } => {
                for block in content {
                    match block {
                        ContentBlock::Text { text } => {
                            output.push_str(text);
                            let _ = event_tx.send(PraxisEvent::ExternalWorkerText {
                                worker_id,
                                content: text.clone(),
                            });
                        }
                        ContentBlock::ToolUse { name, .. } => {
                            let _ = event_tx.send(PraxisEvent::ExternalWorkerToolUse {
                                worker_id,
                                tool_name: name.clone(),
                            });
                        }
                        ContentBlock::Thinking { thinking } => {
                            let _ = event_tx.send(PraxisEvent::ExternalWorkerThinking {
                                worker_id,
                                content: thinking.clone(),
                            });
                        }
                        _ => {}
                    }
                }
            }
            AgentMessage::Result { result, details } => {
                output.push_str(result);
                session_id = details.session_id.clone();
                cost_usd = details.total_cost_usd;
            }
            AgentMessage::Error { message, .. } => {
                output.push_str(&format!("Error: {message}"));
            }
            _ => {}
        }
    }

    // Emit completion event
    let _ = event_tx.send(PraxisEvent::ExternalWorkerCompleted {
        worker_id,
        success: true,
        session_id,
        cost_usd,
        turns: event_count,
    });

    // Convert to AVA Session
    let mut session = Session::new();
    session.add_message(Message::new(Role::User, worker.task.description.clone()));
    session.add_message(Message::new(Role::Assistant, output));

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
        assert!(matches!(
            &events[0],
            PraxisEvent::ExternalWorkerStarted { .. }
        ));

        // Session should have user + assistant messages
        assert_eq!(session.messages.len(), 2);
    }
}
