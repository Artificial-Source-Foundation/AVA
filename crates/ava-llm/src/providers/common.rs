use ava_types::{AvaError, Message, Result, Role};
use serde_json::{json, Value};

pub fn map_messages_openai(messages: &[Message]) -> Vec<Value> {
    messages
        .iter()
        .map(|message| {
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
    AvaError::ToolError(format!("{provider} rate limited: {body}"))
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

pub fn reqwest_error(error: reqwest::Error) -> AvaError {
    AvaError::ToolError(format!("network error: {error}"))
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
        return Err(AvaError::PermissionDenied(format!("{provider} authentication failed")));
    }

    if status == reqwest::StatusCode::TOO_MANY_REQUESTS {
        return Err(rate_limited_error(provider, &body));
    }

    Err(AvaError::ToolError(format!(
        "{provider} request failed ({status}): {body}"
    )))
}

fn map_role(role: &Role) -> &'static str {
    match role {
        Role::System => "system",
        Role::User => "user",
        Role::Assistant => "assistant",
        Role::Tool => "tool",
    }
}
