# Token & Cost Tracking

This document explains how token usage and cost information flows through AVA,
from LLM API responses all the way to the TUI status bar.

## Data Types

### TokenUsage (`crates/ava-types/src/lib.rs:23`)

The shared struct for token counts across all providers:

```rust
pub struct TokenUsage {
    pub input_tokens: usize,
    pub output_tokens: usize,
    pub cache_read_tokens: usize,      // Anthropic cache_read_input_tokens, OpenAI cached_tokens
    pub cache_creation_tokens: usize,  // Anthropic cache_creation_input_tokens
}
```

### StreamChunk (`crates/ava-types/src/lib.rs:36`)

Each streaming chunk can carry optional usage data:

```rust
pub struct StreamChunk {
    pub content: Option<String>,
    pub tool_call: Option<StreamToolCall>,
    pub usage: Option<TokenUsage>,   // typically only in the final chunk
    pub thinking: Option<String>,
    pub done: bool,
}
```

## Flow: Provider to TUI

```
LLM API Response
      |
      v
Provider (parse_usage / parse_gemini_usage / parse_ollama_usage)
      |
      v
StreamChunk { usage: Some(TokenUsage) }
      |
      v
AgentLoop::run_streaming()  -- merges usage across chunks
      |
      v
AgentEvent::TokenUsage { input_tokens, output_tokens, cost_usd }
      |
      v
App::handle_agent_event()  -- accumulates into state
      |
      v
status_bar.rs  -- renders "{tokens}/{context_window}"
```

## Per-Provider Usage Parsing

All parsing functions live in `crates/ava-llm/src/providers/common/parsing.rs`.

### Anthropic & OpenAI (`parse_usage`, line 49)

Works for both API formats by checking alternative field names:

| AVA Field | Anthropic Field | OpenAI Field |
|---|---|---|
| `input_tokens` | `usage.input_tokens` | `usage.prompt_tokens` |
| `output_tokens` | `usage.output_tokens` | `usage.completion_tokens` |
| `cache_read_tokens` | `usage.cache_read_input_tokens` | `usage.prompt_tokens_details.cached_tokens` |
| `cache_creation_tokens` | `usage.cache_creation_input_tokens` | (not available) |

The parser uses `.or_else()` chains so a single function handles both formats:
```rust
let input = usage.get("input_tokens")
    .or_else(|| usage.get("prompt_tokens"))
    .and_then(Value::as_u64)
    .unwrap_or(0) as usize;
```

### Gemini (`parse_gemini_usage`, line 89)

Gemini uses a different top-level key (`usageMetadata`) and different field names:

| AVA Field | Gemini Field |
|---|---|
| `input_tokens` | `usageMetadata.promptTokenCount` |
| `output_tokens` | `usageMetadata.candidatesTokenCount` |
| `cache_read_tokens` | `usageMetadata.cachedContentTokenCount` |
| `cache_creation_tokens` | (not available, always 0) |

### Ollama (`parse_ollama_usage`, line 406)

Ollama places counts at the top level of the response, not nested under `usage`:

| AVA Field | Ollama Field |
|---|---|
| `input_tokens` | `prompt_eval_count` |
| `output_tokens` | `eval_count` |
| `cache_read_tokens` | (not available, always 0) |
| `cache_creation_tokens` | (not available, always 0) |

These appear in non-streaming responses and in the final streaming chunk
(`done: true`).

### OpenRouter & Copilot

Both use OpenAI-compatible formats, so they delegate to `parse_usage`.

## Streaming Usage Merging

During streaming, usage data may arrive in multiple chunks. Anthropic, for
example, sends input token counts in `message_start` and output token counts
in `message_delta`. The agent loop merges these
(`crates/ava-agent/src/agent_loop/mod.rs:442`):

```rust
if let Some(ref mut existing) = last_usage {
    // Merge: Anthropic sends input in message_start, output in message_delta
    if usage.input_tokens > 0 { existing.input_tokens = usage.input_tokens; }
    if usage.output_tokens > 0 { existing.output_tokens = usage.output_tokens; }
    if usage.cache_read_tokens > 0 { existing.cache_read_tokens = usage.cache_read_tokens; }
    if usage.cache_creation_tokens > 0 { existing.cache_creation_tokens = usage.cache_creation_tokens; }
} else {
    last_usage = Some(usage.clone());
}
```

