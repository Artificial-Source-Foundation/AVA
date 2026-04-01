use std::sync::Arc;

use async_trait::async_trait;
use ava_platform::Platform;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::core::file_backup::FileBackupSession;
use crate::registry::Tool;

/// Compute a unified diff between old and new content for display.
fn compute_unified_diff(old: &str, new: &str, path: &str) -> String {
    use similar::TextDiff;
    let diff = TextDiff::from_lines(old, new);
    diff.unified_diff()
        .context_radius(3)
        .header(&format!("a/{path}"), &format!("b/{path}"))
        .to_string()
}

pub struct WriteTool {
    platform: Arc<dyn Platform>,
    backup_session: FileBackupSession,
}

impl WriteTool {
    pub fn new(platform: Arc<dyn Platform>) -> Self {
        Self {
            platform,
            backup_session: crate::core::file_backup::new_backup_session(),
        }
    }

    /// Create a `WriteTool` with a shared backup session for crash-safe undo.
    pub fn with_backup_session(
        platform: Arc<dyn Platform>,
        backup_session: FileBackupSession,
    ) -> Self {
        Self {
            platform,
            backup_session,
        }
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

    fn search_hint(&self) -> &str {
        "create write new file content"
    }

    fn activity_description(&self, args: &Value) -> Option<String> {
        let path = args
            .get("file_path")
            .or_else(|| args.get("path"))
            .and_then(Value::as_str)?;
        Some(format!("Writing {path}"))
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

        let file_path = crate::core::path_guard::enforce_workspace_path(path, "write")?;

        if let Some(parent) = file_path.parent() {
            self.platform.create_dir_all(parent).await?;
        }

        // Persistent backup before mutation (survives crashes).
        if let Err(e) =
            crate::core::file_backup::backup_file_before_edit(&self.backup_session, &file_path)
                .await
        {
            tracing::warn!(path = %path, error = %e, "file backup failed, proceeding with write");
        }

        // Snapshot existing content for diff (empty if new file)
        let old_content = self
            .platform
            .read_file(&file_path)
            .await
            .unwrap_or_default();

        self.platform.write_file(&file_path, content).await?;

        let mut result_content = format!("Wrote {} bytes to {path}", content.len());

        // Include unified diff when overwriting an existing file
        if !old_content.is_empty() && old_content != content {
            let diff = compute_unified_diff(&old_content, content, path);
            if !diff.is_empty() {
                result_content.push_str("\n\n");
                result_content.push_str(&diff);
            }
        }

        Ok(ToolResult {
            call_id: String::new(),
            content: result_content,
            is_error: false,
        })
    }
}
