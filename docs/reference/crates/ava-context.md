# ava-context

Context window management and condensation. Tracks token usage, detects when the context window is filling up, and applies a multi-stage condensation pipeline to keep conversations within limits.

## How It Works

### ContextManager (`src/manager.rs`)

Central coordinator that holds the message buffer and decides when/how to compact.

```rust
pub struct ContextManager {
    messages: Vec<Message>,
    token_limit: usize,
    compaction_threshold_pct: f32,  // default 0.8 (80%)
    tracker: TokenTracker,
    condenser: CondenserKind,       // Sync or Hybrid
}
```

| Method | Description |
|--------|-------------|
| `new(token_limit)` | Creates with sync condenser, 80% threshold |
| `new_with_condenser(config, condenser)` | Creates with hybrid condenser |
| `load_history(messages)` | Bulk-loads prior conversation messages |
| `add_message(message)` | Adds a message and tracks tokens |
| `should_compact()` | Returns `true` when token count exceeds 80% of limit |
| `compact()` | Synchronous compaction (sync condenser or sliding window fallback) |
| `compact_async()` | Async compaction using hybrid pipeline |

**File**: `crates/ava-context/src/manager.rs` (lines 1-125)

### Token Tracking (`src/token_tracker.rs`)

`TokenTracker` estimates tokens using a word-based heuristic: approximately 4/3 tokens per word (~1.33). Tracks `current_tokens` and supports `reset()` + `add_messages()` for recalculation after compaction.

### Condensation Pipeline

#### Sync Condenser (`src/condenser.rs`)

`Condenser` chains sync strategies in order, each reducing the message list.

#### Hybrid Condenser (`src/condenser.rs`)

`HybridCondenser` runs a 3-stage pipeline:
1. **Sync strategies** (tool truncation, sliding window) -- fast, always available
2. **Async strategies** (LLM-based summarization) -- slower, needs API access
3. **Fallback** -- if async fails, falls back to sync-only result

Factory functions: `create_condenser()`, `create_hybrid_condenser()`, `create_hybrid_condenser_with_relevance()`.

**File**: `crates/ava-context/src/condenser.rs` (lines 1-315)

### Strategies (`src/strategies/`)

| Strategy | File | Description |
|----------|------|-------------|
| `ToolTruncationStrategy` | `tool_truncation.rs` | Truncates tool result content exceeding `max_chars` |
| `SummarizationStrategy` | `summarization.rs` | LLM or heuristic summarization; partitions messages into system/old/recent batches |
| `RelevanceStrategy` | `relevance.rs` | PageRank-based file scoring with recency bonus |
| `SlidingWindowStrategy` | `sliding_window.rs` | Groups messages into units (assistant + tool pairs), selects newest-first to fit target |

All implement one of two traits:

```rust
pub trait CondensationStrategy: Send + Sync {
    fn condense(&self, messages: &[Message], target_tokens: usize) -> Result<Vec<Message>>;
}

#[async_trait]
pub trait AsyncCondensationStrategy: Send + Sync {
    async fn condense(&self, messages: &[Message], target_tokens: usize) -> Result<Vec<Message>>;
}
```

**File**: `crates/ava-context/src/strategies/mod.rs` (lines 1-42)

### Configuration (`src/types.rs`)

```rust
pub struct CondenserConfig {
    pub max_tokens: usize,
    pub compaction_threshold_pct: f32,
    pub tool_truncation_max_chars: usize,
    pub summarization_prompt: Option<String>,
    pub sliding_window_min_recent: usize,
    pub relevance_damping: f32,
    pub relevance_top_k: usize,
}
```

Also defines `CondensationResult` (messages + metadata) and `ContextChunk`.

## Source Files

| File | Lines | Purpose |
|------|------:|---------|
| `src/manager.rs` | 125 | ContextManager |
| `src/condenser.rs` | 315 | Condenser, HybridCondenser, factories |
| `src/strategies/mod.rs` | 42 | Strategy traits |
| `src/strategies/tool_truncation.rs` | -- | Tool result truncation |
| `src/strategies/summarization.rs` | -- | LLM/heuristic summarization |
| `src/strategies/relevance.rs` | -- | PageRank relevance scoring |
| `src/strategies/sliding_window.rs` | -- | Sliding window selection |
| `src/token_tracker.rs` | -- | Word-based token estimation |
| `src/types.rs` | -- | CondenserConfig, CondensationResult |
