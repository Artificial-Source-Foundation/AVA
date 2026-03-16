use std::path::Path;
use std::sync::Arc;

use async_trait::async_trait;
use ava_platform::Platform;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

pub struct WriteTool {
    platform: Arc<dyn Platform>,
}

impl WriteTool {
    pub fn new(platform: Arc<dyn Platform>) -> Self {
        Self { platform }
    }
}

#[async_trait]
impl Tool for WriteTool {
    fn name(&self) -> &str {
        "write"
    }

    fn description(&self) -> &str {
        "Write content to a file"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["path", "content"],
            "properties": {
                "path": { "type": "string" },
                "content": { "type": "string" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let path = args
            .get("path")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: path".to_string()))?;
        let content = args.get("content").and_then(Value::as_str).ok_or_else(|| {
            AvaError::ValidationError("missing required field: content".to_string())
        })?;

        tracing::debug!(tool = "write", %path, bytes = content.len(), "executing write tool");

        let file_path = Path::new(path);
        if let Some(parent) = file_path.parent() {
            self.platform.create_dir_all(parent).await?;
        }

        // B66 currently snapshots edit-heavy replacement flows (`edit`/`multiedit`).
        // Plain `write` stays unsnapshotted in this conservative slice until we
        // settle broader snapshot coverage and cleanup behavior.
        self.platform.write_file(file_path, content).await?;

        Ok(ToolResult {
            call_id: String::new(),
            content: format!("Wrote {} bytes to {path}", content.len()),
            is_error: false,
        })
    }
}
