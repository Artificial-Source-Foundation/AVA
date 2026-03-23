use ava_types::{Result, StreamChunk, StreamToolCall, Tool, ToolCall};
use serde_json::{json, Value};
use uuid::Uuid;

use super::AvaError;

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

/// Convert AVA tool definitions to the OpenAI Responses API format.
///
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
/// in the parent object -- used when recursing into property schemas to decide
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
/// - `{"type": "string"}` -> `{"type": ["string", "null"]}`
/// - `{"type": ["string", "integer"]}` -> `{"type": ["string", "integer", "null"]}`
/// - Already includes "null" -> no change
/// - No "type" field -> no change
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

#[cfg(test)]
mod tests {
    use super::*;

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
        // name is required via filter_map -- should skip
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

        // `path` was already required -- should remain non-nullable
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
}
