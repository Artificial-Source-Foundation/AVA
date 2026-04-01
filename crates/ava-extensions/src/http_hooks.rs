//! HTTP webhook hooks for external integrations.
//!
//! Fires JSON POST requests to configured URLs when agent lifecycle events
//! occur (e.g. pre/post tool use, session start/end, agent errors).

use std::collections::HashMap;

use serde::{Deserialize, Serialize};

/// Supported hook event types that can trigger an HTTP webhook.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HookEvent {
    PreToolUse,
    PostToolUse,
    SessionStart,
    SessionEnd,
    AgentError,
}

impl HookEvent {
    /// Parse an event name string into a `HookEvent`.
    pub fn parse(s: &str) -> Option<Self> {
        match s {
            "pre_tool_use" => Some(Self::PreToolUse),
            "post_tool_use" => Some(Self::PostToolUse),
            "session_start" => Some(Self::SessionStart),
            "session_end" => Some(Self::SessionEnd),
            "agent_error" => Some(Self::AgentError),
            _ => None,
        }
    }

    /// Return the canonical string name for this event.
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::PreToolUse => "pre_tool_use",
            Self::PostToolUse => "post_tool_use",
            Self::SessionStart => "session_start",
            Self::SessionEnd => "session_end",
            Self::AgentError => "agent_error",
        }
    }
}

/// Configuration for a single HTTP webhook.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HttpHook {
    /// Target URL for the POST request.
    pub url: String,
    /// HTTP headers to include. Values may contain `$VAR_NAME` for env interpolation.
    pub headers: HashMap<String, String>,
    /// Request timeout in milliseconds.
    pub timeout_ms: u64,
    /// Which events this hook fires on.
    pub events: Vec<String>,
}

/// Configuration for the HTTP hooks subsystem.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct HttpHookConfig {
    /// Hooks to fire on matching events.
    pub hooks: Vec<HttpHook>,
    /// URL allowlist — only URLs starting with one of these prefixes are permitted.
    /// An empty list means all URLs are blocked.
    pub allowed_urls: Vec<String>,
}

/// Error type for HTTP hook operations.
#[derive(Debug, thiserror::Error)]
pub enum HttpHookError {
    #[error("URL not in allowlist: {0}")]
    UrlNotAllowed(String),
    #[error("HTTP request failed: {0}")]
    RequestFailed(String),
    #[error("Invalid event name: {0}")]
    InvalidEvent(String),
}

/// Interpolate environment variables in a string. Replaces `$VAR_NAME` patterns
/// with the corresponding environment variable value.
pub fn interpolate_env_vars(input: &str) -> String {
    let mut result = String::with_capacity(input.len());
    let mut chars = input.chars().peekable();

    while let Some(ch) = chars.next() {
        if ch == '$' {
            let mut var_name = String::new();
            while let Some(&next) = chars.peek() {
                if next.is_ascii_alphanumeric() || next == '_' {
                    var_name.push(next);
                    chars.next();
                } else {
                    break;
                }
            }
            if var_name.is_empty() {
                result.push('$');
            } else {
                match std::env::var(&var_name) {
                    Ok(val) => result.push_str(&val),
                    Err(_) => {
                        // Leave the original $VAR_NAME if not found
                        result.push('$');
                        result.push_str(&var_name);
                    }
                }
            }
        } else {
            result.push(ch);
        }
    }

    result
}

/// Check whether a URL is permitted by the allowlist.
pub fn is_url_allowed(url: &str, allowed_urls: &[String]) -> bool {
    allowed_urls.iter().any(|prefix| url.starts_with(prefix))
}

/// Build the JSON payload for a hook event.
pub fn build_payload(event: &str, payload: serde_json::Value) -> serde_json::Value {
    serde_json::json!({
        "event": event,
        "timestamp": chrono::Utc::now().to_rfc3339(),
        "data": payload,
    })
}

