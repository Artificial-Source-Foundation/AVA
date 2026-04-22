use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{AvaError, Message, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

/// Internal argument key used to thread the originating tool-call ID through
/// the subagent tool execution path.
pub const INTERNAL_TOOL_CALL_ID_ARG: &str = "__ava_internal_call_id";

tokio::task_local! {
    static ORIGINATING_TOOL_CALL_ID: Option<String>;
}

/// Returns the current originating tool-call ID when the subagent tool is
/// executing in a scoped context.
pub fn current_originating_tool_call_id() -> Option<String> {
    ORIGINATING_TOOL_CALL_ID
        .try_with(|call_id| call_id.clone())
        .ok()
        .flatten()
}

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

/// Trait for spawning sub-agent runs from the subagent tool.
///
/// Defined here (in ava-tools) to avoid circular dependencies —
/// the concrete implementation lives in ava-agent where it has
/// access to AgentLoop and LLM infrastructure.
#[async_trait]
pub trait TaskSpawner: Send + Sync {
    /// Spawn a sub-agent with the given prompt and return a [`TaskResult`]
    /// containing the final response text plus the full conversation.
    async fn spawn(&self, prompt: &str) -> ava_types::Result<TaskResult>;

    /// Spawn a named sub-agent with the given prompt. The `agent_type` is
    /// looked up in `subagents.toml` to
    /// resolve model overrides, custom prompts,
    /// and max turns. Falls back to `spawn()` if the agent type has no special
    /// configuration.
    async fn spawn_named(&self, agent_type: &str, prompt: &str) -> ava_types::Result<TaskResult> {
        // Default implementation delegates to `spawn()` for backward compatibility.
        let _ = agent_type;
        self.spawn(prompt).await
    }

    /// Spawn a sub-agent in the background. Returns immediately; the sub-agent
    /// runs on its own and emits a `SubAgentComplete` event when done.
    /// Returns the session ID of the background sub-agent for tracking.
    async fn spawn_background(&self, agent_type: &str, prompt: &str) -> ava_types::Result<String> {
        // Default: just run inline and return session ID (concrete impl overrides this).
        let result = self.spawn_named(agent_type, prompt).await?;
        Ok(result.session_id)
    }
}

/// Tool that spawns a sub-agent to work on a task autonomously.
///
/// The sub-agent gets a focused execution tool surface and does not get
/// user-facing interactive tools like `question`, `todo_write`, or `todo_read`.
/// Delegation remains bounded by backend depth/spawn policy limits.
///
/// Supports two modes:
/// - **Foreground** (default): blocks the main agent until the sub-agent completes
///   and returns its result inline.
/// - **Background** (`background: true`): spawns the sub-agent in parallel and
///   returns immediately. The main agent keeps working and gets notified when done.
pub struct TaskTool {
    spawner: Arc<dyn TaskSpawner>,
}

impl TaskTool {
    pub fn new(spawner: Arc<dyn TaskSpawner>) -> Self {
        Self { spawner }
    }
}

/// Tool that launches a background agent explicitly.
pub struct BackgroundAgentTool {
    spawner: Arc<dyn TaskSpawner>,
}

impl BackgroundAgentTool {
    pub fn new(spawner: Arc<dyn TaskSpawner>) -> Self {
        Self { spawner }
    }
}

#[async_trait]
impl Tool for TaskTool {
    fn name(&self) -> &str {
        "subagent"
    }

    fn description(&self) -> &str {
        "Spawn a sub-agent to work on a task autonomously. The sub-agent has its own \
          conversation context and access to core tools (read, write, edit, bash, glob, \
          grep, apply_patch). Use this when a chunk of work is easier to delegate than to \
          keep in the main thread — for example, codebase reconnaissance, a focused \
          implementation slice, or a final review pass. Avoid using it for tiny single-file \
          edits. Sub-agents cannot ask the user questions, and any further delegation is bounded by backend depth and spawn-budget policy limits.\n\n\
          By default, the sub-agent runs in the foreground: the main agent waits for it to \
          finish and receives the result. This is the normal/default delegation path. Use the \
          dedicated `background_agent` tool only when the user explicitly asks for background, \
          parallel, or non-blocking delegation, or when that requirement is otherwise explicit. \
          Do not choose background delegation by default. The legacy `background: true` flag \
          remains supported for compatibility.\n\n\
          Optionally specify an agent type (for example `scout`, `explore`, `plan`, `review`, \
          `worker`, or `build`) to use a specialist with its own model, prompt, and turn limits \
          configured in subagents.toml."
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["prompt"],
            "properties": {
                "prompt": {
                    "type": "string",
                    "description": "The task description for the sub-agent. Be specific and provide enough context for the sub-agent to complete the work independently."
                },
                "agent": {
                    "type": "string",
                    "description": "Optional agent type to use (e.g. 'scout', 'explore', 'plan', 'review', 'worker', or 'build'). Each agent type can have its own model, system prompt, and turn limits configured in subagents.toml. Defaults to 'subagent'."
                },
                "background": {
                    "type": "boolean",
                    "description": "Legacy compatibility flag. Set to true only when background/non-blocking delegation is explicitly desired. The normal/default path is foreground blocking delegation (`false`). Prefer the dedicated `background_agent` tool for new calls."
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let prompt = args.get("prompt").and_then(Value::as_str).ok_or_else(|| {
            AvaError::ValidationError("missing required field: prompt".to_string())
        })?;

        if prompt.trim().is_empty() {
            return Err(AvaError::ValidationError(
                "prompt cannot be empty".to_string(),
            ));
        }

        let agent_type = args
            .get("agent")
            .and_then(Value::as_str)
            .unwrap_or("subagent");
        let background = args
            .get("background")
            .and_then(Value::as_bool)
            .unwrap_or(false);
        let originating_call_id = args
            .get(INTERNAL_TOOL_CALL_ID_ARG)
            .and_then(Value::as_str)
            .filter(|value| !value.trim().is_empty())
            .map(str::to_owned);

        if background {
            tracing::debug!(
                tool = "subagent",
                mode = "background",
                "spawning background sub-agent"
            );
            if let Some(call_id) = originating_call_id {
                ORIGINATING_TOOL_CALL_ID
                    .scope(
                        Some(call_id),
                        self.spawner.spawn_background(agent_type, prompt),
                    )
                    .await?;
            } else {
                self.spawner.spawn_background(agent_type, prompt).await?;
            }
            Ok(ToolResult {
                call_id: String::new(),
                content: "Background sub-agent launched. \
                     You will be notified when it completes. Continue with other work."
                    .to_string(),
                is_error: false,
            })
        } else {
            tracing::debug!(
                tool = "subagent",
                mode = "foreground",
                "spawning foreground sub-agent"
            );
            let task_result = if let Some(call_id) = originating_call_id {
                ORIGINATING_TOOL_CALL_ID
                    .scope(Some(call_id), self.spawner.spawn_named(agent_type, prompt))
                    .await?
            } else {
                self.spawner.spawn_named(agent_type, prompt).await?
            };
            Ok(ToolResult {
                call_id: String::new(),
                content: task_result.text,
                is_error: false,
            })
        }
    }
}

#[async_trait]
impl Tool for BackgroundAgentTool {
    fn name(&self) -> &str {
        "background_agent"
    }

    fn description(&self) -> &str {
        "Launch a background agent that works independently while the main agent keeps going. Use this only when the user explicitly asked for background, parallel, or non-blocking delegation, or when that requirement is otherwise explicit. Do not use this as the default delegation path. When it finishes, AVA surfaces completion in the UI and routes a summary back to the parent run. Prefer the blocking `subagent` tool for normal delegation."
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["prompt"],
            "properties": {
                "prompt": {
                    "type": "string",
                    "description": "The task description for the background agent. Be specific and keep it independent from the parent's immediate critical path. Use this tool only for explicitly requested background/non-blocking work."
                },
                "agent": {
                    "type": "string",
                    "description": "Optional background agent type to use (e.g. 'scout', 'explore', 'review', 'worker', or 'build'). Defaults to 'subagent'."
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let prompt = args.get("prompt").and_then(Value::as_str).ok_or_else(|| {
            AvaError::ValidationError("missing required field: prompt".to_string())
        })?;

        if prompt.trim().is_empty() {
            return Err(AvaError::ValidationError(
                "prompt cannot be empty".to_string(),
            ));
        }

        let agent_type = args
            .get("agent")
            .and_then(Value::as_str)
            .unwrap_or("subagent");
        let originating_call_id = args
            .get(INTERNAL_TOOL_CALL_ID_ARG)
            .and_then(Value::as_str)
            .filter(|value| !value.trim().is_empty())
            .map(str::to_owned);

        if let Some(call_id) = originating_call_id {
            ORIGINATING_TOOL_CALL_ID
                .scope(
                    Some(call_id),
                    self.spawner.spawn_background(agent_type, prompt),
                )
                .await?;
        } else {
            self.spawner.spawn_background(agent_type, prompt).await?;
        }

        Ok(ToolResult {
            call_id: String::new(),
            content: "Background agent launched. Continue with the main task; AVA will surface completion when it finishes.".to_string(),
            is_error: false,
        })
    }
}
