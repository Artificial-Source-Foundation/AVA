use std::collections::HashMap;
use std::pin::Pin;

use async_trait::async_trait;
use ava_types::{AvaError, Result, Tool as ToolDefinition, ToolCall, ToolResult};
use futures::Stream;
use serde_json::Value;
use tracing::{debug, instrument};

use crate::retry_middleware;

/// Record of a single tool invocation, capturing timing and outcome.
#[derive(Debug, Clone)]
pub struct ToolInvocationRecord {
    pub tool_name: String,
    pub tool_source: ToolSource,
    pub start_time: std::time::Instant,
    pub duration_ms: u64,
    pub success: bool,
    pub error: Option<String>,
}

/// Output from a tool execution — either complete or streaming.
pub enum ToolOutput {
    Complete(ToolResult),
    Streaming(Pin<Box<dyn Stream<Item = String> + Send>>),
}

/// A tool that can be executed by the agent.
///
/// Implementations provide a name, description, JSON Schema parameters,
/// and an async `execute` method. Tools are registered in a [`ToolRegistry`]
/// and invoked by the agent loop when the LLM emits a tool call.
#[async_trait]
pub trait Tool: Send + Sync {
    /// Unique tool name used in LLM tool-call payloads (e.g., "read", "bash").
    fn name(&self) -> &str;
    /// Human-readable description shown to the LLM in the tool list.
    fn description(&self) -> &str;
    /// JSON Schema describing the tool's input parameters.
    fn parameters(&self) -> Value;
    /// Execute the tool with the given arguments, returning a result string.
    async fn execute(&self, args: Value) -> Result<ToolResult>;

    /// Execute with optional streaming output. Default wraps `execute()`.
    async fn execute_streaming(&self, args: Value) -> Result<ToolOutput> {
        self.execute(args).await.map(ToolOutput::Complete)
    }
}

/// Middleware that runs before and after tool execution for cross-cutting concerns
/// such as sandboxing, reliability checks, and error recovery.
#[async_trait]
pub trait Middleware: Send + Sync {
    async fn before(&self, tool_call: &ToolCall) -> Result<()>;
    /// Returns the next result value in the middleware chain.
    ///
    /// Implementations may transform or replace the incoming result.
    async fn after(&self, tool_call: &ToolCall, result: &ToolResult) -> Result<ToolResult>;
}

/// Which tier a tool belongs to — controls whether it's included in the LLM prompt.
///
/// All tiers are always *executable*; the tier only affects which tool definitions
/// are sent to the LLM in the system prompt.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, serde::Serialize, serde::Deserialize)]
pub enum ToolTier {
    /// Always sent to the LLM (read, write, edit, bash, glob, grep).
    Default,
    /// Only sent when extended tools are enabled (apply_patch, web_fetch, multiedit, etc.).
    Extended,
    /// Plugin tools (MCP, custom TOML) — sent when registered.
    Plugin,
}

/// Where a tool came from — used for grouping in `/tools` and selective reload.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ToolSource {
    BuiltIn,
    MCP { server: String },
    Custom { path: String },
}

impl std::fmt::Display for ToolSource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::BuiltIn => write!(f, "built-in"),
            Self::MCP { server } => write!(f, "mcp:{server}"),
            Self::Custom { path } => write!(f, "custom:{path}"),
        }
    }
}

/// Central registry of available tools with middleware pipeline and source tracking.
///
/// Tools are registered with a [`ToolSource`] for grouping (built-in, MCP, custom).
/// Middleware runs in insertion order around every tool execution.
#[derive(Default)]
pub struct ToolRegistry {
    tools: HashMap<String, Box<dyn Tool>>,
    sources: HashMap<String, ToolSource>,
    tiers: HashMap<String, ToolTier>,
    middleware: Vec<Box<dyn Middleware>>,
}

impl ToolRegistry {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn register<T>(&mut self, tool: T)
    where
        T: Tool + 'static,
    {
        let name = tool.name().to_string();
        self.sources.insert(name.clone(), ToolSource::BuiltIn);
        self.tiers.insert(name.clone(), ToolTier::Default);
        self.tools.insert(name, Box::new(tool));
    }

