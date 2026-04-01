use std::sync::LazyLock;

use async_trait::async_trait;
use ava_types::{AvaError, ToolResult};
use regex::Regex;
use serde_json::{json, Value};

use crate::registry::Tool;

const DEFAULT_MAX_LENGTH: usize = 50_000;
const MIN_DOWNLOAD_BYTES: usize = 64 * 1024;
const MAX_DOWNLOAD_BYTES: usize = 1024 * 1024;

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

/// Check if an IPv4 address falls within a private/reserved range.
fn is_private_ipv4(ip: &std::net::Ipv4Addr) -> bool {
    // 127.0.0.0/8 — loopback
    if ip.octets()[0] == 127 {
        return true;
    }
    // 10.0.0.0/8 — private
    if ip.octets()[0] == 10 {
        return true;
    }
    // 172.16.0.0/12 — private (172.16.x.x – 172.31.x.x)
    if ip.octets()[0] == 172 && (16..=31).contains(&ip.octets()[1]) {
        return true;
    }
    // 192.168.0.0/16 — private
    if ip.octets()[0] == 192 && ip.octets()[1] == 168 {
        return true;
    }
    // 169.254.0.0/16 — link-local (includes cloud metadata 169.254.169.254)
    if ip.octets()[0] == 169 && ip.octets()[1] == 254 {
        return true;
    }
    // 0.0.0.0/8 — "this" network
    if ip.octets()[0] == 0 {
        return true;
    }
    false
}

/// Check if an IPv6 address falls within a private/reserved range.
fn is_private_ipv6(ip: &std::net::Ipv6Addr) -> bool {
    // ::1 — loopback
    if ip.is_loopback() {
        return true;
    }
    // fe80::/10 — link-local
    let segments = ip.segments();
    if segments[0] & 0xffc0 == 0xfe80 {
        return true;
    }
    // fc00::/7 — unique local addresses
    if segments[0] & 0xfe00 == 0xfc00 {
        return true;
    }
    // :: (unspecified)
    if ip.is_unspecified() {
        return true;
    }
    // IPv4-mapped IPv6 (::ffff:x.x.x.x) — check the embedded IPv4
    if let Some(ipv4) = ip.to_ipv4_mapped() {
        return is_private_ipv4(&ipv4);
    }
    false
}

/// Check if a URL targets a local/private address (SSRF prevention).
///
/// This performs both hostname-based blocking (for known local names) and
/// DNS resolution to catch hostnames that resolve to private IP ranges.
///
/// Limitation: reqwest performs its own DNS lookup when the real request is
/// sent, so this remains best-effort protection against rebinding rather than
/// a same-resolution guarantee.
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

    // Block known localhost/loopback hostnames
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
            "URL blocked: private/internal network address ({host})"
        )));
    }

    // Block cloud metadata endpoint by hostname
    if host == "169.254.169.254" {
        return Err(AvaError::ValidationError(
            "URL blocked: private/internal network address (cloud metadata endpoint)".into(),
        ));
    }

    // Check if host is a literal IP address
    let bare_host = host.trim_start_matches('[').trim_end_matches(']');
    if let Ok(ipv4) = bare_host.parse::<std::net::Ipv4Addr>() {
        if is_private_ipv4(&ipv4) {
            return Err(AvaError::ValidationError(format!(
                "URL blocked: private/internal network address ({ipv4})"
            )));
        }
    }
    if let Ok(ipv6) = bare_host.parse::<std::net::Ipv6Addr>() {
        if is_private_ipv6(&ipv6) {
            return Err(AvaError::ValidationError(format!(
                "URL blocked: private/internal network address ({ipv6})"
            )));
        }
    }

    // DNS resolution: resolve hostname and check all resulting IPs against blocklists.
    // This catches cases like `http://evil.com` resolving to 127.0.0.1.
    let port = parsed
        .port()
        .unwrap_or(if parsed.scheme() == "https" { 443 } else { 80 });
    let socket_addr = format!("{host}:{port}");
    if let Ok(addrs) = std::net::ToSocketAddrs::to_socket_addrs(&socket_addr.as_str()) {
        for addr in addrs {
            match addr.ip() {
                std::net::IpAddr::V4(ipv4) => {
                    if is_private_ipv4(&ipv4) {
                        return Err(AvaError::ValidationError(format!(
                            "URL blocked: private/internal network address ({host} resolves to {ipv4})"
                        )));
                    }
                }
                std::net::IpAddr::V6(ipv6) => {
                    if is_private_ipv6(&ipv6) {
                        return Err(AvaError::ValidationError(format!(
                            "URL blocked: private/internal network address ({host} resolves to {ipv6})"
                        )));
                    }
                }
            }
        }
    }

    Ok(())
}

