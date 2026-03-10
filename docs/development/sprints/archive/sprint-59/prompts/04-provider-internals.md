# Sprint 59-04: Provider Internals Mega — Retry Jitter, Circuit Breaker, Rich Streaming, Model Registry

## Context

You are working on **AVA**, a Rust-first AI coding agent. Read `CLAUDE.md` and `AGENTS.md` first.

A provider research report at `docs/development/sprints/sprint-59/results/03-provider-research.md` identified several internal improvements to the provider layer. This prompt combines three tracks into one mega sprint:

1. **Retry resilience** (P0): Add jitter to backoff, wire circuit breaker into providers
2. **Rich streaming** (P1): Replace `Stream<Item = String>` with `Stream<Item = StreamChunk>` for usage/tool/thinking metadata
3. **Model registry** (P1): Compiled-in `registry.json` via `include_str!`, replace pattern-match pricing, add name normalization

### Key files — read ALL of these before starting

**Retry & circuit breaker:**
- `crates/ava-llm/src/retry.rs` — `RetryBudget` with exponential backoff (no jitter)
- `crates/ava-llm/src/circuit_breaker.rs` — 3-state CircuitBreaker (Closed/Open/Half-Open)
- `crates/ava-llm/src/providers/common/mod.rs` — `send_with_retry()`, `send_with_retry_cb()`, `send_retrying()`

**Streaming:**
- `crates/ava-llm/src/provider.rs` — `LLMProvider` trait, `generate_stream()` returns `Stream<Item = String>`
- `crates/ava-llm/src/providers/anthropic.rs` — Anthropic streaming SSE parsing
- `crates/ava-llm/src/providers/openai.rs` — OpenAI streaming SSE parsing
- `crates/ava-types/src/lib.rs` — shared types (TokenUsage, etc.)
- `crates/ava-agent/src/loop.rs` — agent loop consuming streams
- `crates/ava-tui/src/app/` — TUI rendering streamed text

**Model catalog:**
- `crates/ava-llm/src/providers/common/` — `model_pricing_usd_per_million()` function
- `crates/ava-config/src/model_catalog/fallback.rs` — hardcoded `CatalogModel` entries + `CURATED_MODELS`
- `crates/ava-config/src/model_catalog/types.rs` — `CatalogModel`, `ModelCatalog` types
- `crates/ava-config/src/model_catalog/fetch.rs` — models.dev fetch logic

**All providers (will be touched):**
- `crates/ava-llm/src/providers/anthropic.rs`
- `crates/ava-llm/src/providers/openai.rs`
- `crates/ava-llm/src/providers/gemini.rs`
- `crates/ava-llm/src/providers/openrouter.rs`
- `crates/ava-llm/src/providers/ollama.rs`
- `crates/ava-llm/src/providers/copilot.rs`
- `crates/ava-llm/src/providers/mock.rs`

### Reference
- Goose retry jitter: `delay * rand::thread_rng().gen_range(0.8..=1.2)`
- Goose model registry: `include_str!("canonical/data/provider_metadata.json")`
- OpenCode streaming: rich events with content, tool calls, usage, reasoning
- See full report: `docs/development/sprints/sprint-59/results/03-provider-research.md`

---

## Phase 1: Retry Jitter

### Task 1a: Add rand dependency

Check if `rand` is already in `crates/ava-llm/Cargo.toml`. If not, add it:
```toml
rand = "0.8"
```

### Task 1b: Add ±20% jitter to `should_retry()`

In `crates/ava-llm/src/retry.rs`, the current code computes pure exponential backoff:
```rust
let delay = self.base_delay.saturating_mul(1u32 << (attempt - 1).min(30));
Some(delay.min(self.max_delay))
```

Add jitter to prevent thundering herd:
```rust
use rand::Rng;

let exponential = self.base_delay.saturating_mul(1u32 << (attempt - 1).min(30));
let jitter_factor = rand::thread_rng().gen_range(0.8..=1.2);
let delay = exponential.mul_f64(jitter_factor).min(self.max_delay);
Some(delay)
```

### Task 1c: Jitter tests

