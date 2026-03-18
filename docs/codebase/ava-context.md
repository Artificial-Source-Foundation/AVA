# ava-context

> Token tracking and context condensation for LLM conversations.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `ContextManager` | Orchestrates message storage, tracking, and compaction |
| `ContextManager::new(token_limit)` | Create with sliding window condenser |
| `ContextManager::new_with_condenser(config, hybrid)` | Create with custom condenser |
| `ContextManager::load_history(messages)` | Populate with existing messages |
| `ContextManager::add_message(msg)` | Append and track token count |
| `ContextManager::add_tool_result(result)` | Convert ToolResult to message |
| `ContextManager::get_messages()` | Access message slice |
| `ContextManager::token_count()` | Current token estimate |
| `ContextManager::should_compact()` | Check if over threshold |
| `ContextManager::try_prune()` | Lightweight tool output pruning |
| `ContextManager::compact()` | Sync compaction (sliding window fallback) |
| `ContextManager::compact_async()` | Async hybrid compaction |
| `ContextManager::last_summary()` | Get latest condensation summary |
| `ContextManager::get_system_message()` | Find system prompt |
| `TokenTracker` | Tracks cumulative token counts |
| `TokenTracker::new(max_tokens)` | Constructor |
| `TokenTracker::add_message(msg)` | Add single message |
| `TokenTracker::add_messages(msgs)` | Add multiple messages |
| `TokenTracker::is_over_limit()` | Check budget |
| `TokenTracker::remaining()` | Tokens left |
| `TokenTracker::reset()` | Clear counter |
| `estimate_tokens(text)` | Word-based token estimation (~1.3 tokens/word) |
| `estimate_tokens_for_message(msg)` | Message with overhead |
| `Condenser` | Sync multi-strategy condenser |
| `Condenser::new(config, strategies)` | Constructor |
| `Condenser::condense(messages)` | Apply strategies until under budget |
| `HybridCondenser` | 3-stage pipeline (sync → async → fallback) |
| `HybridCondenser::new(...)` | Constructor |
| `HybridCondenser::condense(messages)` | Async condensation |
| `HybridCondenser::set_previous_summary(...)` | Enable iterative compaction |
| `create_condenser(max_tokens)` | Factory for sync condenser |
| `create_hybrid_condenser(config, summarizer)` | Factory for hybrid condenser |
| `create_hybrid_condenser_with_relevance(config, summarizer, scores)` | With relevance strategy |
| `CondenserConfig` | Configuration: max_tokens, target_tokens, batch sizes, thresholds |
| `FocusChain` | Tracks recently accessed files |
| `FocusChain::record_access(path, kind)` | Log file read/write/edit |
| `FocusChain::get_focused()` | Most recent first |
| `FocusChain::context_hint()` | Formatted context string |
| `prune_old_tool_outputs(messages, protected_tokens)` | Replace old tool output with summary |
| `CondensationStrategy` trait | Sync: `name()`, `condense(messages, max_tokens)` |
| `AsyncCondensationStrategy` trait | Async variant with `set_previous_summary()` |
| `Summarizer` trait | For LLM-based summarization |
| `SlidingWindowStrategy` | Keeps newest messages within budget |
| `ToolTruncationStrategy` | Truncates long tool results |
| `SummarizationStrategy` | LLM/heuristic summarization |
| `RelevanceStrategy` | PageRank-based message scoring |
| `AmortizedForgettingStrategy` | Keep first N + last M messages |
| `ObservationMaskingStrategy` | Masks old tool results with [MASKED] |
| `ContextError` | Enum: Condensation, TokenBudgetExceeded |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Public re-exports |
| `manager.rs` | ContextManager with sync/async compaction |
| `token_tracker.rs` | TokenTracker and estimation functions |
| `condenser.rs` | Condenser, HybridCondenser, factory functions |
| `focus.rs` | FocusChain for file access tracking |
| `pruner.rs` | Lightweight tool output pruning |
| `types.rs` | ContextChunk, CondensationResult, CondenserConfig |
| `error.rs` | ContextError enum, thiserror definitions |
| `strategies/mod.rs` | Trait definitions, strategy re-exports |
| `strategies/sliding_window.rs` | SlidingWindowStrategy implementation |
| `strategies/tool_truncation.rs` | ToolTruncationStrategy implementation |
| `strategies/summarization.rs` | SummarizationStrategy with heuristic fallback |
| `strategies/relevance.rs` | RelevanceStrategy with PageRank scoring |
| `strategies/amortized_forgetting.rs` | AmortizedForgettingStrategy implementation |
| `strategies/observation_masking.rs` | ObservationMaskingStrategy implementation |
| `tests/manager.rs` | Integration tests |

## Dependencies

Uses: ava-types
Used by: ava-agent, ava-praxis, ava-tui

## Key Patterns

- Strategy pattern with trait objects (`Box<dyn CondensationStrategy>`)
- 3-stage pipeline: sync (tool truncation) → async (LLM summarization) → fallback (sliding window)
- Token estimation uses word count × 4/3 approximation
- `async_trait` crate for async traits
- `thiserror` for error definitions with automatic `From` impls to `AvaError`
- Tool call/result pairing preserved during condensation
- Iterative summarization feeds previous summary into next condensation
- Focus chain auto-prunes entries older than 30 minutes or over 50 entries
