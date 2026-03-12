# Sprint 60-01: Streaming Tool Calls — End-to-End Token-by-Token Rendering

## Context

You are working on **AVA**, a Rust-first AI coding agent. Read `CLAUDE.md` and `AGENTS.md` first.

### The Problem

The TUI **freezes** while waiting for LLM responses, then dumps the entire response at once. This happens because the agent loop uses the **non-streaming** `generate_with_tools()` for any provider with `supports_tools() = true` (all real providers). The streaming path (`generate_stream()`) only runs for providers without tool support.

The architecture is already wired for streaming:
- TUI event loop uses `tokio::select!` with 60fps tick — **works**
- Agent runs in a separate `tokio::spawn` task — **works**
- Events flow via `mpsc::UnboundedSender<AgentEvent>` — **works**
- `StreamChunk` type already has `tool_call: Option<StreamToolCall>` fields — **ready**
- Anthropic & OpenAI APIs both support `stream: true` with tools — **ready**

The **only** missing piece: no `generate_stream_with_tools()` method exists, so the agent loop falls back to blocking calls.

### Key Files — Read ALL before starting

**Trait & types:**
- `crates/ava-llm/src/provider.rs` — `LLMProvider` trait (needs new method)
- `crates/ava-types/src/lib.rs` — `StreamChunk`, `StreamToolCall` (already defined)

**Providers (all need streaming+tools):**
- `crates/ava-llm/src/providers/anthropic.rs` — has `generate_stream()` and `generate_with_tools()` separately
- `crates/ava-llm/src/providers/openai.rs` — same pattern
- `crates/ava-llm/src/providers/gemini.rs`
- `crates/ava-llm/src/providers/openrouter.rs` — delegates to inner OpenAI
- `crates/ava-llm/src/providers/copilot.rs` — delegates to inner provider
- `crates/ava-llm/src/providers/ollama.rs`
- `crates/ava-llm/src/providers/mock.rs`

**Agent loop (consumer):**
- `crates/ava-agent/src/agent_loop/mod.rs` — `run_streaming()` lines 231-298 (the blocking path)

**Parsers:**
- `crates/ava-llm/src/providers/common/parsing.rs` — `parse_anthropic_stream_chunk()`, `parse_openai_stream_chunk()` — already parse tool call deltas in SSE

**TUI (already works, no changes needed):**
- `crates/ava-tui/src/app/mod.rs` — event loop with `select!`
- `crates/ava-tui/src/state/agent.rs` — handles `AgentEvent::Token`

---

## Phase 1: Add `generate_stream_with_tools()` to the Trait

**File**: `crates/ava-llm/src/provider.rs`

Add a new method to `LLMProvider`:

```rust
/// Streaming generation with tool definitions.
/// Returns a stream of `StreamChunk`s carrying text deltas, tool call fragments, usage, and thinking.
/// Default implementation falls back to non-streaming `generate_with_tools()` and emits the result as a single chunk.
async fn generate_stream_with_tools(
    &self,
    messages: &[Message],
    tools: &[Tool],
) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
    // Default: non-streaming fallback
    let response = self.generate_with_tools(messages, tools).await?;
    let mut chunks = Vec::new();
    if !response.content.is_empty() {
        chunks.push(StreamChunk::text(response.content));
    }
    for (i, tc) in response.tool_calls.into_iter().enumerate() {
        chunks.push(StreamChunk {
            tool_call: Some(StreamToolCall {
                index: i,
                id: Some(tc.id),
                name: Some(tc.name),
                arguments_delta: Some(tc.arguments),
            }),
            ..Default::default()
        });
    }
    if let Some(usage) = response.usage {
        chunks.push(StreamChunk::with_usage(usage));
    } else {
        chunks.push(StreamChunk::finished());
    }
    Ok(Box::pin(futures::stream::iter(chunks)))
}

/// Streaming generation with tools AND thinking level.
/// Default falls back to `generate_stream_with_tools()` ignoring thinking.
async fn generate_stream_with_thinking(
    &self,
    messages: &[Message],
    tools: &[Tool],
    _thinking: ThinkingLevel,
) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
    self.generate_stream_with_tools(messages, tools).await
}
```

