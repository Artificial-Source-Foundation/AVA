use async_trait::async_trait;
use ava_types::ToolResult;
use serde_json::{json, Value};

use crate::registry::Tool;

/// Stub LSP operations tool. Echoes the requested operation and explains that
/// a running language server is required for actual results.
pub struct LspTool;

impl LspTool {
    pub fn new() -> Self {
        Self
    }
}

impl Default for LspTool {
    fn default() -> Self {
        Self::new()
    }
}

#[async_trait]
impl Tool for LspTool {
    fn name(&self) -> &str {
        "lsp_ops"
    }

    fn description(&self) -> &str {
        "Perform LSP operations (definition, references, hover, symbols) on source code"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["operation", "file_path"],
            "properties": {
                "operation": {
                    "type": "string",
                    "enum": ["definition", "references", "hover", "symbols"],
                    "description": "The LSP operation to perform"
                },
                "file_path": {
                    "type": "string",
                    "description": "Path to the source file"
                },
                "line": {
                    "type": "integer",
                    "minimum": 1,
                    "description": "Line number (1-indexed)"
                },
                "column": {
                    "type": "integer",
                    "minimum": 1,
                    "description": "Column number (1-indexed)"
                }
            }
        })
    }

    fn search_hint(&self) -> &str {
        "lsp language server definition references hover symbols type"
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let operation = args
            .get("operation")
            .and_then(Value::as_str)
            .unwrap_or("unknown");

        Ok(ToolResult {
            call_id: String::new(),
            content: format!(
                "LSP operation '{operation}' requested. \
                 LSP operations require a running language server. \
                 Configure via .ava/mcp.json or project settings."
            ),
            is_error: false,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tool_metadata() {
        let tool = LspTool::new();
        assert_eq!(tool.name(), "lsp_ops");
        assert!(!tool.description().is_empty());
    }

    #[test]
    fn parameters_schema_valid() {
        let tool = LspTool::new();
        let params = tool.parameters();
        let required = params["required"].as_array().unwrap();
        assert!(required.iter().any(|v| v.as_str() == Some("operation")));
        assert!(required.iter().any(|v| v.as_str() == Some("file_path")));

        let props = params["properties"].as_object().unwrap();
        assert!(props.contains_key("operation"));
        assert!(props.contains_key("file_path"));
        assert!(props.contains_key("line"));
        assert!(props.contains_key("column"));

        let ops = props["operation"]["enum"].as_array().unwrap();
        let op_strs: Vec<&str> = ops.iter().filter_map(|v| v.as_str()).collect();
        assert!(op_strs.contains(&"definition"));
        assert!(op_strs.contains(&"references"));
        assert!(op_strs.contains(&"hover"));
        assert!(op_strs.contains(&"symbols"));
    }

    #[tokio::test]
    async fn stub_response_includes_operation() {
        let tool = LspTool::new();
        let result = tool
            .execute(serde_json::json!({
                "operation": "hover",
                "file_path": "src/main.rs",
                "line": 10,
                "column": 5
            }))
            .await
            .unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("hover"));
        assert!(result.content.contains("language server"));
    }

    #[test]
    fn search_hint_present() {
        let tool = LspTool::new();
        assert!(tool.search_hint().contains("lsp"));
        assert!(tool.search_hint().contains("definition"));
    }
}
