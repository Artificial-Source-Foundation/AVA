use std::time::Duration;

use ava_types::{AvaError, Message, Result, Role, Tool, ToolCall};
use serde_json::{json, Value};
use uuid::Uuid;

const DEFAULT_MAX_RETRIES: usize = 3;

pub fn map_messages_openai(messages: &[Message]) -> Vec<Value> {
    messages
        .iter()
        .map(|message| {
            // Assistant messages with native tool calls
            if message.role == Role::Assistant && !message.tool_calls.is_empty() {
                let tool_calls: Vec<Value> = message
                    .tool_calls
                    .iter()
                    .map(|tc| {
                        json!({
                            "id": tc.id,
                            "type": "function",
                            "function": {
                                "name": tc.name,
                                "arguments": tc.arguments.to_string(),
                            }
                        })
                    })
                    .collect();
                // Always include content (null when empty) — some providers
                // reject assistant messages with tool_calls but no content field.
                let content: Value = if message.content.is_empty() {
                    Value::Null
                } else {
                    json!(message.content)
                };
                return json!({
                    "role": "assistant",
                    "content": content,
                    "tool_calls": tool_calls,
                });
            }

            // Tool result messages
            if message.role == Role::Tool {
                let tool_call_id = message
                    .tool_call_id
                    .as_deref()
                    .filter(|id| !id.is_empty())
                    .unwrap_or("unknown");
                return json!({
                    "role": "tool",
                    "content": message.content,
                    "tool_call_id": tool_call_id,
                });
            }

            json!({
                "role": map_role(&message.role),
                "content": message.content,
            })
        })
        .collect()
}

pub fn map_messages_anthropic(messages: &[Message]) -> (Option<String>, Vec<Value>) {
    let mut system_parts = Vec::new();
    let mut mapped = Vec::new();

    for message in messages {
        if message.role == Role::System {
            system_parts.push(message.content.clone());
            continue;
        }

        // Assistant messages with native tool calls
        if message.role == Role::Assistant && !message.tool_calls.is_empty() {
            let mut content: Vec<Value> = Vec::new();
            if !message.content.is_empty() {
                content.push(json!({"type": "text", "text": message.content}));
            }
            for tc in &message.tool_calls {
                content.push(json!({
                    "type": "tool_use",
                    "id": tc.id,
                    "name": tc.name,
                    "input": tc.arguments,
                }));
            }
            mapped.push(json!({"role": "assistant", "content": content}));
            continue;
        }

        // Tool result messages
        if message.role == Role::Tool {
            let content = vec![json!({
                "type": "tool_result",
                "tool_use_id": message.tool_call_id.as_deref().unwrap_or(""),
                "content": message.content,
            })];
            mapped.push(json!({"role": "user", "content": content}));
            continue;
        }

        let role = if matches!(message.role, Role::Assistant) {
            "assistant"
        } else {
            "user"
        };

        mapped.push(json!({
            "role": role,
            "content": message.content,
        }));
    }

    let system = if system_parts.is_empty() {
        None
    } else {
        Some(system_parts.join("\n"))
    };

    (system, mapped)
}

pub fn map_messages_gemini_parts(messages: &[Message]) -> (Option<Value>, Vec<Value>) {
    let mut system_parts = Vec::new();
    let mut mapped = Vec::new();

    for message in messages {
        if message.role == Role::System {
            system_parts.push(json!({"text": message.content}));
            continue;
        }

        mapped.push(json!({
            "role": if matches!(message.role, Role::Assistant) { "model" } else { "user" },
            "parts": [{"text": message.content}],
        }));
    }

    let system_instruction = if system_parts.is_empty() {
        None
    } else {
        Some(json!({"parts": system_parts}))
    };

    (system_instruction, mapped)
}

pub fn model_pricing_usd_per_million(model: &str) -> (f64, f64) {
    if model.contains("mini") {
        (0.15, 0.60)
    } else if model.contains("claude") {
        (3.00, 15.00)
    } else if model.contains("gemini") {
        (0.35, 1.05)
    } else {
        (2.50, 10.00)
    }
}

