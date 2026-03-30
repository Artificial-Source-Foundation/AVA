pub mod message_mapping;
pub mod overflow;
pub mod parsing;

pub use message_mapping::*;
pub use overflow::is_context_overflow;
pub use parsing::*;

use std::time::Duration;

use ava_types::{AvaError, Result};
use rand::Rng;
use serde_json::Value;

pub const DEFAULT_MAX_RETRIES: usize = 3;

pub fn rate_limited_error(provider: &str, body: &str) -> AvaError {
    let retry_after_secs = parse_retry_after_from_body(body).unwrap_or(30);

    AvaError::RateLimited {
        provider: provider.to_string(),
        retry_after_secs,
    }
}

pub fn reqwest_error(provider: &str, error: reqwest::Error) -> AvaError {
    if error.is_timeout() {
        AvaError::TimeoutError(format!("{provider} request timed out: {error}"))
    } else {
        AvaError::ProviderError {
            provider: provider.to_string(),
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
    validate_status_for_model(response, provider, None).await
}

pub async fn validate_status_for_model(
    response: reqwest::Response,
    provider: &str,
    model: Option<&str>,
) -> Result<reqwest::Response> {
    if response.status().is_success() {
        return Ok(response);
    }

    let status = response.status();
    // Extract retry-after from headers before consuming the body
    let retry_after = parse_retry_after(response.headers());
    let body = response
        .text()
        .await
        .unwrap_or_else(|_| "<body unavailable>".to_string());

    let body_short = truncate_body(&body_message(&body), 200);
    let is_model_not_found = model.is_some() && indicates_model_not_found(&body_short);
    let is_context_overflow = is_context_overflow_message(&body_short);

    match status.as_u16() {
        // 401 Unauthorized — credentials invalid or expired
        401 => Err(AvaError::AuthError {
            provider: provider.to_string(),
            message: body_short,
        }),

        // 402 Payment Required — quota/billing exhausted (terminal, don't retry)
        402 => Err(AvaError::QuotaExhausted {
            provider: provider.to_string(),
            message: body_short,
        }),

        // 403 Forbidden — wrong permissions or plan
        403 => Err(AvaError::Forbidden {
            provider: provider.to_string(),
            message: body_short,
        }),

        // 404 Not Found — model or resource not found
        404 if is_model_not_found => Err(AvaError::ModelNotFound {
            provider: provider.to_string(),
            model: model.unwrap_or_default().to_string(),
        }),

        404 => Err(AvaError::ProviderError {
            provider: provider.to_string(),
            message: format!("resource not found ({status}): {body_short}"),
        }),

        // 429 Too Many Requests — retryable rate limit
        429 => {
            let retry_after_secs = retry_after
                .map(|d| d.as_secs().max(1))
                .or_else(|| parse_retry_after_from_body(&body))
                .unwrap_or(30);

            Err(AvaError::RateLimited {
                provider: provider.to_string(),
                retry_after_secs,
            })
        }

        // 400, 422, etc. — bad request (terminal, don't retry)
        400..=499 if is_model_not_found => Err(AvaError::ModelNotFound {
            provider: provider.to_string(),
            model: model.unwrap_or_default().to_string(),
        }),

        400..=499 => Err(AvaError::ProviderError {
            provider: provider.to_string(),
            message: if is_context_overflow {
                format!("context window exceeded ({status}): {body_short}")
            } else {
                format!("request failed ({status}): {body_short}")
            },
        }),

        // 5xx — server error (retryable with backoff)
        500..=599 => Err(AvaError::ServerError {
            provider: provider.to_string(),
            status: status.as_u16(),
            message: body_short,
        }),

        // Anything else
        _ => Err(AvaError::ProviderError {
            provider: provider.to_string(),
            message: format!("unexpected status ({status}): {body_short}"),
        }),
    }
}

/// Truncate a response body for inclusion in error messages.
fn truncate_body(body: &str, max_len: usize) -> String {
    if body.len() <= max_len {
        body.to_string()
    } else {
        let end = body
            .char_indices()
            .take_while(|(idx, _)| *idx < max_len)
            .last()
            .map(|(idx, ch)| idx + ch.len_utf8())
            .unwrap_or(0);
        format!("{}...", &body[..end])
    }
}

fn body_message(body: &str) -> String {
    extract_error_message(body).unwrap_or_else(|| body.to_string())
}

fn extract_error_message(body: &str) -> Option<String> {
    let value = serde_json::from_str::<Value>(body).ok()?;

    if let Some(text) = value.as_str() {
        let trimmed = text.trim();
        if !trimmed.is_empty() {
            return Some(trimmed.to_string());
        }
    }

    for path in [
        &["error", "message"][..],
        &["error", "error", "message"][..],
        &["message"][..],
        &["detail"][..],
        &["details"][..],
    ] {
        if let Some(text) = value_at_path(&value, path).and_then(Value::as_str) {
            let trimmed = text.trim();
            if !trimmed.is_empty() {
                return Some(trimmed.to_string());
            }
        }
    }

    None
}

fn parse_retry_after_from_body(body: &str) -> Option<u64> {
    let value = serde_json::from_str::<Value>(body).ok()?;
    for path in [
        &["retry_after"][..],
        &["retry_after_secs"][..],
        &["retry_after_seconds"][..],
        &["error", "retry_after"][..],
        &["error", "retry_after_secs"][..],
    ] {
        let Some(value) = value_at_path(&value, path) else {
            continue;
        };
        if let Some(secs) = value.as_u64() {
            return Some(secs.max(1));
        }
        if let Some(text) = value.as_str().and_then(|v| v.parse::<u64>().ok()) {
            return Some(text.max(1));
        }
    }

    None
}

fn value_at_path<'a>(value: &'a Value, path: &[&str]) -> Option<&'a Value> {
    let mut current = value;
    for segment in path {
        current = current.get(*segment)?;
    }
    Some(current)
}