/// Returns `true` if the URL targets a blocked address (for use in redirect policy).
fn is_blocked_url_bool(url: &str) -> bool {
    is_blocked_url(url).is_err()
}

pub(crate) fn validate_redirect_target(
    url: &url::Url,
    previous_len: usize,
) -> Result<(), std::io::Error> {
    if previous_len >= 5 {
        return Err(std::io::Error::other("too many redirects"));
    }
    if is_blocked_url_bool(url.as_str()) {
        return Err(std::io::Error::other(
            "redirect blocked: private/internal network address",
        ));
    }
    Ok(())
}

fn response_byte_limit(max_length: usize) -> usize {
    max_length
        .saturating_mul(4)
        .saturating_add(8192)
        .clamp(MIN_DOWNLOAD_BYTES, MAX_DOWNLOAD_BYTES)
}

async fn read_response_body_limited(
    response: &mut reqwest::Response,
    byte_limit: usize,
) -> Result<(Vec<u8>, bool), AvaError> {
    let mut body = Vec::new();
    let mut truncated = false;

    while let Some(chunk) = response
        .chunk()
        .await
        .map_err(|e| AvaError::ToolError(format!("Failed to read response body: {e}")))?
    {
        let remaining = byte_limit.saturating_sub(body.len());
        if chunk.len() > remaining {
            body.extend_from_slice(&chunk[..remaining]);
            truncated = true;
            break;
        }

        body.extend_from_slice(&chunk);
    }

    Ok((body, truncated))
}

