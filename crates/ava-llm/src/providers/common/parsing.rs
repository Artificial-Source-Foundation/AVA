use ava_types::{AvaError, Result, StreamChunk, StreamToolCall, Tool, ToolCall};
use serde_json::{json, Value};
use uuid::Uuid;

/// Sentinel emitted when a Responses API reasoning item starts.
/// The streaming wrapper in `openai.rs` intercepts this to track timing.
pub const REASONING_START_SENTINEL: &str = "\x00REASONING_START\x00";

/// Sentinel emitted when a Responses API reasoning item completes without
/// summary text (unverified org). The streaming wrapper converts this
/// into a "Thought for X.Xs" message using elapsed time.
pub const REASONING_END_SENTINEL: &str = "\x00REASONING_END\x00";

pub fn model_pricing_usd_per_million(model: &str) -> (f64, f64) {
    // Delegate to the compiled-in model registry for known models.
    if let Some(pricing) = ava_config::model_catalog::registry::registry().pricing(model) {
        return pricing;
    }
    // Fallback heuristic for unknown models not in the registry.
    let m = model.to_lowercase();
    if m.contains("claude") && m.contains("opus") {
        (15.00, 75.00)
    } else if m.contains("claude") && m.contains("haiku") {
        (0.25, 1.25)
    } else if m.contains("claude") {
        (3.00, 15.00)
    } else if m.contains("gpt-4o-mini") || m.contains("gpt-4.1-mini") {
        (0.15, 0.60)
    } else if m.contains("gpt-4o") || m.contains("gpt-4.1") {
        (2.50, 10.00)
    } else if m.contains("o3") || m.contains("o4-mini") {
        (1.10, 4.40)
    } else if m.contains("gemini") && m.contains("flash") {
        (0.075, 0.30)
    } else if m.contains("gemini") && m.contains("pro") {
        (1.25, 5.00)
    } else if m.contains("gemini") {
        (0.35, 1.05)
    } else if m.starts_with("glm-")
        || m.starts_with("minimax-")
        || m.starts_with("k2p5")
        || m.starts_with("kimi-k2")
        || m.starts_with("qwen")
        || m.starts_with("qvq")
    {
        (0.0, 0.0)
    } else if m.contains("mini") {
        (0.15, 0.60)
    } else {
        (2.50, 10.00)
    }
}

/// Parse token usage from an API response payload.
/// Works for both OpenAI-style and Anthropic-style `usage` objects.
/// Extracts cache tokens when present (Anthropic `cache_read_input_tokens` /
/// `cache_creation_input_tokens`, OpenAI `prompt_tokens_details.cached_tokens`).
pub fn parse_usage(payload: &Value) -> Option<ava_types::TokenUsage> {
    let usage = payload.get("usage")?;
    let input = usage
        .get("input_tokens")
        .or_else(|| usage.get("prompt_tokens"))
        .and_then(Value::as_u64)
        .unwrap_or(0) as usize;
    let output = usage
        .get("output_tokens")
        .or_else(|| usage.get("completion_tokens"))
        .and_then(Value::as_u64)
        .unwrap_or(0) as usize;

    // Anthropic cache fields
    let cache_read = usage
        .get("cache_read_input_tokens")
        .and_then(Value::as_u64)
        .unwrap_or(0) as usize;
    let cache_creation = usage
        .get("cache_creation_input_tokens")
        .and_then(Value::as_u64)
        .unwrap_or(0) as usize;

    // OpenAI cache field: prompt_tokens_details.cached_tokens
    let openai_cached = usage
        .get("prompt_tokens_details")
        .and_then(|d| d.get("cached_tokens"))
        .and_then(Value::as_u64)
        .unwrap_or(0) as usize;

    Some(ava_types::TokenUsage {
        input_tokens: input,
        output_tokens: output,
        cache_read_tokens: cache_read.max(openai_cached),
        cache_creation_tokens: cache_creation,
    })
}

/// Parse token usage from a Gemini API response payload.
/// Gemini uses `usageMetadata` with `promptTokenCount` / `candidatesTokenCount`.
pub fn parse_gemini_usage(payload: &Value) -> Option<ava_types::TokenUsage> {
    let meta = payload.get("usageMetadata")?;
    let input = meta
        .get("promptTokenCount")
        .and_then(Value::as_u64)
        .unwrap_or(0) as usize;
    let output = meta
        .get("candidatesTokenCount")
        .and_then(Value::as_u64)
        .unwrap_or(0) as usize;
    let cached = meta
        .get("cachedContentTokenCount")
        .and_then(Value::as_u64)
        .unwrap_or(0) as usize;
    Some(ava_types::TokenUsage {
        input_tokens: input,
        output_tokens: output,
        cache_read_tokens: cached,
        cache_creation_tokens: 0,
    })
}

pub fn estimate_cost_usd(
    input_tokens: usize,
    output_tokens: usize,
    in_rate: f64,
    out_rate: f64,
) -> f64 {
    input_tokens as f64 / 1_000_000.0 * in_rate + output_tokens as f64 / 1_000_000.0 * out_rate
}

/// Estimate cost accounting for prompt cache token pricing.
/// - Cache read tokens cost 10% of normal input rate.
/// - Cache creation tokens cost 125% of normal input rate.
/// - Non-cached input tokens are charged at the normal input rate.
pub fn estimate_cost_with_cache_usd(
    usage: &ava_types::TokenUsage,
    in_rate: f64,
    out_rate: f64,
) -> f64 {
    let m = 1_000_000.0;
    let non_cached_input = usage.input_tokens.saturating_sub(usage.cache_read_tokens);
    non_cached_input as f64 / m * in_rate
        + usage.cache_read_tokens as f64 / m * in_rate * 0.1
        + usage.cache_creation_tokens as f64 / m * in_rate * 1.25
        + usage.output_tokens as f64 / m * out_rate
}

/// Estimate tokens using accurate BPE tokenization (cl100k_base).
///
/// Delegates to `ava_context::count_tokens_default` for precise counting.
/// Always returns at least 1.
pub fn estimate_tokens(input: &str) -> usize {
    ava_context::count_tokens_default(input).max(1)
}

/// Model-aware token estimation using the appropriate BPE encoding.
///
/// Selects cl100k_base or o200k_base based on the model name.
/// Always returns at least 1.
pub fn estimate_tokens_for_model(input: &str, model: &str) -> usize {
    ava_context::count_tokens_for_model(input, model).max(1)
}

pub fn parse_sse_lines(text: &str) -> Vec<String> {
    text.lines()
        .filter_map(|line| {
            // SSE spec allows both "data: payload" and "data:payload"
            line.strip_prefix("data: ")
                .or_else(|| line.strip_prefix("data:"))
        })
        .filter(|payload| *payload != "[DONE]")
        .map(ToString::to_string)
        .collect()
}

/// Buffered SSE parser that handles partial network chunks correctly.
///
/// Network chunks can split mid-event (e.g., a JSON payload split across two
/// TCP frames). This parser accumulates data until complete SSE events
/// (delimited by double newlines) are available, then extracts `data:` lines.
#[derive(Debug, Default)]
pub struct SseParser {
    buffer: String,
}

impl SseParser {
    pub fn new() -> Self {
        Self {
            buffer: String::new(),
        }
    }

    /// Feed a network chunk into the parser and return any complete SSE data payloads.
    pub fn feed(&mut self, chunk: &str) -> Vec<String> {
        self.buffer.push_str(chunk);
        let mut events = Vec::new();

        // SSE events are separated by double newlines (\n\n)
        while let Some(pos) = self.buffer.find("\n\n") {
            let event_text = self.buffer[..pos].to_string();
            self.buffer = self.buffer[pos + 2..].to_string();

            // Extract data lines from the complete event
            for line in event_text.lines() {
                let payload = line
                    .strip_prefix("data: ")
                    .or_else(|| line.strip_prefix("data:"));
                if let Some(payload) = payload {
                    if payload != "[DONE]" {
                        events.push(payload.to_string());
                    }
                }
            }
        }

        events
    }
}

