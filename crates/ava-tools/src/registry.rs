use std::collections::HashMap;
use std::pin::Pin;

use async_trait::async_trait;
use ava_types::{AvaError, Result, Tool as ToolDefinition, ToolCall, ToolResult};
use futures::Stream;
use serde_json::Value;
use tracing::instrument;

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
        self.tools.insert(name, Box::new(tool));
    }

    pub fn register_with_source<T>(&mut self, tool: T, source: ToolSource)
    where
        T: Tool + 'static,
    {
        let name = tool.name().to_string();
        self.sources.insert(name.clone(), source);
        self.tools.insert(name, Box::new(tool));
    }

    pub fn unregister(&mut self, name: &str) {
        self.tools.remove(name);
        self.sources.remove(name);
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
            let available = self
                .tools
                .keys()
                .cloned()
                .collect::<Vec<_>>()
                .join(", ");
            AvaError::ToolNotFound {
                tool: tool_call.name.clone(),
                available,
            }
        })?;

        let mut result = tool.execute(tool_call.arguments.clone()).await?;

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
}