pub fn estimate_cost_usd(input_tokens: usize, output_tokens: usize, in_rate: f64, out_rate: f64) -> f64 {
    input_tokens as f64 / 1_000_000.0 * in_rate + output_tokens as f64 / 1_000_000.0 * out_rate
}

pub fn rate_limited_error(provider: &str, body: &str) -> AvaError {
    let retry_after_secs = serde_json::from_str::<Value>(body)
        .ok()
        .and_then(|v| v.get("retry_after")?.as_u64())
        .unwrap_or(30);

    AvaError::RateLimited {
        provider: provider.to_string(),
        retry_after_secs,
    }
}

pub fn estimate_tokens(input: &str) -> usize {
    (input.chars().count() / 4).max(1)
}

pub fn parse_sse_lines(text: &str) -> Vec<String> {
    text.lines()
        .filter_map(|line| line.strip_prefix("data: "))
        .filter(|payload| *payload != "[DONE]")
        .map(ToString::to_string)
        .collect()
}

pub fn parse_openai_completion_payload(payload: &Value) -> Result<String> {
    let choice = payload
        .get("choices")
        .and_then(Value::as_array)
        .and_then(|choices| choices.first())
        .ok_or_else(|| AvaError::SerializationError("missing OpenAI completion choices".to_string()))?;

    // Content may be null when finish_reason is "stop" with no further text
    let content = choice
        .get("message")
        .and_then(|message| message.get("content"))
        .and_then(Value::as_str)
        .unwrap_or("");

    Ok(content.to_string())
}

pub fn parse_openai_delta_payload(payload: &Value) -> Option<String> {
    payload
        .get("choices")
        .and_then(Value::as_array)
        .and_then(|choices| choices.first())
        .and_then(|choice| choice.get("delta"))
        .and_then(|delta| delta.get("content"))
        .and_then(Value::as_str)
        .map(ToString::to_string)
}

pub fn parse_anthropic_completion_payload(payload: &Value) -> Result<String> {
    payload
        .get("content")
        .and_then(Value::as_array)
        .and_then(|content| content.first())
        .and_then(|part| part.get("text"))
        .and_then(Value::as_str)
        .map(ToString::to_string)
        .ok_or_else(|| AvaError::SerializationError("missing Anthropic completion content".to_string()))
}

pub fn parse_anthropic_delta_payload(payload: &Value) -> Option<String> {
    if payload.get("type").and_then(Value::as_str) != Some("content_block_delta") {
        return None;
    }

    payload
        .get("delta")
        .and_then(|delta| delta.get("text"))
        .and_then(Value::as_str)
        .map(ToString::to_string)
}

pub fn parse_ollama_completion_payload(payload: &Value) -> Result<String> {
    payload
        .get("message")
        .and_then(|message| message.get("content"))
        .and_then(Value::as_str)
        .map(ToString::to_string)
        .ok_or_else(|| AvaError::SerializationError("missing Ollama completion content".to_string()))
}

