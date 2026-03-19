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

        // Auto-retry read-only tools on transient failures
        if result.is_err() && retry_middleware::is_retryable_tool(&tool_call.name) {
            let original_err = result
                .as_ref()
                .err()
                .map(|e| e.to_string())
                .unwrap_or_default();
            if retry_middleware::is_transient_error(&original_err) {
                for attempt in 0..retry_middleware::MAX_RETRIES {
                    if let Some(backoff) = retry_middleware::backoff_for_attempt(attempt) {
                        debug!(
                            tool = %tool_call.name,
                            attempt = attempt + 1,
                            max = retry_middleware::MAX_RETRIES,
                            backoff_ms = backoff.as_millis() as u64,
                            error = %original_err,
                            "retrying read-only tool after transient error"
                        );
                        tokio::time::sleep(backoff).await;
                        result = tool.execute(tool_call.arguments.clone()).await;
                        if result.is_ok() {
                            debug!(
                                tool = %tool_call.name,
                                attempt = attempt + 1,
                                "retry succeeded"
                            );
                            break;
                        }
                    }
                }
            }
        }

        // Also retry when the tool returns Ok but with is_error=true (soft errors)
        if let Ok(ref tool_result) = result {
            if tool_result.is_error
                && retry_middleware::is_retryable_tool(&tool_call.name)
                && retry_middleware::is_transient_error(&tool_result.content)
            {
                let original_content = tool_result.content.clone();
                for attempt in 0..retry_middleware::MAX_RETRIES {
                    if let Some(backoff) = retry_middleware::backoff_for_attempt(attempt) {
                        debug!(
                            tool = %tool_call.name,
                            attempt = attempt + 1,
                            max = retry_middleware::MAX_RETRIES,
                            backoff_ms = backoff.as_millis() as u64,
                            error = %original_content,
                            "retrying read-only tool after transient soft error"
                        );
                        tokio::time::sleep(backoff).await;
                        result = tool.execute(tool_call.arguments.clone()).await;
                        if let Ok(ref r) = result {
                            if !r.is_error {
                                debug!(
                                    tool = %tool_call.name,
                                    attempt = attempt + 1,
                                    "retry succeeded"
                                );
                                break;
                            }
                        }
                    }
                }
            }
        }

        let mut result = result?;

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
