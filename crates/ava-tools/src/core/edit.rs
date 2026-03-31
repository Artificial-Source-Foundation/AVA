use std::path::PathBuf;
use std::sync::Arc;

use async_trait::async_trait;
use ava_platform::Platform;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::core::file_backup::FileBackupSession;
use crate::core::hashline::{self, HashlineCache};
use crate::core::read_state::ReadStateCache;
use crate::edit::{EditEngine, EditRequest};
use crate::git::GhostSnapshotter;
use crate::registry::Tool;

pub struct EditTool {
    platform: Arc<dyn Platform>,
    engine: EditEngine,
    hashline_cache: HashlineCache,
    snapshotter: GhostSnapshotter,
    backup_session: FileBackupSession,
    /// F10 — Shared read-state cache for stale file detection.
    read_state_cache: Option<ReadStateCache>,
}

impl EditTool {
    pub fn new(platform: Arc<dyn Platform>, hashline_cache: HashlineCache) -> Self {
        Self {
            platform,
            engine: EditEngine::new(),
            hashline_cache,
            snapshotter: GhostSnapshotter::new(),
            backup_session: crate::core::file_backup::new_backup_session(),
            read_state_cache: None,
        }
    }

    /// Create an `EditTool` with a shared backup session for crash-safe undo.
    pub fn with_backup_session(
        platform: Arc<dyn Platform>,
        hashline_cache: HashlineCache,
        backup_session: FileBackupSession,
    ) -> Self {
        Self {
            platform,
            engine: EditEngine::new(),
            hashline_cache,
            snapshotter: GhostSnapshotter::new(),
            backup_session,
            read_state_cache: None,
        }
    }

    /// Attach a shared read-state cache for stale file detection (F10).
    pub fn with_read_state_cache(mut self, cache: ReadStateCache) -> Self {
        self.read_state_cache = Some(cache);
        self
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

        let file_path = crate::core::path_guard::enforce_workspace_path(path, "edit")?;
        if !self.platform.exists(&file_path).await {
            return Err(crate::core::path_suggest::missing_file_error(path, &file_path).await);
        }
        let original = self.platform.read_file(&file_path).await?;

        // F10 — Stale file detection: check if the file was modified since last read.
        let stale_warning = if let Some(ref cache) = self.read_state_cache {
            if let Ok(meta) = tokio::fs::metadata(&file_path).await {
                if let Ok(current_mtime) = meta.modified() {
                    cache
                        .read()
                        .ok()
                        .and_then(|c| c.check_stale(&file_path, current_mtime))
                } else {
                    None
                }
            } else {
                None
            }
        } else {
            None
        };

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

        // Persistent backup before mutation (survives crashes).
        if let Err(e) =
            crate::core::file_backup::backup_file_before_edit(&self.backup_session, &file_path)
                .await
        {
            tracing::warn!(path = %path, error = %e, "file backup failed, proceeding with edit");
        }

        let snapshot_note = match self
            .snapshotter
            .snapshot_file_before_write(&file_path, &original)
            .await
        {
            Ok(Some(snapshot)) => format!("; ghost snapshot {}", snapshot.ref_name),
            Ok(None) => String::new(),
            Err(err) => format!("; ghost snapshot unavailable ({err})"),
        };

        self.platform.write_file(&file_path, &updated).await?;

        let change_lines = line_diff_count(&original, &updated);
        let diff_text = compute_unified_diff(&original, &updated, path);
        let mut content = String::new();
        // F10: Prepend stale warning if file was modified since last read.
        if let Some(ref warning) = stale_warning {
            content.push_str(warning);
            content.push_str("\n\n");
        }
        content.push_str(&format!(
            "Applied {strategy}; changed {change_lines} lines{snapshot_note}"
        ));
        if !diff_text.is_empty() {
            content.push_str("\n\n");
            content.push_str(&diff_text);
        }
        Ok(ToolResult {
            call_id: String::new(),
            content,
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

/// Compute a unified diff between old and new content for display.
fn compute_unified_diff(old: &str, new: &str, path: &str) -> String {
    use similar::TextDiff;
    let diff = TextDiff::from_lines(old, new);
    diff.unified_diff()
        .context_radius(3)
        .header(&format!("a/{path}"), &format!("b/{path}"))
        .to_string()
}

fn line_diff_count(before: &str, after: &str) -> usize {
    let before_lines: Vec<&str> = before.lines().collect();
    let after_lines: Vec<&str> = after.lines().collect();
    let max_len = before_lines.len().max(after_lines.len());
    (0..max_len)
        .filter(|idx| before_lines.get(*idx) != after_lines.get(*idx))
        .count()
}
