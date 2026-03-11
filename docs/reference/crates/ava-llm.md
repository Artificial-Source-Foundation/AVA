# ava-llm

> LLM provider abstraction with 7 providers, retry, circuit breaking, and streaming

## Overview

The `ava-llm` crate is AVA's unified interface to large language model APIs. It abstracts away provider-specific API differences behind a single `LLMProvider` trait, enabling the agent runtime to swap providers transparently. The crate handles connection pooling, retry with exponential backoff, circuit breaking for resilience, and cost estimation.

**Crate root:** `crates/ava-llm/`
**Cargo.toml:** `crates/ava-llm/Cargo.toml` (depends on `ava-auth`, `ava-config`, `ava-types`, `reqwest`, `serde_json`, `tokio`, `futures`, `rand`, `uuid`)

### Supported Providers (7 + 7 coding plan aliases)

| Provider | Module | API Style | Auth | Tool Support | Thinking | Cost |
|---|---|---|---|---|---|---|
| Anthropic | `anthropic.rs` | Anthropic Messages | `x-api-key` header | Native | Adaptive (Opus 4.6, Sonnet 4.6) | Per-token |
| OpenAI | `openai.rs` | OpenAI Chat Completions | Bearer token | Native | reasoning_effort (GPT-5, o3, o4, Codex) | Per-token |
| Gemini | `gemini.rs` | Gemini generateContent | `x-goog-api-key` header | No | thinkingBudget (2.5) / thinkingLevel (3.x) | Per-token |
| Ollama | `ollama.rs` | OpenAI-compatible `/api/chat` | None | No | No | Free (local) |
| OpenRouter | `openrouter.rs` | OpenAI-compatible (wraps `OpenAIProvider`) | Bearer token | Native | reasoning.effort | Per-token + 10% markup |
| Copilot | `copilot.rs` | OpenAI-compatible via GitHub proxy | OAuth token exchange | Native | reasoning_effort (Claude, o3/o4, GPT-5) | Free (subscription) |
| Mock | `mock.rs` | In-memory queue | None | No | No | Nominal |

**Coding plan provider aliases** (use Anthropic-compatible or OpenAI-compatible implementations):

| Alias | Implementation | Base URL |
|---|---|---|
| `alibaba` | AnthropicProvider | `https://coding-intl.dashscope.aliyuncs.com/apps/anthropic/v1` |
| `alibaba-cn` | AnthropicProvider | `https://coding.dashscope.aliyuncs.com/apps/anthropic/v1` |
| `kimi-for-coding` | AnthropicProvider | `https://api.kimi.com/coding/v1` |
| `minimax-coding-plan` | AnthropicProvider | `https://api.minimax.io/anthropic/v1` |
| `minimax-cn-coding-plan` | AnthropicProvider | `https://api.minimaxi.com/anthropic/v1` |
| `zai-coding-plan` | OpenAIProvider (ThinkingFormat::Zhipu) | `https://api.z.ai/api/coding/paas/v4` |
| `zhipuai-coding-plan` | OpenAIProvider (ThinkingFormat::Zhipu) | `https://open.bigmodel.cn/api/coding/paas/v4` |

### Module Structure

```
crates/ava-llm/src/
├── lib.rs                          # Public API, re-exports
├── provider.rs                     # LLMProvider trait + LLMResponse + SharedProvider
├── pool.rs                         # ConnectionPool (reqwest::Client reuse)
├── retry.rs                        # RetryBudget (exponential backoff + jitter)
├── circuit_breaker.rs              # CircuitBreaker (Closed/Open/HalfOpen)
├── router.rs                       # ModelRouter + ProviderFactory trait
├── credential_test.rs              # Provider credential verification
└── providers/
    ├── mod.rs                      # create_provider() factory + base_url_for_provider()
    ├── anthropic.rs                # AnthropicProvider
    ├── openai.rs                   # OpenAIProvider + ThinkingFormat enum
    ├── gemini.rs                   # GeminiProvider
    ├── ollama.rs                   # OllamaProvider
    ├── openrouter.rs               # OpenRouterProvider (wraps OpenAIProvider)
    ├── copilot.rs                  # CopilotProvider (GitHub OAuth token exchange)
    ├── mock.rs                     # MockProvider (testing)
    └── common/
        ├── mod.rs                  # Shared HTTP helpers (send_with_retry, validate_status)
        ├── message_mapping.rs      # Message -> provider-specific JSON
        └── parsing.rs              # Response parsing, pricing, token estimation
```