```rust
#[test]
fn retry_delays_have_jitter() {
    let delays: Vec<Duration> = (0..20).map(|_| {
        let mut budget = RetryBudget::new(5, Duration::from_millis(100), Duration::from_secs(30));
        let error = AvaError::ProviderError { provider: "test".into(), message: "429".into() };
        budget.should_retry(&error).unwrap()
    }).collect();
    let first = delays[0];
    assert!(delays.iter().any(|d| *d != first), "Expected jitter but all delays were identical");
}

#[test]
fn retry_jitter_stays_within_bounds() {
    for _ in 0..100 {
        let mut budget = RetryBudget::new(5, Duration::from_millis(1000), Duration::from_secs(60));
        let error = AvaError::ProviderError { provider: "test".into(), message: "429".into() };
        let delay = budget.should_retry(&error).unwrap();
        assert!(delay >= Duration::from_millis(800), "Delay too short: {delay:?}");
        assert!(delay <= Duration::from_millis(1200), "Delay too long: {delay:?}");
    }
}
```

**Before proceeding to Phase 2, invoke the Code Reviewer sub-agent to verify jitter is correctly applied and tests pass.**

---

## Phase 2: Wire Circuit Breaker into Providers

Currently `send_with_retry_cb()` exists but no provider uses it — all call `send_retrying()`.

### Task 2a: Add circuit breaker field to API providers

Give each remote API provider an `Option<Arc<CircuitBreaker>>` field, enabled by default:

```rust
pub struct AnthropicProvider {
    // ...existing fields...
    circuit_breaker: Option<Arc<CircuitBreaker>>,
}
```

Update constructors to create one by default:
```rust
circuit_breaker: Some(Arc::new(CircuitBreaker::new())),
```

### Task 2b: Route requests through circuit breaker

Add a helper method (or update existing request methods) in each provider:
```rust
async fn send_request(&self, request: reqwest::RequestBuilder) -> Result<reqwest::Response> {
    match &self.circuit_breaker {
        Some(cb) => common::send_with_retry_cb(request, "anthropic", cb).await,
        None => common::send_retrying(request, "anthropic").await,
    }
}
```

### Task 2c: Apply to these providers

Add `circuit_breaker` field and route through it:
- `anthropic.rs`
- `openai.rs`
- `gemini.rs`
- `openrouter.rs`
- `copilot.rs`

Do NOT add to:
- `ollama.rs` (local, no rate limits)
- `mock.rs` (test only)

### Task 2d: Circuit breaker integration tests

```rust
#[test]
fn circuit_breaker_opens_after_failures() { ... }

#[test]
fn circuit_breaker_half_open_allows_probe() { ... }
```

**Before proceeding to Phase 3, invoke the Code Reviewer sub-agent to verify circuit breaker is wired into all 5 remote API providers.**

---

## Phase 3: Compiled-In Model Registry

### Task 3a: Create registry.json

Create `crates/ava-config/src/model_catalog/registry.json` with ALL known models:

```json
{
  "models": [
    {
      "id": "claude-opus-4.6",
      "provider": "anthropic",
      "name": "Claude Opus 4.6",
      "aliases": ["opus", "opus-4.6", "claude-opus-4-6"],
      "capabilities": { "tool_call": true, "vision": true, "reasoning": true, "streaming": true },
      "limits": { "context_window": 200000, "max_output": 32000 },
      "cost": { "input_per_million": 15.0, "output_per_million": 75.0, "cache_read_per_million": 1.5, "cache_write_per_million": 18.75 }
    }
  ]
}
```

Include models from ALL providers:
- anthropic (claude-opus-4.6, claude-sonnet-4.6, claude-sonnet-4.5, claude-haiku-4.5)
- openai (gpt-5.3-codex, gpt-5.2-codex, gpt-5.1-codex, gpt-5, gpt-4.1, codex-mini-latest, etc.)
- google (gemini-2.5-pro, gemini-2.5-flash)
- copilot (all copilot models — $0 cost)
- coding plan providers (zai, zhipuai, alibaba, alibaba-cn, kimi, minimax — all $0 cost)

Cross-reference with existing `fallback.rs` and `CURATED_MODELS` to ensure nothing is missed.

### Task 3b: Create registry module

Create `crates/ava-config/src/model_catalog/registry.rs`:

```rust
use serde::Deserialize;

static REGISTRY_JSON: &str = include_str!("registry.json");

#[derive(Debug, Clone, Deserialize)]
pub struct ModelRegistry {
    pub models: Vec<RegisteredModel>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct RegisteredModel {
    pub id: String,
    pub provider: String,
    pub name: String,
    #[serde(default)]
    pub aliases: Vec<String>,
    pub capabilities: ModelCapabilities,
    pub limits: ModelLimits,
    pub cost: ModelCost,
}

#[derive(Debug, Clone, Deserialize)]
pub struct ModelCapabilities {
    pub tool_call: bool,
    #[serde(default)]
    pub vision: bool,
    #[serde(default)]
    pub reasoning: bool,
    #[serde(default)]
    pub streaming: bool,
}

#[derive(Debug, Clone, Deserialize)]
pub struct ModelLimits {
    pub context_window: usize,
    pub max_output: Option<usize>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct ModelCost {
    pub input_per_million: f64,
    pub output_per_million: f64,
    #[serde(default)]
    pub cache_read_per_million: Option<f64>,
    #[serde(default)]
    pub cache_write_per_million: Option<f64>,
}

impl ModelRegistry {
    pub fn load() -> Self {
        serde_json::from_str(REGISTRY_JSON)
            .expect("registry.json is embedded at compile time and must be valid")
    }

    /// Look up by exact ID or alias (case-insensitive).
    pub fn find(&self, query: &str) -> Option<&RegisteredModel> {
        let q = query.to_lowercase();
        self.models.iter().find(|m| {
            m.id.to_lowercase() == q
                || m.aliases.iter().any(|a| a.to_lowercase() == q)
        })
    }

    /// Look up for a specific provider.
    pub fn find_for_provider(&self, provider: &str, model: &str) -> Option<&RegisteredModel> {
        let q = model.to_lowercase();
        self.models.iter().find(|m| {
            m.provider == provider
                && (m.id.to_lowercase() == q || m.aliases.iter().any(|a| a.to_lowercase() == q))
        })
    }

    /// Get (input, output) pricing in USD per million tokens.
    pub fn pricing(&self, model: &str) -> Option<(f64, f64)> {
        self.find(model).map(|m| (m.cost.input_per_million, m.cost.output_per_million))
    }

    /// All models for a provider.
    pub fn models_for_provider(&self, provider: &str) -> Vec<&RegisteredModel> {
        self.models.iter().filter(|m| m.provider == provider).collect()
    }

    /// Fuzzy normalize a model name to canonical ID.
    /// Handles: aliases, missing dashes/dots, word reordering.
    pub fn normalize(&self, query: &str) -> Option<String> {
        if let Some(m) = self.find(query) {
            return Some(m.id.clone());
        }
        let normalized = query.replace(['.', '-', '_'], "").to_lowercase();
        self.models.iter().find(|m| {
            m.id.replace(['.', '-', '_'], "").to_lowercase() == normalized
                || m.aliases.iter().any(|a| a.replace(['.', '-', '_'], "").to_lowercase() == normalized)
        }).map(|m| m.id.clone())
    }
}

/// Global lazy-initialized registry.
pub fn registry() -> &'static ModelRegistry {
    use std::sync::OnceLock;
    static INSTANCE: OnceLock<ModelRegistry> = OnceLock::new();
    INSTANCE.get_or_init(ModelRegistry::load)
}
```

Register in `crates/ava-config/src/model_catalog/mod.rs`: add `pub mod registry;`

### Task 3c: Replace model_pricing_usd_per_million

Update the existing `model_pricing_usd_per_million()` function to delegate to the registry:
```rust
pub fn model_pricing_usd_per_million(model: &str) -> (f64, f64) {
    ava_config::model_catalog::registry::registry()
        .pricing(model)
        .unwrap_or((0.0, 0.0))
}
```

Keep the same function signature for backward compatibility.

### Task 3d: Wire normalization into provider creation

In `crates/ava-llm/src/providers/mod.rs`, normalize model names before creating providers:
```rust
pub fn create_provider(provider_name: &str, model: &str, ...) -> Result<Box<dyn LLMProvider>> {
    let effective_model = ava_config::model_catalog::registry::registry()
        .normalize(model)
        .unwrap_or_else(|| model.to_string());
    // ... use effective_model instead of model
}
```

