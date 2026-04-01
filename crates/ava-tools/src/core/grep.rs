use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

use async_trait::async_trait;
use glob::Pattern;
use grep_regex::RegexMatcher;
use grep_searcher::sinks::UTF8;
use grep_searcher::{Searcher, SearcherBuilder};
use ignore::{WalkBuilder, WalkState};
use serde_json::{json, Value};

use ava_types::{AvaError, ToolResult};

use crate::registry::Tool;

const MAX_MATCHES: usize = 500;

#[derive(Debug, Clone, Eq, PartialEq)]
struct GrepMatch {
    path: String,
    line_number: u64,
    line: String,
}

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

    fn search_hint(&self) -> &str {
        "search content regex pattern ripgrep"
    }

    fn activity_description(&self, args: &Value) -> Option<String> {
        let pattern = args.get("pattern").and_then(Value::as_str)?;
        Some(format!("Searching for '{pattern}'"))
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let pattern = args.get("pattern").and_then(Value::as_str).ok_or_else(|| {
            AvaError::ValidationError("missing required field: pattern".to_string())
        })?;
        let path = args.get("path").and_then(Value::as_str).unwrap_or(".");
        let include = args.get("include").and_then(Value::as_str);

        tracing::debug!(tool = "grep", %pattern, %path, "executing grep tool");

        let search_root = crate::core::path_guard::enforce_workspace_path(path, "grep")?;

        RegexMatcher::new(pattern).map_err(|e| AvaError::ToolError(e.to_string()))?;
        let include_glob = include
            .map(Pattern::new)
            .transpose()
            .map_err(|e| AvaError::ValidationError(format!("invalid include pattern: {e}")))?;

        let matches = Arc::new(Mutex::new(Vec::<GrepMatch>::new()));
        let match_count = Arc::new(AtomicUsize::new(0));
        let limit_reached = Arc::new(AtomicBool::new(false));
        let walker = WalkBuilder::new(search_root.as_path())
            .hidden(false)
            .git_ignore(true)
            .git_global(true)
            .git_exclude(true)
            .build_parallel();

        walker.run(|| {
            let matches = Arc::clone(&matches);
            let match_count = Arc::clone(&match_count);
            let limit_reached = Arc::clone(&limit_reached);
            let search_root = search_root.clone();
            let include_glob = include_glob.clone();
            let pattern = pattern.to_string();

            Box::new(move |dent| {
                if limit_reached.load(Ordering::Relaxed) {
                    return WalkState::Quit;
                }

                let Ok(dent) = dent else {
                    return WalkState::Continue;
                };
                if !dent.file_type().map(|kind| kind.is_file()).unwrap_or(false) {
                    return WalkState::Continue;
                }

                let file_path = dent.path();
                if let Some(include_glob) = &include_glob {
                    let relative = file_path.strip_prefix(&search_root).unwrap_or(file_path);
                    let file_name = relative
                        .file_name()
                        .and_then(|value| value.to_str())
                        .unwrap_or_default();
                    if !include_glob.matches(file_name) && !include_glob.matches_path(relative) {
                        return WalkState::Continue;
                    }
                }

                let Ok(matcher) = RegexMatcher::new(&pattern) else {
                    return WalkState::Quit;
                };
                let mut searcher: Searcher = SearcherBuilder::new().line_number(true).build();
                let mut local_matches = Vec::new();
                let display_path = file_path.display().to_string();

                let result = searcher.search_path(
                    &matcher,
                    file_path,
                    UTF8(|line_num, line| {
                        if reserve_match_slot(&match_count).is_none() {
                            limit_reached.store(true, Ordering::Relaxed);
                            return Ok(false);
                        }
                        local_matches.push(GrepMatch {
                            path: display_path.clone(),
                            line_number: line_num,
                            line: line.to_string(),
                        });
                        Ok(true)
                    }),
                );

                if !local_matches.is_empty() {
                    let mut global_matches =
                        matches.lock().unwrap_or_else(|error| error.into_inner());
                    global_matches.extend(local_matches);
                    global_matches.truncate(MAX_MATCHES);
                    if global_matches.len() >= MAX_MATCHES {
                        limit_reached.store(true, Ordering::Relaxed);
                        return WalkState::Quit;
                    }
                }

                if result.is_err() {
                    return WalkState::Continue;
                }

                WalkState::Continue
            })
        });

        let mut matches = matches
            .lock()
            .unwrap_or_else(|error| error.into_inner())
            .clone();
        matches.sort_by(|left, right| {
            left.path
                .cmp(&right.path)
                .then_with(|| left.line_number.cmp(&right.line_number))
        });

        let mut content = matches
            .into_iter()
            .map(|matched| format!("{}:{}:{}", matched.path, matched.line_number, matched.line))
            .collect::<Vec<_>>()
            .join("\n");
        if limit_reached.load(Ordering::Relaxed) && !content.is_empty() {
            content.push_str(&format!(
                "\n\n(Results truncated: showing first {MAX_MATCHES} matches. Consider using a more specific path or pattern.)"
            ));
        }
        let limit = super::output_fallback::tool_inline_limit("grep");
        let content = super::output_fallback::save_tool_output_fallback("grep", &content, limit);

        Ok(ToolResult {
            call_id: String::new(),
            content,
            is_error: false,
        })
    }

    fn is_concurrency_safe(&self, _args: &serde_json::Value) -> bool {
        true
    }
}

fn reserve_match_slot(counter: &AtomicUsize) -> Option<usize> {
    loop {
        let current = counter.load(Ordering::Relaxed);
        if current >= MAX_MATCHES {
            return None;
        }
        if counter
            .compare_exchange(current, current + 1, Ordering::Relaxed, Ordering::Relaxed)
            .is_ok()
        {
            return Some(current);
        }
    }
}
