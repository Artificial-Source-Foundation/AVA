use std::path::PathBuf;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

pub struct LspOpsTool {
    manager: Arc<ava_lsp::LspManager>,
}

impl LspOpsTool {
    pub fn new(manager: Arc<ava_lsp::LspManager>) -> Self {
        Self { manager }
    }
}

#[async_trait]
impl Tool for LspOpsTool {
    fn name(&self) -> &str {
        "lsp_ops"
    }

    fn description(&self) -> &str {
        "Use on-demand language-server features like definition, references, hover, and symbols"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["operation"],
            "properties": {
                "operation": {
                    "type": "string",
                    "enum": ["definition", "references", "hover", "document_symbols", "workspace_symbols"]
                },
                "path": { "type": "string", "description": "File path for file-based operations" },
                "line": { "type": "integer", "description": "0-based line number for cursor operations" },
                "character": { "type": "integer", "description": "0-based character offset for cursor operations" },
                "query": { "type": "string", "description": "Search query for workspace symbols" },
                "include_declaration": { "type": "boolean", "description": "Include declarations in reference results" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let operation = args
            .get("operation")
            .and_then(Value::as_str)
            .ok_or_else(|| {
                AvaError::ValidationError("missing required field: operation".to_string())
            })?;

        let content = match operation {
            "definition" => {
                let (path, line, character) = parse_cursor_args(&args)?;
                json!({ "locations": self.manager.definition(&path, line, character).await.map_err(to_tool_error)? })
            }
            "references" => {
                let (path, line, character) = parse_cursor_args(&args)?;
                let include_declaration = args
                    .get("include_declaration")
                    .and_then(Value::as_bool)
                    .unwrap_or(false);
                json!({ "locations": self.manager.references(&path, line, character, include_declaration).await.map_err(to_tool_error)? })
            }
            "hover" => {
                let (path, line, character) = parse_cursor_args(&args)?;
                json!({ "hover": self.manager.hover(&path, line, character).await.map_err(to_tool_error)? })
            }
            "document_symbols" => {
                let path = parse_path(&args)?;
                json!({ "symbols": self.manager.document_symbols(&path).await.map_err(to_tool_error)? })
            }
            "workspace_symbols" => {
                let query = args
                    .get("query")
                    .and_then(Value::as_str)
                    .unwrap_or_default();
                json!({ "symbols": self.manager.workspace_symbols(query).await.map_err(to_tool_error)? })
            }
            other => {
                return Err(AvaError::ValidationError(format!(
                    "unsupported LSP operation: {other}"
                )))
            }
        };

        Ok(ToolResult {
            call_id: String::new(),
            content: content.to_string(),
            is_error: false,
        })
    }
}

fn parse_path(args: &Value) -> ava_types::Result<PathBuf> {
    args.get("path")
        .and_then(Value::as_str)
        .map(PathBuf::from)
        .ok_or_else(|| AvaError::ValidationError("missing required field: path".to_string()))
}

fn parse_cursor_args(args: &Value) -> ava_types::Result<(PathBuf, u32, u32)> {
    let path = parse_path(args)?;
    let line = args
        .get("line")
        .and_then(Value::as_u64)
        .ok_or_else(|| AvaError::ValidationError("missing required field: line".to_string()))?
        as u32;
    let character = args
        .get("character")
        .and_then(Value::as_u64)
        .ok_or_else(|| AvaError::ValidationError("missing required field: character".to_string()))?
        as u32;
    Ok((path, line, character))
}

fn to_tool_error(error: ava_lsp::LspError) -> AvaError {
    AvaError::ToolError(error.to_string())
}