fn indicates_model_not_found(message: &str) -> bool {
    let msg = message.to_lowercase();
    (msg.contains("model")
        && (msg.contains("not found")
            || msg.contains("does not exist")
            || msg.contains("unknown")
            || msg.contains("unsupported")))
        || (msg.contains("deployment")
            && (msg.contains("not found") || msg.contains("does not exist")))
        || msg.contains("no such model")
        || msg.contains("engine not found")
}

fn is_context_overflow_message(message: &str) -> bool {
    let msg = message.to_lowercase();
    [
        "context_length_exceeded",
        "maximum context length",
        "token limit",
        "input is too long",
        "request too large",
        "prompt is too long",
        "context window",
        "too many tokens",
        "payload too large",
        "reduce your prompt",
    ]
    .iter()
    .any(|pattern| msg.contains(pattern))
}

/// Parse retry delay from HTTP response headers.
///
/// Checks (in priority order):
/// 1. `retry-after-ms` — milliseconds (OpenAI, Azure)
/// 2. `retry-after` — seconds as integer, or HTTP-date (RFC 7231 §7.1.3)
/// 3. `x-ratelimit-reset` — Unix timestamp (GitHub, some LLM providers)
///
/// Returns the delay as a `Duration`, or `None` if no parseable header is found.
/// For absolute timestamps, returns the duration until that time (floored at 0).
pub fn parse_retry_after(headers: &reqwest::header::HeaderMap) -> Option<Duration> {
    // 1. Prefer retry-after-ms (milliseconds) — used by OpenAI and some providers
    if let Some(ms) = headers
        .get("retry-after-ms")
        .and_then(|v| v.to_str().ok())
        .and_then(|s| s.parse::<u64>().ok())
    {
        return Some(Duration::from_millis(ms));
    }

    // 2. Standard retry-after: integer seconds or HTTP-date
    if let Some(value) = headers.get("retry-after").and_then(|v| v.to_str().ok()) {
        // Try integer seconds first
        if let Ok(secs) = value.parse::<u64>() {
            return Some(Duration::from_secs(secs));
        }
        // Try HTTP-date (e.g. "Sun, 22 Mar 2026 12:00:00 GMT")
        if let Ok(date) = httpdate::parse_http_date(value) {
            let now = std::time::SystemTime::now();
            return Some(date.duration_since(now).unwrap_or(Duration::ZERO));
        }
    }

    // 3. x-ratelimit-reset — Unix timestamp (seconds since epoch)
    if let Some(ts) = headers
        .get("x-ratelimit-reset")
        .and_then(|v| v.to_str().ok())
        .and_then(|s| s.parse::<u64>().ok())
    {
        let reset_time = std::time::UNIX_EPOCH + Duration::from_secs(ts);
        let now = std::time::SystemTime::now();
        return Some(reset_time.duration_since(now).unwrap_or(Duration::ZERO));
    }

    None
}

