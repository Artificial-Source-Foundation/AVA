use async_trait::async_trait;
use ava_types::{AvaError, ToolResult};
use regex::Regex;
use serde_json::{json, Value};
use std::sync::LazyLock;
use url::form_urlencoded;

use crate::core::web_fetch::is_blocked_url;
use crate::registry::Tool;

pub struct WebSearchTool;

impl Default for WebSearchTool {
    fn default() -> Self {
        Self::new()
    }
}

impl WebSearchTool {
    pub fn new() -> Self {
        Self
    }
}

#[async_trait]
impl Tool for WebSearchTool {
    fn name(&self) -> &str {
        "web_search"
    }

    fn description(&self) -> &str {
        "Search the web using an opt-in provider backend and return parsed result snippets"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["query"],
            "properties": {
                "query": {
                    "type": "string",
                    "description": "Search query"
                },
                "provider": {
                    "type": "string",
                    "enum": ["duckduckgo"],
                    "description": "Search provider (default: duckduckgo)"
                },
                "max_results": {
                    "type": "integer",
                    "minimum": 1,
                    "maximum": 20,
                    "description": "Maximum number of results to return (default: 5)"
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let query = args
            .get("query")
            .and_then(Value::as_str)
            .map(str::trim)
            .filter(|q| !q.is_empty())
            .ok_or_else(|| {
                AvaError::ValidationError("missing required field: query".to_string())
            })?;

        let provider = args
            .get("provider")
            .and_then(Value::as_str)
            .unwrap_or("duckduckgo");

        if provider != "duckduckgo" {
            return Err(AvaError::ValidationError(format!(
                "unsupported web_search provider: {provider}"
            )));
        }

        let max_results = args
            .get("max_results")
            .and_then(Value::as_u64)
            .unwrap_or(5)
            .clamp(1, 20) as usize;

        tracing::debug!(tool = "web_search", %query, %provider, "executing web_search tool");

        let search_url = build_duckduckgo_url(query);
        is_blocked_url(&search_url)?;

        let client = reqwest::Client::builder()
            .user_agent("ava-web-search/2.1")
            .timeout(std::time::Duration::from_secs(20))
            .build()
            .map_err(|e| AvaError::ToolError(format!("failed to create HTTP client: {e}")))?;

        let response = client
            .get(&search_url)
            .send()
            .await
            .map_err(|e| AvaError::ToolError(format!("web search request failed: {e}")))?;

        let status = response.status();
        let body = response
            .text()
            .await
            .map_err(|e| AvaError::ToolError(format!("failed to read search response: {e}")))?;

        if !status.is_success() {
            return Ok(ToolResult {
                call_id: String::new(),
                content: format!(
                    "web_search provider returned non-success status {} for query: {}",
                    status, query
                ),
                is_error: true,
            });
        }

        // NOTE: Search results are returned as tool_result content in the "tool" role.
        // The LLM treats tool-role content as data, not instructions, so HTML/prompt
        // injection in search results does not require additional sanitization here.
        // HTML tags are already stripped by parse_duckduckgo_results via TAG_RE.
        let parsed = parse_duckduckgo_results(&body, max_results);
        if parsed.is_empty() {
            return Ok(ToolResult {
                call_id: String::new(),
                content: "web_search parsed 0 results; provider format may have changed"
                    .to_string(),
                is_error: true,
            });
        }

        Ok(ToolResult {
            call_id: String::new(),
            content: serde_json::to_string_pretty(&json!({
                "provider": provider,
                "query": query,
                "results": parsed,
            }))
            .map_err(|e| AvaError::SerializationError(e.to_string()))?,
            is_error: false,
        })
    }
}

fn build_duckduckgo_url(query: &str) -> String {
    let encoded = form_urlencoded::Serializer::new(String::new())
        .append_pair("q", query)
        .finish();
    format!("https://duckduckgo.com/html/?{encoded}")
}

fn parse_duckduckgo_results(html: &str, max_results: usize) -> Vec<Value> {
    static RESULT_RE: LazyLock<Regex> = LazyLock::new(|| {
        Regex::new(
            r#"(?is)<a[^>]*class=["'][^"']*result__a[^"']*["'][^>]*href=["']([^"']+)["'][^>]*>(.*?)</a>"#,
        )
        .expect("valid ddg result regex")
    });
    static TAG_RE: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r"<[^>]+>").expect("valid html tag regex"));

    let mut out = Vec::new();
    for cap in RESULT_RE.captures_iter(html).take(max_results) {
        let href = cap.get(1).map(|m| m.as_str()).unwrap_or_default();
        let title_html = cap.get(2).map(|m| m.as_str()).unwrap_or_default();
        let title = TAG_RE.replace_all(title_html, "").trim().to_string();

        if !title.is_empty() && !href.is_empty() {
            out.push(json!({"title": title, "url": href}));
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn web_search_metadata_is_valid() {
        let tool = WebSearchTool::new();
        assert_eq!(tool.name(), "web_search");
        assert!(!tool.description().is_empty());
    }

    #[test]
    fn web_search_builds_duckduckgo_url() {
        let url = build_duckduckgo_url("rust tokio");
        assert!(url.starts_with("https://duckduckgo.com/html/?q="));
        assert!(url.contains("rust+tokio") || url.contains("rust%20tokio"));
    }

    #[test]
    fn web_search_parses_result_entries() {
        let sample = r#"
            <a class="result__a" href="https://example.com/a">First <b>Result</b></a>
            <a class="result__a" href="https://example.com/b">Second Result</a>
        "#;

        let results = parse_duckduckgo_results(sample, 5);
        assert_eq!(results.len(), 2);
        assert_eq!(results[0]["title"], "First Result");
    }

    #[tokio::test]
    async fn web_search_requires_query() {
        let tool = WebSearchTool::new();
        let result = tool.execute(json!({})).await;
        assert!(result.is_err());
    }
}
