use ava_types::{StreamChunk, StreamToolCall, ToolCall};
use serde_json::{json, Value};
use uuid::Uuid;

use super::parse_usage;

/// Sentinel emitted when a Responses API reasoning item starts.
/// The streaming wrapper in `openai.rs` intercepts this to track timing.
pub const REASONING_START_SENTINEL: &str = "\x00REASONING_START\x00";

/// Sentinel emitted when a Responses API reasoning item completes without
/// summary text (unverified org). The streaming wrapper converts this
/// into a "Thought for X.Xs" message using elapsed time.
pub const REASONING_END_SENTINEL: &str = "\x00REASONING_END\x00";

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
/// 1. **Summary deltas** (`response.reasoning_summary_text.delta`) -- streamed
///    reasoning summaries, available when `reasoning.summary` is set and the
///    organization is verified.
/// 2. **Raw reasoning deltas** (`response.reasoning_text.delta`) -- full reasoning
///    content, only available with reasoning content access.
/// 3. **Reasoning output item** (`response.output_item.added`/`.done` with
///    `type: "reasoning"`) -- emitted for all reasoning models. When no summary
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
                // Reasoning item starting -- emit a sentinel that the streaming
                // wrapper in openai.rs will translate into a timed "Thinking"
                // indicator (e.g., "Thought for 2.3s") once reasoning completes.
                "reasoning" => Some(StreamChunk {
                    thinking: Some(REASONING_START_SENTINEL.to_string()),
                    ..Default::default()
                }),
                // Function call starting -- emit a StreamToolCall with the
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
                // Skip text/message -- already streamed via deltas
                "text" | "output_text" | "message" => None,
                _ => None,
            }
        }

        "response.output_item.done" => {
            let item = payload.get("item")?;
            let item_type = item.get("type").and_then(Value::as_str)?;

            match item_type {
                // Reasoning item completed -- extract any summary text.
                // The summary array contains objects like {"type":"summary_text","text":"..."}.
                // Summaries require OpenAI org verification; without it the array is empty.
                "reasoning" => {
                    // Always emit just the end sentinel. The summary text was
                    // already streamed via `response.reasoning_summary_text.delta`
                    // events -- emitting the full summary here would duplicate it
                    // because the frontend accumulates deltas with `prev + content`.
                    Some(StreamChunk {
                        thinking: Some(REASONING_END_SENTINEL.to_string()),
                        ..Default::default()
                    })
                }
                // Skip text/message -- already streamed via delta events
                "text" | "output_text" | "message" => None,
                // Tool calls -- emit name/id only; arguments were already
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

                    // The Responses API puts the output array position in the top-level
                    // `output_index` of the event (same as delta events and output_item.added),
                    // NOT inside the `item` object. Using `item.index` here would default to 0
                    // for reasoning models where the function_call is at output_index 1 (after
                    // the reasoning item at index 0), causing the id/name to be accumulated into
                    // the wrong slot and producing empty arguments ("missing required parameter").
                    let index = payload
                        .get("output_index")
                        .or_else(|| payload.get("index"))
                        .or_else(|| item.get("index"))
                        .and_then(Value::as_u64)
                        .unwrap_or(0) as usize;

                    Some(StreamChunk {
                        tool_call: Some(StreamToolCall {
                            index,
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

        // response.created -- acknowledge the stream started (no content to emit)
        "response.created" => None,

        // response.failed -- extract error message and surface it as text
        "response.failed" => {
            let error_msg = payload
                .get("response")
                .and_then(|r| r.get("error"))
                .and_then(|e| e.get("message"))
                .and_then(Value::as_str)
                .unwrap_or("response failed");
            Some(StreamChunk::text(format!("[Error] {error_msg}")))
        }

        // response.incomplete -- the response was cut short
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

        // Reasoning summary part added -- may contain initial text.
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

#[cfg(test)]
mod tests {
    use super::*;

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
    fn responses_reasoning_output_item_done_always_emits_end_sentinel() {
        // Even when summary text is present, we emit the end sentinel because
        // the summary was already streamed via delta events. Emitting the full
        // text here would duplicate it in the frontend's accumulator.
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
        assert_eq!(chunk.thinking.as_deref(), Some(REASONING_END_SENTINEL));
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
        // No summary field at all -- treated same as empty summary
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
        // arguments_delta is None -- arguments were already streamed via delta events.
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
}
