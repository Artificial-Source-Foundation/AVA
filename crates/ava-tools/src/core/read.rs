use std::path::{Path, PathBuf};
use std::sync::Arc;

use async_trait::async_trait;
use ava_platform::Platform;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::core::hashline::{self, HashlineCache};
use crate::registry::Tool;

/// Default maximum lines returned when no explicit `limit` is provided.
const MAX_LINES_DEFAULT: usize = 2000;

pub struct ReadTool {
    platform: Arc<dyn Platform>,
    hashline_cache: HashlineCache,
}

impl ReadTool {
    pub fn new(platform: Arc<dyn Platform>, hashline_cache: HashlineCache) -> Self {
        Self {
            platform,
            hashline_cache,
        }
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
                "limit": { "type": "integer", "minimum": 1 },
                "hash_lines": {
                    "type": "boolean",
                    "description": "When true, prefix each line with a [hash] anchor for use with hash-anchored edits"
                }
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
        let hash_lines = args
            .get("hash_lines")
            .and_then(Value::as_bool)
            .unwrap_or(false);

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

        // Populate hashline cache when hash_lines is enabled
        if hash_lines {
            let entries = hashline::build_cache(&content);
            if let Ok(mut cache) = self.hashline_cache.write() {
                cache.insert(PathBuf::from(path), entries);
            }
        }

        let start = usize::try_from(offset.saturating_sub(1)).unwrap_or(usize::MAX);
        let mut lines: Vec<String> = content
            .lines()
            .enumerate()
            .skip(start)
            .map(|(idx, line)| {
                if hash_lines {
                    let h = hashline::hash_line(line);
                    format!("{:>6}\t[{h}] {line}", idx + 1)
                } else {
                    format!("{:>6}\t{line}", idx + 1)
                }
            })
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
