use std::sync::LazyLock;

use async_trait::async_trait;
use ava_types::{AvaError, ToolResult};
use regex::Regex;
use serde_json::{json, Value};

use crate::registry::Tool;

static RE_SCRIPT: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?is)<script[^>]*>.*?</script>").unwrap());
static RE_STYLE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?is)<style[^>]*>.*?</style>").unwrap());
static RE_BLOCK: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"(?i)<(?:br|/p|/div|/li|/tr|/h[1-6])[^>]*>").unwrap());
static RE_TAGS: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"<[^>]+>").unwrap());
static RE_BLANK_LINES: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\n{3,}").unwrap());

pub struct WebFetchTool;

impl Default for WebFetchTool {
    fn default() -> Self {
        Self
    }
}

impl WebFetchTool {
    pub fn new() -> Self {
        Self
    }
}

/// Check if a URL targets a local/private address (SSRF prevention).
pub(crate) fn is_blocked_url(url: &str) -> Result<(), AvaError> {
    let lower = url.to_lowercase();

    // Only allow http and https schemes
    if !lower.starts_with("http://") && !lower.starts_with("https://") {
        return Err(AvaError::ValidationError(format!(
            "Only http:// and https:// URLs are allowed, got: {url}"
        )));
    }

    // Parse URL to extract host
    let parsed =
        url::Url::parse(url).map_err(|e| AvaError::ValidationError(format!("Invalid URL: {e}")))?;

    let host = parsed
        .host_str()
        .ok_or_else(|| AvaError::ValidationError("URL has no host".into()))?
        .to_lowercase();

    // Block localhost and loopback addresses
    let blocked_hosts = [
        "localhost",
        "127.0.0.1",
        "::1",
        "[::1]",
        "0.0.0.0",
        "0:0:0:0:0:0:0:1",
    ];

    if blocked_hosts.iter().any(|&h| host == h) {
        return Err(AvaError::ValidationError(format!(
            "Blocked URL: requests to {host} are not allowed (SSRF prevention)"
        )));
    }

    // Block 127.x.x.x range
    if host.starts_with("127.") {
        return Err(AvaError::ValidationError(format!(
            "Blocked URL: requests to {host} are not allowed (SSRF prevention)"
        )));
    }

    Ok(())
}

/// Returns `true` if the URL targets a blocked address (for use in redirect policy).
fn is_blocked_url_bool(url: &str) -> bool {
    is_blocked_url(url).is_err()
}

/// Strip HTML tags and extract text content.
fn strip_html(html: &str) -> String {
    // Remove script and style blocks entirely
    let text = RE_SCRIPT.replace_all(html, "");
    let text = RE_STYLE.replace_all(&text, "");

    // Replace <br>, <p>, <div>, <li>, <tr> with newlines for readability
    let text = RE_BLOCK.replace_all(&text, "\n");

    // Strip remaining tags
    let text = RE_TAGS.replace_all(&text, "");

    // Decode common HTML entities
    let text = text
        .replace("&amp;", "&")
        .replace("&lt;", "<")
        .replace("&gt;", ">")
        .replace("&quot;", "\"")
        .replace("&#39;", "'")
        .replace("&apos;", "'")
        .replace("&nbsp;", " ");

    // Collapse multiple blank lines
    let text = RE_BLANK_LINES.replace_all(&text, "\n\n");

    text.trim().to_string()
}

#[async_trait]
impl Tool for WebFetchTool {
    fn name(&self) -> &str {
        "web_fetch"
    }