pub fn parse_openai_completion_payload(payload: &Value) -> Result<String> {
    let choice = payload
        .get("choices")
        .and_then(Value::as_array)
        .and_then(|choices| choices.first())
        .ok_or_else(|| {
            AvaError::SerializationError("missing OpenAI completion choices".to_string())
        })?;

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
        .and_then(|content| {
            content
                .iter()
                .find(|block| block.get("type").and_then(Value::as_str) == Some("text"))
        })
        .and_then(|part| part.get("text"))
        .and_then(Value::as_str)
        .map(ToString::to_string)
        .ok_or_else(|| {
            AvaError::SerializationError("missing Anthropic completion content".to_string())
        })
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

/// Parse a rich StreamChunk from an Anthropic SSE event payload.
pub fn parse_anthropic_stream_chunk(payload: &Value) -> Option<StreamChunk> {
    let event_type = payload.get("type").and_then(Value::as_str)?;
    match event_type {
        "content_block_delta" => {
            let delta = payload.get("delta")?;
            let delta_type = delta.get("type").and_then(Value::as_str).unwrap_or("");
            match delta_type {
                "text_delta" => {
                    let text = delta.get("text").and_then(Value::as_str)?;
                    Some(StreamChunk::text(text))
                }
                "thinking_delta" => {
                    let thinking = delta.get("thinking").and_then(Value::as_str)?;
                    Some(StreamChunk {
                        thinking: Some(thinking.to_string()),
                        ..Default::default()
                    })
                }
                "input_json_delta" => {
                    let partial_json = delta.get("partial_json").and_then(Value::as_str)?;
                    let index = payload.get("index").and_then(Value::as_u64).unwrap_or(0) as usize;
                    Some(StreamChunk {
                        tool_call: Some(StreamToolCall {
                            index,
                            id: None,
                            name: None,
                            arguments_delta: Some(partial_json.to_string()),
                        }),
                        ..Default::default()
                    })
                }
                _ => None,
            }
        }
        "content_block_start" => {
            let content_block = payload.get("content_block")?;
            let block_type = content_block.get("type").and_then(Value::as_str)?;
            if block_type == "tool_use" {
                let index = payload.get("index").and_then(Value::as_u64).unwrap_or(0) as usize;
                let id = content_block
                    .get("id")
                    .and_then(Value::as_str)
                    .map(String::from);
                let name = content_block
                    .get("name")
                    .and_then(Value::as_str)
                    .map(String::from);
                Some(StreamChunk {
                    tool_call: Some(StreamToolCall {
                        index,
                        id,
                        name,
                        arguments_delta: None,
                    }),
                    ..Default::default()
                })
            } else {
                None
            }
        }
        "message_delta" => {
            let usage = payload.get("usage").map(|u| {
                let output = u.get("output_tokens").and_then(Value::as_u64).unwrap_or(0) as usize;
                ava_types::TokenUsage {
                    input_tokens: 0,
                    output_tokens: output,
                    ..Default::default()
                }
            });
            if usage.is_some() {
                Some(StreamChunk {
                    usage,
                    done: true,
                    ..Default::default()
                })
            } else {
                None
            }
        }
        "message_start" => {
            let usage = payload
                .get("message")
                .and_then(|m| m.get("usage"))
                .map(|u| {
                    let input = u.get("input_tokens").and_then(Value::as_u64).unwrap_or(0) as usize;
                    let cache_read = u
                        .get("cache_read_input_tokens")
                        .and_then(Value::as_u64)
                        .unwrap_or(0) as usize;
                    let cache_creation = u
                        .get("cache_creation_input_tokens")
                        .and_then(Value::as_u64)
                        .unwrap_or(0) as usize;
                    ava_types::TokenUsage {
                        input_tokens: input,
                        output_tokens: 0,
                        cache_read_tokens: cache_read,
                        cache_creation_tokens: cache_creation,
                    }
                });
            if usage.is_some() {
                Some(StreamChunk {
                    usage,
                    ..Default::default()
                })
            } else {
                None
            }
        }
        _ => None,
    }
}

/// Parse a rich StreamChunk from an OpenAI SSE event payload.
pub fn parse_openai_stream_chunk(payload: &Value) -> Option<StreamChunk> {
    let choice = payload
        .get("choices")
        .and_then(Value::as_array)
        .and_then(|c| c.first())?;
    let delta = choice.get("delta")?;

    let mut chunk = StreamChunk::default();
    let mut has_data = false;

    // Text content
    if let Some(content) = delta.get("content").and_then(Value::as_str) {
        if !content.is_empty() {
            chunk.content = Some(content.to_string());
            has_data = true;
        }
    }

    // Reasoning/thinking content (Chat Completions uses `reasoning_content` in delta)
    if let Some(reasoning) = delta
        .get("reasoning_content")
        .or_else(|| delta.get("reasoning"))
        .and_then(Value::as_str)
    {
        if !reasoning.is_empty() {
            chunk.thinking = Some(reasoning.to_string());
            has_data = true;
        }
    }

    // Tool calls
    if let Some(tool_calls) = delta.get("tool_calls").and_then(Value::as_array) {
        if let Some(tc) = tool_calls.first() {
            let index = tc.get("index").and_then(Value::as_u64).unwrap_or(0) as usize;
            let id = tc.get("id").and_then(Value::as_str).map(String::from);
            let name = tc
                .get("function")
                .and_then(|f| f.get("name"))
                .and_then(Value::as_str)
                .map(String::from);
            let arguments_delta = tc
                .get("function")
                .and_then(|f| f.get("arguments"))
                .and_then(Value::as_str)
                .map(String::from);
            chunk.tool_call = Some(StreamToolCall {
                index,
                id,
                name,
                arguments_delta,
            });
            has_data = true;
        }
    }

    // Finish reason
    if let Some(finish) = choice.get("finish_reason").and_then(Value::as_str) {
        if finish == "stop" || finish == "tool_calls" {
            chunk.done = true;
            has_data = true;
        }
    }

    // Usage (typically in the final chunk with stream_options: include_usage)
    if let Some(usage) = payload.get("usage") {
        let input = usage
            .get("prompt_tokens")
            .and_then(Value::as_u64)
            .unwrap_or(0) as usize;
        let output = usage
            .get("completion_tokens")
            .and_then(Value::as_u64)
            .unwrap_or(0) as usize;
        let cached = usage
            .get("prompt_tokens_details")
            .and_then(|d| d.get("cached_tokens"))
            .and_then(Value::as_u64)
            .unwrap_or(0) as usize;
        if input > 0 || output > 0 {
            chunk.usage = Some(ava_types::TokenUsage {
                input_tokens: input,
                output_tokens: output,
                cache_read_tokens: cached,
                cache_creation_tokens: 0,
            });
            has_data = true;
        }
    }

    if has_data {
        Some(chunk)
    } else {
        None
    }
}

pub fn parse_ollama_completion_payload(payload: &Value) -> Result<String> {
    payload
        .get("message")
        .and_then(|message| message.get("content"))
        .and_then(Value::as_str)
        .map(ToString::to_string)
        .ok_or_else(|| {
            AvaError::SerializationError("missing Ollama completion content".to_string())
        })
}

/// Parse tool calls from an Ollama API response payload.
/// Ollama returns tool calls in `message.tool_calls[]` using an OpenAI-compatible format.
pub fn parse_ollama_tool_calls(payload: &Value) -> Vec<ToolCall> {
    let Some(tool_calls) = payload
        .get("message")
        .and_then(|m| m.get("tool_calls"))
        .and_then(Value::as_array)
    else {
        return vec![];
    };

    tool_calls
        .iter()
        .filter_map(|tc| {
            let function = tc.get("function")?;
            let name = function.get("name").and_then(Value::as_str)?.to_string();
            let arguments = function.get("arguments").cloned().unwrap_or(json!({}));
            let id = tc
                .get("id")
                .and_then(Value::as_str)
                .map(String::from)
                .unwrap_or_else(|| Uuid::new_v4().to_string());
            Some(ToolCall {
                id,
                name,
                arguments,
            })
        })
        .collect()
}

/// Parse a streaming tool call from an Ollama response chunk.
pub fn parse_ollama_stream_tool_call(payload: &Value) -> Option<StreamToolCall> {
    let tool_calls = payload
        .get("message")
        .and_then(|m| m.get("tool_calls"))
        .and_then(Value::as_array)?;

    let tc = tool_calls.first()?;
    let function = tc.get("function")?;
    let name = function
        .get("name")
        .and_then(Value::as_str)
        .map(String::from);
    let arguments = function.get("arguments").map(|a| a.to_string());
    let id = tc
        .get("id")
        .and_then(Value::as_str)
        .map(String::from)
        .or_else(|| Some(Uuid::new_v4().to_string()));

    Some(StreamToolCall {
        index: 0,
        id,
        name,
        arguments_delta: arguments,
    })
}

/// Parse token usage from an Ollama API response payload.
/// Ollama uses `prompt_eval_count` (input) and `eval_count` (output) at the top level.
/// These appear in non-streaming responses and in the final streaming chunk (`done: true`).
pub fn parse_ollama_usage(payload: &Value) -> Option<ava_types::TokenUsage> {
    let input = payload
        .get("prompt_eval_count")
        .and_then(Value::as_u64)
        .unwrap_or(0) as usize;
    let output = payload
        .get("eval_count")
        .and_then(Value::as_u64)
        .unwrap_or(0) as usize;
    if input == 0 && output == 0 {
        return None;
    }
    Some(ava_types::TokenUsage {
        input_tokens: input,
        output_tokens: output,
        cache_read_tokens: 0,
        cache_creation_tokens: 0,
    })
}

pub fn parse_gemini_completion_payload(payload: &Value) -> Result<String> {
    let parts = payload
        .get("candidates")
        .and_then(Value::as_array)
        .and_then(|candidates| candidates.first())
        .and_then(|candidate| candidate.get("content"))
        .and_then(|content| content.get("parts"))
        .and_then(Value::as_array)
        .ok_or_else(|| {
            AvaError::SerializationError("missing Gemini completion content".to_string())
        })?;

    // Collect text from all text parts (skip functionCall and thought parts)
    let text: String = parts
        .iter()
        .filter(|part| part.get("functionCall").is_none())
        .filter(|part| part.get("thought").and_then(Value::as_bool) != Some(true))
        .filter_map(|part| part.get("text").and_then(Value::as_str))
        .collect::<Vec<_>>()
        .join("");

    if text.is_empty() && parts.iter().any(|p| p.get("functionCall").is_some()) {
        // Tool call only response — return empty string (not an error)
        Ok(String::new())
    } else if text.is_empty() {
        Err(AvaError::SerializationError(
            "missing Gemini completion content".to_string(),
        ))
    } else {
        Ok(text)
    }
}

/// Parse tool calls from a Gemini API response payload.
/// Gemini returns tool calls as `functionCall` parts within `candidates[].content.parts[]`.
pub fn parse_gemini_tool_calls(payload: &Value) -> Vec<ToolCall> {
    let Some(parts) = payload
        .get("candidates")
        .and_then(Value::as_array)
        .and_then(|candidates| candidates.first())
        .and_then(|candidate| candidate.get("content"))
        .and_then(|content| content.get("parts"))
        .and_then(Value::as_array)
    else {
        return vec![];
    };

    parts
        .iter()
        .filter_map(|part| {
            let fc = part.get("functionCall")?;
            let name = fc.get("name").and_then(Value::as_str)?.to_string();
            let arguments = fc.get("args").cloned().unwrap_or(json!({}));
            let id = fc
                .get("id")
                .and_then(Value::as_str)
                .map(String::from)
                .unwrap_or_else(|| Uuid::new_v4().to_string());
            Some(ToolCall {
                id,
                name,
                arguments,
            })
        })
        .collect()
}

/// Convert AVA tool definitions to the Gemini function calling format.
/// Transform a JSON Schema for a specific provider family.
///
/// Different providers have quirks in how they handle tool schemas:
/// - **Gemini**: Integer enums must be represented as string enums. Arrays must have
///   an `items` field (Gemini rejects bare `{"type":"array"}` without `items`).
/// - **OpenAI Responses API**: Requires strict mode (handled separately in
///   `tools_to_responses_api_format` via `make_strict_schema`).
/// - **Anthropic**: No transform needed (works as-is).
///
/// This function applies in-place transformations for the given provider string.
pub fn transform_schema_for_provider(provider: &str, mut schema: Value) -> Value {
    match provider {
        "gemini" => {
            transform_schema_gemini(&mut schema);
            schema
        }
        // OpenAI strict transforms are handled in tools_to_responses_api_format
        // Anthropic, OpenRouter, Ollama, Copilot, Inception: no transform needed
        _ => schema,
    }
}

/// Recursively transform a JSON Schema for Gemini compatibility.
///
/// Gemini-specific transformations:
/// 1. Convert `integer` enum values to `string` enums (Gemini rejects integer enums
///    in function declarations).
/// 2. Add `items: {"type": "string"}` to arrays that have no `items` field (Gemini
///    rejects `{"type": "array"}` without an `items` specification).
fn transform_schema_gemini(schema: &mut Value) {
    let Some(obj) = schema.as_object_mut() else {
        return;
    };

    let schema_type = obj
        .get("type")
        .and_then(Value::as_str)
        .unwrap_or("")
        .to_string();

    // Fix 1: Convert integer enum to string enum.
    // Gemini's functionDeclarations schema doesn't support integer enums.
    if schema_type == "integer" {
        if let Some(enum_vals) = obj.get("enum") {
            if enum_vals.as_array().is_some() {
                // Convert type to string and stringify all enum values
                obj.insert("type".to_string(), Value::String("string".to_string()));
                if let Some(Value::Array(vals)) = obj.get_mut("enum") {
                    let stringified: Vec<Value> = vals
                        .iter()
                        .map(|v| {
                            Value::String(match v {
                                Value::Number(n) => n.to_string(),
                                Value::String(s) => s.clone(),
                                other => other.to_string(),
                            })
                        })
                        .collect();
                    *vals = stringified;
                }
            }
        }
    }

    // Fix 2: Ensure arrays have an `items` field.
    if schema_type == "array" && !obj.contains_key("items") {
        obj.insert("items".to_string(), json!({"type": "string"}));
    }

    // Recurse into properties
    if let Some(Value::Object(props)) = obj.get_mut("properties") {
        for prop_schema in props.values_mut() {
            transform_schema_gemini(prop_schema);
        }
    }

    // Recurse into items
    if let Some(items) = obj.get_mut("items") {
        transform_schema_gemini(items);
    }

    // Recurse into anyOf / oneOf / allOf
    for key in &["anyOf", "oneOf", "allOf"] {
        if let Some(Value::Array(variants)) = obj.get_mut(*key) {
            for variant in variants.iter_mut() {
                transform_schema_gemini(variant);
            }
        }
    }
}

pub fn tools_to_gemini_format(tools: &[Tool]) -> Vec<Value> {
    let declarations: Vec<Value> = tools
        .iter()
        .map(|tool| {
            let mut params = tool.parameters.clone();
            transform_schema_gemini(&mut params);
            json!({
                "name": tool.name,
                "description": tool.description,
                "parameters": params,
            })
        })
        .collect();
    vec![json!({ "functionDeclarations": declarations })]
}

/// Parse a rich StreamChunk from a Gemini SSE event payload.
/// Handles text, thinking, tool calls, and usage.
pub fn parse_gemini_stream_chunk(payload: &Value) -> Option<StreamChunk> {
    let candidate = payload
        .get("candidates")
        .and_then(Value::as_array)
        .and_then(|c| c.first());

    let mut chunk = StreamChunk::default();
    let mut has_data = false;

    if let Some(candidate) = candidate {
        if let Some(parts) = candidate
            .get("content")
            .and_then(|c| c.get("parts"))
            .and_then(Value::as_array)
        {
            for part in parts {
                // Thinking content
                if part.get("thought").and_then(Value::as_bool) == Some(true) {
                    if let Some(text) = part.get("text").and_then(Value::as_str) {
                        if !text.is_empty() {
                            chunk.thinking = Some(text.to_string());
                            has_data = true;
                        }
                    }
                    continue;
                }

                // Function call
                if let Some(fc) = part.get("functionCall") {
                    let id = fc
                        .get("id")
                        .and_then(Value::as_str)
                        .map(String::from)
                        .unwrap_or_else(|| Uuid::new_v4().to_string());
                    let name = fc.get("name").and_then(Value::as_str).map(String::from);
                    let args = fc.get("args").map(|a| a.to_string());
                    chunk.tool_call = Some(StreamToolCall {
                        index: 0,
                        id: Some(id),
                        name,
                        arguments_delta: args,
                    });
                    has_data = true;
                    continue;
                }

                // Text content
                if let Some(text) = part.get("text").and_then(Value::as_str) {
                    if !text.is_empty() {
                        chunk.content = Some(text.to_string());
                        has_data = true;
                    }
                }
            }
        }

        // Finish reason
        if let Some(reason) = candidate.get("finishReason").and_then(Value::as_str) {
            if reason == "STOP" || reason == "MAX_TOKENS" {
                chunk.done = true;
                has_data = true;
            }
        }
    }

    // Usage metadata
    if let Some(usage) = parse_gemini_usage(payload) {
        chunk.usage = Some(usage);
        has_data = true;
    }

    if has_data {
        Some(chunk)
    } else {
        None
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
    tools_to_anthropic_format_cached(tools, false)
}

/// Convert tools to Anthropic format, optionally adding `cache_control` to the
/// last tool definition so that the entire tool-definition prefix is cached.
pub fn tools_to_anthropic_format_cached(tools: &[Tool], cache: bool) -> Vec<Value> {
    let len = tools.len();
    tools
        .iter()
        .enumerate()
        .map(|(i, tool)| {
            let mut t = json!({
                "name": tool.name,
                "description": tool.description,
                "input_schema": tool.parameters,
            });
            if cache && i == len - 1 {
                t["cache_control"] = json!({"type": "ephemeral"});
            }
            t
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
            let id = tc
                .get("id")
                .and_then(Value::as_str)
                .unwrap_or("")
                .to_string();
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
/// Convert AVA tool definitions to the OpenAI Responses API format.
///
/// Recursively make a JSON Schema strict for the OpenAI Responses API:
/// - Add `additionalProperties: false` to every object
/// - Add ALL property keys to `required`
/// - Recurse into nested objects and array items
fn make_strict_schema(schema: &mut Value) {
    make_strict_schema_with_required(schema, &[]);
}

/// Recursively make a JSON Schema strict for the OpenAI Responses API.
///
/// OpenAI's strict mode requires:
/// 1. `additionalProperties: false` on every object schema
/// 2. ALL properties listed in `required`
/// 3. Optional properties (not originally in `required`) must allow `null`
///    so the model can omit them by sending `null` rather than inventing values.
///
/// `parent_required` is the set of property names that are already required
/// in the parent object — used when recursing into property schemas to decide
/// whether to add `null` to the type union.
fn make_strict_schema_with_required(schema: &mut Value, parent_required: &[String]) {
    let _ = parent_required; // used by callers, not at this level
    if let Some(obj) = schema.as_object_mut() {
        // If this is an object type with properties, make it strict
        if obj.get("type").and_then(|t| t.as_str()) == Some("object") {
            obj.entry("additionalProperties".to_string())
                .or_insert(Value::Bool(false));

            // Collect the set of properties that are already required so we
            // know which ones are optional and need a null-type allowance.
            let originally_required: Vec<String> = obj
                .get("required")
                .and_then(|r| r.as_array())
                .map(|arr| {
                    arr.iter()
                        .filter_map(|v| v.as_str().map(String::from))
                        .collect()
                })
                .unwrap_or_default();

            // Make all property keys required (OpenAI strict mode requirement).
            if let Some(props) = obj.get("properties").and_then(|p| p.as_object()) {
                let all_keys: Vec<Value> = props.keys().map(|k| Value::String(k.clone())).collect();
                obj.insert("required".to_string(), Value::Array(all_keys));
            }

            // Recurse into properties, passing required-status for null-type widening.
            if let Some(Value::Object(props)) = obj.get_mut("properties") {
                for (prop_name, prop_schema) in props.iter_mut() {
                    let is_optional = !originally_required.contains(prop_name);
                    if is_optional {
                        // Optional property promoted to required: allow null so
                        // the model can send null to indicate "not specified".
                        allow_null_in_schema(prop_schema);
                    }
                    make_strict_schema(prop_schema);
                }
            }
        } else {
            // Recurse into properties for non-top-level objects
            if let Some(Value::Object(props)) = obj.get_mut("properties") {
                for prop_schema in props.values_mut() {
                    make_strict_schema(prop_schema);
                }
            }
        }
        // Recurse into array items
        if let Some(items) = obj.get_mut("items") {
            make_strict_schema(items);
        }
    }
}

/// Widen a JSON Schema type to also allow `null`.
///
/// - `{"type": "string"}` → `{"type": ["string", "null"]}`
/// - `{"type": ["string", "integer"]}` → `{"type": ["string", "integer", "null"]}`
/// - Already includes "null" → no change
/// - No "type" field → no change
fn allow_null_in_schema(schema: &mut Value) {
    let Some(obj) = schema.as_object_mut() else {
        return;
    };
    match obj.get("type") {
        Some(Value::String(t)) => {
            if t != "null" {
                let original = t.clone();
                obj.insert(
                    "type".to_string(),
                    Value::Array(vec![
                        Value::String(original),
                        Value::String("null".to_string()),
                    ]),
                );
            }
        }
        Some(Value::Array(types)) => {
            let already_nullable = types.iter().any(|t| t.as_str() == Some("null"));
            if !already_nullable {
                let mut new_types = types.clone();
                new_types.push(Value::String("null".to_string()));
                obj.insert("type".to_string(), Value::Array(new_types));
            }
        }
        _ => {}
    }
}

/// The Responses API uses a flat tool schema:
/// `{ "type": "function", "name": "...", "description": "...", "parameters": {...} }`
/// instead of the Chat Completions nested `{ "type": "function", "function": {...} }` format.
pub fn tools_to_responses_api_format(tools: &[Tool]) -> Vec<Value> {
    tools
        .iter()
        .map(|tool| {
            // The Responses API with strict:true requires (recursively):
            // 1. additionalProperties: false on every object schema
            // 2. ALL properties listed in "required" array
            let mut params = tool.parameters.clone();
            make_strict_schema(&mut params);
            json!({
                "type": "function",
                "name": tool.name,
                "description": tool.description,
                "parameters": params,
                "strict": true,
            })
        })
        .collect()
}

/// Parse a Responses API SSE streaming event into a `StreamChunk`.
///
/// The Responses API uses event types like `response.output_text.delta`,
/// `response.function_call_arguments.delta`, `response.completed`, etc.
/// instead of the Chat Completions `choices[].delta` format.
///
/// ## Reasoning handling
///
/// Reasoning content flows through three channels depending on the model and
/// organization verification status:
///
/// 1. **Summary deltas** (`response.reasoning_summary_text.delta`) — streamed
///    reasoning summaries, available when `reasoning.summary` is set and the
///    organization is verified.
/// 2. **Raw reasoning deltas** (`response.reasoning_text.delta`) — full reasoning
///    content, only available with reasoning content access.
/// 3. **Reasoning output item** (`response.output_item.added`/`.done` with
///    `type: "reasoning"`) — emitted for all reasoning models. When no summary
///    deltas arrive, we emit a placeholder so the UI shows a thinking bubble.
///
/// This approach is modelled after OpenAI's Codex CLI reference implementation.
pub fn parse_responses_api_stream_chunk(payload: &Value) -> Option<StreamChunk> {
    let event_type = payload.get("type").and_then(Value::as_str)?;

    match event_type {
        // ── Text content deltas ──
        "response.text.delta" | "response.output_text.delta" => {
            let delta = payload.get("delta").and_then(Value::as_str)?;
            if delta.is_empty() {
                return None;
            }
            Some(StreamChunk::text(delta))
        }

        // ── Reasoning/thinking deltas ──
        // Covers multiple event names used by the Responses API across
        // different model families and API versions.
        "response.reasoning.delta"
        | "response.reasoning_text.delta"
        | "response.reasoning_summary.delta"
        | "response.reasoning_summary_text.delta"
        | "response.reasoning_summary_part.delta" => {
            // The delta text may be at top-level "delta" or nested under "part.text"
            let delta = payload
                .get("delta")
                .and_then(Value::as_str)
                .or_else(|| {
                    payload
                        .get("part")
                        .and_then(|p| p.get("text"))
                        .and_then(Value::as_str)
                })
                .or_else(|| payload.get("text").and_then(Value::as_str))?;
            if delta.is_empty() {
                return None;
            }
            Some(StreamChunk {
                thinking: Some(delta.to_string()),
                ..Default::default()
            })
        }

        // ── Tool call argument deltas (streamed incrementally) ──
        "response.tool_call_arguments.delta" | "response.function_call_arguments.delta" => {
            let call_id = payload
                .get("call_id")
                .or_else(|| payload.get("tool_call_id"))
                .or_else(|| payload.get("id"))
                .and_then(Value::as_str)
                .map(String::from);
            let name = payload
                .get("name")
                .or_else(|| payload.get("function_name"))
                .and_then(Value::as_str)
                .map(String::from);
            let arguments_delta = payload
                .get("delta")
                .or_else(|| payload.get("arguments"))
                .and_then(Value::as_str)
                .map(String::from);

            // The Responses API uses `output_index` to identify which output item
            // this delta belongs to (e.g., a function call at index 1 after a
            // reasoning item at index 0). Fall back to `index` for compatibility
            // with other providers, then to 0 as a last resort.
            let index = payload
                .get("output_index")
                .or_else(|| payload.get("index"))
                .and_then(Value::as_u64)
                .unwrap_or(0) as usize;

            Some(StreamChunk {
                tool_call: Some(StreamToolCall {
                    index,
                    id: call_id,
                    name,
                    arguments_delta,
                }),
                ..Default::default()
            })
        }

        // ── Output item events ──
        //
        // Text content is already emitted via delta events so we skip it.
        //
        // For reasoning items:
        // - `output_item.added` with `type: "reasoning"` emits a thinking
        //   indicator so the UI shows a thinking bubble even when no summary
        //   deltas are streamed (e.g., unverified orgs or models without
        //   streaming summaries).
        // - `output_item.done` with `type: "reasoning"` extracts any summary
        //   text bundled in the completed item.
        "response.output_item.added" => {
            let item = payload.get("item")?;
            let item_type = item.get("type").and_then(Value::as_str)?;

            match item_type {
                // Reasoning item starting — emit a sentinel that the streaming
                // wrapper in openai.rs will translate into a timed "Thinking"
                // indicator (e.g., "Thought for 2.3s") once reasoning completes.
                "reasoning" => Some(StreamChunk {
                    thinking: Some(REASONING_START_SENTINEL.to_string()),
                    ..Default::default()
                }),
                // Function call starting — emit a StreamToolCall with the
                // correct index, name, and id. This seeds the accumulator at
                // the right index so that subsequent argument delta events
                // (which use `output_index` to identify which function call
                // they belong to) land on the correct accumulator entry.
                // Without this, reasoning models that produce a reasoning item
                // at output_index 0 and a function call at output_index 1 would
                // fail: argument deltas would default to index 0 but the
                // output_item.done would create a separate entry at index 1,
                // leaving the function call with empty arguments.
                "function_call" | "tool_call" => {
                    let call_id = item
                        .get("call_id")
                        .or_else(|| item.get("id"))
                        .and_then(Value::as_str)
                        .map(String::from);
                    let name = item.get("name").and_then(Value::as_str).map(String::from);
                    // The Responses API puts the output array position in the
                    // top-level `output_index` of the event (same as delta events),
                    // not nested inside `item`. Fall back to `item.index` for any
                    // provider that embeds it differently, then to 0.
                    let index = payload
                        .get("output_index")
                        .or_else(|| payload.get("index"))
                        .or_else(|| item.get("index"))
                        .and_then(Value::as_u64)
                        .unwrap_or(0) as usize;
                    Some(StreamChunk {
                        tool_call: Some(StreamToolCall {
                            index,
                            id: call_id,
                            name,
                            arguments_delta: None,
                        }),
                        ..Default::default()
                    })
                }
                // Skip text/message — already streamed via deltas
                "text" | "output_text" | "message" => None,
                _ => None,
            }
        }

        "response.output_item.done" => {
            let item = payload.get("item")?;
            let item_type = item.get("type").and_then(Value::as_str)?;

            match item_type {
                // Reasoning item completed — extract any summary text.
                // The summary array contains objects like {"type":"summary_text","text":"..."}.
                // Summaries require OpenAI org verification; without it the array is empty.
                "reasoning" => {
                    let mut summary_text = String::new();
                    if let Some(summary) = item.get("summary").and_then(Value::as_array) {
                        for entry in summary {
                            if let Some(text) = entry.get("text").and_then(Value::as_str) {
                                summary_text.push_str(text);
                            }
                        }
                    }
                    if summary_text.is_empty() {
                        // No summary text (unverified org) — emit end sentinel
                        // so the streaming wrapper can compute elapsed reasoning time.
                        Some(StreamChunk {
                            thinking: Some(REASONING_END_SENTINEL.to_string()),
                            ..Default::default()
                        })
                    } else {
                        Some(StreamChunk {
                            thinking: Some(summary_text),
                            ..Default::default()
                        })
                    }
                }
                // Skip text/message — already streamed via delta events
                "text" | "output_text" | "message" => None,
                // Tool calls — emit name/id only; arguments were already
                // streamed via `response.function_call_arguments.delta` events.
                // Emitting the full arguments here would duplicate them in the
                // accumulator, producing invalid JSON like `{...}{...}` which
                // falls back to `{}` and triggers "missing required parameter".
                "function_call" | "tool_call" => {
                    let call_id = item
                        .get("call_id")
                        .or_else(|| item.get("tool_call_id"))
                        .or_else(|| item.get("id"))
                        .and_then(Value::as_str)
                        .unwrap_or("")
                        .to_string();
                    let name = item
                        .get("name")
                        .or_else(|| item.get("function").and_then(|f| f.get("name")))
                        .and_then(Value::as_str)?
                        .to_string();

                    Some(StreamChunk {
                        tool_call: Some(StreamToolCall {
                            index: item.get("index").and_then(Value::as_u64).unwrap_or(0) as usize,
                            id: Some(if call_id.is_empty() {
                                Uuid::new_v4().to_string()
                            } else {
                                call_id
                            }),
                            name: Some(name),
                            arguments_delta: None,
                        }),
                        ..Default::default()
                    })
                }
                _ => None,
            }
        }

        // ── Completion / lifecycle events ──
        "response.done" | "response.completed" => {
            let usage = payload
                .get("response")
                .and_then(|r| r.get("usage"))
                .or_else(|| payload.get("usage"));
            if let Some(usage) = usage {
                let input = usage
                    .get("input_tokens")
                    .or_else(|| usage.get("prompt_tokens"))
                    .and_then(Value::as_u64)
                    .unwrap_or(0) as usize;
                let output = usage
                    .get("output_tokens")
                    .or_else(|| usage.get("completion_tokens"))
                    .and_then(Value::as_u64)
                    .unwrap_or(0) as usize;
                let cached = usage
                    .get("input_tokens_details")
                    .or_else(|| usage.get("prompt_tokens_details"))
                    .and_then(|d| d.get("cached_tokens"))
                    .and_then(Value::as_u64)
                    .unwrap_or(0) as usize;
                if input > 0 || output > 0 {
                    return Some(StreamChunk {
                        usage: Some(ava_types::TokenUsage {
                            input_tokens: input,
                            output_tokens: output,
                            cache_read_tokens: cached,
                            cache_creation_tokens: 0,
                        }),
                        done: true,
                        ..Default::default()
                    });
                }
            }
            Some(StreamChunk::finished())
        }

        // response.created — acknowledge the stream started (no content to emit)
        "response.created" => None,

        // response.failed — extract error message and surface it as text
        "response.failed" => {
            let error_msg = payload
                .get("response")
                .and_then(|r| r.get("error"))
                .and_then(|e| e.get("message"))
                .and_then(Value::as_str)
                .unwrap_or("response failed");
            Some(StreamChunk::text(format!("[Error] {error_msg}")))
        }

        // response.incomplete — the response was cut short
        "response.incomplete" => {
            let reason = payload
                .get("response")
                .and_then(|r| r.get("incomplete_details"))
                .and_then(|d| d.get("reason"))
                .and_then(Value::as_str)
                .unwrap_or("unknown");
            Some(StreamChunk::text(format!(
                "[Incomplete response: {reason}]"
            )))
        }

        // ── Content part delta ──
        // Used by some Responses API versions for streaming both text and
        // reasoning summary content parts.
        "response.content_part.delta" => {
            let part_type = payload
                .get("part")
                .and_then(|p| p.get("type"))
                .and_then(Value::as_str)
                .unwrap_or("");
            let delta = payload.get("delta").and_then(Value::as_str)?;
            if delta.is_empty() {
                return None;
            }
            if part_type == "reasoning_summary_text" || part_type == "reasoning" {
                Some(StreamChunk {
                    thinking: Some(delta.to_string()),
                    ..Default::default()
                })
            } else {
                Some(StreamChunk::text(delta))
            }
        }

        // Reasoning summary part added — may contain initial text.
        // Even without text, reasoning has started; however, we already emit
        // an indicator via output_item.added, so only emit when text is present.
        "response.reasoning_summary_part.added" => {
            let text = payload
                .get("part")
                .and_then(|p| p.get("text"))
                .and_then(Value::as_str)
                .unwrap_or("");
            if text.is_empty() {
                return None;
            }
            Some(StreamChunk {
                thinking: Some(text.to_string()),
                ..Default::default()
            })
        }

        // ── Done events for text/reasoning ──
        // Contain the full completed text. Skip to avoid duplicating
        // content already streamed via deltas.
        "response.output_text.done"
        | "response.text.done"
        | "response.reasoning_summary_text.done"
        | "response.reasoning_summary_part.done"
        | "response.reasoning_text.done"
        | "response.reasoning.done"
        | "response.content_part.done" => None,

        // Refusal
        "response.refusal.delta" => {
            let delta = payload.get("delta").and_then(Value::as_str)?;
            Some(StreamChunk::text(format!("[Refusal] {delta}")))
        }

        _ => None,
    }
}

/// Parse a non-streaming Responses API response into content + tool calls + usage + thinking.
///
/// Reasoning summaries are extracted from `"reasoning"` output items whose `summary`
/// array contains `{"type":"summary_text","text":"..."}` entries.  These summaries are
/// only populated when the OpenAI organization is verified; unverified orgs receive an
/// empty array.
pub fn parse_responses_api_payload(
    payload: &Value,
) -> (
    String,
    Vec<ToolCall>,
    Option<ava_types::TokenUsage>,
    Option<String>,
) {
    let mut content = String::new();
    let mut tool_calls = Vec::new();
    let mut thinking = String::new();

    // Output is an array of items
    if let Some(output) = payload.get("output").and_then(Value::as_array) {
        for item in output {
            let item_type = item.get("type").and_then(Value::as_str).unwrap_or("");
            match item_type {
                "message" => {
                    if let Some(msg_content) = item.get("content").and_then(Value::as_array) {
                        for block in msg_content {
                            let block_type =
                                block.get("type").and_then(Value::as_str).unwrap_or("");
                            if block_type == "text" || block_type == "output_text" {
                                if let Some(text) = block.get("text").and_then(Value::as_str) {
                                    content.push_str(text);
                                }
                            }
                        }
                    }
                }
                "text" | "output_text" => {
                    if let Some(text) = item.get("text").and_then(Value::as_str) {
                        content.push_str(text);
                    }
                }
                // Extract reasoning summary text from reasoning items.
                "reasoning" => {
                    if let Some(summary) = item.get("summary").and_then(Value::as_array) {
                        for entry in summary {
                            if let Some(text) = entry.get("text").and_then(Value::as_str) {
                                thinking.push_str(text);
                            }
                        }
                    }
                }
                "function_call" | "tool_call" => {
                    let call_id = item
                        .get("call_id")
                        .or_else(|| item.get("id"))
                        .and_then(Value::as_str)
                        .unwrap_or("")
                        .to_string();
                    let name = item
                        .get("name")
                        .or_else(|| item.get("function").and_then(|f| f.get("name")))
                        .and_then(Value::as_str)
                        .unwrap_or("")
                        .to_string();
                    let args_str = item
                        .get("arguments")
                        .or_else(|| item.get("function").and_then(|f| f.get("arguments")))
                        .and_then(Value::as_str)
                        .unwrap_or("{}");
                    let arguments = serde_json::from_str(args_str).unwrap_or(json!({}));
                    if !name.is_empty() {
                        tool_calls.push(ToolCall {
                            id: if call_id.is_empty() {
                                Uuid::new_v4().to_string()
                            } else {
                                call_id
                            },
                            name,
                            arguments,
                        });
                    }
                }
                _ => {}
            }
        }
    }

    let usage = parse_usage(payload);
    let thinking_opt = if thinking.is_empty() {
        None
    } else {
        Some(thinking)
    };

    (content, tool_calls, usage, thinking_opt)
}

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

    // ── model_pricing_usd_per_million ──

    #[test]
    fn pricing_claude_opus() {
        let (i, o) = model_pricing_usd_per_million("claude-opus-4-6");
        assert_eq!(i, 5.00);
        assert_eq!(o, 25.00);
    }

    #[test]
    fn pricing_claude_haiku() {
        let (i, o) = model_pricing_usd_per_million("anthropic/claude-haiku-4.5");
        assert_eq!(i, 0.25);
        assert_eq!(o, 1.25);
    }

    #[test]
    fn pricing_claude_sonnet_default() {
        let (i, o) = model_pricing_usd_per_million("claude-sonnet-4");
        assert_eq!(i, 3.00);
        assert_eq!(o, 15.00);
    }

    #[test]
    fn pricing_gpt4o() {
        let (i, o) = model_pricing_usd_per_million("gpt-4o");
        assert_eq!(i, 2.50);
        assert_eq!(o, 10.00);
    }

    #[test]
    fn pricing_gpt4o_mini() {
        let (i, o) = model_pricing_usd_per_million("gpt-4o-mini");
        assert_eq!(i, 0.15);
        assert_eq!(o, 0.60);
    }

    #[test]
    fn pricing_gemini_flash() {
        let (i, o) = model_pricing_usd_per_million("gemini-2.5-flash");
        assert_eq!(i, 0.15);
        assert_eq!(o, 0.60);
    }

    #[test]
    fn pricing_gemini_pro() {
        let (i, o) = model_pricing_usd_per_million("gemini-2.5-pro");
        assert_eq!(i, 1.25);
        assert_eq!(o, 10.00);
    }

    #[test]
    fn pricing_free_tier_models() {
        for model in &[
            "glm-4",
            "minimax-01",
            "k2p5-test",
            "kimi-k2-base",
            "qwen-max",
            "qvq-72b",
        ] {
            let (i, o) = model_pricing_usd_per_million(model);
            assert_eq!((i, o), (0.0, 0.0), "expected free for {model}");
        }
    }

    #[test]
    fn pricing_unknown_model_fallback() {
        let (i, o) = model_pricing_usd_per_million("totally-unknown-model");
        assert_eq!(i, 2.50);
        assert_eq!(o, 10.00);
    }

    #[test]
    fn pricing_unknown_mini_model() {
        let (i, o) = model_pricing_usd_per_million("some-mini-model");
        assert_eq!(i, 0.15);
        assert_eq!(o, 0.60);
    }

    #[test]
    fn pricing_case_insensitive() {
        let (i, o) = model_pricing_usd_per_million("CLAUDE-OPUS-4-6");
        assert_eq!(i, 5.00);
        assert_eq!(o, 25.00);
    }

    // ── parse_usage ──

    #[test]
    fn parse_usage_anthropic_style() {
        let payload = json!({"usage": {"input_tokens": 100, "output_tokens": 50}});
        let u = parse_usage(&payload).unwrap();
        assert_eq!(u.input_tokens, 100);
        assert_eq!(u.output_tokens, 50);
    }

    #[test]
    fn parse_usage_openai_style() {
        let payload = json!({"usage": {"prompt_tokens": 200, "completion_tokens": 80}});
        let u = parse_usage(&payload).unwrap();
        assert_eq!(u.input_tokens, 200);
        assert_eq!(u.output_tokens, 80);
    }

    #[test]
    fn parse_usage_missing() {
        assert!(parse_usage(&json!({})).is_none());
    }

    #[test]
    fn parse_usage_partial_fields() {
        let payload = json!({"usage": {"input_tokens": 10}});
        let u = parse_usage(&payload).unwrap();
        assert_eq!(u.input_tokens, 10);
        assert_eq!(u.output_tokens, 0);
    }

    #[test]
    fn parse_usage_wrong_type() {
        let payload = json!({"usage": {"input_tokens": "not_a_number"}});
        let u = parse_usage(&payload).unwrap();
        assert_eq!(u.input_tokens, 0);
        assert_eq!(u.output_tokens, 0);
    }

    #[test]
    fn parse_usage_null_usage() {
        // When usage is JSON null, get("usage") returns Some(Null),
        // but sub-field lookups return None, so we get zeroed usage.
        let u = parse_usage(&json!({"usage": null}));
        assert!(u.is_some());
        let u = u.unwrap();
        assert_eq!(u.input_tokens, 0);
        assert_eq!(u.output_tokens, 0);
    }

    // ── estimate_cost_usd ──

    #[test]
    fn estimate_cost_basic() {
        let cost = estimate_cost_usd(1_000_000, 1_000_000, 3.0, 15.0);
        assert!((cost - 18.0).abs() < 1e-9);
    }

    #[test]
    fn estimate_cost_zero_tokens() {
        assert_eq!(estimate_cost_usd(0, 0, 3.0, 15.0), 0.0);
    }

    #[test]
    fn estimate_cost_zero_rates() {
        assert_eq!(estimate_cost_usd(1000, 1000, 0.0, 0.0), 0.0);
    }

    // ── estimate_tokens ──

    #[test]
    fn estimate_tokens_normal() {
        // "hello world" is 2 tokens in cl100k_base BPE
        assert_eq!(estimate_tokens("hello world"), 2);
    }

    #[test]
    fn estimate_tokens_empty() {
        assert_eq!(estimate_tokens(""), 1); // max(0,1) = 1
    }

    #[test]
    fn estimate_tokens_short() {
        // "hi" is 1 token in cl100k_base
        assert_eq!(estimate_tokens("hi"), 1);
    }

    #[test]
    fn estimate_tokens_unicode() {
        // BPE encodes emojis as multiple tokens (byte-level)
        let count = estimate_tokens("🔥🔥🔥🔥");
        assert!(count >= 1, "emoji tokens should be >= 1, got {count}");
    }

    #[test]
    fn estimate_tokens_model_aware() {
        let text = "The quick brown fox jumps over the lazy dog";
        let cl100k = estimate_tokens(text);
        let o200k = estimate_tokens_for_model(text, "gpt-4o");
        // Both should be reasonable; they may differ slightly
        assert!(cl100k >= 8 && cl100k <= 12, "cl100k got {cl100k}");
        assert!(o200k >= 8 && o200k <= 12, "o200k got {o200k}");
    }

    // ── parse_sse_lines ──

    #[test]
    fn parse_sse_valid_lines() {
        let text = "data: {\"text\":\"hello\"}\ndata: {\"text\":\"world\"}\n";
        let lines = parse_sse_lines(text);
        assert_eq!(lines.len(), 2);
        assert_eq!(lines[0], "{\"text\":\"hello\"}");
    }

    #[test]
    fn parse_sse_filters_done() {
        let text = "data: {\"text\":\"hi\"}\ndata: [DONE]\n";
        let lines = parse_sse_lines(text);
        assert_eq!(lines.len(), 1);
    }

    #[test]
    fn parse_sse_empty_input() {
        assert!(parse_sse_lines("").is_empty());
    }

    #[test]
    fn parse_sse_no_data_prefix() {
        let text = "event: message\nid: 1\n";
        assert!(parse_sse_lines(text).is_empty());
    }

    #[test]
    fn parse_sse_mixed_lines() {
        let text = "event: start\ndata: payload1\n\ndata: [DONE]\ndata: payload2\n";
        let lines = parse_sse_lines(text);
        assert_eq!(lines, vec!["payload1", "payload2"]);
    }

    // ── SseParser (buffered) ──

    #[test]
    fn sse_parser_complete_events() {
        let mut parser = SseParser::new();
        let events = parser.feed("data: {\"text\":\"hello\"}\n\ndata: {\"text\":\"world\"}\n\n");
        assert_eq!(events.len(), 2);
        assert_eq!(events[0], "{\"text\":\"hello\"}");
        assert_eq!(events[1], "{\"text\":\"world\"}");
    }

    #[test]
    fn sse_parser_partial_chunk() {
        let mut parser = SseParser::new();
        // First chunk: incomplete event (no double newline)
        let events = parser.feed("data: {\"tex");
        assert!(events.is_empty());
        // Second chunk: completes the event
        let events = parser.feed("t\":\"hello\"}\n\n");
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], "{\"text\":\"hello\"}");
    }

    #[test]
    fn sse_parser_split_across_three_chunks() {
        let mut parser = SseParser::new();
        assert!(parser.feed("da").is_empty());
        assert!(parser.feed("ta: payload").is_empty());
        let events = parser.feed("1\n\n");
        assert_eq!(events, vec!["payload1"]);
    }

    #[test]
    fn sse_parser_filters_done() {
        let mut parser = SseParser::new();
        let events = parser.feed("data: {\"ok\":true}\n\ndata: [DONE]\n\n");
        assert_eq!(events.len(), 1);
        assert_eq!(events[0], "{\"ok\":true}");
    }

    #[test]
    fn sse_parser_ignores_non_data_lines() {
        let mut parser = SseParser::new();
        let events = parser.feed("event: message\ndata: payload\n\n");
        assert_eq!(events, vec!["payload"]);
    }

    #[test]
    fn sse_parser_empty_input() {
        let mut parser = SseParser::new();
        assert!(parser.feed("").is_empty());
    }

    #[test]
    fn sse_parser_buffered_across_multiple_events() {
        let mut parser = SseParser::new();
        // Feed two partial events in one chunk
        let events = parser.feed("data: first\n\ndata: sec");
        assert_eq!(events, vec!["first"]);
        // Complete the second event
        let events = parser.feed("ond\n\n");
        assert_eq!(events, vec!["second"]);
    }

    // ── parse_openai_completion_payload ──

    #[test]
    fn parse_openai_completion_valid() {
        let payload = json!({
            "choices": [{"message": {"content": "Hello!"}}]
        });
        assert_eq!(parse_openai_completion_payload(&payload).unwrap(), "Hello!");
    }

    #[test]
    fn parse_openai_completion_null_content() {
        let payload = json!({
            "choices": [{"message": {"content": null}, "finish_reason": "stop"}]
        });
        assert_eq!(parse_openai_completion_payload(&payload).unwrap(), "");
    }

    #[test]
    fn parse_openai_completion_no_choices() {
        assert!(parse_openai_completion_payload(&json!({})).is_err());
    }

    #[test]
    fn parse_openai_completion_empty_choices() {
        assert!(parse_openai_completion_payload(&json!({"choices": []})).is_err());
    }

    // ── parse_openai_delta_payload ──

    #[test]
    fn parse_openai_delta_valid() {
        let payload = json!({
            "choices": [{"delta": {"content": "tok"}}]
        });
        assert_eq!(parse_openai_delta_payload(&payload).unwrap(), "tok");
    }

    #[test]
    fn parse_openai_delta_no_content() {
        let payload = json!({"choices": [{"delta": {}}]});
        assert!(parse_openai_delta_payload(&payload).is_none());
    }

    #[test]
    fn parse_openai_delta_empty() {
        assert!(parse_openai_delta_payload(&json!({})).is_none());
    }

    // ── parse_anthropic_completion_payload ──

    #[test]
    fn parse_anthropic_completion_valid() {
        let payload = json!({
            "content": [{"type": "text", "text": "Hi there"}]
        });
        assert_eq!(
            parse_anthropic_completion_payload(&payload).unwrap(),
            "Hi there"
        );
    }

    #[test]
    fn parse_anthropic_completion_no_content() {
        assert!(parse_anthropic_completion_payload(&json!({})).is_err());
    }

    #[test]
    fn parse_anthropic_completion_empty_content() {
        assert!(parse_anthropic_completion_payload(&json!({"content": []})).is_err());
    }

    #[test]
    fn parse_anthropic_completion_tool_use_only() {
        let payload = json!({
            "content": [{"type": "tool_use", "id": "t1", "name": "read", "input": {}}]
        });
        assert!(parse_anthropic_completion_payload(&payload).is_err());
    }

    #[test]
    fn parse_anthropic_completion_thinking_before_text() {
        // Some Anthropic-compatible providers (e.g. Alibaba) prepend a thinking block
        let payload = json!({
            "content": [
                {"type": "thinking", "thinking": "Let me think...", "signature": ""},
                {"type": "text", "text": "SMOKE_OK"}
            ]
        });
        assert_eq!(
            parse_anthropic_completion_payload(&payload).unwrap(),
            "SMOKE_OK"
        );
    }

    #[test]
    fn parse_anthropic_completion_thinking_only() {
        // If only thinking blocks exist with no text block, should error
        let payload = json!({
            "content": [
                {"type": "thinking", "thinking": "hmm", "signature": ""}
            ]
        });
        assert!(parse_anthropic_completion_payload(&payload).is_err());
    }

    // ── parse_anthropic_delta_payload ──

    #[test]
    fn parse_anthropic_delta_valid() {
        let payload = json!({
            "type": "content_block_delta",
            "delta": {"text": "chunk"}
        });
        assert_eq!(parse_anthropic_delta_payload(&payload).unwrap(), "chunk");
    }

    #[test]
    fn parse_anthropic_delta_wrong_type() {
        let payload = json!({"type": "message_start", "delta": {"text": "x"}});
        assert!(parse_anthropic_delta_payload(&payload).is_none());
    }

    #[test]
    fn parse_anthropic_delta_missing_type() {
        let payload = json!({"delta": {"text": "x"}});
        assert!(parse_anthropic_delta_payload(&payload).is_none());
    }

    // ── parse_ollama_completion_payload ──

    #[test]
    fn parse_ollama_completion_valid() {
        let payload = json!({"message": {"content": "Ollama says hi"}});
        assert_eq!(
            parse_ollama_completion_payload(&payload).unwrap(),
            "Ollama says hi"
        );
    }

    #[test]
    fn parse_ollama_completion_missing() {
        assert!(parse_ollama_completion_payload(&json!({})).is_err());
    }

    #[test]
    fn parse_ollama_completion_null_content() {
        assert!(parse_ollama_completion_payload(&json!({"message": {"content": null}})).is_err());
    }

    // ── parse_ollama_usage ──

    #[test]
    fn parse_ollama_usage_valid() {
        let payload = json!({"prompt_eval_count": 120, "eval_count": 45, "done": true});
        let u = parse_ollama_usage(&payload).unwrap();
        assert_eq!(u.input_tokens, 120);
        assert_eq!(u.output_tokens, 45);
        assert_eq!(u.cache_read_tokens, 0);
        assert_eq!(u.cache_creation_tokens, 0);
    }

    #[test]
    fn parse_ollama_usage_missing() {
        assert!(parse_ollama_usage(&json!({})).is_none());
    }

    #[test]
    fn parse_ollama_usage_zeros() {
        let payload = json!({"prompt_eval_count": 0, "eval_count": 0});
        assert!(parse_ollama_usage(&payload).is_none());
    }

    #[test]
    fn parse_ollama_usage_partial() {
        let payload = json!({"prompt_eval_count": 50});
        let u = parse_ollama_usage(&payload).unwrap();
        assert_eq!(u.input_tokens, 50);
        assert_eq!(u.output_tokens, 0);
    }

    #[test]
    fn parse_ollama_usage_null() {
        assert!(parse_ollama_usage(&json!(null)).is_none());
    }

    // ── parse_gemini_completion_payload ──

    #[test]
    fn parse_gemini_completion_valid() {
        let payload = json!({
            "candidates": [{
                "content": {
                    "parts": [{"text": "Gemini response"}]
                }
            }]
        });
        assert_eq!(
            parse_gemini_completion_payload(&payload).unwrap(),
            "Gemini response"
        );
    }

    #[test]
    fn parse_gemini_completion_missing() {
        assert!(parse_gemini_completion_payload(&json!({})).is_err());
    }

    #[test]
    fn parse_gemini_completion_empty_candidates() {
        assert!(parse_gemini_completion_payload(&json!({"candidates": []})).is_err());
    }

    #[test]
    fn parse_gemini_completion_empty_parts() {
        let payload = json!({"candidates": [{"content": {"parts": []}}]});
        assert!(parse_gemini_completion_payload(&payload).is_err());
    }

    // ── tools_to_openai_format ──

    #[test]
    fn tools_to_openai_single() {
        let tools = vec![Tool {
            name: "read".to_string(),
            description: "Read a file".to_string(),
            parameters: json!({"type": "object"}),
        }];
        let formatted = tools_to_openai_format(&tools);
        assert_eq!(formatted.len(), 1);
        assert_eq!(formatted[0]["type"], "function");
        assert_eq!(formatted[0]["function"]["name"], "read");
        assert_eq!(formatted[0]["function"]["description"], "Read a file");
    }

    #[test]
    fn tools_to_openai_empty() {
        assert!(tools_to_openai_format(&[]).is_empty());
    }

    #[test]
    fn tools_to_openai_multiple() {
        let tools = vec![
            Tool {
                name: "a".into(),
                description: "d1".into(),
                parameters: json!({}),
            },
            Tool {
                name: "b".into(),
                description: "d2".into(),
                parameters: json!({}),
            },
        ];
        let formatted = tools_to_openai_format(&tools);
        assert_eq!(formatted.len(), 2);
        assert_eq!(formatted[1]["function"]["name"], "b");
    }

    // ── tools_to_anthropic_format ──

    #[test]
    fn tools_to_anthropic_single() {
        let tools = vec![Tool {
            name: "write".to_string(),
            description: "Write a file".to_string(),
            parameters: json!({"type": "object"}),
        }];
        let formatted = tools_to_anthropic_format(&tools);
        assert_eq!(formatted.len(), 1);
        assert_eq!(formatted[0]["name"], "write");
        assert_eq!(formatted[0]["input_schema"], json!({"type": "object"}));
    }

    #[test]
    fn tools_to_anthropic_empty() {
        assert!(tools_to_anthropic_format(&[]).is_empty());
    }

    // ── parse_openai_tool_calls ──

    #[test]
    fn parse_openai_tool_calls_valid() {
        let payload = json!({
            "choices": [{
                "message": {
                    "tool_calls": [{
                        "id": "call_123",
                        "type": "function",
                        "function": {
                            "name": "read_file",
                            "arguments": "{\"path\":\"/tmp/test\"}"
                        }
                    }]
                }
            }]
        });
        let calls = parse_openai_tool_calls(&payload);
        assert_eq!(calls.len(), 1);
        assert_eq!(calls[0].id, "call_123");
        assert_eq!(calls[0].name, "read_file");
        assert_eq!(calls[0].arguments, json!({"path": "/tmp/test"}));
    }

    #[test]
    fn parse_openai_tool_calls_no_choices() {
        assert!(parse_openai_tool_calls(&json!({})).is_empty());
    }

    #[test]
    fn parse_openai_tool_calls_no_tool_calls_field() {
        let payload = json!({"choices": [{"message": {"content": "hi"}}]});
        assert!(parse_openai_tool_calls(&payload).is_empty());
    }

    #[test]
    fn parse_openai_tool_calls_malformed_arguments() {
        let payload = json!({
            "choices": [{
                "message": {
                    "tool_calls": [{
                        "id": "call_1",
                        "function": {
                            "name": "test",
                            "arguments": "not-json"
                        }
                    }]
                }
            }]
        });
        let calls = parse_openai_tool_calls(&payload);
        assert_eq!(calls.len(), 1);
        assert_eq!(calls[0].arguments, json!({})); // fallback
    }

    #[test]
    fn parse_openai_tool_calls_missing_name() {
        let payload = json!({
            "choices": [{
                "message": {
                    "tool_calls": [{
                        "id": "call_1",
                        "function": {"arguments": "{}"}
                    }]
                }
            }]
        });
        // name is required via filter_map — should skip
        assert!(parse_openai_tool_calls(&payload).is_empty());
    }

    #[test]
    fn parse_openai_tool_calls_empty_id_gets_uuid() {
        let payload = json!({
            "choices": [{
                "message": {
                    "tool_calls": [{
                        "id": "",
                        "function": {"name": "test", "arguments": "{}"}
                    }]
                }
            }]
        });
        let calls = parse_openai_tool_calls(&payload);
        assert_eq!(calls.len(), 1);
        assert!(!calls[0].id.is_empty()); // should be a UUID
        assert_ne!(calls[0].id, "");
    }

    #[test]
    fn parse_openai_tool_calls_multiple() {
        let payload = json!({
            "choices": [{
                "message": {
                    "tool_calls": [
                        {"id": "c1", "function": {"name": "a", "arguments": "{}"}},
                        {"id": "c2", "function": {"name": "b", "arguments": "{}"}}
                    ]
                }
            }]
        });
        let calls = parse_openai_tool_calls(&payload);
        assert_eq!(calls.len(), 2);
        assert_eq!(calls[0].name, "a");
        assert_eq!(calls[1].name, "b");
    }

    // ── parse_anthropic_tool_calls ──

    #[test]
    fn parse_anthropic_tool_calls_valid() {
        let payload = json!({
            "content": [
                {"type": "text", "text": "I will read the file."},
                {"type": "tool_use", "id": "tu_1", "name": "read_file", "input": {"path": "/x"}}
            ]
        });
        let calls = parse_anthropic_tool_calls(&payload);
        assert_eq!(calls.len(), 1);
        assert_eq!(calls[0].id, "tu_1");
        assert_eq!(calls[0].name, "read_file");
        assert_eq!(calls[0].arguments, json!({"path": "/x"}));
    }

    #[test]
    fn parse_anthropic_tool_calls_no_content() {
        assert!(parse_anthropic_tool_calls(&json!({})).is_empty());
    }

    #[test]
    fn parse_anthropic_tool_calls_text_only() {
        let payload = json!({"content": [{"type": "text", "text": "Hello"}]});
        assert!(parse_anthropic_tool_calls(&payload).is_empty());
    }

    #[test]
    fn parse_anthropic_tool_calls_missing_name() {
        let payload = json!({
            "content": [{"type": "tool_use", "id": "tu_1", "input": {}}]
        });
        assert!(parse_anthropic_tool_calls(&payload).is_empty());
    }

    #[test]
    fn parse_anthropic_tool_calls_missing_input() {
        let payload = json!({
            "content": [{"type": "tool_use", "id": "tu_1", "name": "test"}]
        });
        let calls = parse_anthropic_tool_calls(&payload);
        assert_eq!(calls.len(), 1);
        assert_eq!(calls[0].arguments, json!({})); // fallback
    }

    #[test]
    fn parse_anthropic_tool_calls_empty_id_gets_uuid() {
        let payload = json!({
            "content": [{"type": "tool_use", "id": "", "name": "test", "input": {}}]
        });
        let calls = parse_anthropic_tool_calls(&payload);
        assert_eq!(calls.len(), 1);
        assert!(!calls[0].id.is_empty());
    }

    #[test]
    fn parse_anthropic_tool_calls_multiple() {
        let payload = json!({
            "content": [
                {"type": "tool_use", "id": "t1", "name": "read", "input": {}},
                {"type": "tool_use", "id": "t2", "name": "write", "input": {"x": 1}}
            ]
        });
        let calls = parse_anthropic_tool_calls(&payload);
        assert_eq!(calls.len(), 2);
        assert_eq!(calls[1].name, "write");
    }

    // ── Extra edge cases across parsers ──

    #[test]
    fn parse_openai_completion_extra_fields_ignored() {
        let payload = json!({
            "id": "cmpl-xxx",
            "object": "chat.completion",
            "choices": [{"message": {"content": "ok", "role": "assistant"}, "index": 0}],
            "usage": {"prompt_tokens": 10, "completion_tokens": 5}
        });
        assert_eq!(parse_openai_completion_payload(&payload).unwrap(), "ok");
    }

    #[test]
    fn parse_anthropic_completion_extra_fields_ignored() {
        let payload = json!({
            "id": "msg_xxx",
            "type": "message",
            "role": "assistant",
            "content": [{"type": "text", "text": "ok"}],
            "stop_reason": "end_turn",
            "usage": {"input_tokens": 10, "output_tokens": 5}
        });
        assert_eq!(parse_anthropic_completion_payload(&payload).unwrap(), "ok");
    }

    // ── parse_usage cache tokens ──

    #[test]
    fn parse_usage_anthropic_cache_tokens() {
        let payload = json!({
            "usage": {
                "input_tokens": 100,
                "output_tokens": 50,
                "cache_creation_input_tokens": 200,
                "cache_read_input_tokens": 150
            }
        });
        let u = parse_usage(&payload).unwrap();
        assert_eq!(u.input_tokens, 100);
        assert_eq!(u.output_tokens, 50);
        assert_eq!(u.cache_read_tokens, 150);
        assert_eq!(u.cache_creation_tokens, 200);
    }

    #[test]
    fn parse_usage_openai_cache_tokens() {
        let payload = json!({
            "usage": {
                "prompt_tokens": 200,
                "completion_tokens": 80,
                "prompt_tokens_details": {
                    "cached_tokens": 120
                }
            }
        });
        let u = parse_usage(&payload).unwrap();
        assert_eq!(u.input_tokens, 200);
        assert_eq!(u.output_tokens, 80);
        assert_eq!(u.cache_read_tokens, 120);
        assert_eq!(u.cache_creation_tokens, 0);
    }

    #[test]
    fn parse_usage_no_cache_fields_default_zero() {
        let payload = json!({"usage": {"input_tokens": 50, "output_tokens": 25}});
        let u = parse_usage(&payload).unwrap();
        assert_eq!(u.cache_read_tokens, 0);
        assert_eq!(u.cache_creation_tokens, 0);
    }

    // ── estimate_cost_with_cache_usd ──

    #[test]
    fn estimate_cost_with_cache_no_cache() {
        let usage = ava_types::TokenUsage {
            input_tokens: 1_000_000,
            output_tokens: 1_000_000,
            cache_read_tokens: 0,
            cache_creation_tokens: 0,
        };
        let cost = estimate_cost_with_cache_usd(&usage, 3.0, 15.0);
        assert!((cost - 18.0).abs() < 1e-9);
    }

    #[test]
    fn estimate_cost_with_cache_read_discount() {
        // 1M input, 500k from cache read (10% rate), 500k normal
        let usage = ava_types::TokenUsage {
            input_tokens: 1_000_000,
            output_tokens: 0,
            cache_read_tokens: 500_000,
            cache_creation_tokens: 0,
        };
        // non-cached: 500k * 3.0/1M = 1.5, cached: 500k * 3.0*0.1/1M = 0.15
        let cost = estimate_cost_with_cache_usd(&usage, 3.0, 15.0);
        assert!((cost - 1.65).abs() < 1e-9);
    }

    #[test]
    fn estimate_cost_with_cache_creation_surcharge() {
        let usage = ava_types::TokenUsage {
            input_tokens: 1_000_000,
            output_tokens: 0,
            cache_read_tokens: 0,
            cache_creation_tokens: 1_000_000,
        };
        // non-cached: 1M * 3.0/1M = 3.0, creation: 1M * 3.0*1.25/1M = 3.75
        let cost = estimate_cost_with_cache_usd(&usage, 3.0, 15.0);
        assert!((cost - 6.75).abs() < 1e-9);
    }

    #[test]
    fn all_parsers_handle_null_payload() {
        let null = json!(null);
        assert!(parse_openai_completion_payload(&null).is_err());
        assert!(parse_openai_delta_payload(&null).is_none());
        assert!(parse_anthropic_completion_payload(&null).is_err());
        assert!(parse_anthropic_delta_payload(&null).is_none());
        assert!(parse_ollama_completion_payload(&null).is_err());
        assert!(parse_gemini_completion_payload(&null).is_err());
        assert!(parse_openai_tool_calls(&null).is_empty());
        assert!(parse_anthropic_tool_calls(&null).is_empty());
        assert!(parse_usage(&null).is_none());
        assert!(parse_gemini_usage(&null).is_none());
        assert!(parse_ollama_usage(&null).is_none());
    }

    // ── parse_gemini_usage ──

    #[test]
    fn parse_gemini_usage_valid() {
        let payload = json!({
            "usageMetadata": {
                "promptTokenCount": 150,
                "candidatesTokenCount": 42
            }
        });
        let u = parse_gemini_usage(&payload).unwrap();
        assert_eq!(u.input_tokens, 150);
        assert_eq!(u.output_tokens, 42);
        assert_eq!(u.cache_read_tokens, 0);
    }

    #[test]
    fn parse_gemini_usage_with_cached() {
        let payload = json!({
            "usageMetadata": {
                "promptTokenCount": 200,
                "candidatesTokenCount": 50,
                "cachedContentTokenCount": 100
            }
        });
        let u = parse_gemini_usage(&payload).unwrap();
        assert_eq!(u.input_tokens, 200);
        assert_eq!(u.output_tokens, 50);
        assert_eq!(u.cache_read_tokens, 100);
    }

    #[test]
    fn parse_gemini_usage_missing() {
        assert!(parse_gemini_usage(&json!({})).is_none());
    }

    #[test]
    fn parse_gemini_usage_partial_fields() {
        let payload = json!({"usageMetadata": {"promptTokenCount": 10}});
        let u = parse_gemini_usage(&payload).unwrap();
        assert_eq!(u.input_tokens, 10);
        assert_eq!(u.output_tokens, 0);
    }

    // ── Responses API stream chunk: reasoning ──

    #[test]
    fn responses_reasoning_output_item_added_emits_start_sentinel() {
        let payload = json!({
            "type": "response.output_item.added",
            "item": {"type": "reasoning"}
        });
        let chunk = parse_responses_api_stream_chunk(&payload).unwrap();
        assert_eq!(chunk.thinking.as_deref(), Some(REASONING_START_SENTINEL));
        assert!(chunk.content.is_none());
    }

    #[test]
    fn responses_reasoning_output_item_done_extracts_summary() {
        let payload = json!({
            "type": "response.output_item.done",
            "item": {
                "type": "reasoning",
                "summary": [
                    {"type": "summary_text", "text": "The model considered "},
                    {"type": "summary_text", "text": "multiple approaches."}
                ]
            }
        });
        let chunk = parse_responses_api_stream_chunk(&payload).unwrap();
        assert_eq!(
            chunk.thinking.as_deref(),
            Some("The model considered multiple approaches.")
        );
    }

    #[test]
    fn responses_reasoning_output_item_done_empty_summary_emits_end_sentinel() {
        let payload = json!({
            "type": "response.output_item.done",
            "item": {
                "type": "reasoning",
                "summary": []
            }
        });
        let chunk = parse_responses_api_stream_chunk(&payload).unwrap();
        assert_eq!(chunk.thinking.as_deref(), Some(REASONING_END_SENTINEL));
    }

    #[test]
    fn responses_reasoning_output_item_done_no_summary_emits_end_sentinel() {
        let payload = json!({
            "type": "response.output_item.done",
            "item": {"type": "reasoning"}
        });
        // No summary field at all — treated same as empty summary
        let chunk = parse_responses_api_stream_chunk(&payload).unwrap();
        assert_eq!(chunk.thinking.as_deref(), Some(REASONING_END_SENTINEL));
    }

    #[test]
    fn responses_reasoning_summary_text_delta() {
        let payload = json!({
            "type": "response.reasoning_summary_text.delta",
            "delta": "thinking about it"
        });
        let chunk = parse_responses_api_stream_chunk(&payload).unwrap();
        assert_eq!(chunk.thinking.as_deref(), Some("thinking about it"));
        assert!(chunk.content.is_none());
    }

    #[test]
    fn responses_reasoning_text_delta() {
        let payload = json!({
            "type": "response.reasoning_text.delta",
            "delta": "raw reasoning content"
        });
        let chunk = parse_responses_api_stream_chunk(&payload).unwrap();
        assert_eq!(chunk.thinking.as_deref(), Some("raw reasoning content"));
    }

    #[test]
    fn responses_created_returns_none() {
        let payload = json!({"type": "response.created", "response": {}});
        assert!(parse_responses_api_stream_chunk(&payload).is_none());
    }

    #[test]
    fn responses_failed_returns_error_text() {
        let payload = json!({
            "type": "response.failed",
            "response": {
                "error": {
                    "code": "context_length_exceeded",
                    "message": "Input too long"
                }
            }
        });
        let chunk = parse_responses_api_stream_chunk(&payload).unwrap();
        assert_eq!(chunk.content.as_deref(), Some("[Error] Input too long"));
    }

    #[test]
    fn responses_incomplete_returns_reason() {
        let payload = json!({
            "type": "response.incomplete",
            "response": {
                "incomplete_details": {"reason": "max_tokens"}
            }
        });
        let chunk = parse_responses_api_stream_chunk(&payload).unwrap();
        assert_eq!(
            chunk.content.as_deref(),
            Some("[Incomplete response: max_tokens]")
        );
    }

    #[test]
    fn responses_done_events_are_skipped() {
        for event_type in &[
            "response.output_text.done",
            "response.reasoning_summary_text.done",
            "response.reasoning_text.done",
            "response.reasoning.done",
            "response.content_part.done",
        ] {
            let payload = json!({"type": event_type, "text": "full text"});
            assert!(
                parse_responses_api_stream_chunk(&payload).is_none(),
                "{event_type} should return None"
            );
        }
    }

    #[test]
    fn responses_tool_call_from_output_item_done() {
        let payload = json!({
            "type": "response.output_item.done",
            "item": {
                "type": "function_call",
                "call_id": "call_abc",
                "name": "read",
                "arguments": "{\"path\":\"/tmp\"}"
            }
        });
        let chunk = parse_responses_api_stream_chunk(&payload).unwrap();
        let tc = chunk.tool_call.unwrap();
        assert_eq!(tc.id.as_deref(), Some("call_abc"));
        assert_eq!(tc.name.as_deref(), Some("read"));
        // arguments_delta is None — arguments were already streamed via delta events.
        // Emitting them again here would duplicate them in the accumulator.
        assert_eq!(tc.arguments_delta, None);
    }

    #[test]
    fn responses_function_call_arguments_delta_uses_output_index() {
        // The Responses API uses `output_index` (not `index`) in
        // response.function_call_arguments.delta events.  When a reasoning
        // item occupies output_index 0 and the function call is at
        // output_index 1, the delta must be mapped to accumulator index 1
        // so it lands on the right entry.
        let payload = json!({
            "type": "response.function_call_arguments.delta",
            "call_id": "call_xyz",
            "output_index": 1,
            "delta": "{\"path\":\"/foo\"}"
        });
        let chunk = parse_responses_api_stream_chunk(&payload).unwrap();
        let tc = chunk.tool_call.unwrap();
        assert_eq!(
            tc.index, 1,
            "output_index should map to StreamToolCall.index"
        );
        assert_eq!(tc.id.as_deref(), Some("call_xyz"));
        assert_eq!(tc.arguments_delta.as_deref(), Some("{\"path\":\"/foo\"}"));
    }

    #[test]
    fn responses_output_item_added_function_call_seeds_accumulator() {
        // output_item.added for a function_call should emit a StreamToolCall
        // with the correct index, name, and id so the accumulator is seeded
        // at the right position before argument delta events arrive.
        let payload = json!({
            "type": "response.output_item.added",
            "item": {
                "type": "function_call",
                "call_id": "call_abc",
                "name": "glob",
                "index": 1
            }
        });
        let chunk = parse_responses_api_stream_chunk(&payload).unwrap();
        let tc = chunk.tool_call.unwrap();
        assert_eq!(tc.index, 1);
        assert_eq!(tc.id.as_deref(), Some("call_abc"));
        assert_eq!(tc.name.as_deref(), Some("glob"));
        assert_eq!(tc.arguments_delta, None);
    }

    #[test]
    fn make_strict_schema_adds_null_to_optional_fields() {
        // Optional fields (not in the original `required` array) should be
        // widened to ["T", "null"] so the model can send null to indicate
        // "not specified" rather than being forced to invent a value.
        let mut schema = serde_json::json!({
            "type": "object",
            "required": ["path"],
            "properties": {
                "path": {"type": "string"},
                "offset": {"type": "integer", "minimum": 1},
                "limit": {"type": "integer", "minimum": 1},
                "hash_lines": {"type": "boolean"}
            }
        });
        make_strict_schema(&mut schema);

        // All keys are now required
        let required = schema["required"].as_array().unwrap();
        assert!(required.contains(&serde_json::json!("path")));
        assert!(required.contains(&serde_json::json!("offset")));
        assert!(required.contains(&serde_json::json!("limit")));
        assert!(required.contains(&serde_json::json!("hash_lines")));

        // `path` was already required — should remain non-nullable
        assert_eq!(schema["properties"]["path"]["type"], "string");

        // Optional fields should now allow null
        let offset_type = &schema["properties"]["offset"]["type"];
        assert!(offset_type.is_array(), "offset should have union type");
        let offset_types = offset_type.as_array().unwrap();
        assert!(offset_types.contains(&serde_json::json!("integer")));
        assert!(offset_types.contains(&serde_json::json!("null")));

        let hash_type = &schema["properties"]["hash_lines"]["type"];
        assert!(hash_type.is_array());
        let hash_types = hash_type.as_array().unwrap();
        assert!(hash_types.contains(&serde_json::json!("boolean")));
        assert!(hash_types.contains(&serde_json::json!("null")));
    }

    // ── Responses API non-streaming: reasoning ──

    #[test]
    fn responses_payload_extracts_reasoning_summary() {
        let payload = json!({
            "output": [
                {
                    "type": "reasoning",
                    "summary": [
                        {"type": "summary_text", "text": "I considered the options."}
                    ]
                },
                {
                    "type": "message",
                    "role": "assistant",
                    "content": [{"type": "output_text", "text": "Hello"}]
                }
            ]
        });
        let (content, tool_calls, _usage, thinking) = parse_responses_api_payload(&payload);
        assert_eq!(content, "Hello");
        assert!(tool_calls.is_empty());
        assert_eq!(thinking.as_deref(), Some("I considered the options."));
    }

    #[test]
    fn responses_payload_empty_reasoning_summary() {
        let payload = json!({
            "output": [
                {"type": "reasoning", "summary": []},
                {"type": "message", "role": "assistant", "content": [{"type": "output_text", "text": "ok"}]}
            ]
        });
        let (_content, _, _, thinking) = parse_responses_api_payload(&payload);
        assert!(thinking.is_none());
    }

    // ── transform_schema_for_provider / Gemini schema transforms ──────

    #[test]
    fn gemini_transform_integer_enum_to_string() {
        let schema = json!({
            "type": "integer",
            "enum": [0, 1, 2],
            "description": "A level"
        });
        let result = transform_schema_for_provider("gemini", schema);
        assert_eq!(result["type"], "string");
        let enums = result["enum"].as_array().unwrap();
        assert_eq!(enums[0], json!("0"));
        assert_eq!(enums[1], json!("1"));
        assert_eq!(enums[2], json!("2"));
    }

    #[test]
    fn gemini_transform_array_without_items_gets_items() {
        let schema = json!({
            "type": "object",
            "properties": {
                "paths": {"type": "array"}
            },
            "required": ["paths"]
        });
        let result = transform_schema_for_provider("gemini", schema);
        let items = &result["properties"]["paths"]["items"];
        assert!(
            !items.is_null(),
            "array without items should get items field"
        );
        assert_eq!(items["type"], "string");
    }

    #[test]
    fn gemini_transform_array_with_items_unchanged() {
        let schema = json!({
            "type": "array",
            "items": {"type": "integer"}
        });
        let result = transform_schema_for_provider("gemini", schema);
        // Already has items, should not be replaced
        assert_eq!(result["items"]["type"], "integer");
    }

    #[test]
    fn gemini_transform_non_enum_integer_unchanged() {
        // Integer without enum should remain integer
        let schema = json!({
            "type": "integer",
            "minimum": 0
        });
        let result = transform_schema_for_provider("gemini", schema);
        assert_eq!(result["type"], "integer");
    }

    #[test]
    fn non_gemini_provider_schema_unchanged() {
        let schema = json!({
            "type": "integer",
            "enum": [1, 2, 3]
        });
        let result = transform_schema_for_provider("anthropic", schema.clone());
        assert_eq!(result, schema);

        let result = transform_schema_for_provider("openai", schema.clone());
        assert_eq!(result, schema);
    }

    #[test]
    fn tools_to_gemini_format_applies_schema_transform() {
        let tools = vec![Tool {
            name: "set_level".to_string(),
            description: "Set the level".to_string(),
            parameters: json!({
                "type": "object",
                "properties": {
                    "level": {
                        "type": "integer",
                        "enum": [0, 1, 2],
                        "description": "The verbosity level"
                    },
                    "tags": {
                        "type": "array",
                        "description": "Tags to apply"
                    }
                },
                "required": ["level"]
            }),
        }];
        let result = tools_to_gemini_format(&tools);
        let decls = result[0]["functionDeclarations"].as_array().unwrap();
        let params = &decls[0]["parameters"];

        // Integer enum should be converted to string enum
        assert_eq!(params["properties"]["level"]["type"], "string");
        // Array should have items
        assert!(!params["properties"]["tags"]["items"].is_null());
    }
}
