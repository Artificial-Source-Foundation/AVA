use std::path::{Path, PathBuf};
use std::sync::Arc;

use async_trait::async_trait;
use ava_platform::Platform;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::core::hashline::{self, HashlineCache};
use crate::edit::{EditEngine, EditRequest};
use crate::git::GhostSnapshotter;
use crate::registry::Tool;

pub struct EditTool {
    platform: Arc<dyn Platform>,
    engine: EditEngine,
    hashline_cache: HashlineCache,
    snapshotter: GhostSnapshotter,
}

impl EditTool {
    pub fn new(platform: Arc<dyn Platform>, hashline_cache: HashlineCache) -> Self {
        Self {
            platform,
            engine: EditEngine::new(),
            hashline_cache,
            snapshotter: GhostSnapshotter::new(),
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

        tracing::debug!(tool = "edit", %path, %replace_all, "executing edit tool");

        let file_path = Path::new(path);
        let original = self.platform.read_file(file_path).await?;

        // Strategy 0: Try hash-anchored resolution before the fuzzy cascade.
        // Strip hash anchors from new_text as well (LLM may copy them).
        let resolved_old = self.try_resolve_hashline(path, old_text)?;
        let effective_old = resolved_old.as_deref().unwrap_or(old_text);
        let effective_new = hashline::strip_hashes(new_text);

        let (updated, strategy) = if replace_all {
            let occurrences = original.matches(effective_old).count();
            if occurrences == 0 {
                return Err(AvaError::ToolError("No matching text found".to_string()));
            }
            (
                original.replace(effective_old, &effective_new),
                if resolved_old.is_some() {
                    "hashline_replace_all".to_string()
                } else {
                    "replace_all".to_string()
                },
            )
        } else {
            let request = EditRequest::new(
                original.clone(),
                effective_old.to_string(),
                effective_new.to_string(),
            );
            let result = self
                .engine
                .apply(&request)
                .map_err(|e| AvaError::ToolError(format!("edit matching failed: {e}")))?;
            let strategy = if resolved_old.is_some() {
                format!("hashline+{}", result.strategy)
            } else {
                result.strategy
            };
            (result.content, strategy)
        };

        let snapshot_note = match self
            .snapshotter
            .snapshot_file_before_write(file_path, &original)
            .await
        {
            Ok(Some(snapshot)) => format!("; ghost snapshot {}", snapshot.ref_name),
            Ok(None) => String::new(),
            Err(err) => format!("; ghost snapshot unavailable ({err})"),
        };

        self.platform.write_file(file_path, &updated).await?;

        let change_lines = line_diff_count(&original, &updated);
        Ok(ToolResult {
            call_id: String::new(),
            content: format!("Applied {strategy}; changed {change_lines} lines{snapshot_note}"),
            is_error: false,
        })
    }
}

impl EditTool {
    /// Try to resolve hash anchors in `old_text` using the hashline cache.
    ///
    /// Returns `Ok(Some(resolved))` if hash anchors were found and resolved,
    /// `Ok(None)` if no hash anchors present (fall through to normal matching),
    /// or `Err` if hashes are stale or not found.
    fn try_resolve_hashline(
        &self,
        path: &str,
        old_text: &str,
    ) -> ava_types::Result<Option<String>> {
        let cache = self
            .hashline_cache
            .read()
            .map_err(|e| AvaError::ToolError(format!("hashline cache lock poisoned: {e}")))?;

        let path_buf = PathBuf::from(path);
        let Some(entries) = cache.get(&path_buf) else {
            return Ok(None); // No cache for this file — skip hashline resolution
        };

        match hashline::resolve_anchors(old_text, entries) {
            Ok(resolved) => Ok(resolved),
            Err(hashline::HashlineError::StaleFile {
                hash,
                expected,
                actual,
            }) => Err(AvaError::ToolError(format!(
                "Stale file: hash [{hash}] expected \"{expected}\" but file now has \"{actual}\". \
                 Re-read the file with hash_lines to get fresh hashes."
            ))),
            Err(hashline::HashlineError::HashNotFound(hash)) => Err(AvaError::ToolError(format!(
                "Hash [{hash}] not found in cache. Read the file with hash_lines first."
            ))),
        }
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