## Cost Calculation

### Model Pricing (`model_pricing_usd_per_million`, line 5)

Located in `crates/ava-llm/src/providers/common/parsing.rs`. Returns
`(input_rate, output_rate)` in USD per million tokens. First checks the
compiled-in model registry (`crates/ava-config/src/model_catalog/registry.rs`),
then falls back to heuristic matching based on model name substrings.

### Basic Cost (`estimate_cost_usd`, line 111)

```rust
input_tokens / 1_000_000 * in_rate + output_tokens / 1_000_000 * out_rate
```

### Cache-Aware Cost (`estimate_cost_with_cache_usd`, line 119)

Accounts for Anthropic's prompt caching pricing:
- Cache read tokens cost **10%** of normal input rate
- Cache creation tokens cost **125%** of normal input rate
- Non-cached input tokens cost the full input rate

```rust
let non_cached_input = usage.input_tokens.saturating_sub(usage.cache_read_tokens);
non_cached_input / M * in_rate
    + usage.cache_read_tokens / M * in_rate * 0.1
    + usage.cache_creation_tokens / M * in_rate * 1.25
    + usage.output_tokens / M * out_rate
```

## Agent Event Emission

After the stream completes, the agent loop emits a `TokenUsage` event
(`crates/ava-agent/src/agent_loop/mod.rs:471`):

```rust
if let Some(usage) = last_usage {
    let (in_rate, out_rate) = model_pricing_usd_per_million(&self.config.model);
    let cost = estimate_cost_with_cache_usd(&usage, in_rate, out_rate);
    yield AgentEvent::TokenUsage { input_tokens, output_tokens, cost_usd: cost };
}
```

## TUI Accumulation

The TUI receives `TokenUsage` events and accumulates them into per-session
totals (`crates/ava-tui/src/app/event_handler.rs:46`):

```rust
AgentEvent::TokenUsage { input_tokens, output_tokens, cost_usd } => {
    self.state.agent.tokens_used.input += input_tokens;
    self.state.agent.tokens_used.output += output_tokens;
    self.state.agent.cost += cost_usd;
}
```

## Sub-Agent Token Propagation

When a sub-agent completes, its token usage is propagated to the parent via
`AgentEvent::SubAgentComplete` (`crates/ava-agent/src/stack.rs:657`). The TUI
adds sub-agent tokens to the parent's running totals
(`crates/ava-tui/src/app/event_handler.rs:213`):

```rust
AgentEvent::SubAgentComplete { input_tokens, output_tokens, cost_usd, .. } => {
    self.state.agent.tokens_used.input += input_tokens;
    self.state.agent.tokens_used.output += output_tokens;
    self.state.agent.cost += cost_usd;
}
```

The sub-agent's cost is computed using the same `model_pricing_usd_per_million`
and `estimate_cost_usd` functions at `crates/ava-agent/src/stack.rs:660`.

## Session-Level Usage

The `Session` struct (`crates/ava-types/src/session.rs`) stores cumulative
`token_usage: TokenUsage`. The non-streaming `AgentLoop::run()` method sets
`session.token_usage = total_usage` before returning
(`crates/ava-agent/src/agent_loop/mod.rs:258`). This is used for session
metadata persistence and cost auditing.

## Key Files

| File | Role |
|---|---|
| `crates/ava-types/src/lib.rs` | `TokenUsage`, `StreamChunk` type definitions |
| `crates/ava-llm/src/providers/common/parsing.rs` | `parse_usage`, `parse_gemini_usage`, `parse_ollama_usage`, cost functions |
| `crates/ava-agent/src/agent_loop/mod.rs` | Streaming usage merging, `AgentEvent::TokenUsage` emission |
| `crates/ava-agent/src/stack.rs` | Sub-agent cost computation and `SubAgentComplete` event |
| `crates/ava-tui/src/app/event_handler.rs` | TUI accumulation of token/cost state |
| `crates/ava-config/src/model_catalog/registry.rs` | Compiled-in model pricing registry |
