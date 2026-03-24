//! ExternalWorker — delegates task execution to an external CLI agent (e.g., Claude Code Agent SDK).
//!
//! Instead of running AVA's internal AgentLoop, an ExternalWorker spawns a CLI agent
//! process that runs its own autonomous agent loop. Results are mapped back to AVA's
//! Session format for seamless Praxis session merging.

use ava_cli_providers::bridge::AgentRole;
use ava_cli_providers::config::CLIAgentEvent;
use ava_cli_providers::runner::CLIAgentRunner;
use ava_cli_providers::BridgeOptions;
use ava_types::{Message, Result, Role, Session};
use tokio::sync::mpsc;
use uuid::Uuid;

use crate::events::PraxisEvent;
use crate::{Budget, Task};

/// A worker that delegates to an external CLI agent instead of an internal AgentLoop.
pub struct ExternalWorker {
    pub(crate) id: Uuid,
    pub(crate) lead: String,
    pub(crate) runner: CLIAgentRunner,
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
        runner: CLIAgentRunner,
        budget: Budget,
        task: Task,
        cwd: String,
        role: AgentRole,
        system_prompt: Option<String>,
    ) -> Self {
        let agent_name = runner.config().name.clone();
        Self {
            id: Uuid::new_v4(),
            lead,
            runner,
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
}

impl Clone for ExternalWorker {
    fn clone(&self) -> Self {
        Self {
            id: self.id,
            lead: self.lead.clone(),
            runner: self.runner.clone(),
            budget: self.budget.clone(),
            task: self.task.clone(),
            agent_name: self.agent_name.clone(),
            cwd: self.cwd.clone(),
            role: self.role,
            system_prompt: self.system_prompt.clone(),
        }
    }
}

/// Run an external CLI agent worker, streaming events and returning a Session.
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

    let bridge_opts = BridgeOptions {
        max_turns: Some(worker.budget.max_turns),
        system_prompt: worker.system_prompt.clone(),
        ..Default::default()
    };

    let (tx, mut rx) = tokio::sync::mpsc::channel(256);

    let runner = worker.runner.clone();
    let task_desc = worker.task.description.clone();
    let cwd = worker.cwd.clone();
    let role = worker.role;
    let files: Vec<String> = worker.task.files.clone();
    let files_ref: Option<Vec<String>> = if files.is_empty() { None } else { Some(files) };

    let run_handle = tokio::spawn(async move {
        let files_slice: Option<&[String]> = files_ref.as_deref();
        ava_cli_providers::execute_with_cli_agent_ext(
            &runner,
            &task_desc,
            role,
            &cwd,
            files_slice,
            Some(tx),
            &bridge_opts,
        )
        .await
    });

    // Stream events to Praxis
    let worker_id = worker.id;
    while let Some(event) = rx.recv().await {
        match &event {
            CLIAgentEvent::Text { content } => {
                let _ = event_tx.send(PraxisEvent::ExternalWorkerText {
                    worker_id,
                    content: content.clone(),
                });
            }
            CLIAgentEvent::Assistant { content, .. } => {
                for block in content {
                    match block {
                        ava_cli_providers::ContentBlock::Text { text } => {
                            let _ = event_tx.send(PraxisEvent::ExternalWorkerText {
                                worker_id,
                                content: text.clone(),
                            });
                        }
                        ava_cli_providers::ContentBlock::ToolUse { name, .. } => {
                            let _ = event_tx.send(PraxisEvent::ExternalWorkerToolUse {
                                worker_id,
                                tool_name: name.clone(),
                            });
                        }
                        ava_cli_providers::ContentBlock::Thinking { thinking } => {
                            let _ = event_tx.send(PraxisEvent::ExternalWorkerThinking {
                                worker_id,
                                content: thinking.clone(),
                            });
                        }
                        _ => {}
                    }
                }
            }
            CLIAgentEvent::ToolUse { tool_name, .. } => {
                let _ = event_tx.send(PraxisEvent::ExternalWorkerToolUse {
                    worker_id,
                    tool_name: tool_name.clone(),
                });
            }
            _ => {}
        }
    }

    // Collect result
    let cli_result = run_handle.await.map_err(|e| {
        ava_types::AvaError::ToolError(format!("external worker task failed: {e}"))
    })??;

    // Emit completion event
    let _ = event_tx.send(PraxisEvent::ExternalWorkerCompleted {
        worker_id,
        success: cli_result.success,
        session_id: cli_result.session_id.clone(),
        cost_usd: cli_result.total_cost_usd,
        turns: cli_result.events.len(),
    });

    // Convert to AVA Session
    let mut session = Session::new();
    session.add_message(Message::new(Role::User, worker.task.description.clone()));
    session.add_message(Message::new(Role::Assistant, cli_result.output));

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

    #[test]
    fn external_worker_creation() {
        let config = ava_cli_providers::CLIAgentConfig {
            name: "claude-code".to_string(),
            binary: "claude".to_string(),
            ..Default::default()
        };
        let runner = CLIAgentRunner::new(config);
        let budget = Budget::new(100_000, 20, 5.0);

        let worker = ExternalWorker::new(
            "Backend Lead".to_string(),
            runner,
            budget,
            test_task(),
            "/tmp/project".to_string(),
            AgentRole::Engineer,
            None,
        );

        assert_eq!(worker.lead(), "Backend Lead");
        assert_eq!(worker.agent_name, "claude-code");
        assert_eq!(worker.task().description, "Fix the bug");
        assert_eq!(worker.budget().max_turns, 20);
    }

    #[test]
    fn external_worker_clones() {
        let config = ava_cli_providers::CLIAgentConfig {
            name: "test".to_string(),
            binary: "test".to_string(),
            ..Default::default()
        };
        let runner = CLIAgentRunner::new(config);
        let worker = ExternalWorker::new(
            "QA Lead".to_string(),
            runner,
            Budget::new(50_000, 10, 2.0),
            test_task(),
            "/tmp".to_string(),
            AgentRole::Reviewer,
            Some("Be thorough".to_string()),
        );

        let cloned = worker.clone();
        assert_eq!(cloned.id(), worker.id());
        assert_eq!(cloned.lead(), worker.lead());
        assert_eq!(cloned.system_prompt, Some("Be thorough".to_string()));
    }
}