/// Compute retry delay: use server-suggested delay (from headers) as the floor,
/// fall back to exponential backoff, then apply ±20% jitter.
fn compute_retry_delay(
    server_hint: Option<Duration>,
    attempt: usize,
    max_delay: Duration,
) -> Duration {
    let exponential = Duration::from_secs(2u64.saturating_pow(attempt as u32));
    let base = match server_hint {
        Some(hint) => hint.max(exponential),
        None => exponential,
    };
    let jitter_factor = rand::thread_rng().gen_range(0.8..=1.2);
    base.mul_f64(jitter_factor).min(max_delay)
}

/// Send an HTTP request with retry logic for transient failures.
///
/// Retries on:
/// - 429 (rate limit) — respects `retry-after-ms`, `retry-after`, `x-ratelimit-reset` headers
/// - 5xx (server error) — respects retry-after headers, falls back to exponential backoff
/// - Network errors — exponential backoff
///
/// When a server supplies a retry delay via headers, that value is used as the
/// minimum wait time (i.e. `max(server_hint, exponential_backoff)`), with ±20%
/// jitter applied on top. This matches the behavior of OpenCode, Codex CLI, and
/// Gemini CLI.
///
/// Fails immediately on 4xx (except 429).
pub async fn send_with_retry(
    request: reqwest::RequestBuilder,
    provider: &str,
    max_retries: usize,
) -> Result<reqwest::Response> {
    let max_delay = Duration::from_secs(120);
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
                let server_hint = parse_retry_after(resp.headers());
                let delay = compute_retry_delay(server_hint, attempts, max_delay);
                tracing::warn!(
                    provider,
                    attempt = attempts,
                    delay_ms = delay.as_millis() as u64,
                    server_hint_ms = server_hint.map(|d| d.as_millis() as u64),
                    "rate limited (429), retrying"
                );
                tokio::time::sleep(delay).await;
            }
            Ok(resp) if resp.status().is_server_error() => {
                attempts += 1;
                if attempts > max_retries {
                    return validate_status(resp, provider)
                        .await
                        .map(|_| unreachable!());
                }
                let server_hint = parse_retry_after(resp.headers());
                let delay = compute_retry_delay(server_hint, attempts, max_delay);
                tracing::warn!(
                    provider,
                    attempt = attempts,
                    status = resp.status().as_u16(),
                    delay_ms = delay.as_millis() as u64,
                    server_hint_ms = server_hint.map(|d| d.as_millis() as u64),
                    "server error, retrying"
                );
                tokio::time::sleep(delay).await;
            }
            Ok(resp) => return Ok(resp),
            Err(e) => {
                attempts += 1;
                if attempts > max_retries {
                    return Err(reqwest_error(provider, e));
                }
                let delay = compute_retry_delay(None, attempts, max_delay);
                tracing::warn!(
                    provider,
                    attempt = attempts,
                    delay_ms = delay.as_millis() as u64,
                    error = %e,
                    "network error, retrying"
                );
                tokio::time::sleep(delay).await;
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
    fn parse_retry_after_ms_header() {
        let mut headers = reqwest::header::HeaderMap::new();
        headers.insert("retry-after-ms", "1500".parse().unwrap());
        let d = parse_retry_after(&headers).unwrap();
        assert_eq!(d, Duration::from_millis(1500));
    }

    #[test]
    fn parse_retry_after_seconds_header() {
        let mut headers = reqwest::header::HeaderMap::new();
        headers.insert("retry-after", "30".parse().unwrap());
        let d = parse_retry_after(&headers).unwrap();
        assert_eq!(d, Duration::from_secs(30));
    }

    #[test]
    fn parse_retry_after_http_date() {
        let mut headers = reqwest::header::HeaderMap::new();
        // Use a date 10 seconds in the future
        let future = std::time::SystemTime::now() + Duration::from_secs(10);
        let date_str = httpdate::fmt_http_date(future);
        headers.insert("retry-after", date_str.parse().unwrap());
        let d = parse_retry_after(&headers).unwrap();
        // Should be approximately 10 seconds (within 2s tolerance for test timing)
        assert!(
            d >= Duration::from_secs(8) && d <= Duration::from_secs(12),
            "Expected ~10s delay, got {d:?}"
        );
    }

    #[test]
    fn parse_retry_after_http_date_in_past() {
        let mut headers = reqwest::header::HeaderMap::new();
        // Use a date in the past
        let past = std::time::SystemTime::now() - Duration::from_secs(60);
        let date_str = httpdate::fmt_http_date(past);
        headers.insert("retry-after", date_str.parse().unwrap());
        let d = parse_retry_after(&headers).unwrap();
        assert_eq!(d, Duration::ZERO, "Past date should return zero delay");
    }

    #[test]
    fn parse_retry_after_x_ratelimit_reset() {
        let mut headers = reqwest::header::HeaderMap::new();
        let future_ts = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs()
            + 15;
        headers.insert("x-ratelimit-reset", future_ts.to_string().parse().unwrap());
        let d = parse_retry_after(&headers).unwrap();
        // Should be approximately 15 seconds
        assert!(
            d >= Duration::from_secs(13) && d <= Duration::from_secs(17),
            "Expected ~15s delay, got {d:?}"
        );
    }

    #[test]
    fn parse_retry_after_x_ratelimit_reset_in_past() {
        let mut headers = reqwest::header::HeaderMap::new();
        let past_ts = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs()
            - 60;
        headers.insert("x-ratelimit-reset", past_ts.to_string().parse().unwrap());
        let d = parse_retry_after(&headers).unwrap();
        assert_eq!(d, Duration::ZERO, "Past timestamp should return zero delay");
    }

    #[test]
    fn parse_retry_after_ms_takes_priority() {
        let mut headers = reqwest::header::HeaderMap::new();
        headers.insert("retry-after-ms", "500".parse().unwrap());
        headers.insert("retry-after", "60".parse().unwrap());
        let d = parse_retry_after(&headers).unwrap();
        assert_eq!(
            d,
            Duration::from_millis(500),
            "retry-after-ms should take priority"
        );
    }

    #[test]
    fn parse_retry_after_no_headers() {
        let headers = reqwest::header::HeaderMap::new();
        assert!(parse_retry_after(&headers).is_none());
    }

    #[test]
    fn extract_error_message_prefers_nested_provider_message() {
        let body = r#"{"error":{"message":"deployment not found"}}"#;
        assert_eq!(
            extract_error_message(body).as_deref(),
            Some("deployment not found")
        );
    }

    #[test]
    fn parse_retry_after_from_body_supports_nested_fields() {
        let body = r#"{"error":{"retry_after":12}}"#;
        assert_eq!(parse_retry_after_from_body(body), Some(12));
    }

    #[test]
    fn model_not_found_detection_catches_azure_style_messages() {
        assert!(indicates_model_not_found(
            "The API deployment for this resource does not exist"
        ));
    }

    #[test]
    fn context_overflow_detection_matches_common_provider_messages() {
        assert!(is_context_overflow_message(
            "This model's maximum context length was exceeded"
        ));
    }

    #[test]
    fn compute_retry_delay_uses_server_hint_as_floor() {
        let hint = Duration::from_secs(30);
        let delay = compute_retry_delay(Some(hint), 1, Duration::from_secs(120));
        // With hint=30s and attempt=1 (exp=2s), should use hint as base
        // With ±20% jitter: [24s, 36s]
        assert!(
            delay >= Duration::from_secs(24) && delay <= Duration::from_secs(36),
            "Expected delay based on 30s hint with jitter, got {delay:?}"
        );
    }

    #[test]
    fn compute_retry_delay_uses_backoff_when_larger() {
        let hint = Duration::from_millis(100);
        let delay = compute_retry_delay(Some(hint), 3, Duration::from_secs(120));
        // attempt=3 -> exp=8s, which is larger than 100ms hint
        // With ±20% jitter: [6.4s, 9.6s]
        assert!(
            delay >= Duration::from_millis(6400) && delay <= Duration::from_millis(9600),
            "Expected delay based on 8s backoff with jitter, got {delay:?}"
        );
    }

    #[test]
    fn compute_retry_delay_caps_at_max() {
        let hint = Duration::from_secs(200);
        let delay = compute_retry_delay(Some(hint), 1, Duration::from_secs(120));
        assert!(
            delay <= Duration::from_secs(120),
            "Delay should be capped at max_delay, got {delay:?}"
        );
    }

    #[test]
    fn compute_retry_delay_fallback_without_hint() {
        let delay = compute_retry_delay(None, 2, Duration::from_secs(120));
        // attempt=2 -> exp=4s, with ±20% jitter: [3.2s, 4.8s]
        assert!(
            delay >= Duration::from_millis(3200) && delay <= Duration::from_millis(4800),
            "Expected exponential backoff with jitter, got {delay:?}"
        );
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
