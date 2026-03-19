pub mod message_mapping;
pub mod parsing;

pub use message_mapping::*;
pub use parsing::*;

use std::time::Duration;

use ava_types::{AvaError, Result};

const DEFAULT_MAX_RETRIES: usize = 3;

pub fn rate_limited_error(provider: &str, body: &str) -> AvaError {
    let retry_after_secs = serde_json::from_str::<serde_json::Value>(body)
        .ok()
        .and_then(|v| v.get("retry_after")?.as_u64())
        .unwrap_or(30);

    AvaError::RateLimited {
        provider: provider.to_string(),
        retry_after_secs,
    }
}

pub fn reqwest_error(error: reqwest::Error) -> AvaError {
    if error.is_timeout() {
        AvaError::TimeoutError(format!("request timed out: {error}"))
    } else {
        AvaError::ProviderError {
            provider: "http".to_string(),
            message: if error.is_connect() {
                format!("connection failed (is the server reachable?): {error}")
            } else {
                format!("network error: {error}")
            },
        }
    }
}

pub async fn validate_status(
    response: reqwest::Response,
    provider: &str,
) -> Result<reqwest::Response> {
    if response.status().is_success() {
        return Ok(response);
    }

    let status = response.status();
    let body = response
        .text()
        .await
        .unwrap_or_else(|_| "<body unavailable>".to_string());

    if status == reqwest::StatusCode::UNAUTHORIZED {
        return Err(AvaError::MissingApiKey {
            provider: provider.to_string(),
        });
    }

    if status == reqwest::StatusCode::TOO_MANY_REQUESTS {
        return Err(rate_limited_error(provider, &body));
    }

    if status == reqwest::StatusCode::NOT_FOUND {
        return Err(AvaError::ProviderError {
            provider: provider.to_string(),
            message: format!("resource not found ({status}): {body}"),
        });
    }

    Err(AvaError::ProviderError {
        provider: provider.to_string(),
        message: format!("request failed ({status}): {body}"),
    })
}

/// Parse the `Retry-After` (seconds) or `retry-after-ms` (milliseconds) header
/// from an HTTP response. Returns the delay as a `Duration`, preferring the
/// millisecond variant when both are present.
pub fn parse_retry_after(headers: &reqwest::header::HeaderMap) -> Option<Duration> {
    // Prefer retry-after-ms (milliseconds) — used by OpenAI and some providers
    if let Some(ms) = headers
        .get("retry-after-ms")
        .and_then(|v| v.to_str().ok())
        .and_then(|s| s.parse::<u64>().ok())
    {
        return Some(Duration::from_millis(ms));
    }
    // Fall back to standard retry-after (seconds)
    if let Some(secs) = headers
        .get("retry-after")
        .and_then(|v| v.to_str().ok())
        .and_then(|s| s.parse::<u64>().ok())
    {
        return Some(Duration::from_secs(secs));
    }
    None
}

/// Send an HTTP request with retry logic for transient failures.
///
/// Retries on:
/// - 429 (rate limit) — respects Retry-After header
/// - 5xx (server error) — exponential backoff
/// - Network errors — exponential backoff
///
/// Fails immediately on 4xx (except 429).
pub async fn send_with_retry(
    request: reqwest::RequestBuilder,
    provider: &str,
    max_retries: usize,
) -> Result<reqwest::Response> {
    let mut attempts = 0;
    loop {
        let cloned = request.try_clone().ok_or_else(|| AvaError::ProviderError {
            provider: provider.to_string(),
            message: "request body is not clonable (streaming body?)".to_string(),
        })?;

        match cloned.send().await {
            Ok(resp) if resp.status() == reqwest::StatusCode::TOO_MANY_REQUESTS => {
                attempts += 1;
                if attempts > max_retries {
                    return validate_status(resp, provider)
                        .await
                        .map(|_| unreachable!());
                }
                let delay = parse_retry_after(resp.headers())
                    .unwrap_or_else(|| Duration::from_secs(2u64.saturating_pow(attempts as u32)));
                tokio::time::sleep(delay).await;
            }
            Ok(resp) if resp.status().is_server_error() => {
                attempts += 1;
                if attempts > max_retries {
                    return validate_status(resp, provider)
                        .await
                        .map(|_| unreachable!());
                }
                let delay = 2u64.saturating_pow(attempts as u32);
                tokio::time::sleep(Duration::from_secs(delay)).await;
            }
            Ok(resp) => return Ok(resp),
            Err(e) => {
                attempts += 1;
                if attempts > max_retries {
                    return Err(reqwest_error(e));
                }
                let delay = 2u64.saturating_pow(attempts as u32);
                tokio::time::sleep(Duration::from_secs(delay)).await;
            }
        }
    }
}

