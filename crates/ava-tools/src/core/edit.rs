use std::path::Path;
use std::sync::Arc;

use async_trait::async_trait;
use ava_platform::Platform;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::edit::{EditEngine, EditRequest};
use crate::registry::Tool;

pub struct EditTool {
    platform: Arc<dyn Platform>,
    engine: EditEngine,
}

impl EditTool {
    pub fn new(platform: Arc<dyn Platform>) -> Self {
        Self {
            platform,
            engine: EditEngine::new(),
        }
    }
}

#[async_trait]
impl Tool for EditTool {
    fn name(&self) -> &str {
        "edit"
    }

    fn description(&self) -> &str {
        "Edit existing file content"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["path", "old_text", "new_text"],
            "properties": {
                "path": { "type": "string" },
                "old_text": { "type": "string" },
                "new_text": { "type": "string" },
                "replace_all": { "type": "boolean" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let path = args
            .get("path")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: path".to_string()))?;
        let old_text = args
            .get("old_text")
            .and_then(Value::as_str)
            .ok_or_else(|| {
                AvaError::ValidationError("missing required field: old_text".to_string())
            })?;
        let new_text = args
            .get("new_text")
            .and_then(Value::as_str)
            .ok_or_else(|| {
                AvaError::ValidationError("missing required field: new_text".to_string())
            })?;
        let replace_all = args
            .get("replace_all")
            .and_then(Value::as_bool)
            .unwrap_or(false);

        let file_path = Path::new(path);
        let original = self.platform.read_file(file_path).await?;

        let (updated, strategy) = if replace_all {
            let occurrences = original.matches(old_text).count();
            if occurrences == 0 {
                return Err(AvaError::ToolError("No matching text found".to_string()));
            }
            (original.replace(old_text, new_text), "replace_all".to_string())
        } else {
            let request = EditRequest::new(original.clone(), old_text.to_string(), new_text.to_string());
            let result = self
                .engine
                .apply(&request)
                .map_err(|_| AvaError::ToolError("No matching edit strategy found".to_string()))?;
            (result.content, result.strategy)
        };

        self.platform.write_file(file_path, &updated).await?;

        let change_lines = line_diff_count(&original, &updated);
        Ok(ToolResult {
            call_id: String::new(),
            content: format!("Applied {strategy}; changed {change_lines} lines"),
            is_error: false,
        })
    }
}

fn line_diff_count(before: &str, after: &str) -> usize {
    let before_lines: Vec<&str> = before.lines().collect();
    let after_lines: Vec<&str> = after.lines().collect();
    let max_len = before_lines.len().max(after_lines.len());
    (0..max_len)
        .filter(|idx| before_lines.get(*idx) != after_lines.get(*idx))
        .count()
}