    /// Register a tool with a specific tier. The tier controls whether the tool
    /// definition is sent to the LLM; it does not affect executability.
    pub fn register_with_tier<T>(&mut self, tool: T, tier: ToolTier)
    where
        T: Tool + 'static,
    {
        let name = tool.name().to_string();
        self.sources.insert(name.clone(), ToolSource::BuiltIn);
        self.tiers.insert(name.clone(), tier);
        self.tools.insert(name, Box::new(tool));
    }

    pub fn register_with_source<T>(&mut self, tool: T, source: ToolSource)
    where
        T: Tool + 'static,
    {
        let name = tool.name().to_string();

        // SEC-3: Prevent external tools from shadowing built-in tools
        if let Some(existing_source) = self.sources.get(&name) {
            if *existing_source == ToolSource::BuiltIn && source != ToolSource::BuiltIn {
                tracing::warn!(
                    "Rejecting tool '{name}' from {source} — would shadow built-in tool"
                );
                return;
            }
        }

        let tier = match &source {
            ToolSource::BuiltIn => ToolTier::Default,
            ToolSource::MCP { .. } | ToolSource::Custom { .. } => ToolTier::Plugin,
        };
        self.sources.insert(name.clone(), source);
        self.tiers.insert(name.clone(), tier);
        self.tools.insert(name, Box::new(tool));
    }

    pub fn unregister(&mut self, name: &str) {
        self.tools.remove(name);
        self.sources.remove(name);
        self.tiers.remove(name);
    }

    /// Remove all tools matching a given source predicate.
    pub fn remove_by_source<F>(&mut self, predicate: F)
    where
        F: Fn(&ToolSource) -> bool,
    {
        let to_remove: Vec<String> = self
            .sources
            .iter()
            .filter(|(_, src)| predicate(src))
            .map(|(name, _)| name.clone())
            .collect();
        for name in to_remove {
            self.tools.remove(&name);
            self.sources.remove(&name);
            self.tiers.remove(&name);
        }
    }

    pub fn add_middleware<M>(&mut self, middleware: M)
    where
        M: Middleware + 'static,
    {
        self.middleware.push(Box::new(middleware));
    }

    #[instrument(skip(self), fields(tool = %tool_call.name))]
    pub async fn execute(&self, tool_call: ToolCall) -> Result<ToolResult> {
        for middleware in &self.middleware {
            middleware.before(&tool_call).await?;
        }

        let tool = self.tools.get(&tool_call.name).ok_or_else(|| {
            let available = self.tools.keys().cloned().collect::<Vec<_>>().join(", ");
            AvaError::ToolNotFound {
                tool: tool_call.name.clone(),
                available,
            }
        })?;

        let mut result = tool.execute(tool_call.arguments.clone()).await;
        let mut retry_attempts = 0;

        while retry_attempts < retry_middleware::MAX_RETRIES {
            let Some(error_message) = retryable_tool_failure(&tool_call.name, &result) else {
                break;
            };
            let Some(backoff) = retry_middleware::backoff_for_attempt(retry_attempts) else {
                break;
            };

            retry_attempts += 1;
            debug!(
                tool = %tool_call.name,
                attempt = retry_attempts,
                max = retry_middleware::MAX_RETRIES,
                backoff_ms = backoff.as_millis() as u64,
                error = %error_message,
                "retrying read-only tool after transient failure"
            );
            tokio::time::sleep(backoff).await;
            result = tool.execute(tool_call.arguments.clone()).await;
        }

        if retry_attempts > 0 {
            match &result {
                Ok(tool_result) if !tool_result.is_error => {
                    debug!(
                        tool = %tool_call.name,
                        attempts = retry_attempts,
                        "read-only tool retries recovered successfully"
                    );
                }
                Ok(tool_result) if !retry_middleware::is_transient_error(&tool_result.content) => {
                    debug!(
                        tool = %tool_call.name,
                        attempts = retry_attempts,
                        error = %tool_result.content,
                        "read-only tool retries ended with non-transient soft failure"
                    );
                }
                Err(error) if !retry_middleware::is_transient_error(&error.to_string()) => {
                    debug!(
                        tool = %tool_call.name,
                        attempts = retry_attempts,
                        error = %error,
                        "read-only tool retries ended with non-transient failure"
                    );
                }
                _ => {}
            }
        }

        let mut result = result?;

        // Always normalise call_id to the LLM-assigned tool call ID so that
        // downstream consumers (TUI, trajectory log, summarisation) see a
        // consistent identifier regardless of what each tool implementation
        // returns. MCP bridge tools, for example, emit a fabricated id of the
        // form "mcp-{server}-{tool}" which would mismatch the real call id.
        result.call_id = tool_call.id.clone();

        for middleware in &self.middleware {
            result = middleware.after(&tool_call, &result).await?;
        }

        Ok(result)
    }

