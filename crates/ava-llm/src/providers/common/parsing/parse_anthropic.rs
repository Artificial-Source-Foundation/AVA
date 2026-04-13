use ava_types::{Result, StreamChunk, StreamToolCall, Tool, ToolCall};
use serde_json::{json, Value};
use uuid::Uuid;

use super::AvaError;

pub(crate) const MISSING_ANTHROPIC_COMPLETION_CONTENT: &str =
    "missing Anthropic completion content";

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
            AvaError::SerializationError(MISSING_ANTHROPIC_COMPLETION_CONTENT.to_string())
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

#[cfg(test)]
mod tests {
    use super::*;

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

    // ── parse_anthropic_stream_chunk ──

    #[test]
    fn parse_anthropic_stream_chunk_text_delta() {
        let payload = json!({
            "type": "content_block_delta",
            "delta": {"type": "text_delta", "text": "Hello world"}
        });
        let chunk = parse_anthropic_stream_chunk(&payload).unwrap();
        assert_eq!(chunk.text_content(), Some("Hello world"));
        assert!(chunk.thinking.is_none());
        assert!(chunk.tool_call.is_none());
    }

    #[test]
    fn parse_anthropic_stream_chunk_thinking_delta() {
        let payload = json!({
            "type": "content_block_delta",
            "delta": {"type": "thinking_delta", "thinking": "Let me reason..."}
        });
        let chunk = parse_anthropic_stream_chunk(&payload).unwrap();
        assert_eq!(chunk.thinking.as_deref(), Some("Let me reason..."));
        assert!(chunk.content.is_none());
    }

    #[test]
    fn parse_anthropic_stream_chunk_input_json_delta() {
        let payload = json!({
            "type": "content_block_delta",
            "index": 1,
            "delta": {"type": "input_json_delta", "partial_json": "{\"path\":"}
        });
        let chunk = parse_anthropic_stream_chunk(&payload).unwrap();
        let tc = chunk.tool_call.unwrap();
        assert_eq!(tc.index, 1);
        assert_eq!(tc.arguments_delta.as_deref(), Some("{\"path\":"));
        assert!(tc.id.is_none());
        assert!(tc.name.is_none());
    }

    #[test]
    fn parse_anthropic_stream_chunk_content_block_start_tool_use() {
        let payload = json!({
            "type": "content_block_start",
            "index": 2,
            "content_block": {"type": "tool_use", "id": "toolu_abc", "name": "read"}
        });
        let chunk = parse_anthropic_stream_chunk(&payload).unwrap();
        let tc = chunk.tool_call.unwrap();
        assert_eq!(tc.index, 2);
        assert_eq!(tc.id.as_deref(), Some("toolu_abc"));
        assert_eq!(tc.name.as_deref(), Some("read"));
        assert!(tc.arguments_delta.is_none());
    }

    #[test]
    fn parse_anthropic_stream_chunk_content_block_start_text_returns_none() {
        let payload = json!({
            "type": "content_block_start",
            "index": 0,
            "content_block": {"type": "text", "text": ""}
        });
        assert!(parse_anthropic_stream_chunk(&payload).is_none());
    }

    #[test]
    fn parse_anthropic_stream_chunk_message_delta_with_usage() {
        let payload = json!({
            "type": "message_delta",
            "usage": {"output_tokens": 42}
        });
        let chunk = parse_anthropic_stream_chunk(&payload).unwrap();
        assert!(chunk.done);
        let usage = chunk.usage.unwrap();
        assert_eq!(usage.output_tokens, 42);
        assert_eq!(usage.input_tokens, 0);
    }

    #[test]
    fn parse_anthropic_stream_chunk_message_delta_without_usage_returns_none() {
        let payload = json!({
            "type": "message_delta",
            "delta": {"stop_reason": "end_turn"}
        });
        assert!(parse_anthropic_stream_chunk(&payload).is_none());
    }

    #[test]
    fn parse_anthropic_stream_chunk_message_start_with_cache_usage() {
        let payload = json!({
            "type": "message_start",
            "message": {
                "usage": {
                    "input_tokens": 100,
                    "cache_read_input_tokens": 50,
                    "cache_creation_input_tokens": 25
                }
            }
        });
        let chunk = parse_anthropic_stream_chunk(&payload).unwrap();
        let usage = chunk.usage.unwrap();
        assert_eq!(usage.input_tokens, 100);
        assert_eq!(usage.cache_read_tokens, 50);
        assert_eq!(usage.cache_creation_tokens, 25);
        assert_eq!(usage.output_tokens, 0);
        assert!(!chunk.done);
    }

    #[test]
    fn parse_anthropic_stream_chunk_unknown_event_type_returns_none() {
        let payload = json!({"type": "ping"});
        assert!(parse_anthropic_stream_chunk(&payload).is_none());
    }

    #[test]
    fn parse_anthropic_stream_chunk_unknown_delta_type_returns_none() {
        let payload = json!({
            "type": "content_block_delta",
            "delta": {"type": "citations_delta", "citation": {"cited_text": "foo"}}
        });
        assert!(parse_anthropic_stream_chunk(&payload).is_none());
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

    #[test]
    fn tools_to_anthropic_cached_marks_only_last_tool() {
        let tools = vec![
            Tool {
                name: "read".to_string(),
                description: "Read a file".to_string(),
                parameters: json!({"type": "object"}),
            },
            Tool {
                name: "write".to_string(),
                description: "Write a file".to_string(),
                parameters: json!({"type": "object"}),
            },
        ];

        let formatted = tools_to_anthropic_format_cached(&tools, true);
        assert!(formatted[0].get("cache_control").is_none());
        assert_eq!(formatted[1]["cache_control"]["type"], "ephemeral");
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
}