Don't error on unknown models — pass through as-is.

### Task 3e: Generate fallback_catalog from registry

Replace the hardcoded `fallback_catalog()` to delegate to the registry:
```rust
pub fn fallback_catalog() -> ModelCatalog {
    let reg = super::registry::registry();
    let mut providers: HashMap<String, Vec<CatalogModel>> = HashMap::new();
    for model in &reg.models {
        providers.entry(model.provider.clone()).or_default().push(CatalogModel {
            id: model.id.clone(),
            name: model.name.clone(),
            provider_id: model.provider.clone(),
            tool_call: model.capabilities.tool_call,
            cost_input: Some(model.cost.input_per_million),
            cost_output: Some(model.cost.output_per_million),
            context_window: Some(model.limits.context_window),
            max_output: model.limits.max_output,
        });
    }
    ModelCatalog { providers, fetched_at: 0 }
}
```

### Task 3f: Registry tests

```rust
#[test]
fn registry_loads_successfully() {
    let reg = ModelRegistry::load();
    assert!(!reg.models.is_empty());
}

#[test]
fn registry_find_by_id() {
    let reg = ModelRegistry::load();
    let m = reg.find("claude-opus-4.6").unwrap();
    assert_eq!(m.provider, "anthropic");
    assert!(m.capabilities.tool_call);
}

#[test]
fn registry_find_by_alias() {
    let reg = ModelRegistry::load();
    assert!(reg.find("opus").is_some());
    assert!(reg.find("sonnet").is_some());
}

#[test]
fn normalize_aliases() {
    let reg = ModelRegistry::load();
    assert_eq!(reg.normalize("opus"), Some("claude-opus-4.6".to_string()));
}

#[test]
fn normalize_fuzzy() {
    let reg = ModelRegistry::load();
    assert_eq!(reg.normalize("gpt4o"), Some("gpt-4o".to_string()));
    assert_eq!(reg.normalize("claude-opus-4-6"), Some("claude-opus-4.6".to_string()));
}

#[test]
fn normalize_unknown_returns_none() {
    let reg = ModelRegistry::load();
    assert_eq!(reg.normalize("totally-unknown"), None);
}

#[test]
fn pricing_returns_correct_values() {
    let reg = ModelRegistry::load();
    let (inp, out) = reg.pricing("claude-opus-4.6").unwrap();
    assert!(inp > 0.0);
    assert!(out > 0.0);
}

#[test]
fn coding_plan_models_are_free() {
    let reg = ModelRegistry::load();
    for provider in ["zai-coding-plan", "zhipuai-coding-plan", "kimi-for-coding", "minimax-coding-plan", "copilot"] {
        for m in reg.models_for_provider(provider) {
            assert_eq!(m.cost.input_per_million, 0.0, "Expected free for {}/{}", provider, m.id);
            assert_eq!(m.cost.output_per_million, 0.0, "Expected free for {}/{}", provider, m.id);
        }
    }
}
```

**Before proceeding to Phase 4, invoke the Code Reviewer sub-agent to verify the registry JSON is valid, pricing delegation works, normalization handles edge cases, and fallback catalog is correctly generated.**

---

## Phase 4: Rich Streaming with StreamChunk

### Task 4a: Define StreamChunk type

Add to `crates/ava-types/src/lib.rs` (or a new `streaming.rs` if lib.rs is large):

```rust
/// A chunk from a streaming LLM response.
#[derive(Debug, Clone, Default)]
pub struct StreamChunk {
    /// Text content delta.
    pub content: Option<String>,
    /// Tool call fragment being assembled incrementally.
    pub tool_call: Option<StreamToolCall>,
    /// Token usage metadata (typically only in the final chunk).
    pub usage: Option<TokenUsage>,
    /// Thinking/reasoning content delta.
    pub thinking: Option<String>,
    /// Whether this is the final chunk.
    pub done: bool,
}

/// A partial tool call from streaming chunks.
#[derive(Debug, Clone)]
pub struct StreamToolCall {
    /// Tool call index (for parallel tool calls).
    pub index: usize,
    /// Tool call ID (may arrive in first chunk only).
    pub id: Option<String>,
    /// Tool/function name (may arrive in first chunk only).
    pub name: Option<String>,
    /// Incremental JSON arguments fragment.
    pub arguments_delta: Option<String>,
}

impl StreamChunk {
    pub fn text(s: impl Into<String>) -> Self {
        Self { content: Some(s.into()), ..Default::default() }
    }
    pub fn finished() -> Self {
        Self { done: true, ..Default::default() }
    }
    pub fn with_usage(usage: TokenUsage) -> Self {
        Self { usage: Some(usage), done: true, ..Default::default() }
    }
    pub fn text_content(&self) -> Option<&str> {
        self.content.as_deref()
    }
}
```