    pub fn list_tools(&self) -> Vec<ToolDefinition> {
        let mut tools: Vec<ToolDefinition> = self
            .tools
            .values()
            .map(|tool| ToolDefinition {
                name: tool.name().to_string(),
                description: tool.description().to_string(),
                parameters: tool.parameters(),
            })
            .collect();
        tools.sort_by(|left, right| left.name.cmp(&right.name));
        tools
    }

    /// List only tools matching the given tiers. Used to control which tool
    /// definitions are sent to the LLM in the system prompt.
    pub fn list_tools_for_tiers(&self, tiers: &[ToolTier]) -> Vec<ToolDefinition> {
        let mut tools: Vec<ToolDefinition> = self
            .tools
            .values()
            .filter(|tool| {
                let tier = self
                    .tiers
                    .get(tool.name())
                    .copied()
                    .unwrap_or(ToolTier::Default);
                tiers.contains(&tier)
            })
            .map(|tool| ToolDefinition {
                name: tool.name().to_string(),
                description: tool.description().to_string(),
                parameters: tool.parameters(),
            })
            .collect();
        tools.sort_by(|left, right| left.name.cmp(&right.name));
        tools
    }

    /// List tools with their source information.
    pub fn list_tools_with_source(&self) -> Vec<(ToolDefinition, ToolSource)> {
        let mut tools: Vec<(ToolDefinition, ToolSource)> = self
            .tools
            .values()
            .map(|tool| {
                let name = tool.name().to_string();
                let source = self
                    .sources
                    .get(&name)
                    .cloned()
                    .unwrap_or(ToolSource::BuiltIn);
                (
                    ToolDefinition {
                        name,
                        description: tool.description().to_string(),
                        parameters: tool.parameters(),
                    },
                    source,
                )
            })
            .collect();
        tools.sort_by(|a, b| a.0.name.cmp(&b.0.name));
        tools
    }

    pub fn tool_count(&self) -> usize {
        self.tools.len()
    }

    /// Check whether a tool with the given name is registered.
    pub fn has_tool(&self, name: &str) -> bool {
        self.tools.contains_key(name)
    }

    /// Return the names of all registered tools.
    pub fn tool_names(&self) -> Vec<String> {
        self.tools.keys().cloned().collect()
    }

    /// Look up the source of a registered tool.
    pub fn tool_source(&self, name: &str) -> Option<ToolSource> {
        self.sources.get(name).cloned()
    }

    /// Look up the JSON Schema parameters for a registered tool.
    pub fn tool_parameters(&self, name: &str) -> Option<Value> {
        self.tools.get(name).map(|tool| tool.parameters())
    }

    /// Execute a tool call and return both the result and an invocation record
    /// capturing timing and success/failure metadata.
    #[instrument(skip(self), fields(tool = %tool_call.name))]
    pub async fn execute_tracked(
        &self,
        tool_call: ToolCall,
    ) -> (Result<ToolResult>, ToolInvocationRecord) {
        let start = std::time::Instant::now();
        let tool_name = tool_call.name.clone();
        let source = self.tool_source(&tool_name).unwrap_or(ToolSource::BuiltIn);

        let result = self.execute(tool_call).await;

        let record = ToolInvocationRecord {
            tool_name,
            tool_source: source,
            start_time: start,
            duration_ms: start.elapsed().as_millis() as u64,
            success: match &result {
                Ok(r) => !r.is_error,
                Err(_) => false,
            },
            error: match &result {
                Err(e) => Some(e.to_string()),
                Ok(r) if r.is_error => Some(r.content.clone()),
                _ => None,
            },
        };

        (result, record)
    }
}