Also add these to `SharedProvider`'s `impl LLMProvider` to delegate to inner.

*Before proceeding to Phase 2, invoke the Code Reviewer sub-agent to verify the trait is correct, default implementations compile, and SharedProvider delegates properly.*

---

## Phase 2: Implement Streaming+Tools in Anthropic Provider

**File**: `crates/ava-llm/src/providers/anthropic.rs`

The Anthropic provider already has:
- `build_request_body_with_tools(messages, tools, stream: bool)` — the `stream` param is already there
- `generate_stream()` — SSE parsing that yields `StreamChunk`
- `parse_anthropic_stream_chunk()` — already handles `content_block_start` for tool_use, `input_json_delta`, etc.

Implement `generate_stream_with_tools()`:

```rust
async fn generate_stream_with_tools(
    &self,
    messages: &[Message],
    tools: &[Tool],
) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
    let body = self.build_request_body_with_tools(messages, tools, true); // stream: true
    let client = self.client().await?;
    let request = self.build_request(&client).json(&body);

    let response = self.send_request(request).await?;
    let response = common::validate_status(response, "Anthropic").await?;

    // Same SSE parsing as generate_stream() — parse_anthropic_stream_chunk already handles tool calls
    let stream = response.bytes_stream().flat_map(|chunk| {
        let chunks = chunk
            .ok()
            .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
            .map(|text| {
                common::parse_sse_lines(&text)
                    .into_iter()
                    .filter_map(|line| serde_json::from_str::<Value>(&line).ok())
                    .filter_map(|payload| common::parse_anthropic_stream_chunk(&payload))
                    .collect::<Vec<_>>()
            })
            .unwrap_or_default();
        futures::stream::iter(chunks)
    });

    Ok(Box::pin(stream))
}
```

Also implement `generate_stream_with_thinking()` — same as above but use `build_request_body_with_thinking()` with `stream: true`, and add the `anthropic-beta` header for native Anthropic.

**Verify** that `parse_anthropic_stream_chunk` in `parsing.rs` correctly handles these SSE events for tool calls:
- `content_block_start` with `"type": "tool_use"` → `StreamChunk { tool_call: Some(StreamToolCall { id, name, index }) }`
- `content_block_delta` with `"type": "input_json_delta"` → `StreamChunk { tool_call: Some(StreamToolCall { arguments_delta }) }`
- `content_block_stop` → no special handling needed
- `message_delta` with `usage` → `StreamChunk::with_usage(usage)`

If any of these aren't handled, add them. Check the existing code carefully.

*Before proceeding to Phase 3, invoke the Code Reviewer sub-agent to verify the Anthropic streaming+tools implementation is correct.*

---

## Phase 3: Implement Streaming+Tools in OpenAI Provider

**File**: `crates/ava-llm/src/providers/openai.rs`

Same pattern. The OpenAI provider already has `build_request_body_with_tools()`. Implement `generate_stream_with_tools()` using `stream: true` and the existing `parse_openai_stream_chunk()`.

**Verify** that `parse_openai_stream_chunk` handles:
- `choices[0].delta.tool_calls[i].id` → `StreamToolCall { id }`
- `choices[0].delta.tool_calls[i].function.name` → `StreamToolCall { name }`
- `choices[0].delta.tool_calls[i].function.arguments` → `StreamToolCall { arguments_delta }`
- `usage` in final chunk → `StreamChunk::with_usage(usage)`

If not handled, add parsing.

*Before proceeding to Phase 4, invoke the Code Reviewer sub-agent.*

---

## Phase 4: Wire Remaining Providers

**OpenRouter** (`openrouter.rs`) — delegates to inner OpenAI provider, so delegate `generate_stream_with_tools()` to `self.inner.generate_stream_with_tools()`.

