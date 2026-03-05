use std::collections::HashMap;

use async_trait::async_trait;
use ava_types::{AvaError, Result, Tool as ToolDefinition, ToolCall, ToolResult};
use serde_json::Value;

#[async_trait]
pub trait Tool: Send + Sync {
    fn name(&self) -> &str;
    fn description(&self) -> &str;
    fn parameters(&self) -> Value;
    async fn execute(&self, args: Value) -> Result<ToolResult>;
}

#[async_trait]
pub trait Middleware: Send + Sync {
    async fn before(&self, tool_call: &ToolCall) -> Result<()>;
    /// Returns the next result value in the middleware chain.
    ///
    /// Implementations may transform or replace the incoming result.
    async fn after(&self, tool_call: &ToolCall, result: &ToolResult) -> Result<ToolResult>;
}

#[derive(Default)]
pub struct ToolRegistry {
    tools: HashMap<String, Box<dyn Tool>>,
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
        self.tools.insert(tool.name().to_string(), Box::new(tool));
    }

    pub fn add_middleware<M>(&mut self, middleware: M)
    where
        M: Middleware + 'static,
    {
        self.middleware.push(Box::new(middleware));
    }

    pub async fn execute(&self, tool_call: ToolCall) -> Result<ToolResult> {
        for middleware in &self.middleware {
            middleware.before(&tool_call).await?;
        }

        let tool = self
            .tools
            .get(&tool_call.name)
            .ok_or_else(|| AvaError::NotFound(format!("ToolNotFound: {}", tool_call.name)))?;

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
}