fn retryable_tool_failure(tool_name: &str, result: &Result<ToolResult>) -> Option<String> {
    if !retry_middleware::is_retryable_tool(tool_name) {
        return None;
    }

    match result {
        Err(error) => {
            let message = error.to_string();
            retry_middleware::is_transient_error(&message).then_some(message)
        }
        Ok(tool_result) if tool_result.is_error => {
            retry_middleware::is_transient_error(&tool_result.content)
                .then_some(tool_result.content.clone())
        }
        Ok(_) => None,
    }
}

#[cfg(test)]
mod tests {
    use std::collections::VecDeque;
    use std::sync::{Arc, Mutex};

    use super::*;

    #[derive(Clone)]
    enum MockOutcome {
        Hard(Result<ToolResult>),
    }

    struct SequencedTool {
        name: &'static str,
        outcomes: Arc<Mutex<VecDeque<MockOutcome>>>,
        calls: Arc<Mutex<usize>>,
    }

    impl SequencedTool {
        fn new(name: &'static str, outcomes: Vec<MockOutcome>) -> (Self, Arc<Mutex<usize>>) {
            let calls = Arc::new(Mutex::new(0));
            (
                Self {
                    name,
                    outcomes: Arc::new(Mutex::new(outcomes.into())),
                    calls: calls.clone(),
                },
                calls,
            )
        }
    }

    #[async_trait]
    impl Tool for SequencedTool {
        fn name(&self) -> &str {
            self.name
        }

        fn description(&self) -> &str {
            "Sequenced test tool"
        }

        fn parameters(&self) -> Value {
            serde_json::json!({
                "type": "object"
            })
        }

        async fn execute(&self, _args: Value) -> Result<ToolResult> {
            *self.calls.lock().unwrap() += 1;
            match self.outcomes.lock().unwrap().pop_front() {
                Some(MockOutcome::Hard(result)) => result,
                None => Ok(ToolResult {
                    call_id: String::new(),
                    content: "ok".to_string(),
                    is_error: false,
                }),
            }
        }
    }

    #[tokio::test]
    async fn retries_stop_when_follow_up_error_is_non_transient() {
        let mut registry = ToolRegistry::new();
        let (tool, calls) = SequencedTool::new(
            "read",
            vec![
                MockOutcome::Hard(Err(AvaError::TimeoutError("timed out".to_string()))),
                MockOutcome::Hard(Err(AvaError::ValidationError(
                    "invalid argument".to_string(),
                ))),
                MockOutcome::Hard(Err(AvaError::TimeoutError(
                    "should not be retried".to_string(),
                ))),
            ],
        );
        registry.register(tool);

        let result = registry
            .execute(ToolCall {
                id: "call-1".to_string(),
                name: "read".to_string(),
                arguments: serde_json::json!({}),
            })
            .await;

        assert!(matches!(result, Err(AvaError::ValidationError(_))));
        assert_eq!(*calls.lock().unwrap(), 2);
    }

    #[tokio::test]
    async fn retries_stop_when_soft_error_turns_permanent() {
        let mut registry = ToolRegistry::new();
        let (tool, calls) = SequencedTool::new(
            "read",
            vec![
                MockOutcome::Hard(Ok(ToolResult {
                    call_id: String::new(),
                    content: "timeout while reading".to_string(),
                    is_error: true,
                })),
                MockOutcome::Hard(Ok(ToolResult {
                    call_id: String::new(),
                    content: "file not found".to_string(),
                    is_error: true,
                })),
                MockOutcome::Hard(Ok(ToolResult {
                    call_id: String::new(),
                    content: "should not be retried".to_string(),
                    is_error: true,
                })),
            ],
        );
        registry.register(tool);

        let result = registry
            .execute(ToolCall {
                id: "call-2".to_string(),
                name: "read".to_string(),
                arguments: serde_json::json!({}),
            })
            .await
            .unwrap();

        assert!(result.is_error);
        assert_eq!(result.content, "file not found");
        assert_eq!(*calls.lock().unwrap(), 2);
    }
}