### Task 4b: Update LLMProvider trait

In `crates/ava-llm/src/provider.rs`, change:

```rust
// OLD
async fn generate_stream(&self, messages: &[Message])
    -> Result<Pin<Box<dyn Stream<Item = String> + Send>>>;

// NEW
async fn generate_stream(&self, messages: &[Message])
    -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>>;
```

### Task 4c: Update ALL provider implementations

Update `generate_stream()` in every provider to yield `StreamChunk`:

**Anthropic** (`anthropic.rs`):
- `content_block_delta` → `StreamChunk::text(delta)`
- `message_delta` with usage → `StreamChunk::with_usage(usage)`
- `content_block_start` for tool use → `StreamChunk { tool_call: Some(...) }`
- Thinking blocks → `StreamChunk { thinking: Some(text) }`

**OpenAI** (`openai.rs`):
- `choices[0].delta.content` → `StreamChunk::text(delta)`
- `choices[0].delta.tool_calls` → `StreamChunk { tool_call: Some(...) }`
- Final `usage` → `StreamChunk::with_usage(usage)`
- `choices[0].delta.reasoning_content` → `StreamChunk { thinking: Some(text) }`

**Gemini** (`gemini.rs`):
- `candidates[0].content.parts` → appropriate `StreamChunk`

**OpenRouter** (`openrouter.rs`):
- Same as OpenAI (OpenAI-compatible)

**Ollama** (`ollama.rs`):
- `message.content` → `StreamChunk::text(delta)`
- `done: true` → `StreamChunk::finished()`

**Copilot** (`copilot.rs`):
- Same as OpenAI (OpenAI-compatible via Copilot proxy)

**Mock** (`mock.rs`):
- Yield `StreamChunk::text(word)` for each word

### Task 4d: Update consumers

**Agent loop** (`crates/ava-agent/src/loop.rs`):
- Accumulate `.content` deltas into response text
- Accumulate `.tool_call` fragments into complete tool calls
- Extract `.usage` from final chunk for cost tracking
- Store `.thinking` content

**TUI** (`crates/ava-tui/src/app/`):
- Extract `.content` from `StreamChunk` for display (was plain `String`)
- Optionally show thinking indicator when `.thinking` is present
- Update cost display when `.usage` arrives

### Task 4e: Backward compatibility adapter

```rust
/// Convert StreamChunk stream to plain String stream (lossy — drops tool calls, usage, thinking).
pub fn text_only_stream(
    stream: Pin<Box<dyn Stream<Item = StreamChunk> + Send>>,
) -> Pin<Box<dyn Stream<Item = String> + Send>> {
    Box::pin(stream.filter_map(|chunk| async move { chunk.content }))
}
```

### Task 4f: StreamChunk tests

```rust
#[test]
fn stream_chunk_text_helper() {
    let chunk = StreamChunk::text("hello");
    assert_eq!(chunk.text_content(), Some("hello"));
    assert!(!chunk.done);
}

#[test]
fn stream_chunk_finished() {
    let chunk = StreamChunk::finished();
    assert!(chunk.done);
    assert!(chunk.content.is_none());
}

#[test]
fn stream_chunk_with_usage() {
    let usage = TokenUsage { input_tokens: 100, output_tokens: 50, ..Default::default() };
    let chunk = StreamChunk::with_usage(usage);
    assert!(chunk.done);
    assert!(chunk.usage.is_some());
}
```