/// Send with retry + optional circuit breaker guard.
///
/// When a circuit breaker is provided, requests are rejected immediately if
/// the breaker is open, and successes/failures are recorded to drive state
/// transitions.
pub async fn send_with_retry_cb(
    request: reqwest::RequestBuilder,
    provider: &str,
    max_retries: usize,
    circuit_breaker: Option<&crate::circuit_breaker::CircuitBreaker>,
) -> Result<reqwest::Response> {
    if let Some(cb) = circuit_breaker {
        if !cb.allow_request() {
            return Err(AvaError::ProviderUnavailable {
                provider: provider.to_string(),
            });
        }
    }

    let result = send_with_retry(request, provider, max_retries).await;

    if let Some(cb) = circuit_breaker {
        match &result {
            Ok(resp) if resp.status().is_success() || resp.status().is_redirection() => {
                cb.record_success();
            }
            Ok(resp) if resp.status().is_server_error() => {
                cb.record_failure();
            }
            Err(_) => {
                cb.record_failure();
            }
            _ => {}
        }
    }

    result
}

/// Convenience wrapper using the default retry count.
pub async fn send_retrying(
    request: reqwest::RequestBuilder,
    provider: &str,
) -> Result<reqwest::Response> {
    send_with_retry(request, provider, DEFAULT_MAX_RETRIES).await
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_types::{Message, Role, Tool, ToolCall};
    use serde_json::json;

    #[test]
    fn openai_request_body_includes_tools() {
        let pool = std::sync::Arc::new(crate::pool::ConnectionPool::new());
        let provider = crate::providers::openai::OpenAIProvider::new(pool, "key", "gpt-4");
        let messages = vec![Message::new(Role::User, "hello")];
        let tools = vec![Tool {
            name: "read".to_string(),
            description: "Read a file".to_string(),
            parameters: json!({"type": "object", "properties": {"path": {"type": "string"}}}),
        }];
        let body = provider.build_request_body_with_tools(&messages, &tools, false);
        let tool_array = body
            .get("tools")
            .and_then(serde_json::Value::as_array)
            .unwrap();
        assert_eq!(tool_array.len(), 1);
        assert_eq!(tool_array[0]["function"]["name"].as_str().unwrap(), "read");
        assert_eq!(tool_array[0]["type"].as_str().unwrap(), "function");
    }

    #[test]
    fn parse_openai_response_with_tool_calls() {
        let payload = json!({
            "choices": [{
                "message": {
                    "content": null,
                    "tool_calls": [{
                        "id": "call_abc123",
                        "type": "function",
                        "function": {
                            "name": "read",
                            "arguments": "{\"path\":\"/tmp/test.txt\"}"
                        }
                    }]
                }
            }]
        });
        let calls = parse_openai_tool_calls(&payload);
        assert_eq!(calls.len(), 1);
        assert_eq!(calls[0].id, "call_abc123");
        assert_eq!(calls[0].name, "read");
        assert_eq!(calls[0].arguments["path"], "/tmp/test.txt");
    }

    #[test]
    fn parse_anthropic_response_with_tool_use() {
        let payload = json!({
            "content": [
                {"type": "text", "text": "I'll read the file."},
                {
                    "type": "tool_use",
                    "id": "toolu_abc",
                    "name": "read",
                    "input": {"path": "/tmp/file.txt"}
                }
            ]
        });
        let calls = parse_anthropic_tool_calls(&payload);
        assert_eq!(calls.len(), 1);
        assert_eq!(calls[0].id, "toolu_abc");
        assert_eq!(calls[0].name, "read");
        assert_eq!(calls[0].arguments["path"], "/tmp/file.txt");
    }

    #[test]
    fn openai_message_mapping_with_tool_results() {
        let tc = ToolCall {
            id: "call_1".to_string(),
            name: "read".to_string(),
            arguments: json!({"path": "/tmp/test"}),
        };
        let assistant_msg = Message::new(Role::Assistant, "").with_tool_calls(vec![tc]);
        let tool_msg = Message::new(Role::Tool, "file contents").with_tool_call_id("call_1");

        let mapped = map_messages_openai(&[assistant_msg, tool_msg]);
        assert_eq!(mapped.len(), 2);

        // Assistant message should have tool_calls
        assert!(mapped[0].get("tool_calls").is_some());
        assert_eq!(mapped[0]["tool_calls"][0]["id"], "call_1");

        // Tool message should have tool_call_id
        assert_eq!(mapped[1]["role"], "tool");
        assert_eq!(mapped[1]["tool_call_id"], "call_1");
    }

    #[test]
    fn anthropic_message_mapping_with_tool_results() {
        let tc = ToolCall {
            id: "toolu_1".to_string(),
            name: "bash".to_string(),
            arguments: json!({"command": "ls"}),
        };
        let assistant_msg =
            Message::new(Role::Assistant, "Let me list files.").with_tool_calls(vec![tc]);
        let tool_msg =
            Message::new(Role::Tool, "file1.txt\nfile2.txt").with_tool_call_id("toolu_1");

        let (_, mapped) = map_messages_anthropic(&[assistant_msg, tool_msg]);
        assert_eq!(mapped.len(), 2);

        // Assistant should have tool_use content block
        let content = mapped[0]["content"].as_array().unwrap();
        assert_eq!(content[0]["type"], "text");
        assert_eq!(content[1]["type"], "tool_use");
        assert_eq!(content[1]["id"], "toolu_1");

        // Tool result should be a user message with tool_result content block
        assert_eq!(mapped[1]["role"], "user");
        let result_content = mapped[1]["content"].as_array().unwrap();
        assert_eq!(result_content[0]["type"], "tool_result");
        assert_eq!(result_content[0]["tool_use_id"], "toolu_1");
    }

    #[test]
    fn tools_to_openai_format_produces_valid_schema() {
        let tools = vec![Tool {
            name: "glob".to_string(),
            description: "Find files".to_string(),
            parameters: json!({"type": "object", "properties": {"pattern": {"type": "string"}}}),
        }];
        let formatted = tools_to_openai_format(&tools);
        assert_eq!(formatted.len(), 1);
        assert_eq!(formatted[0]["type"], "function");
        assert_eq!(formatted[0]["function"]["name"], "glob");
    }

    #[test]
    fn tools_to_anthropic_format_produces_valid_schema() {
        let tools = vec![Tool {
            name: "glob".to_string(),
            description: "Find files".to_string(),
            parameters: json!({"type": "object", "properties": {"pattern": {"type": "string"}}}),
        }];
        let formatted = tools_to_anthropic_format(&tools);
        assert_eq!(formatted.len(), 1);
        assert_eq!(formatted[0]["name"], "glob");
        assert!(formatted[0].get("input_schema").is_some());
    }

    #[test]
    fn parse_openai_tool_calls_empty_response() {
        let payload = json!({"choices": [{"message": {"content": "Hello"}}]});
        let calls = parse_openai_tool_calls(&payload);
        assert!(calls.is_empty());
    }

    #[test]
    fn parse_anthropic_tool_calls_text_only() {
        let payload = json!({"content": [{"type": "text", "text": "Hello!"}]});
        let calls = parse_anthropic_tool_calls(&payload);
        assert!(calls.is_empty());
    }

    #[test]
    fn parse_usage_anthropic_format() {
        let payload = json!({
            "usage": {"input_tokens": 1500, "output_tokens": 300}
        });
        let usage = parse_usage(&payload).unwrap();
        assert_eq!(usage.input_tokens, 1500);
        assert_eq!(usage.output_tokens, 300);
    }

    #[test]
    fn parse_usage_openai_format() {
        let payload = json!({
            "usage": {"prompt_tokens": 2000, "completion_tokens": 500}
        });
        let usage = parse_usage(&payload).unwrap();
        assert_eq!(usage.input_tokens, 2000);
        assert_eq!(usage.output_tokens, 500);
    }

    #[test]
    fn parse_usage_missing() {
        let payload = json!({"content": "hello"});
        assert!(parse_usage(&payload).is_none());
    }

    #[test]
    fn pricing_differentiates_claude_models() {
        let (opus_in, _) = model_pricing_usd_per_million("claude-opus-4");
        let (sonnet_in, _) = model_pricing_usd_per_million("claude-sonnet-4");
        let (haiku_in, _) = model_pricing_usd_per_million("claude-haiku-3.5");
        assert!(
            opus_in > sonnet_in,
            "opus should be more expensive than sonnet"
        );
        assert!(
            sonnet_in > haiku_in,
            "sonnet should be more expensive than haiku"
        );
    }

    #[test]
    fn pricing_differentiates_gemini_models() {
        let (flash_in, _) = model_pricing_usd_per_million("gemini-2.0-flash");
        let (pro_in, _) = model_pricing_usd_per_million("gemini-2.5-pro");
        assert!(pro_in > flash_in);
    }

    #[test]
    fn tools_to_anthropic_format_cached_adds_cache_control_to_last_tool() {
        let tools = vec![
            Tool {
                name: "glob".to_string(),
                description: "Find files".to_string(),
                parameters: json!({"type": "object", "properties": {"pattern": {"type": "string"}}}),
            },
            Tool {
                name: "read".to_string(),
                description: "Read a file".to_string(),
                parameters: json!({"type": "object", "properties": {"path": {"type": "string"}}}),
            },
        ];

        let formatted = tools_to_anthropic_format_cached(&tools, true);
        assert_eq!(formatted.len(), 2);
        // First tool should NOT have cache_control
        assert!(formatted[0].get("cache_control").is_none());
        // Last tool SHOULD have cache_control
        assert_eq!(formatted[1]["cache_control"], json!({"type": "ephemeral"}));
    }

    #[test]
    fn tools_to_anthropic_format_cached_false_no_cache_control() {
        let tools = vec![Tool {
            name: "glob".to_string(),
            description: "Find files".to_string(),
            parameters: json!({"type": "object", "properties": {"pattern": {"type": "string"}}}),
        }];

        let formatted = tools_to_anthropic_format_cached(&tools, false);
        assert_eq!(formatted.len(), 1);
        assert!(formatted[0].get("cache_control").is_none());
    }

    #[test]
    fn parse_usage_with_cache_tokens() {
        let payload = json!({
            "usage": {
                "input_tokens": 2000,
                "output_tokens": 300,
                "cache_read_input_tokens": 1500,
                "cache_creation_input_tokens": 500
            }
        });
        let usage = parse_usage(&payload).unwrap();
        assert_eq!(usage.input_tokens, 2000);
        assert_eq!(usage.output_tokens, 300);
        assert_eq!(usage.cache_read_tokens, 1500);
        assert_eq!(usage.cache_creation_tokens, 500);
    }

    #[test]
    fn estimate_cost_with_cache_applies_discounts() {
        let usage = ava_types::TokenUsage {
            input_tokens: 2000,
            output_tokens: 100,
            cache_read_tokens: 1500,
            cache_creation_tokens: 0,
        };
        // in_rate = $3/M, out_rate = $15/M (Sonnet-class)
        let cost = estimate_cost_with_cache_usd(&usage, 3.0, 15.0);
        // Non-cached: 500 tokens at $3/M = $0.0015
        // Cache read: 1500 tokens at $0.3/M = $0.00045
        // Output: 100 tokens at $15/M = $0.0015
        let expected = 500.0 / 1_000_000.0 * 3.0
            + 1500.0 / 1_000_000.0 * 3.0 * 0.1
            + 100.0 / 1_000_000.0 * 15.0;
        assert!((cost - expected).abs() < 1e-10);
    }
}