    fn description(&self) -> &str {
        "Fetch a URL and return its content. For HTML pages, extracts text content. For JSON, returns pretty-printed JSON."
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["url"],
            "properties": {
                "url": {
                    "type": "string",
                    "description": "The URL to fetch"
                },
                "max_length": {
                    "type": "integer",
                    "minimum": 1,
                    "description": "Maximum characters to return (default: 50000)"
                }
            }
        })
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let url = args
            .get("url")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: url".into()))?;

        let max_length = args
            .get("max_length")
            .and_then(Value::as_u64)
            .unwrap_or(50_000) as usize;

        tracing::debug!(tool = "web_fetch", %url, "executing web_fetch tool");

        // SSRF prevention
        is_blocked_url(url)?;

        // Build client with timeout and redirect policy that validates each hop
        let client = reqwest::Client::builder()
            .user_agent("ava/2.1")
            .timeout(std::time::Duration::from_secs(30))
            .redirect(reqwest::redirect::Policy::custom(|attempt| {
                if attempt.previous().len() >= 5 {
                    attempt.error(std::io::Error::other("too many redirects"))
                } else if is_blocked_url_bool(attempt.url().as_str()) {
                    attempt.stop()
                } else {
                    attempt.follow()
                }
            }))
            .build()
            .map_err(|e| AvaError::ToolError(format!("Failed to create HTTP client: {e}")))?;

        let response = match client.get(url).send().await {
            Ok(resp) => resp,
            Err(e) => {
                if e.is_timeout() {
                    return Ok(ToolResult {
                        call_id: String::new(),
                        content: format!("Request timed out after 30 seconds: {url}"),
                        is_error: true,
                    });
                }
                if e.is_connect() {
                    return Ok(ToolResult {
                        call_id: String::new(),
                        content: format!("Connection failed (DNS or network error): {e}"),
                        is_error: true,
                    });
                }
                return Ok(ToolResult {
                    call_id: String::new(),
                    content: format!("Request failed: {e}"),
                    is_error: true,
                });
            }
        };

        let status = response.status().as_u16();
        let final_url = response.url().to_string();
        let content_type = response
            .headers()
            .get(reqwest::header::CONTENT_TYPE)
            .and_then(|v| v.to_str().ok())
            .unwrap_or("unknown")
            .to_string();

        let body = response
            .text()
            .await
            .map_err(|e| AvaError::ToolError(format!("Failed to read response body: {e}")))?;

        // Process content based on content type
        let processed = if content_type.contains("application/json") {
            // Try to pretty-print JSON
            match serde_json::from_str::<Value>(&body) {
                Ok(parsed) => serde_json::to_string_pretty(&parsed).unwrap_or(body),
                Err(_) => body,
            }
        } else if content_type.contains("text/html") {
            strip_html(&body)
        } else {
            // Plain text or other — return as-is
            body
        };

        // Truncate if needed
        let (content, truncated) = if processed.len() > max_length {
            let mut truncate_at = max_length;
            while truncate_at > 0 && !processed.is_char_boundary(truncate_at) {
                truncate_at -= 1;
            }
            (format!("{}[truncated]", &processed[..truncate_at]), true)
        } else {
            (processed, false)
        };

        // Build result with metadata
        let mut meta = format!("URL: {final_url}\nStatus: {status}\nContent-Type: {content_type}");
        if truncated {
            meta.push_str(&format!(
                "\nNote: Response truncated to {max_length} characters"
            ));
        }
        if status >= 400 {
            meta.push_str(&format!("\nWarning: Non-success status code {status}"));
        }

        let output = format!("{meta}\n\n{content}");

        Ok(ToolResult {
            call_id: String::new(),
            content: output,
            is_error: status >= 400,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tool_metadata() {
        let tool = WebFetchTool::new();
        assert_eq!(tool.name(), "web_fetch");
        assert!(!tool.description().is_empty());
        let params = tool.parameters();
        assert_eq!(params["required"], json!(["url"]));
    }

    #[test]
    fn blocks_localhost() {
        assert!(is_blocked_url("http://localhost/secret").is_err());
        assert!(is_blocked_url("http://127.0.0.1/secret").is_err());
        assert!(is_blocked_url("http://127.0.0.2/secret").is_err());
        assert!(is_blocked_url("http://[::1]/secret").is_err());
        assert!(is_blocked_url("http://0.0.0.0/secret").is_err());
    }

    #[test]
    fn blocks_file_urls() {
        assert!(is_blocked_url("file:///etc/passwd").is_err());
    }

    #[test]
    fn blocks_non_http_schemes() {
        assert!(is_blocked_url("ftp://example.com/file").is_err());
        assert!(is_blocked_url("gopher://example.com").is_err());
    }

    #[test]
    fn allows_valid_urls() {
        assert!(is_blocked_url("https://example.com").is_ok());
        assert!(is_blocked_url("http://example.com/path?q=1").is_ok());
    }

    #[test]
    fn strip_html_basic() {
        let html = "<html><body><h1>Title</h1><p>Hello <b>world</b></p></body></html>";
        let text = strip_html(html);
        assert!(text.contains("Title"));
        assert!(text.contains("Hello world"));
        assert!(!text.contains("<h1>"));
        assert!(!text.contains("<b>"));
    }

    #[test]
    fn strip_html_removes_scripts() {
        let html = "<p>Before</p><script>alert('xss')</script><p>After</p>";
        let text = strip_html(html);
        assert!(text.contains("Before"));
        assert!(text.contains("After"));
        assert!(!text.contains("alert"));
    }

    #[test]
    fn strip_html_decodes_entities() {
        let html = "&amp; &lt; &gt; &quot; &#39;";
        let text = strip_html(html);
        assert_eq!(text, "& < > \" '");
    }

    #[tokio::test]
    async fn missing_url_errors() {
        let tool = WebFetchTool::new();
        let result = tool.execute(json!({})).await;
        assert!(result.is_err());
    }

    #[tokio::test]
    async fn blocked_url_errors() {
        let tool = WebFetchTool::new();
        let result = tool
            .execute(json!({"url": "http://localhost/secret"}))
            .await;
        assert!(result.is_err());
    }
}
