use std::collections::BTreeMap;
use std::path::Path;
use std::sync::Arc;

use async_trait::async_trait;
use ava_platform::Platform;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::edit::{EditEngine, EditRequest};
use crate::git::GhostSnapshotter;
use crate::registry::Tool;

pub struct MultiEditTool {
    platform: Arc<dyn Platform>,
    engine: EditEngine,
    snapshotter: GhostSnapshotter,
}

impl MultiEditTool {
    pub fn new(platform: Arc<dyn Platform>) -> Self {
        Self {
            platform,
            engine: EditEngine::new(),
            snapshotter: GhostSnapshotter::new(),
        }
    }
}

#[async_trait]
impl Tool for MultiEditTool {
    fn name(&self) -> &str {
        "multiedit"
    }

    fn description(&self) -> &str {
        "Apply multiple edits across one or more files atomically"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["edits"],
            "properties": {
                "edits": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "required": ["path", "old_text", "new_text"],
                        "properties": {
                            "path": { "type": "string" },
                            "old_text": { "type": "string" },
                            "new_text": { "type": "string" }
                        }
                    }
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let edits = args.get("edits").and_then(Value::as_array).ok_or_else(|| {
            AvaError::ValidationError("missing required field: edits".to_string())
        })?;

        tracing::debug!(
            tool = "multiedit",
            edit_count = edits.len(),
            "executing multiedit tool"
        );

        if edits.is_empty() {
            return Err(AvaError::ValidationError(
                "edits array must not be empty".to_string(),
            ));
        }

        // Parse all edits
        let mut parsed: Vec<(&str, &str, &str)> = Vec::with_capacity(edits.len());
        for edit in edits {
            let path = edit
                .get("path")
                .and_then(Value::as_str)
                .ok_or_else(|| AvaError::ValidationError("edit missing field: path".to_string()))?;
            let old_text = edit
                .get("old_text")
                .and_then(Value::as_str)
                .ok_or_else(|| {
                    AvaError::ValidationError("edit missing field: old_text".to_string())
                })?;
            let new_text = edit
                .get("new_text")
                .and_then(Value::as_str)
                .ok_or_else(|| {
                    AvaError::ValidationError("edit missing field: new_text".to_string())
                })?;
            parsed.push((path, old_text, new_text));
        }

        // Group edits by file path, preserving order
        let mut by_file: BTreeMap<&str, Vec<(&str, &str)>> = BTreeMap::new();
        for (path, old_text, new_text) in &parsed {
            by_file.entry(path).or_default().push((old_text, new_text));
        }

        // Validation pass: read all files and check all edits can be applied
        let mut file_contents: BTreeMap<&str, String> = BTreeMap::new();
        let mut failures: Vec<String> = Vec::new();

        for (path, edits_for_file) in &by_file {
            let file_path = Path::new(path);
            let content = match self.platform.read_file(file_path).await {
                Ok(c) => c,
                Err(e) => {
                    failures.push(format!("{path}: cannot read file: {e}"));
                    continue;
                }
            };

            // Validate each edit can be applied sequentially
            let mut working = content.clone();
            for (old_text, new_text) in edits_for_file {
                let request =
                    EditRequest::new(working.clone(), old_text.to_string(), new_text.to_string());
                match self.engine.apply(&request) {
                    Ok(result) => working = result.content,
                    Err(_) => {
                        let snippet = if old_text.len() > 60 {
                            format!("{}...", &old_text[..60])
                        } else {
                            old_text.to_string()
                        };
                        failures.push(format!("{path}: no match for \"{snippet}\""));
                    }
                }
            }

            file_contents.insert(path, content);
        }

        if !failures.is_empty() {
            return Err(AvaError::ToolError(format!(
                "Validation failed for {} edit(s):\n{}",
                failures.len(),
                failures.join("\n")
            )));
        }

        // Apply pass: all edits validated, now apply and write
        let mut total_edits = 0usize;
        let file_count = by_file.len();
        let mut snapshot_count = 0usize;
        let mut snapshot_warnings: Vec<String> = Vec::new();

        for (path, edits_for_file) in &by_file {
            let original = file_contents.remove(path).ok_or_else(|| {
                AvaError::ToolError(format!("{path}: file content missing after validation"))
            })?;
            let mut working = original.clone();
            for (old_text, new_text) in edits_for_file {
                let request = EditRequest::new(working, old_text.to_string(), new_text.to_string());
                let result = self.engine.apply(&request).map_err(|e| {
                    AvaError::ToolError(format!("{path}: validated edit failed: {e}"))
                })?;
                working = result.content;
                total_edits += 1;
            }
            match self
                .snapshotter
                .snapshot_file_before_write(Path::new(path), &original)
                .await
            {
                Ok(Some(_)) => snapshot_count += 1,
                Ok(None) => {}
                Err(err) => {
                    snapshot_warnings.push(format!("{path}: {err}"));
                }
            }
            self.platform.write_file(Path::new(path), &working).await?;
        }

        let snapshot_note = if snapshot_warnings.is_empty() {
            String::new()
        } else {
            format!(
                "; ghost snapshot unavailable ({})",
                snapshot_warnings.join("; ")
            )
        };

        Ok(ToolResult {
            call_id: String::new(),
            content: format!(
                "Applied {total_edits} edits across {file_count} files; ghost snapshots: {snapshot_count}{snapshot_note}"
            ),
            is_error: false,
        })
    }
}