/// Fire an HTTP hook, sending a JSON POST request to the configured URL.
///
/// Returns an error if the URL is not in the allowlist or the request fails.
pub async fn fire_http_hook(
    hook: &HttpHook,
    event: &str,
    payload: serde_json::Value,
    allowed_urls: &[String],
) -> Result<(), HttpHookError> {
    // Check URL allowlist
    if !is_url_allowed(&hook.url, allowed_urls) {
        return Err(HttpHookError::UrlNotAllowed(hook.url.clone()));
    }

    // Check if this hook subscribes to this event
    if !hook.events.iter().any(|e| e == event) {
        return Ok(());
    }

    let body = build_payload(event, payload);

    // Build headers with env var interpolation
    let mut header_map = reqwest::header::HeaderMap::new();
    header_map.insert(
        reqwest::header::CONTENT_TYPE,
        reqwest::header::HeaderValue::from_static("application/json"),
    );
    for (key, value) in &hook.headers {
        let interpolated = interpolate_env_vars(value);
        if let (Ok(name), Ok(val)) = (
            reqwest::header::HeaderName::from_bytes(key.as_bytes()),
            reqwest::header::HeaderValue::from_str(&interpolated),
        ) {
            header_map.insert(name, val);
        }
    }

    let client = reqwest::Client::builder()
        .timeout(std::time::Duration::from_millis(hook.timeout_ms))
        .build()
        .map_err(|e| HttpHookError::RequestFailed(e.to_string()))?;

    let response = client
        .post(&hook.url)
        .headers(header_map)
        .json(&body)
        .send()
        .await
        .map_err(|e| HttpHookError::RequestFailed(e.to_string()))?;

    let status = response.status();
    tracing::info!(
        url = %hook.url,
        event = %event,
        status = %status,
        "HTTP hook fired"
    );

    if !status.is_success() {
        return Err(HttpHookError::RequestFailed(format!(
            "HTTP {} from {}",
            status, hook.url
        )));
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn url_allowlist_blocks_disallowed_urls() {
        let allowed = vec![
            "https://hooks.example.com/".to_string(),
            "https://api.myservice.io/".to_string(),
        ];

        assert!(is_url_allowed(
            "https://hooks.example.com/webhook",
            &allowed
        ));
        assert!(is_url_allowed(
            "https://api.myservice.io/v1/events",
            &allowed
        ));
        assert!(!is_url_allowed("https://evil.com/steal", &allowed));
        assert!(!is_url_allowed(
            "https://hooks.example.org/webhook",
            &allowed
        ));
    }

    #[test]
    fn url_allowlist_empty_blocks_all() {
        let allowed: Vec<String> = vec![];
        assert!(!is_url_allowed(
            "https://hooks.example.com/webhook",
            &allowed
        ));
    }

    #[test]
    fn env_var_interpolation_works() {
        std::env::set_var("AVA_TEST_TOKEN", "secret123");
        let result = interpolate_env_vars("Bearer $AVA_TEST_TOKEN");
        assert_eq!(result, "Bearer secret123");
        std::env::remove_var("AVA_TEST_TOKEN");
    }

    #[test]
    fn env_var_interpolation_preserves_missing_vars() {
        let result = interpolate_env_vars("Bearer $NONEXISTENT_VAR_12345");
        assert_eq!(result, "Bearer $NONEXISTENT_VAR_12345");
    }

    #[test]
    fn env_var_interpolation_handles_no_vars() {
        let result = interpolate_env_vars("plain text");
        assert_eq!(result, "plain text");
    }

    #[test]
    fn payload_formatted_correctly() {
        let data = serde_json::json!({"tool": "read", "path": "/tmp/foo"});
        let payload = build_payload("pre_tool_use", data.clone());

        assert_eq!(payload["event"], "pre_tool_use");
        assert!(payload["timestamp"].as_str().is_some());
        assert_eq!(payload["data"], data);
    }

    #[test]
    fn hook_event_parsing() {
        assert_eq!(
            HookEvent::parse("pre_tool_use"),
            Some(HookEvent::PreToolUse)
        );
        assert_eq!(HookEvent::parse("session_end"), Some(HookEvent::SessionEnd));
        assert_eq!(HookEvent::parse("unknown"), None);
    }

    #[tokio::test]
    async fn fire_hook_rejects_disallowed_url() {
        let hook = HttpHook {
            url: "https://evil.com/steal".to_string(),
            headers: HashMap::new(),
            timeout_ms: 5000,
            events: vec!["pre_tool_use".to_string()],
        };
        let allowed = vec!["https://hooks.example.com/".to_string()];
        let result = fire_http_hook(&hook, "pre_tool_use", serde_json::json!({}), &allowed).await;

        assert!(matches!(result, Err(HttpHookError::UrlNotAllowed(_))));
    }

    #[tokio::test]
    async fn fire_hook_skips_non_matching_event() {
        let hook = HttpHook {
            url: "https://hooks.example.com/webhook".to_string(),
            headers: HashMap::new(),
            timeout_ms: 5000,
            events: vec!["session_start".to_string()],
        };
        let allowed = vec!["https://hooks.example.com/".to_string()];

        // Should succeed silently (event doesn't match)
        let result = fire_http_hook(&hook, "pre_tool_use", serde_json::json!({}), &allowed).await;

        assert!(result.is_ok());
    }
}
