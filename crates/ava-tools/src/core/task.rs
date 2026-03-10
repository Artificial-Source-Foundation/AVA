use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{AvaError, Message, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

/// Result returned by a sub-agent task spawn, containing both the final
/// response text and the full conversation session for storage/display.
#[derive(Debug, Clone)]
pub struct TaskResult {
    /// The sub-agent's final response text (extracted from the last assistant message).
    pub text: String,
    /// Unique session ID for the sub-agent's conversation.
    pub session_id: String,
    /// The sub-agent's full conversation messages (all turns, tool calls, etc.).
    pub messages: Vec<Message>,
}

/// Trait for spawning sub-agent runs from the task tool.
///
/// Defined here (in ava-tools) to avoid circular dependencies —
/// the concrete implementation lives in ava-agent where it has
/// access to AgentLoop and LLM infrastructure.
#[async_trait]
pub trait TaskSpawner: Send + Sync {
    /// Spawn a sub-agent with the given prompt and return a [`TaskResult`]
    /// containing the final response text plus the full conversation.
    async fn spawn(&self, prompt: &str) -> ava_types::Result<TaskResult>;
}

/// Tool that spawns a sub-agent to work on a task autonomously.
///
/// The sub-agent gets a subset of tools (read, write, edit, bash, glob, grep,
/// apply_patch) but NOT task, todo_write, todo_read, or question — preventing
/// infinite recursion and user-facing interactions from child agents.
pub struct TaskTool {
    spawner: Arc<dyn TaskSpawner>,
}

impl TaskTool {
    pub fn new(spawner: Arc<dyn TaskSpawner>) -> Self {
        Self { spawner }
    }
}

#[async_trait]
impl Tool for TaskTool {
    fn name(&self) -> &str {
        "task"
    }

    fn description(&self) -> &str {
        "Spawn a sub-agent to work on a task autonomously. The sub-agent has its own \
         conversation context and access to core tools (read, write, edit, bash, glob, \
         grep, apply_patch). Use this when a task can be cleanly delegated — for example, \
         writing a module, running a test suite, or researching a codebase question. \
         The sub-agent cannot ask the user questions or spawn further sub-agents."
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["prompt"],
            "properties": {
                "prompt": {
                    "type": "string",
                    "description": "The task description for the sub-agent. Be specific and provide enough context for the sub-agent to complete the work independently."
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let prompt = args
            .get("prompt")
            .and_then(Value::as_str)
            .ok_or_else(|| {
                AvaError::ValidationError("missing required field: prompt".to_string())
            })?;

        if prompt.trim().is_empty() {
            return Err(AvaError::ValidationError(
                "prompt cannot be empty".to_string(),
            ));
        }

        let task_result = self.spawner.spawn(prompt).await?;

        Ok(ToolResult {
            call_id: String::new(),
            content: task_result.text,
            is_error: false,
        })
    }
}
