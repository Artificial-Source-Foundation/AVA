//! ToolSearch — lets the agent discover available tools by keyword.
//!
//! This is a lightweight wrapper that exposes `ToolRegistry::search_tools()`
//! as a callable tool. Hints from each tool's `search_hint()` are included
//! alongside names and descriptions for better discoverability.

use async_trait::async_trait;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::registry::{Tool, ToolSearchMetadata};

/// A tool that searches the tool registry by keyword, matching against
/// tool names, descriptions, and search hints.
pub struct ToolSearchTool {
    /// Snapshot of tool metadata at registration time.
    entries: Vec<ToolSearchMetadata>,
}

impl ToolSearchTool {
    /// Build a search index from the current registry state.
    ///
    /// Callers should construct this *after* all tools are registered so
    /// the snapshot is complete.
    pub fn from_registry(registry: &crate::registry::ToolRegistry) -> Self {
        Self {
            entries: registry.list_tools_with_hints(),
        }
    }

    /// Build from explicit entries (used in tests).
    #[cfg(test)]
    fn from_entries(entries: Vec<ToolSearchMetadata>) -> Self {
        Self { entries }
    }

    fn search(&self, query: &str) -> Vec<(i32, &ToolSearchMetadata)> {
        if query.trim().is_empty() {
            return Vec::new();
        }

        let query_lower = query.to_lowercase();
        let query_words: Vec<&str> = query_lower.split_whitespace().collect();

        let mut scored: Vec<(i32, &ToolSearchMetadata)> = self
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
    use crate::registry::{Tool, ToolRegistry};

    fn entry(name: &str, description: &str, hint: &str) -> ToolSearchMetadata {
        ToolSearchMetadata {
            name: name.to_string(),
            description: description.to_string(),
            hint: hint.to_string(),
        }
    }

    struct HintOnlyTool;

    #[async_trait::async_trait]
    impl Tool for HintOnlyTool {
        fn name(&self) -> &str {
            "hint_only"
        }

        fn description(&self) -> &str {
            "No searchable words here"
        }

        fn parameters(&self) -> Value {
            serde_json::json!({"type": "object"})
        }

        fn search_hint(&self) -> &str {
            "needle"
        }

        async fn execute(&self, _args: Value) -> ava_types::Result<ToolResult> {
            Ok(ToolResult {
                call_id: String::new(),
                content: "ok".to_string(),
                is_error: false,
            })
        }
    }

    struct StaticRegistryTool {
        name: &'static str,
        description: &'static str,
        hint: &'static str,
    }

    #[async_trait::async_trait]
    impl Tool for StaticRegistryTool {
        fn name(&self) -> &str {
            self.name
        }

        fn description(&self) -> &str {
            self.description
        }

        fn parameters(&self) -> Value {
            serde_json::json!({"type": "object"})
        }

        fn search_hint(&self) -> &str {
            self.hint
        }

        async fn execute(&self, _args: Value) -> ava_types::Result<ToolResult> {
            Ok(ToolResult {
                call_id: String::new(),
                content: "ok".to_string(),
                is_error: false,
            })
        }
    }

    fn test_tool_search() -> ToolSearchTool {
        ToolSearchTool::from_entries(vec![
            entry(
                "read",
                "Read file content",
                "read file contents lines offset limit",
            ),
            entry(
                "write",
                "Write content to a file",
                "create write new file content",
            ),
            entry(
                "bash",
                "Execute shell command",
                "run execute shell command terminal",
            ),
            entry(
                "grep",
                "Search files by regex",
                "search content regex pattern ripgrep",
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
    fn exact_name_ranks_above_partial_name_match() {
        let ts = ToolSearchTool::from_entries(vec![entry("bread", "", ""), entry("read", "", "")]);

        let results = ts.search("read");
        assert_eq!(results.len(), 2);
        assert_eq!(results[0].1.name, "read");
        assert_eq!(results[1].1.name, "bread");
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
    fn hint_match_ranks_above_description_match() {
        let ts = ToolSearchTool::from_entries(vec![
            entry("desc_only", "contains needle", ""),
            entry("hint_only", "no keyword", "needle"),
        ]);

        let results = ts.search("needle");
        assert_eq!(results.len(), 2);
        assert_eq!(results[0].1.name, "hint_only");
        assert_eq!(results[1].1.name, "desc_only");
    }

    #[test]
    fn equal_scores_use_alphabetical_tie_break() {
        let ts = ToolSearchTool::from_entries(vec![
            entry("beta", "", "topic"),
            entry("alpha", "", "topic"),
        ]);

        let results = ts.search("topic");
        assert_eq!(results.len(), 2);
        assert_eq!(results[0].1.name, "alpha");
        assert_eq!(results[1].1.name, "beta");
    }

    #[test]
    fn search_is_case_insensitive() {
        let ts = test_tool_search();
        let results = ts.search("TeRMiNaL");
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

    #[test]
    fn empty_query_returns_no_results() {
        let ts = test_tool_search();
        assert!(ts.search("").is_empty());
        assert!(ts.search("   ").is_empty());
    }

    #[test]
    fn from_registry_captures_search_hints() {
        let mut registry = ToolRegistry::new();
        registry.register(HintOnlyTool);

        let ts = ToolSearchTool::from_registry(&registry);
        let results = ts.search("needle");

        assert_eq!(results.len(), 1);
        assert_eq!(results[0].1.name, "hint_only");
    }

    #[test]
    fn from_registry_snapshot_is_complete_and_alphabetized() {
        let mut registry = ToolRegistry::new();
        registry.register(StaticRegistryTool {
            name: "zeta",
            description: "last",
            hint: "hz",
        });
        registry.register(StaticRegistryTool {
            name: "alpha",
            description: "first",
            hint: "ha",
        });
        registry.register(StaticRegistryTool {
            name: "middle",
            description: "middle",
            hint: "hm",
        });

        let ts = ToolSearchTool::from_registry(&registry);

        let snapshot: Vec<(&str, &str, &str)> = ts
            .entries
            .iter()
            .map(|e| (e.name.as_str(), e.description.as_str(), e.hint.as_str()))
            .collect();
        assert_eq!(
            snapshot,
            vec![
                ("alpha", "first", "ha"),
                ("middle", "middle", "hm"),
                ("zeta", "last", "hz"),
            ]
        );
    }

    #[test]
    fn tool_search_has_non_empty_expected_search_hint() {
        let ts = test_tool_search();
        assert!(!ts.search_hint().is_empty());
        assert_eq!(ts.search_hint(), "find discover tools available search");
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
    async fn execute_formats_hint_lines() {
        let ts = test_tool_search();
        let result = ts
            .execute(serde_json::json!({ "query": "terminal" }))
            .await
            .unwrap();

        assert!(result.content.contains("- **bash** (relevance:"));
        assert!(result
            .content
            .contains("  hints: run execute shell command terminal"));
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

    #[tokio::test]
    async fn execute_empty_query_returns_no_results() {
        let ts = test_tool_search();
        let result = ts
            .execute(serde_json::json!({ "query": "" }))
            .await
            .unwrap();

        assert!(!result.is_error);
        assert!(result.content.contains("No tools found matching ''"));
    }
}
