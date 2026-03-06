use async_trait::async_trait;
use glob::Pattern;
use grep_regex::RegexMatcher;
use grep_searcher::sinks::UTF8;
use grep_searcher::{Searcher, SearcherBuilder};
use ignore::WalkBuilder;
use serde_json::{json, Value};

use ava_types::{AvaError, ToolResult};

use crate::registry::Tool;

const MAX_MATCHES: usize = 500;

pub struct GrepTool;

impl GrepTool {
    pub fn new() -> Self {
        Self
    }
}

impl Default for GrepTool {
    fn default() -> Self {
        Self::new()
    }
}

#[async_trait]
impl Tool for GrepTool {
    fn name(&self) -> &str {
        "grep"
    }

    fn description(&self) -> &str {
        "Search files by regex"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["pattern"],
            "properties": {
                "pattern": { "type": "string" },
                "path": { "type": "string" },
                "include": { "type": "string" }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let pattern = args
            .get("pattern")
            .and_then(Value::as_str)
            .ok_or_else(|| {
                AvaError::ValidationError("missing required field: pattern".to_string())
            })?;
        let path = args.get("path").and_then(Value::as_str).unwrap_or(".");
        let include = args.get("include").and_then(Value::as_str);

        let matcher = RegexMatcher::new(pattern).map_err(|e| AvaError::ToolError(e.to_string()))?;
        let include_glob = include
            .map(Pattern::new)
            .transpose()
            .map_err(|e| AvaError::ValidationError(format!("invalid include pattern: {e}")))?;

        let mut searcher: Searcher = SearcherBuilder::new()
            .line_number(true)
            .build();

        let mut matches = Vec::new();
        let walker = WalkBuilder::new(path)
            .hidden(false)
            .git_ignore(true)
            .git_global(true)
            .git_exclude(true)
            .build();

        for dent in walker {
            let dent = match dent {
                Ok(d) => d,
                Err(_) => continue,
            };
            if !dent
                .file_type()
                .map(|kind| kind.is_file())
                .unwrap_or(false)
            {
                continue;
            }

            let file_path = dent.path();
            if let Some(include_glob) = &include_glob {
                let file_name = file_path
                    .file_name()
                    .and_then(|value| value.to_str())
                    .unwrap_or_default();
                if !include_glob.matches(file_name) {
                    continue;
                }
            }

            let result = searcher.search_path(
                &matcher,
                file_path,
                UTF8(|line_num, line| {
                    matches.push(format!("{}:{}:{}", file_path.display(), line_num, line));
                    Ok(matches.len() < MAX_MATCHES)
                }),
            );

            if result.is_err() {
                continue;
            }

            if matches.len() >= MAX_MATCHES {
                break;
            }
        }

        Ok(ToolResult {
            call_id: String::new(),
            content: matches.join("\n"),
            is_error: false,
        })
    }
}