---

## Provider Trait

### LLMProvider

**File:** `crates/ava-llm/src/provider.rs:24-126`

The core async trait that all providers implement. It is `Send + Sync` to allow sharing across tokio tasks.

```rust
#[async_trait]
pub trait LLMProvider: Send + Sync {
    // Required methods (5)
    async fn generate(&self, messages: &[Message]) -> Result<String>;
    async fn generate_stream(&self, messages: &[Message])
        -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>>;
    fn estimate_tokens(&self, input: &str) -> usize;
    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64;
    fn model_name(&self) -> &str;

    // Optional methods with defaults
    fn supports_tools(&self) -> bool { false }
    async fn generate_with_tools(&self, messages: &[Message], tools: &[Tool])
        -> Result<LLMResponse> { /* falls back to generate() */ }
    fn supports_thinking(&self) -> bool { false }
    fn thinking_levels(&self) -> &[ThinkingLevel] { &[] }
    async fn generate_with_thinking(&self, messages: &[Message], tools: &[Tool],
        thinking: ThinkingLevel) -> Result<LLMResponse> { /* falls back to generate_with_tools() */ }
    async fn generate_stream_with_tools(&self, messages: &[Message], tools: &[Tool])
        -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> { /* falls back to generate_with_tools, emits as chunks */ }
    async fn generate_stream_with_thinking(&self, messages: &[Message], tools: &[Tool],
        thinking: ThinkingLevel) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> { /* falls back */ }
}
```

### LLMResponse

**File:** `crates/ava-llm/src/provider.rs:9-17`

Returned by `generate_with_tools()` and `generate_with_thinking()`:

```rust
pub struct LLMResponse {
    pub content: String,              // Text response
    pub tool_calls: Vec<ToolCall>,    // Native tool call requests
    pub usage: Option<TokenUsage>,    // Token counts from the API
    pub thinking: Option<String>,     // Internal reasoning content (thinking models only)
}
```

### SharedProvider

**File:** `crates/ava-llm/src/provider.rs:130-210`

A newtype wrapper around `Arc<dyn LLMProvider>` that itself implements `LLMProvider`. This allows a single provider instance to be shared between `ava-agent` (which takes `Box<dyn LLMProvider>`) and `ava-praxis` (which needs a cloneable reference).

```rust
pub struct SharedProvider {
    inner: Arc<dyn LLMProvider>,
}

impl SharedProvider {
    pub fn new(inner: Arc<dyn LLMProvider>) -> Self { ... }
}

// Delegates all 11 LLMProvider methods to inner
impl LLMProvider for SharedProvider { ... }
```

---

## Providers

### Anthropic

**File:** `crates/ava-llm/src/providers/anthropic.rs`

Uses the Anthropic Messages API (`/v1/messages`). Supports native tool calling, prompt caching, and adaptive thinking.

**Authentication:** `x-api-key` header + `anthropic-version: 2023-06-01` header.

**Request format:**

```json
{
  "model": "claude-sonnet-4-20250514",
  "max_tokens": 4096,
  "messages": [...],
  "system": [{"type": "text", "text": "...", "cache_control": {"type": "ephemeral"}}],
  "tools": [{"name": "...", "description": "...", "input_schema": {...}}],
  "stream": true
}
```

Key implementation details:

