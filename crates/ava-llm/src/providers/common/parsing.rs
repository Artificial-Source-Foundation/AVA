use ava_types::{AvaError, Result, StreamChunk, StreamToolCall, Tool, ToolCall};
use serde_json::{json, Value};
use uuid::Uuid;

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

pub fn estimate_tokens(input: &str) -> usize {
    (input.chars().count() / 4).max(1)
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

    // Reasoning/thinking content
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
        .ok_or_else(|| {
            AvaError::SerializationError("missing Gemini completion content".to_string())
        })
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
/// The Responses API uses a flat tool schema:
/// `{ "type": "function", "name": "...", "description": "...", "parameters": {...} }`
/// instead of the Chat Completions nested `{ "type": "function", "function": {...} }` format.
pub fn tools_to_responses_api_format(tools: &[Tool]) -> Vec<Value> {
    tools
        .iter()
        .map(|tool| {
            json!({
                "type": "function",
                "name": tool.name,
                "description": tool.description,
                "parameters": tool.parameters,
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
pub fn parse_responses_api_stream_chunk(payload: &Value) -> Option<StreamChunk> {
    let event_type = payload.get("type").and_then(Value::as_str)?;

    match event_type {
        // Text content deltas
        "response.text.delta" | "response.output_text.delta" => {
            let delta = payload.get("delta").and_then(Value::as_str)?;
            if delta.is_empty() {
                return None;
            }
            Some(StreamChunk::text(delta))
        }

        // Reasoning/thinking deltas
        "response.reasoning.delta"
        | "response.reasoning_text.delta"
        | "response.reasoning_summary.delta"
        | "response.reasoning_summary_text.delta" => {
            let delta = payload.get("delta").and_then(Value::as_str)?;
            if delta.is_empty() {
                return None;
            }
            Some(StreamChunk {
                thinking: Some(delta.to_string()),
                ..Default::default()
            })
        }

        // Tool call argument deltas (streamed incrementally)
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

            Some(StreamChunk {
                tool_call: Some(StreamToolCall {
                    index: payload.get("index").and_then(Value::as_u64).unwrap_or(0) as usize,
                    id: call_id,
                    name,
                    arguments_delta,
                }),
                ..Default::default()
            })
        }

        // Output item events — capture tool call identity and completed calls
        "response.output_item.added" | "response.output_item.done" => {
            let item = payload.get("item")?;
            let item_type = item.get("type").and_then(Value::as_str)?;

            match item_type {
                "text" | "output_text" => {
                    let text = item.get("text").and_then(Value::as_str)?;
                    if text.is_empty() {
                        return None;
                    }
                    Some(StreamChunk::text(text))
                }
                "reasoning" => {
                    let text = item.get("text").and_then(Value::as_str)?;
                    if text.is_empty() {
                        return None;
                    }
                    Some(StreamChunk {
                        thinking: Some(text.to_string()),
                        ..Default::default()
                    })
                }
                "function_call" | "tool_call" if event_type == "response.output_item.done" => {
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
                    let arguments = item
                        .get("arguments")
                        .or_else(|| item.get("function").and_then(|f| f.get("arguments")))
                        .and_then(Value::as_str)
                        .unwrap_or("{}");

                    Some(StreamChunk {
                        tool_call: Some(StreamToolCall {
                            index: item.get("index").and_then(Value::as_u64).unwrap_or(0) as usize,
                            id: Some(if call_id.is_empty() {
                                Uuid::new_v4().to_string()
                            } else {
                                call_id
                            }),
                            name: Some(name),
                            arguments_delta: Some(arguments.to_string()),
                        }),
                        ..Default::default()
                    })
                }
                "message" => {
                    // Extract text from message content blocks
                    let content = item.get("content").and_then(Value::as_array)?;
                    let text: String = content
                        .iter()
                        .filter(|c| {
                            let t = c.get("type").and_then(Value::as_str).unwrap_or("");
                            t == "text" || t == "output_text"
                        })
                        .filter_map(|c| c.get("text").and_then(Value::as_str))
                        .collect::<Vec<_>>()
                        .join("");
                    if text.is_empty() {
                        return None;
                    }
                    Some(StreamChunk::text(&text))
                }
                _ => None,
            }
        }

        // Completion events — extract usage
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

        // Refusal
        "response.refusal.delta" => {
            let delta = payload.get("delta").and_then(Value::as_str)?;
            Some(StreamChunk::text(format!("[Refusal] {delta}")))
        }

        _ => None,
    }
}

/// Parse a non-streaming Responses API response into content + tool calls + usage.
pub fn parse_responses_api_payload(
    payload: &Value,
) -> (String, Vec<ToolCall>, Option<ava_types::TokenUsage>) {
    let mut content = String::new();
    let mut tool_calls = Vec::new();

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

    (content, tool_calls, usage)
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
        assert_eq!(estimate_tokens("hello world"), 2); // 11 chars / 4 = 2
    }

    #[test]
    fn estimate_tokens_empty() {
        assert_eq!(estimate_tokens(""), 1); // max(0,1) = 1
    }

    #[test]
    fn estimate_tokens_short() {
        assert_eq!(estimate_tokens("hi"), 1); // max(0,1) = 1
    }

    #[test]
    fn estimate_tokens_unicode() {
        // Each emoji is 1 char; 4 emojis / 4 = 1
        assert_eq!(estimate_tokens("🔥🔥🔥🔥"), 1);
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
}
