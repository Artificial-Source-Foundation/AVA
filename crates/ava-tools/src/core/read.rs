use std::path::Path;
use std::sync::Arc;

use async_trait::async_trait;
use ava_platform::Platform;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

/// Default maximum lines returned when no explicit `limit` is provided.
const MAX_LINES_DEFAULT: usize = 2000;

pub struct ReadTool {
    platform: Arc<dyn Platform>,
}

impl ReadTool {
    pub fn new(platform: Arc<dyn Platform>) -> Self {
        Self { platform }
    }
}

#[async_trait]
impl Tool for ReadTool {
    fn name(&self) -> &str {
        "read"
    }

    fn description(&self) -> &str {
        "Read file content with line numbers"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["path"],
            "properties": {
                "path": { "type": "string" },
                "offset": { "type": "integer", "minimum": 1 },
                "limit": { "type": "integer", "minimum": 1 }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let path = args
            .get("path")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: path".to_string()))?;

        let offset = args.get("offset").and_then(Value::as_u64).unwrap_or(1);
        let limit = args.get("limit").and_then(Value::as_u64);

        let content = self
            .platform
            .read_file(Path::new(path))
            .await
            .map_err(|err| match err {
                AvaError::IoError(message) if message.contains("No such file") => {
                    AvaError::NotFound(format!("file not found: {path}"))
                }
                AvaError::IoError(message) if message.contains("Permission denied") => {
                    AvaError::PermissionDenied(format!("permission denied: {path} ({message})"))
                }
                other => other,
            })?;

        let start = usize::try_from(offset.saturating_sub(1)).unwrap_or(usize::MAX);
        let mut lines: Vec<String> = content
            .lines()
            .enumerate()
            .skip(start)
            .map(|(idx, line)| format!("{:>6}\t{line}", idx + 1))
            .collect();

        let cap = limit
            .map(|l| usize::try_from(l).unwrap_or(usize::MAX))
            .unwrap_or(MAX_LINES_DEFAULT);
        let truncated = lines.len() > cap;
        lines.truncate(cap);

        let mut content = lines.join("\n");
        if truncated {
            content.push_str(&format!(
                "\n\n[Truncated: showing first {cap} lines. Use offset/limit to read more.]"
            ));
        }

        Ok(ToolResult {
            call_id: String::new(),
            content,
            is_error: false,
        })
    }
}
