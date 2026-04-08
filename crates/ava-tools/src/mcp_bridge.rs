use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{Result, Tool as ToolDefinition, ToolResult};
use serde_json::Value;

use crate::registry::Tool;

/// Trait for calling MCP tools — abstracts the MCP manager so ava-tools
/// doesn't depend on ava-mcp directly.
#[async_trait]
pub trait MCPToolCaller: Send + Sync {
    async fn call_tool(&self, name: &str, arguments: Value) -> Result<ToolResult>;
}

/// A bridge tool that wraps an MCP server tool and implements the ava-tools `Tool` trait.
/// This allows MCP tools to be registered in the `ToolRegistry` and called like any
/// other built-in tool.
///
/// Tools are namespaced as `mcp_{server_name}_{tool_name}` to prevent collisions
/// between different MCP servers. The original tool name is preserved for dispatch
/// to the MCP server.
pub struct MCPBridgeTool {
    /// Namespaced name: `mcp_{server}_{tool}`
    namespaced_name: String,
    /// Original tool name as known by the MCP server (used for dispatch).
    original_name: String,
    definition: ToolDefinition,
    caller: Arc<dyn MCPToolCaller>,
}

impl MCPBridgeTool {
    pub fn new(
        definition: ToolDefinition,
        caller: Arc<dyn MCPToolCaller>,
        server_name: &str,
    ) -> Self {
        // Use underscores instead of dots — OpenAI requires tool names to match ^[a-zA-Z0-9_-]+$
        let namespaced_name = format!("mcp_{}_{}", server_name, definition.name);
        let original_name = definition.name.clone();
        Self {
            namespaced_name,
            original_name,
            definition,
            caller,
        }
    }
}

#[async_trait]
impl Tool for MCPBridgeTool {
    fn name(&self) -> &str {
        &self.namespaced_name
    }

    fn description(&self) -> &str {
        &self.definition.description
    }

    fn parameters(&self) -> Value {
        self.definition.parameters.clone()
    }

    async fn execute(&self, args: Value) -> Result<ToolResult> {
        // Dispatch using the original (non-namespaced) name that the MCP server knows.
        self.caller.call_tool(&self.original_name, args).await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    struct MockCaller;

    #[async_trait]
    impl MCPToolCaller for MockCaller {
        async fn call_tool(&self, name: &str, _arguments: Value) -> Result<ToolResult> {
            Ok(ToolResult {
                call_id: format!("mock-{name}"),
                content: format!("result from {name}"),
                is_error: false,
            })
        }
    }

    #[tokio::test]
    async fn bridge_tool_delegates_to_caller() {
        let caller: Arc<dyn MCPToolCaller> = Arc::new(MockCaller);
        let tool = MCPBridgeTool::new(
            ToolDefinition {
                name: "test_tool".to_string(),
                description: "A test MCP tool".to_string(),
                parameters: json!({"type": "object"}),
            },
            caller,
            "my_server",
        );

        // Name should be namespaced
        assert_eq!(tool.name(), "mcp_my_server_test_tool");
        assert_eq!(tool.description(), "A test MCP tool");

        // Execute should dispatch using the original name
        let result = tool.execute(json!({})).await.unwrap();
        assert_eq!(result.content, "result from test_tool");
        assert!(!result.is_error);
    }

    #[tokio::test]
    async fn bridge_tool_in_registry() {
        use crate::registry::ToolRegistry;

        let caller: Arc<dyn MCPToolCaller> = Arc::new(MockCaller);
        let mut registry = ToolRegistry::new();

        registry.register(MCPBridgeTool::new(
            ToolDefinition {
                name: "weather".to_string(),
                description: "Get weather".to_string(),
                parameters: json!({"type": "object", "properties": {"city": {"type": "string"}}}),
            },
            caller,
            "acme",
        ));

        let tools = registry.list_tools();
        assert_eq!(tools.len(), 1);
        assert_eq!(tools[0].name, "mcp_acme_weather");

        let result = registry
            .execute(ava_types::ToolCall {
                id: "call-1".to_string(),
                name: "mcp_acme_weather".to_string(),
                arguments: json!({"city": "London"}),
            })
            .await
            .unwrap();
        assert_eq!(result.content, "result from weather");
    }
}
