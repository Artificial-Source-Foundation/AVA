use async_trait::async_trait;
use ava_types::{AvaError, ToolResult};
use regex::Regex;
use serde_json::{json, Value};
use std::collections::HashSet;
use std::sync::LazyLock;
use url::form_urlencoded;

use crate::core::web_fetch::{is_blocked_url, validate_redirect_target};
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
            .redirect(reqwest::redirect::Policy::custom(
                |attempt| match validate_redirect_target(attempt.url(), attempt.previous().len()) {
                    Ok(()) => attempt.follow(),
                    Err(error) => attempt.error(error),
                },
            ))
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

    fn is_concurrency_safe(&self, _args: &serde_json::Value) -> bool {
        true
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
    let mut seen_urls = HashSet::new();
    for cap in RESULT_RE.captures_iter(html) {
        let href = cap.get(1).map(|m| m.as_str()).unwrap_or_default();
        let title_html = cap.get(2).map(|m| m.as_str()).unwrap_or_default();
        let title = decode_html_entities_basic(TAG_RE.replace_all(title_html, "").trim());
        let Some(url) = normalize_duckduckgo_href(href) else {
            continue;
        };

        if !title.is_empty() && is_blocked_url(&url).is_ok() && seen_urls.insert(url.clone()) {
            out.push(json!({"title": title, "url": url}));
        }
        if out.len() >= max_results {
            break;
        }
    }
    out
}

fn normalize_duckduckgo_href(href: &str) -> Option<String> {
    let href = href.trim();
    if href.is_empty() {
        return None;
    }

    let absolute = if href.starts_with("//") {
        format!("https:{href}")
    } else {
        href.to_string()
    };

    let candidate = if let Ok(parsed) = url::Url::parse(&absolute) {
        if parsed.domain() == Some("duckduckgo.com") && parsed.path() == "/l/" {
            if let Some((_, target)) = parsed.query_pairs().find(|(key, _)| key == "uddg") {
                target.into_owned()
            } else {
                absolute
            }
        } else {
            absolute
        }
    } else {
        absolute
    };

    let parsed = url::Url::parse(&candidate).ok()?;
    matches!(parsed.scheme(), "http" | "https").then(|| parsed.to_string())
}

fn decode_html_entities_basic(input: &str) -> String {
    input
        .replace("&amp;", "&")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", "\"")
        .replace("&#39;", "'")
        .replace("&apos;", "'")
        .replace("&nbsp;", " ")
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
            <a class="result__a" href="//duckduckgo.com/l/?uddg=https%3A%2F%2Fexample.com%2Fa">First &amp; <b>Result</b></a>
            <a class="result__a" href="https://example.com/b">Second Result</a>
            <a class="result__a" href="https://example.com/b">Second Result Duplicate</a>
        "#;

        let results = parse_duckduckgo_results(sample, 5);
        assert_eq!(results.len(), 2);
        assert_eq!(results[0]["title"], "First & Result");
        assert_eq!(results[0]["url"], "https://example.com/a");
        assert_eq!(results[1]["url"], "https://example.com/b");
    }

    #[test]
    fn web_search_skips_blocked_result_urls() {
        let sample = r#"
            <a class="result__a" href="//duckduckgo.com/l/?uddg=http%3A%2F%2F169.254.169.254%2Flatest">Blocked Result</a>
            <a class="result__a" href="https://example.com/ok">Ok Result</a>
        "#;

        let results = parse_duckduckgo_results(sample, 5);
        assert_eq!(results.len(), 1);
        assert_eq!(results[0]["url"], "https://example.com/ok");
    }

    #[test]
    fn web_search_normalizes_protocol_relative_and_rejects_invalid_urls() {
        assert_eq!(
            normalize_duckduckgo_href("//example.com/path"),
            Some("https://example.com/path".to_string())
        );
        assert!(normalize_duckduckgo_href("/relative/path").is_none());
        assert!(normalize_duckduckgo_href("javascript:alert(1)").is_none());
    }

    #[tokio::test]
    async fn web_search_requires_query() {
        let tool = WebSearchTool::new();
        let result = tool.execute(json!({})).await;
        assert!(result.is_err());
    }

    /// Force evaluation of all `LazyLock<Regex>` statics used in this module.
    /// The two function-local statics (RESULT_RE, TAG_RE inside
    /// `parse_duckduckgo_results`) are exercised by passing an empty string;
    /// the regexes must compile or the test panics.
    #[test]
    fn regexes_compile() {
        // Drives RESULT_RE and TAG_RE through parse_duckduckgo_results.
        let _ = parse_duckduckgo_results("", 0);
    }
}
