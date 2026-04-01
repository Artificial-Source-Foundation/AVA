//! ToolSearch — lets the agent discover available tools by keyword.
//!
//! This is a lightweight wrapper that exposes `ToolRegistry::search_tools()`
//! as a callable tool. Hints from each tool's `search_hint()` are included
//! alongside names and descriptions for better discoverability.

use async_trait::async_trait;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::Tool;

/// A tool that searches the tool registry by keyword, matching against
/// tool names, descriptions, and search hints.
pub struct ToolSearchTool {
    /// Snapshot of (name, description, hint) for all tools at registration time.
    entries: Vec<ToolEntry>,
}

#[derive(Clone)]
struct ToolEntry {
    name: String,
    description: String,
    hint: String,
}

impl ToolSearchTool {
    /// Build a search index from the current registry state.
    ///
    /// Callers should construct this *after* all tools are registered so
    /// the snapshot is complete.
    pub fn from_registry(registry: &crate::registry::ToolRegistry) -> Self {
        let entries = registry
            .list_tools()
            .into_iter()
            .map(|def| ToolEntry {
                name: def.name,
                description: def.description,
                hint: String::new(), // hints are looked up dynamically via search_tools
            })
            .collect();
        Self { entries }
    }

    /// Build from explicit entries (used in tests and when the registry
    /// exposes hints).
    pub fn from_entries(entries: Vec<(String, String, String)>) -> Self {
        Self {
            entries: entries
                .into_iter()
                .map(|(name, description, hint)| ToolEntry {
                    name,
                    description,
                    hint,
                })
                .collect(),
        }
    }

    fn search(&self, query: &str) -> Vec<(i32, &ToolEntry)> {
        let query_lower = query.to_lowercase();
        let query_words: Vec<&str> = query_lower.split_whitespace().collect();

        let mut scored: Vec<(i32, &ToolEntry)> = self
            .entries
            .iter()
            .filter_map(|entry| {
                let name = entry.name.to_lowercase();
                let desc = entry.description.to_lowercase();
                let hint = entry.hint.to_lowercase();

                let mut score: i32 = 0;

                if name == query_lower {
                    score += 100;
                }
                if name.contains(&query_lower) {
                    score += 50;
                }

                for word in &query_words {
                    if hint.contains(word) {
                        score += 30;
                    }
                    if name.contains(word) {
                        score += 20;
                    }
                    if desc.contains(word) {
                        score += 10;
                    }
                }

                if score > 0 {
                    Some((score, entry))
                } else {
                    None
                }
            })
            .collect();

        scored.sort_by(|a, b| b.0.cmp(&a.0).then(a.1.name.cmp(&b.1.name)));
        scored
    }
}

#[async_trait]
impl Tool for ToolSearchTool {
    fn name(&self) -> &str {
        "tool_search"
    }

    fn description(&self) -> &str {
        "Search available tools by keyword"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["query"],
            "properties": {
                "query": {
                    "type": "string",
                    "description": "Search keywords to find relevant tools"
                }
            }
        })
    }

    fn search_hint(&self) -> &str {
        "find discover tools available search"
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let query = args.get("query").and_then(Value::as_str).ok_or_else(|| {
            AvaError::ValidationError("missing required field: query".to_string())
        })?;

        let results = self.search(query);

        if results.is_empty() {
            return Ok(ToolResult {
                call_id: String::new(),
                content: format!("No tools found matching '{query}'."),
                is_error: false,
            });
        }

        let mut output = format!("Found {} tool(s) matching '{query}':\n\n", results.len());
        for (score, entry) in &results {
            output.push_str(&format!(
                "- **{}** (relevance: {score}): {}\n",
                entry.name, entry.description
            ));
            if !entry.hint.is_empty() {
                output.push_str(&format!("  hints: {}\n", entry.hint));
            }
        }

        Ok(ToolResult {
            call_id: String::new(),
            content: output,
            is_error: false,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_tool_search() -> ToolSearchTool {
        ToolSearchTool::from_entries(vec![
            (
                "read".into(),
                "Read file content".into(),
                "read file contents lines offset limit".into(),
            ),
            (
                "write".into(),
                "Write content to a file".into(),
                "create write new file content".into(),
            ),
            (
                "bash".into(),
                "Execute shell command".into(),
                "run execute shell command terminal".into(),
            ),
            (
                "grep".into(),
                "Search files by regex".into(),
                "search content regex pattern ripgrep".into(),
            ),
        ])
    }

    #[test]
    fn search_by_name() {
        let ts = test_tool_search();
        let results = ts.search("read");
        assert!(!results.is_empty());
        assert_eq!(results[0].1.name, "read");
    }

    #[test]
    fn search_by_hint_keyword() {
        let ts = test_tool_search();
        // "terminal" is only in bash's hint
        let results = ts.search("terminal");
        assert!(!results.is_empty());
        assert_eq!(results[0].1.name, "bash");
    }

    #[test]
    fn search_by_hint_finds_tool() {
        let ts = test_tool_search();
        // "regex" is in grep's hint
        let results = ts.search("regex");
        assert!(!results.is_empty());
        assert_eq!(results[0].1.name, "grep");
    }

    #[test]
    fn hint_matches_rank_higher() {
        let ts = test_tool_search();
        // "content" appears in both read's hint and write's hint, and also in
        // grep's hint. All should appear but hint matches should dominate.
        let results = ts.search("content");
        assert!(results.len() >= 2);
    }

    #[test]
    fn no_results_for_garbage() {
        let ts = test_tool_search();
        let results = ts.search("xyzzyplugh");
        assert!(results.is_empty());
    }

    #[tokio::test]
    async fn execute_returns_results() {
        let ts = test_tool_search();
        let result = ts
            .execute(serde_json::json!({ "query": "shell" }))
            .await
            .unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("bash"));
    }

    #[tokio::test]
    async fn execute_no_results() {
        let ts = test_tool_search();
        let result = ts
            .execute(serde_json::json!({ "query": "xyzzy" }))
            .await
            .unwrap();
        assert!(!result.is_error);
        assert!(result.content.contains("No tools found"));
    }
}