**Before proceeding to Phase 5, invoke the Code Reviewer sub-agent to verify all 7 providers yield StreamChunk, all consumers handle it correctly, and no compile errors remain.**

---

## Phase 5: Final Verification

```bash
cargo test --workspace
cargo clippy --workspace
```

All must pass with zero warnings.

**Invoke the Code Reviewer sub-agent for a FINAL review of ALL changes across all 4 phases. Verify:**

**Retry & Circuit Breaker (Phase 1-2):**
1. Jitter ±20% applied to all retry delays
2. Jitter tests confirm non-deterministic delays within bounds
3. Circuit breaker wired into anthropic, openai, gemini, openrouter, copilot
4. Circuit breaker NOT added to ollama or mock
5. Existing retry behavior preserved

**Model Registry (Phase 3):**
6. `registry.json` embedded via `include_str!` with all known models
7. Aliases resolve correctly (e.g., "opus" → "claude-opus-4.6")
8. Fuzzy normalization works (e.g., "gpt4o" → "gpt-4o")
9. `model_pricing_usd_per_million()` delegates to registry
10. `fallback_catalog()` generated from registry (single source of truth)
11. Coding plan + copilot models are $0

**Rich Streaming (Phase 4):**
12. `StreamChunk` defined in `ava-types` with content, tool_call, usage, thinking, done
13. `generate_stream()` returns `Stream<Item = StreamChunk>` on all 7 providers
14. Agent loop accumulates tool calls from stream fragments
15. TUI renders streaming text correctly (no regression)
16. Usage/token data available from streaming responses

**General:**
17. No clippy warnings
18. All tests pass
19. No breaking changes to external API (internal trait change is OK)

---

## Files Modified (Expected)

| File | Change |
|------|--------|
| `crates/ava-llm/Cargo.toml` | Add `rand` dependency (if not present) |
| `crates/ava-llm/src/retry.rs` | Add jitter to `should_retry()`, tests |
| `crates/ava-llm/src/provider.rs` | Change `generate_stream` return type to `StreamChunk` |
| `crates/ava-llm/src/providers/anthropic.rs` | Circuit breaker + StreamChunk |
| `crates/ava-llm/src/providers/openai.rs` | Circuit breaker + StreamChunk |
| `crates/ava-llm/src/providers/gemini.rs` | Circuit breaker + StreamChunk |
| `crates/ava-llm/src/providers/openrouter.rs` | Circuit breaker + StreamChunk |
| `crates/ava-llm/src/providers/copilot.rs` | Circuit breaker + StreamChunk |
| `crates/ava-llm/src/providers/ollama.rs` | StreamChunk only (no circuit breaker) |
| `crates/ava-llm/src/providers/mock.rs` | StreamChunk only |
| `crates/ava-llm/src/providers/common/` | Delegate pricing to registry |
| `crates/ava-llm/src/providers/mod.rs` | Model name normalization |
| `crates/ava-types/src/lib.rs` | Add `StreamChunk`, `StreamToolCall` |
| `crates/ava-config/src/model_catalog/registry.json` | **NEW** — compiled-in model metadata |
| `crates/ava-config/src/model_catalog/registry.rs` | **NEW** — registry module |
| `crates/ava-config/src/model_catalog/mod.rs` | Register registry module |
| `crates/ava-config/src/model_catalog/fallback.rs` | Delegate to registry |
| `crates/ava-agent/src/loop.rs` | Consume `StreamChunk` |
| `crates/ava-tui/src/app/` | Render from `StreamChunk` |

## Acceptance Criteria

- [ ] `cargo test --workspace` passes
- [ ] `cargo clippy --workspace` clean
- [ ] Retry delays have ±20% jitter (verified by test)
- [ ] Circuit breaker wired into 5 remote API providers
- [ ] Ollama/mock have no circuit breaker
- [ ] `registry.json` embedded at compile time with all known models
- [ ] Model aliases resolve correctly
- [ ] Fuzzy normalization works
- [ ] `fallback_catalog()` generated from registry (no duplication)
- [ ] `generate_stream()` returns `Stream<Item = StreamChunk>`
- [ ] All 7 providers yield rich StreamChunk
- [ ] Agent loop accumulates tool calls + usage from stream
- [ ] TUI renders streaming text correctly (no regression)