**Copilot** (`copilot.rs`) — same delegation pattern.

**Gemini** (`gemini.rs`) — implement if Gemini streaming supports tool calls. If not, rely on the default fallback (non-streaming single-chunk).

**Ollama** (`ollama.rs`) — Ollama's streaming API may not support tool calls in stream mode. Use the default fallback.

**Mock** (`mock.rs`) — use the default fallback or emit chunks for testing.

*Before proceeding to Phase 5, invoke the Code Reviewer sub-agent to verify all providers compile and delegate correctly.*

---

## Phase 5: Update Agent Loop to Use Streaming+Tools

**File**: `crates/ava-agent/src/agent_loop/mod.rs`

This is the critical change. Replace lines 231-298 in `run_streaming()`:

### Current (blocking):
```rust
let (response_text, tool_calls) = if native_tools {
    // BLOCKING — waits for full response, then emits as one big Token
    let result = self.llm.generate_with_tools(...).await;
    match result {
        Ok(response) => {
            yield AgentEvent::Token(response.content.clone()); // all at once
            (response.content, response.tool_calls)
        }
        ...
    }
} else {
    // STREAMING — token by token (but no tools)
    ...
};
```

### New (always streaming):
```rust
let (response_text, tool_calls) = {
    let stream_result = if native_tools {
        let tool_defs = self.tools.list_tools();
        if self.config.thinking_level != ThinkingLevel::Off {
            self.llm.generate_stream_with_thinking(
                self.context.get_messages(), &tool_defs, self.config.thinking_level,
            ).await
        } else {
            self.llm.generate_stream_with_tools(
                self.context.get_messages(), &tool_defs,
            ).await
        }
    } else {
        self.llm.generate_stream(self.context.get_messages()).await
    };

    match stream_result {
        Ok(mut stream) => {
            let mut full_text = String::new();
            let mut accumulated_tool_calls: Vec<ToolCallAccumulator> = Vec::new();
            let mut last_usage = None;

            while let Some(chunk) = stream.next().await {
                // Emit text tokens as they arrive
                if let Some(text) = chunk.text_content() {
                    full_text.push_str(text);
                    yield AgentEvent::Token(text.to_string());
                }
                // Emit thinking
                if let Some(ref thinking) = chunk.thinking {
                    yield AgentEvent::Thinking(thinking.clone());
                }
                // Accumulate tool call fragments
                if let Some(ref tc) = chunk.tool_call {
                    accumulate_tool_call(&mut accumulated_tool_calls, tc);
                }
                // Capture usage
                if let Some(ref usage) = chunk.usage {
                    last_usage = Some(usage.clone());
                }
            }

            // Emit token usage
            if let Some(usage) = last_usage {
                let (in_rate, out_rate) = common::model_pricing_usd_per_million(&self.config.model);
                let cost = common::estimate_cost_usd(usage.input_tokens, usage.output_tokens, in_rate, out_rate);
                yield AgentEvent::TokenUsage {
                    input_tokens: usage.input_tokens,
                    output_tokens: usage.output_tokens,
                    cost_usd: cost,
                };
            }

            // Convert accumulated tool calls to ToolCall structs
            let tool_calls = if native_tools {
                finalize_tool_calls(accumulated_tool_calls)
            } else {
                parse_tool_calls(&full_text).unwrap_or_default()
            };

            (full_text, tool_calls)
        }
        Err(error) => {
            yield AgentEvent::Error(error.to_string());
            return;
        }
    }
};
```

### Tool Call Accumulator

Add a helper struct and functions (in `response.rs` or inline):