pub fn parse_gemini_completion_payload(payload: &Value) -> Result<String> {
    payload
        .get("candidates")
        .and_then(Value::as_array)
        .and_then(|candidates| candidates.first())
        .and_then(|candidate| candidate.get("content"))
        .and_then(|content| content.get("parts"))
        .and_then(Value::as_array)
        .and_then(|parts| parts.first())
        .and_then(|part| part.get("text"))
        .and_then(Value::as_str)
        .map(ToString::to_string)
        .ok_or_else(|| AvaError::SerializationError("missing Gemini completion content".to_string()))
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
        let cloned = request.try_clone().ok_or_else(|| {
            AvaError::ProviderError {
                provider: provider.to_string(),
                message: "request body is not clonable (streaming body?)".to_string(),
            }
        })?;

        match cloned.send().await {
            Ok(resp) if resp.status() == reqwest::StatusCode::TOO_MANY_REQUESTS => {
                attempts += 1;
                if attempts > max_retries {
                    return validate_status(resp, provider).await.map(|_| unreachable!());
                }
                let delay = resp
                    .headers()
                    .get("retry-after")
                    .and_then(|v| v.to_str().ok())
                    .and_then(|v| v.parse::<u64>().ok())
                    .unwrap_or_else(|| 2u64.saturating_pow(attempts as u32));
                tokio::time::sleep(Duration::from_secs(delay)).await;
            }
            Ok(resp) if resp.status().is_server_error() => {
                attempts += 1;
                if attempts > max_retries {
                    return validate_status(resp, provider).await.map(|_| unreachable!());
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

/// Convenience wrapper using the default retry count.
pub async fn send_retrying(
    request: reqwest::RequestBuilder,
    provider: &str,
) -> Result<reqwest::Response> {
    send_with_retry(request, provider, DEFAULT_MAX_RETRIES).await
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

pub async fn validate_status(response: reqwest::Response, provider: &str) -> Result<reqwest::Response> {
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

fn map_role(role: &Role) -> &'static str {
    match role {
        Role::System => "system",
        Role::User => "user",
        Role::Assistant => "assistant",
        Role::Tool => "tool",
    }
}

/// Convert AVA tool definitions to the OpenAI function calling format.
pub fn tools_to_openai_format(tools: &[Tool]) -> Vec<Value> {
    tools
        .iter()
        .map(|tool| {
            json!({
                "type": "function",
                "function": {
                    "name": tool.name,
                    "description": tool.description,
                    "parameters": tool.parameters,
                }
            })
        })
        .collect()
}

/// Convert AVA tool definitions to the Anthropic tool use format.
pub fn tools_to_anthropic_format(tools: &[Tool]) -> Vec<Value> {
    tools
        .iter()
        .map(|tool| {
            json!({
                "name": tool.name,
                "description": tool.description,
                "input_schema": tool.parameters,
            })
        })
        .collect()
}

/// Parse tool calls from an OpenAI completion response payload.
pub fn parse_openai_tool_calls(payload: &Value) -> Vec<ToolCall> {
    let Some(choice) = payload
        .get("choices")
        .and_then(Value::as_array)
        .and_then(|c| c.first())
    else {
        return vec![];
    };

    let Some(tool_calls) = choice
        .get("message")
        .and_then(|m| m.get("tool_calls"))
        .and_then(Value::as_array)
    else {
        return vec![];
    };

    tool_calls
        .iter()
        .filter_map(|tc| {
            let id = tc.get("id").and_then(Value::as_str).unwrap_or("").to_string();
            let function = tc.get("function")?;
            let name = function.get("name").and_then(Value::as_str)?.to_string();
            let arguments_str = function
                .get("arguments")
                .and_then(Value::as_str)
                .unwrap_or("{}");
            let arguments = serde_json::from_str(arguments_str).unwrap_or(json!({}));
            Some(ToolCall {
                id: if id.is_empty() {
                    Uuid::new_v4().to_string()
                } else {
                    id
                },
                name,
                arguments,
            })
        })
        .collect()
}

/// Parse tool use blocks from an Anthropic completion response payload.
pub fn parse_anthropic_tool_calls(payload: &Value) -> Vec<ToolCall> {
    let Some(content) = payload.get("content").and_then(Value::as_array) else {
        return vec![];
    };

    content
        .iter()
        .filter(|block| block.get("type").and_then(Value::as_str) == Some("tool_use"))
        .filter_map(|block| {
            let id = block
                .get("id")
                .and_then(Value::as_str)
                .unwrap_or("")
                .to_string();
            let name = block.get("name").and_then(Value::as_str)?.to_string();
            let arguments = block.get("input").cloned().unwrap_or(json!({}));
            Some(ToolCall {
                id: if id.is_empty() {
                    Uuid::new_v4().to_string()
                } else {
                    id
                },
                name,
                arguments,
            })
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

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
        let tool_array = body.get("tools").and_then(Value::as_array).unwrap();
        assert_eq!(tool_array.len(), 1);
        assert_eq!(
            tool_array[0]["function"]["name"].as_str().unwrap(),
            "read"
        );
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
        let assistant_msg = Message::new(Role::Assistant, "")
            .with_tool_calls(vec![tc]);
        let tool_msg = Message::new(Role::Tool, "file contents")
            .with_tool_call_id("call_1");

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
        let assistant_msg = Message::new(Role::Assistant, "Let me list files.")
            .with_tool_calls(vec![tc]);
        let tool_msg = Message::new(Role::Tool, "file1.txt\nfile2.txt")
            .with_tool_call_id("toolu_1");

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
}