fn truncate_to_char_boundary(content: &str, max_length: usize) -> (String, bool) {
    if content.len() <= max_length {
        return (content.to_string(), false);
    }

    let mut truncate_at = max_length;
    while truncate_at > 0 && !content.is_char_boundary(truncate_at) {
        truncate_at -= 1;
    }
    (format!("{}[truncated]", &content[..truncate_at]), true)
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
                    "description": "Maximum UTF-8 bytes of processed text to return (default: 50000)"
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
            .unwrap_or(DEFAULT_MAX_LENGTH as u64) as usize;
        let byte_limit = response_byte_limit(max_length);

        tracing::debug!(tool = "web_fetch", %url, "executing web_fetch tool");

        // SSRF prevention
        is_blocked_url(url)?;

        // Build client with timeout and redirect policy that validates each hop
        let client = reqwest::Client::builder()
            .user_agent("ava/2.1")
            .timeout(std::time::Duration::from_secs(30))
            .redirect(reqwest::redirect::Policy::custom(
                |attempt| match validate_redirect_target(attempt.url(), attempt.previous().len()) {
                    Ok(()) => attempt.follow(),
                    Err(error) => attempt.error(error),
                },
            ))
            .build()
            .map_err(|e| AvaError::ToolError(format!("Failed to create HTTP client: {e}")))?;

        let mut response = match client.get(url).send().await {
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

        let (body_bytes, body_truncated) =
            read_response_body_limited(&mut response, byte_limit).await?;
        let body = String::from_utf8_lossy(&body_bytes).to_string();

        // Process content based on content type
        let processed = if content_type.contains("application/json") && !body_truncated {
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
        let (content, char_truncated) = truncate_to_char_boundary(&processed, max_length);
        let truncated = body_truncated || char_truncated;

        // Build result with metadata
        let mut meta = format!("URL: {final_url}\nStatus: {status}\nContent-Type: {content_type}");
        if truncated {
            meta.push_str(&format!(
                "\nNote: Response truncated to {max_length} UTF-8 bytes"
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

    fn is_concurrency_safe(&self, _args: &serde_json::Value) -> bool {
        true
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
    fn blocks_private_ip_ranges() {
        // 10.0.0.0/8
        assert!(is_blocked_url("http://10.0.0.1/").is_err());
        assert!(is_blocked_url("http://10.255.255.255/").is_err());
        // 172.16.0.0/12
        assert!(is_blocked_url("http://172.16.0.1/").is_err());
        assert!(is_blocked_url("http://172.31.255.255/").is_err());
        // 172.15.x.x should NOT be blocked
        assert!(is_blocked_url("http://172.15.0.1/").is_ok());
        // 172.32.x.x should NOT be blocked
        assert!(is_blocked_url("http://172.32.0.1/").is_ok());
        // 192.168.0.0/16
        assert!(is_blocked_url("http://192.168.0.1/").is_err());
        assert!(is_blocked_url("http://192.168.255.255/").is_err());
        // 169.254.169.254 — cloud metadata
        assert!(is_blocked_url("http://169.254.169.254/latest/meta-data/").is_err());
        // 169.254.0.0/16 — link-local
        assert!(is_blocked_url("http://169.254.1.1/").is_err());
    }

    #[test]
    fn blocks_ipv6_private() {
        // ::1 loopback
        assert!(is_blocked_url("http://[::1]/").is_err());
        // fe80:: link-local
        assert!(is_blocked_url("http://[fe80::1]/").is_err());
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
    fn private_ipv4_detection() {
        use std::net::Ipv4Addr;
        assert!(is_private_ipv4(&Ipv4Addr::new(10, 0, 0, 1)));
        assert!(is_private_ipv4(&Ipv4Addr::new(172, 16, 0, 1)));
        assert!(is_private_ipv4(&Ipv4Addr::new(172, 31, 255, 255)));
        assert!(!is_private_ipv4(&Ipv4Addr::new(172, 15, 0, 1)));
        assert!(!is_private_ipv4(&Ipv4Addr::new(172, 32, 0, 1)));
        assert!(is_private_ipv4(&Ipv4Addr::new(192, 168, 1, 1)));
        assert!(is_private_ipv4(&Ipv4Addr::new(127, 0, 0, 1)));
        assert!(is_private_ipv4(&Ipv4Addr::new(169, 254, 169, 254)));
        assert!(is_private_ipv4(&Ipv4Addr::new(0, 0, 0, 0)));
        assert!(!is_private_ipv4(&Ipv4Addr::new(8, 8, 8, 8)));
        assert!(!is_private_ipv4(&Ipv4Addr::new(93, 184, 216, 34)));
    }

    #[test]
    fn private_ipv6_detection() {
        use std::net::Ipv6Addr;
        assert!(is_private_ipv6(&Ipv6Addr::LOCALHOST));
        assert!(is_private_ipv6(&Ipv6Addr::UNSPECIFIED));
        // fe80:: link-local
        assert!(is_private_ipv6(&"fe80::1".parse().unwrap()));
        // fc00:: unique-local
        assert!(is_private_ipv6(&"fc00::1".parse().unwrap()));
        assert!(is_private_ipv6(&"fd00::1".parse().unwrap()));
        // Public IPv6 should pass
        assert!(!is_private_ipv6(&"2001:db8::1".parse().unwrap()));
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

    #[test]
    fn redirect_validation_blocks_private_targets() {
        let url = url::Url::parse("http://169.254.169.254/latest").unwrap();
        let error = validate_redirect_target(&url, 0).unwrap_err();
        assert!(error.to_string().contains("redirect blocked"));
    }

    #[test]
    fn redirect_validation_limits_redirect_hops() {
        let url = url::Url::parse("https://example.com").unwrap();
        let error = validate_redirect_target(&url, 5).unwrap_err();
        assert!(error.to_string().contains("too many redirects"));
    }

    #[test]
    fn response_byte_limit_scales_and_caps() {
        assert_eq!(response_byte_limit(10), MIN_DOWNLOAD_BYTES);
        assert_eq!(response_byte_limit(DEFAULT_MAX_LENGTH), 208_192);
        assert_eq!(response_byte_limit(5_000_000), MAX_DOWNLOAD_BYTES);
    }

    #[test]
    fn truncate_to_char_boundary_preserves_utf8() {
        let content = "alpha🙂beta";
        let (truncated, did_truncate) = truncate_to_char_boundary(content, 9);
        assert!(did_truncate);
        assert_eq!(truncated, "alpha🙂[truncated]");
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

    /// Force evaluation of all `LazyLock<Regex>` statics in this module so that
    /// any malformed pattern causes a test failure rather than a runtime panic.
    #[test]
    fn regexes_compile() {
        let _ = RE_SCRIPT.as_str();
        let _ = RE_STYLE.as_str();
        let _ = RE_BLOCK.as_str();
        let _ = RE_TAGS.as_str();
        let _ = RE_BLANK_LINES.as_str();
    }
}