```rust
struct ToolCallAccumulator {
    index: usize,
    id: String,
    name: String,
    arguments_json: String,
}

fn accumulate_tool_call(accumulators: &mut Vec<ToolCallAccumulator>, tc: &StreamToolCall) {
    // Find or create accumulator for this index
    let acc = if let Some(acc) = accumulators.iter_mut().find(|a| a.index == tc.index) {
        acc
    } else {
        accumulators.push(ToolCallAccumulator {
            index: tc.index,
            id: String::new(),
            name: String::new(),
            arguments_json: String::new(),
        });
        accumulators.last_mut().unwrap()
    };
    if let Some(ref id) = tc.id {
        acc.id = id.clone();
    }
    if let Some(ref name) = tc.name {
        acc.name = name.clone();
    }
    if let Some(ref args) = tc.arguments_delta {
        acc.arguments_json.push_str(args);
    }
}

fn finalize_tool_calls(accumulators: Vec<ToolCallAccumulator>) -> Vec<ToolCall> {
    accumulators.into_iter().map(|acc| ToolCall {
        id: acc.id,
        name: acc.name,
        arguments: acc.arguments_json,
    }).collect()
}
```

*Before proceeding to Phase 6, invoke the Code Reviewer sub-agent to verify the agent loop correctly accumulates tool calls from stream fragments and emits tokens in real-time.*

---

## Phase 6: Verify SSE Parsers Handle Tool Calls

**File**: `crates/ava-llm/src/providers/common/parsing.rs`

Verify and fix if needed:

### Anthropic SSE tool call events:
```
event: content_block_start
data: {"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_xxx","name":"read"}}

event: content_block_delta
data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"path\":\"/tmp"}}

event: content_block_stop
data: {"type":"content_block_stop","index":1}
```

`parse_anthropic_stream_chunk()` should return:
- On `content_block_start` with tool_use: `StreamChunk { tool_call: Some(StreamToolCall { index, id, name, arguments_delta: None }) }`
- On `input_json_delta`: `StreamChunk { tool_call: Some(StreamToolCall { index, arguments_delta: Some(partial_json) }) }`

### OpenAI SSE tool call events:
```
data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_xxx","function":{"name":"read","arguments":""}}]}}]}
data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"path\":\"/tmp"}}]}}]}
```

`parse_openai_stream_chunk()` should return:
- `StreamChunk { tool_call: Some(StreamToolCall { index, id, name, arguments_delta }) }`

Check the existing parsing code. If tool call parsing is missing or incomplete, add it.

*Before proceeding to Phase 7, invoke the Code Reviewer sub-agent.*

---

## Phase 7: Final Verification

```bash
cargo build --workspace 2>&1
cargo test --workspace 2>&1
cargo clippy --workspace 2>&1
```

All must pass with zero warnings.

### Manual test (if possible):
```bash
# Should stream tokens in real-time, not freeze
cargo run --bin ava -- "Say hello and then read the file CLAUDE.md" \
  --headless --provider anthropic --model claude-haiku-4.5 --max-turns 5
```

*Invoke the Code Reviewer sub-agent for a FINAL review of ALL changes across all phases. Verify:*

1. `generate_stream_with_tools()` defined on trait with sensible default
2. `generate_stream_with_thinking()` defined on trait with sensible default
3. `SharedProvider` delegates both new methods
4. Anthropic provider implements true streaming+tools
5. OpenAI provider implements true streaming+tools
6. OpenRouter/Copilot delegate to inner provider
7. Gemini/Ollama/Mock use default fallback (safe)
8. Agent loop always uses streaming path — no more blocking `generate_with_tools()` in `run_streaming()`
9. Tool call fragments accumulated correctly from stream
10. Token events emitted in real-time (not batched)
11. Usage/cost events still emitted correctly
12. Dedup guard still works
13. Stuck detection still works
14. No regressions in existing tests

## Acceptance Criteria

- [ ] `cargo test --workspace` passes
- [ ] `cargo clippy --workspace` clean
- [ ] TUI renders tokens as they stream (no freeze)
- [ ] Tool calls work correctly via streaming (accumulated from fragments)
- [ ] Thinking content streams correctly
- [ ] Usage/cost tracking still works
- [ ] All 7 providers compile and work
- [ ] Default fallback ensures providers without streaming+tools still function