- **System messages** are extracted from the message array and sent as a top-level `system` field with `cache_control: ephemeral` for prompt caching (line 92-98).
- **Tool definitions** use `input_schema` (not `parameters`) per the Anthropic API format.
- **Tool results** are mapped as `role: "user"` messages with `type: "tool_result"` content blocks.
- **Third-party mode** (`with_base_url()`): sets `third_party: true`, adds `User-Agent: ava/coding-agent` header, skips `anthropic-beta` header. Used by Alibaba, Kimi, MiniMax providers (line 199-208).
- **URL construction** (`messages_url()`, line 186-192): avoids double `/v1` for third-party providers whose base URL already ends with `/v1`.

**Adaptive thinking** (line 117-164):

Supported for Claude Opus 4.6 and Claude Sonnet 4.6 models. Uses:
```json
{
  "thinking": {"type": "adaptive"},
  "output_config": {"effort": "low|medium|high|max"}
}
```
Adds `anthropic-beta: interleaved-thinking-2025-05-14` header for native Anthropic (not third-party).

**Kimi thinking** (line 78-81, 130-139): Uses `thinking: {type: "enabled", budgetTokens: N}` for K2.5 models.

**Streaming:** SSE events parsed via `sse_to_stream()` (line 447-482). Events processed:
- `content_block_delta` with `text_delta`, `thinking_delta`, `input_json_delta`
- `content_block_start` with `tool_use` type
- `message_start` (input usage + cache tokens)
- `message_delta` (output usage + done flag)

**Circuit breaker:** Wired in via `send_with_retry_cb()` with `CircuitBreaker::default_provider()` (5 failures, 30s cooldown).

---

### OpenAI

**File:** `crates/ava-llm/src/providers/openai.rs`

Uses the OpenAI Chat Completions API (`/v1/chat/completions`). Supports native tool calling and reasoning modes.

**Authentication:** Bearer token (`Authorization: Bearer <key>`).

**Request format:**

```json
{
  "model": "gpt-4o",
  "messages": [{"role": "system|user|assistant|tool", "content": "..."}],
  "tools": [{"type": "function", "function": {"name": "...", "description": "...", "parameters": {...}}}],
  "stream": true,
  "stream_options": {"include_usage": true}
}
```

Key implementation details:

- **System messages** stay inline as `role: "system"` messages (unlike Anthropic's extraction).
- **Tool definitions** use `type: "function"` wrapper with `parameters` (not `input_schema`).
- **Tool results** use `role: "tool"` with `tool_call_id` field.
- **Streaming usage:** Requests `stream_options: {include_usage: true}` to get token counts in the final SSE chunk (line 317).

**ThinkingFormat enum** (line 17-25):

Three variants for OpenAI-compatible providers with different reasoning APIs:
- `ThinkingFormat::OpenAI` — `reasoning_effort: "low"|"medium"|"high"|"xhigh"` + `reasoning_summary: "auto"` + `include: ["reasoning.encrypted_content"]`
- `ThinkingFormat::DashScope` — `enable_thinking: true` (Alibaba)
- `ThinkingFormat::Zhipu` — `thinking: {type: "enabled", clear_thinking: false}` (ZAI/ZhipuAI)

**Reasoning model detection** (line 66-86):
- OpenAI format: GPT-5.x, Codex, o3, o4 models
- DashScope format: Qwen, QwQ, DeepSeek-R1, Kimi models (excluding kimi-k2-thinking)
- Zhipu format: all GLM models

**xhigh support** (line 90-98): Codex 5.2+, Codex 5.3+, GPT-5.3+ support the `"xhigh"` reasoning effort level.

**Circuit breaker:** `CircuitBreaker::default_provider()`.

---

### Gemini

**File:** `crates/ava-llm/src/providers/gemini.rs`

Uses the Gemini API (`/v1beta/models/{model}:generateContent`). Does not support native tool calling.

**Authentication:** `x-goog-api-key` header (not Bearer).

**Request format:**

```json
{
  "contents": [{"role": "user|model", "parts": [{"text": "..."}]}],
  "system_instruction": {"parts": [{"text": "..."}]}
}
```

Key differences from other providers:
- Assistant role is mapped to `"model"` (not `"assistant"`).
- System messages become a `system_instruction` object (not inline).
- Streaming URL uses `?alt=sse` query parameter.
- No native tool calling support (tools are text-extracted by the agent loop).

**Thinking support** (line 38-112):

Two thinking modes based on model generation:
- **Gemini 2.5:** `thinkingConfig: {includeThoughts: true, thinkingBudget: N}`
  - Low=4000, Medium=8000, High=16000, Max=24576
- **Gemini 3.x:** `thinkingConfig: {includeThoughts: true, thinkingLevel: "low"|"high"}`

Thinking responses contain `parts` with `thought: true` flag (line 116-133).

**Token usage** (via `parse_gemini_usage()`): Uses `usageMetadata.promptTokenCount` / `candidatesTokenCount` / `cachedContentTokenCount`.

**Circuit breaker:** `CircuitBreaker::default_provider()`.

---

### Ollama

**File:** `crates/ava-llm/src/providers/ollama.rs`

Connects to a local Ollama instance (`/api/chat` endpoint). No authentication required.

**Base URL resolution** (from `create_provider()` in `providers/mod.rs`, line 146-151):
1. Credential store `base_url` field
2. `OLLAMA_BASE_URL` environment variable
3. Default: `http://localhost:11434`

**Key differences:**
- **No circuit breaker** (local service, expected to be always available).
- **No tool support** — `generate_with_tools()` ignores tools, returns text-only with parsed usage.
- **Cost is always $0** (line 148).
- **Streaming format** is newline-delimited JSON (not SSE). Each line is a JSON object with `message.content` and optionally `done: true`.
- **Token usage** via `parse_ollama_usage()`: Uses `prompt_eval_count` (input) and `eval_count` (output) at the top level.
- **Retry:** Uses `send_retrying()` (no circuit breaker variant), which defaults to 3 retries.

---

### OpenRouter

**File:** `crates/ava-llm/src/providers/openrouter.rs`

Wraps `OpenAIProvider` with OpenRouter-specific features. Uses the OpenRouter API at `https://openrouter.ai/api`.

**Architecture:** Contains an `inner: OpenAIProvider` field. Most methods delegate directly to `inner`:
- `generate()`, `generate_stream()`, `generate_with_tools()`, `generate_stream_with_tools()` all delegate.
- `estimate_cost()` delegates then applies a **10% markup** (line 129): `self.inner.estimate_cost(...) * 1.10`.

**Reasoning support** (line 58-98):

OpenRouter uses its own reasoning format:
```json
{
  "reasoning": {"effort": "low|medium|high"}
}
```
Supported for models containing: `gpt-5`, `codex`, `claude`, `gemini-3`, or starting with `o3`/`o4`.

**Thinking levels:** Low, Medium, High (no Max — capped at High, line 163-170).

**Circuit breaker:** `CircuitBreaker::default_provider()` (separate from the inner OpenAIProvider's breaker).

---

### Copilot

**File:** `crates/ava-llm/src/providers/copilot.rs`

Uses GitHub Copilot's proxy API. All models (Claude, GPT, Gemini) route through a single `/chat/completions` endpoint; the Copilot proxy handles backend routing.

**Authentication flow** (line 54-79):
1. Takes a GitHub OAuth token (`gho_...`) at construction.
2. Exchanges it for a short-lived Copilot API token via `ava_auth::copilot::exchange_copilot_token()`.
3. Caches the token in `Arc<RwLock<Option<CopilotToken>>>`.
4. Re-exchanges when expired (~30 minute lifetime, checked via `token.is_expired()`).
5. Pre-warms the connection pool for the resolved endpoint after token exchange.

**Required headers** (line 92-115):

| Header | Value |
|---|---|
| `Authorization` | `Bearer {copilot_api_token}` |
| `X-Initiator` | `"user"` or `"agent"` (inferred from last message role) |
| `Openai-Intent` | `"conversation-edits"` |
| `User-Agent` | `"GitHubCopilotChat/0.35.0"` |
| `Editor-Version` | `"vscode/1.107.0"` |
| `Editor-Plugin-Version` | `"copilot-chat/0.35.0"` |
| `Copilot-Integration-Id` | `"vscode-chat"` |

**Initiator inference** (line 84-89): If the last message is `Role::User`, initiator is `"user"`, otherwise `"agent"`.

**Thinking support** (line 148-168): Uses `reasoning_effort` parameter (OpenAI-compatible format) for Claude and OpenAI reasoning models. Max level maps to `"high"`.

**Cost is always $0** (line 271) — Copilot is subscription-billed.

**Credential requirement:** Requires `oauth_token` in the credential store (not `api_key`). Attempting to create without it yields `"not connected -- use /connect copilot"` error (see `providers/mod.rs`, line 130-143).

**Circuit breaker:** `CircuitBreaker::default_provider()`.

---

### Mock

**File:** `crates/ava-llm/src/providers/mock.rs`

Testing provider that returns pre-queued responses from a `VecDeque<String>` protected by `Arc<Mutex<...>>`.

```rust
MockProvider::new("test-model", vec!["response1".into(), "response2".into()])
```

- `generate()` pops the next response from the queue.
- `generate_stream()` wraps `generate()` into a single `StreamChunk::text()`.
- `estimate_cost()` returns `(input + output) * 0.0000005`.
- Does not support tools or thinking.

---

## Infrastructure

### ConnectionPool

**File:** `crates/ava-llm/src/pool.rs`

Session-scoped HTTP client pool that reuses `reqwest::Client` instances across providers sharing the same base URL. `Send + Sync` via `RwLock<HashMap<String, Arc<reqwest::Client>>>`.

**Default configuration:**

| Parameter | Value |
|---|---|
| `connect_timeout` | 10 seconds |
| `request_timeout` | 120 seconds |
| `pool_max_idle_per_host` | 10 |
| `keep_alive` | 90 seconds |

**Double-checked locking** (line 54-82): Uses read lock for fast path, then write lock with re-check for creation to avoid race conditions.

**Custom timeouts:** `ConnectionPool::with_timeouts(connect, request)`.

**Stats:** `pool.stats().await` returns `PoolStats { active_clients, base_urls }`.

---

### RetryBudget

**File:** `crates/ava-llm/src/retry.rs`

Budget-aware retry with exponential backoff and jitter. Only retries errors where `AvaError::is_retryable()` returns true (rate limits, timeouts, network errors).

**Algorithm:**
1. Check `error.is_retryable()` — if false, return `None` immediately (budget not consumed).
2. Decrement `remaining` counter.
3. Compute delay: `base_delay * 2^(attempt-1)` with +/-20% jitter via `rand::gen_range(0.8..=1.2)`.
4. Cap at `max_delay`.

**Defaults:** `base_delay = 1s`, `max_delay = 60s`.

**Backoff progression** (base 1s): 1s, 2s, 4s, 8s, 16s, 32s, 60s (capped).

```rust
let mut budget = RetryBudget::new(3);
if let Some(delay) = budget.should_retry(&error) {
    tokio::time::sleep(delay).await;
    // retry...
}
budget.reset(); // reuse for next operation
```

---

### CircuitBreaker

**File:** `crates/ava-llm/src/circuit_breaker.rs`

Lock-free circuit breaker using `AtomicU32` (failure count) and `AtomicU8` (state). Protects against cascading failures from unresponsive providers.

**States:**

| State | Behavior |
|---|---|
| **Closed** (normal) | All requests allowed. Failures increment counter. |
| **Open** (tripped) | All requests rejected immediately. Transitions to Half-Open after cooldown. |
| **Half-Open** (probing) | One probe request allowed. Success -> Closed. Failure -> Open. |

**Default provider configuration** (`CircuitBreaker::default_provider()`):
- **Failure threshold:** 5
- **Cooldown:** 30 seconds

**State transitions:**
- `record_failure()`: increments counter. If count >= threshold -> Open. If Half-Open -> Open.
- `record_success()`: resets counter to 0. If Half-Open or Closed -> Closed.
- `allow_request()`: Closed -> true. Open -> check cooldown elapsed -> Half-Open transition. Half-Open -> true.

**Thread safety:** Uses `Ordering::Acquire`/`Release` for state reads/writes. `last_failure` is `Mutex<Option<Instant>>` (poisoning handled via `unwrap_or_else(|e| e.into_inner())`).

**Wired into 5 providers:** Anthropic, OpenAI, Gemini, OpenRouter, Copilot. Not used by Ollama (local) or Mock.

### HTTP Retry + Circuit Breaker Integration

**File:** `crates/ava-llm/src/providers/common/mod.rs:82-176`

The `send_with_retry()` function handles HTTP-level retries:
- **429 (Too Many Requests):** Respects `Retry-After` header, falls back to `2^attempt` seconds.
- **5xx (Server Error):** Exponential backoff `2^attempt` seconds.
- **Network errors:** Exponential backoff `2^attempt` seconds.
- **4xx (except 429):** Fails immediately, no retry.

The `send_with_retry_cb()` function wraps `send_with_retry()` with circuit breaker:
1. Check `cb.allow_request()` — if denied, return `AvaError::ProviderUnavailable`.
2. Execute request with retries.
3. Record success (2xx/3xx) or failure (5xx/error) on the circuit breaker.

---

## Message Mapping

**File:** `crates/ava-llm/src/providers/common/message_mapping.rs`

Three mapping functions convert AVA's unified `Message` type to provider-specific JSON:

### `map_messages_openai(messages) -> Vec<Value>`

Used by: OpenAI, OpenRouter, Copilot, Ollama.

| AVA Role | OpenAI Role | Notes |
|---|---|---|
| System | `"system"` | Inline, not extracted |
| User | `"user"` | |
| Assistant | `"assistant"` | With `tool_calls` array when present |
| Tool | `"tool"` | With `tool_call_id` field |

- Assistant messages with empty content set `content: null` (some providers reject missing content field).
- Tool messages without a `tool_call_id` default to `"unknown"`.

### `map_messages_anthropic(messages) -> (Option<String>, Vec<Value>)`

Used by: Anthropic, Alibaba, Kimi, MiniMax.

Returns `(system_text, messages)` — system messages are **extracted** and joined with `\n`.

| AVA Role | Anthropic Role | Notes |
|---|---|---|
| System | Extracted to return value | Joined if multiple |
| User | `"user"` | |
| Assistant | `"assistant"` | Content blocks: `[{type: "text"}, {type: "tool_use"}]` |
| Tool | `"user"` | Content: `[{type: "tool_result", tool_use_id: "..."}]` |

- Tool results are sent as `role: "user"` messages (Anthropic requirement).
- Empty assistant content omits the text block (only tool_use blocks sent).

### `map_messages_gemini_parts(messages) -> (Option<Value>, Vec<Value>)`

Used by: Gemini.

Returns `(system_instruction, contents)`.

| AVA Role | Gemini Role | Notes |
|---|---|---|
| System | Extracted to `system_instruction.parts` | |
| User | `"user"` | |
| Assistant | `"model"` | Note: "model" not "assistant" |
| Tool | `"user"` | (No native tool support) |

---

## Token & Cost Tracking

### Token Usage Parsing

**File:** `crates/ava-llm/src/providers/common/parsing.rs`

Four provider-specific usage parsing functions that all return `Option<TokenUsage>`:

#### `parse_usage(payload)` (line 49-85)

Handles both OpenAI and Anthropic non-streaming responses:

| Provider | Input field | Output field | Cache fields |
|---|---|---|---|
| Anthropic | `usage.input_tokens` | `usage.output_tokens` | `cache_read_input_tokens`, `cache_creation_input_tokens` |
| OpenAI | `usage.prompt_tokens` | `usage.completion_tokens` | `prompt_tokens_details.cached_tokens` |

Cache read tokens take `max(anthropic_cache_read, openai_cached)` to handle both formats.

#### `parse_gemini_usage(payload)` (line 89-109)

Gemini uses `usageMetadata`:
- `promptTokenCount` -> `input_tokens`
- `candidatesTokenCount` -> `output_tokens`
- `cachedContentTokenCount` -> `cache_read_tokens`

#### `parse_ollama_usage(payload)` (line 406-418)

Ollama uses top-level fields:
- `prompt_eval_count` -> `input_tokens`
- `eval_count` -> `output_tokens`
- Returns `None` if both are 0.

#### Streaming Usage

- **Anthropic:** `message_start` event carries input + cache tokens. `message_delta` event carries output tokens + done flag.
- **OpenAI/Copilot/OpenRouter:** Final SSE chunk with `usage` object (requires `stream_options: {include_usage: true}`).
- **Ollama:** Final streaming chunk with `done: true` carries `prompt_eval_count` + `eval_count`.
- **Gemini:** Each SSE chunk may carry `usageMetadata`.

### Cost Estimation

**File:** `crates/ava-llm/src/providers/common/parsing.rs:1-43, 111-126`

#### `model_pricing_usd_per_million(model) -> (input_rate, output_rate)`

Returns per-million-token pricing in USD. First checks the compiled-in model registry (`ava_config::model_catalog::registry`), then falls back to heuristic matching:

| Model Pattern | Input $/M | Output $/M |
|---|---|---|
| Claude Opus | 15.00 | 75.00 |
| Claude Sonnet | 3.00 | 15.00 |
| Claude Haiku | 0.25 | 1.25 |
| GPT-4o / GPT-4.1 | 2.50 | 10.00 |
| GPT-4o-mini / GPT-4.1-mini | 0.15 | 0.60 |
| o3 / o4-mini | 1.10 | 4.40 |
| Gemini Flash | 0.075 | 0.30 |
| Gemini Pro | 1.25 | 5.00 |
| GLM, MiniMax, K2P5, Kimi, Qwen | 0.00 | 0.00 |
| Default (unknown) | 2.50 | 10.00 |

#### `estimate_cost_usd(input_tokens, output_tokens, in_rate, out_rate) -> f64`

Simple calculation: `input/1M * in_rate + output/1M * out_rate`.

#### `estimate_cost_with_cache_usd(usage, in_rate, out_rate) -> f64`

Cache-aware pricing:
- **Cache read tokens:** 10% of normal input rate.
- **Cache creation tokens:** 125% of normal input rate.
- **Non-cached input tokens:** Full input rate.

#### `estimate_tokens(input) -> usize`

Heuristic: `chars / 4`, minimum 1. Used by all providers (line 128-130).

### StreamChunk

**Defined in:** `ava-types` (imported by ava-llm)

Rich streaming data carrier used by all `generate_stream*` methods:

```rust
pub struct StreamChunk {
    pub content: Option<String>,           // Text delta
    pub tool_call: Option<StreamToolCall>, // Tool call fragment
    pub usage: Option<TokenUsage>,         // Token counts (typically final chunk)
    pub thinking: Option<String>,          // Reasoning/thinking delta
    pub done: bool,                        // Stream completion signal
}

pub struct StreamToolCall {
    pub index: usize,                      // Tool call index (for parallel calls)
    pub id: Option<String>,                // Tool call ID (first fragment only)
    pub name: Option<String>,              // Function name (first fragment only)
    pub arguments_delta: Option<String>,   // JSON argument fragment
}
```

---

## Provider Resolution

### ModelRouter

**File:** `crates/ava-llm/src/router.rs`

Routes `(provider_name, model_name)` pairs to cached `Arc<dyn LLMProvider>` instances. Serves as the top-level entry point for obtaining providers.

```rust
let router = ModelRouter::new(credentials);
let provider = router.route("anthropic", "claude-sonnet-4").await?;
```

**Resolution order** (line 64-88):
1. Check provider cache (`RwLock<HashMap<String, Arc<dyn LLMProvider>>>`).
2. Try external factories (`ProviderFactory` trait) — used for CLI agent providers (`cli:*` prefix).
3. Fall back to `create_provider()` in `providers/mod.rs`.
4. Cache and return.

**Cache key format:** `"{provider}:{model}"`.

**Credential updates:** `update_credentials()` clears the provider cache, forcing re-creation on next `route()` call.

### ProviderFactory Trait

**File:** `crates/ava-llm/src/router.rs:14-17`

Extension point for registering providers that live outside `ava-llm` (e.g., `ava-cli-providers`):

```rust
pub trait ProviderFactory: Send + Sync {
    fn create(&self, provider_name: &str, model: &str) -> Result<Box<dyn LLMProvider>>;
    fn handles(&self, provider_name: &str) -> bool;
}
```

### create_provider()

**File:** `crates/ava-llm/src/providers/mod.rs:51-212`

Factory function that creates providers from credential store entries. Maps provider names to concrete types, extracts API keys, and configures base URLs. CLI providers (`cli:*`) are rejected with an error directing to `ModelRouter::register_factory()`.

### Credential Testing

**File:** `crates/ava-llm/src/credential_test.rs`

`test_provider_credentials()` sends a "Hello" message to verify a provider's API key is valid. Returns a formatted status string with response time. 20-second timeout.

`default_model_for_provider()` returns a sensible default model for each provider name (used when no model is specified).

---

## Provider Comparison

### Feature Matrix

| Feature | Anthropic | OpenAI | Gemini | Ollama | OpenRouter | Copilot | Mock |
|---|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Tool calling | Yes | Yes | No | No | Yes | Yes | No |
| Streaming | SSE | SSE | SSE | NDJSON | SSE | SSE | Single chunk |
| Thinking | Adaptive | reasoning_effort | thinkingBudget/Level | No | reasoning.effort | reasoning_effort | No |
| Circuit breaker | Yes | Yes | Yes | No | Yes | Yes | No |
| Prompt caching | Yes (ephemeral) | Yes (cached_tokens) | Yes (cachedContent) | No | Via inner | Via proxy | No |
| Cost tracking | Per-token | Per-token | Per-token | Free | Per-token + 10% | Free | Nominal |
| Custom base URL | Yes | Yes | No | Yes | Yes | Dynamic (token) | No |

### Thinking Level Support

| Level | Anthropic | OpenAI | Gemini 2.5 | Gemini 3.x | OpenRouter | Copilot |
|---|---|---|---|---|---|---|
| Off | All models | All models | All models | All models | All models | All models |
| Low | Opus/Sonnet 4.6 | GPT-5, o3, o4 | budget=4000 | "low" | All reasoning | Claude, o3/o4, GPT-5 |
| Medium | Opus/Sonnet 4.6 | GPT-5, o3, o4 | budget=8000 | "low" | All reasoning | Claude, o3/o4, GPT-5 |
| High | Opus/Sonnet 4.6 | GPT-5, o3, o4 | budget=16000 | "high" | All reasoning | Claude, o3/o4, GPT-5 |
| Max | Opus/Sonnet 4.6 | Codex 5.2+, GPT-5.3+ (xhigh) | budget=24576 | "high" | Maps to High | Maps to High |

### Response Parsing Paths

| Provider | Completion payload | Tool calls | Usage |
|---|---|---|---|
| Anthropic | `content[type=text].text` | `content[type=tool_use]` | `usage.{input,output}_tokens` + cache fields |
| OpenAI | `choices[0].message.content` | `choices[0].message.tool_calls[].function` | `usage.{prompt,completion}_tokens` + `prompt_tokens_details.cached_tokens` |
| Gemini | `candidates[0].content.parts[0].text` | N/A | `usageMetadata.{promptTokenCount,candidatesTokenCount}` |
| Ollama | `message.content` | N/A | `prompt_eval_count`, `eval_count` |
| Copilot | Same as OpenAI | Same as OpenAI | Same as OpenAI |
| OpenRouter | Same as OpenAI | Same as OpenAI | Same as OpenAI |
