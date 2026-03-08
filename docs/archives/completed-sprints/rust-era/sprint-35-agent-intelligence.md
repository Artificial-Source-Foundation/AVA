# Sprint 35: Agent Intelligence Mega-Sprint

> Combines Sprints 35 + 36 + 43 (partial) from the roadmap.

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in the "Key Files to Read" section below
2. Read `CLAUDE.md` and `docs/development/roadmap.md` for context
3. Read the existing tests in each crate to understand test patterns
4. Enter plan mode and produce a detailed implementation plan
5. Get the plan confirmed before proceeding
6. Only then start implementing

## Goal

Make AVA's agent loop smarter, more efficient, and more resilient. Three themes:

1. **Smart completion** — agent stops naturally instead of wasting empty turns
2. **Better context** — accurate token counting, chunk-aware truncation, prompt caching
3. **Resilience** — self-correction on errors, circuit breaker for failing providers

## Key Files to Read

```
crates/ava-agent/src/loop.rs          # Agent loop — completion logic lives here
crates/ava-agent/src/stack.rs         # AgentStack — provider + config setup
crates/ava-agent/src/system_prompt.rs # System prompt generation
crates/ava-agent/src/lib.rs           # Module exports
crates/ava-agent/tests/              # Existing tests

crates/ava-llm/src/provider.rs        # LLMProvider trait — add token counting
crates/ava-llm/src/providers/common.rs # send_with_retry — add circuit breaker
crates/ava-llm/src/providers/anthropic.rs
crates/ava-llm/src/providers/openai.rs
crates/ava-llm/src/providers/openrouter.rs
crates/ava-llm/src/retry.rs           # RetryBudget — extend for circuit breaker
crates/ava-llm/src/pool.rs            # ConnectionPool

crates/ava-context/src/manager.rs      # ContextManager — compaction
crates/ava-context/src/strategies.rs   # Condensation strategies
crates/ava-context/src/token_tracker.rs # Token estimation — make accurate
crates/ava-context/src/condenser.rs

crates/ava-types/src/message.rs        # Message type
crates/ava-types/src/error.rs          # AvaError — is_retryable()
```

## Theme 1: Smart Completion (from Sprint 35)

### Story 1.1: Natural completion signal

The agent currently wastes 2-3 empty API calls after finishing because there's no way to signal "I'm done." Fix this with a simple heuristic:

**Rule**: If the model returns non-empty text content AND zero tool calls, the task is complete. Stop the loop.

```rust
// In the agent loop, after getting a response:
if !response.content.trim().is_empty() && response.tool_calls.is_empty() {
    // Model gave a final answer with no tool calls — done
    emit(AgentEvent::Complete { message: response.content });
    break;
}
```

**Edge cases to handle**:
- Model gives text + tool calls → continue (thinking aloud while acting)
- Model gives empty text + tool calls → continue (pure tool execution)
- Model gives empty text + no tool calls → stuck detector handles this (existing)
- Model gives ONLY whitespace → treat as empty, don't stop

**Acceptance criteria**:
- Agent stops immediately after a final text-only response
- No extra empty turns after completion
- Stuck detector still fires for genuinely stuck cases
- All existing agent tests pass
- Add test: `test_natural_completion_on_text_only_response`
- Add test: `test_continues_when_tool_calls_present`

### Story 1.2: System prompt nudge for native tools

Add to the system prompt: prefer native tools over bash equivalents.

```
Prefer native tools (read, write, edit, glob, grep) over bash equivalents. Native tools are faster, sandboxed, and produce structured output.
```

**Acceptance criteria**:
- System prompt includes the nudge
- Existing tests pass

## Theme 2: Better Context (from Sprint 35 + 36)

### Story 2.1: Accurate token counting

Current token counting uses a simple `chars / 4` estimate. Replace with a more accurate method.

**Approach**: Use a proper tokenizer. Options (choose the best fit):
1. `tiktoken-rs` crate — OpenAI's tokenizer (works for GPT models)
2. Simple word-based heuristic: `words * 1.3` (more accurate than chars/4)
3. Provider-specific: let each provider implement `count_tokens(&self, text: &str) -> usize`

**Recommended**: Add a `count_tokens(text: &str) -> usize` function in `ava-context/src/token_tracker.rs` that uses a better heuristic. Don't add heavy dependencies — a word-based approach (`text.split_whitespace().count() * 4 / 3`) is fine and much better than `chars / 4`.

Also update `estimate_tokens_for_message()` to account for message overhead (role, tool metadata).

**Acceptance criteria**:
- Token counting is within 20% of actual for typical code/text
- TokenTracker uses the improved counting
- ContextManager decisions (should_compact) are more accurate
- Add test: `test_token_estimation_accuracy` (compare against known counts)

### Story 2.2: Chunk-aware truncation

When context is truncated, don't cut mid-function or mid-tool-result. Respect boundaries.

**Approach**: In `SlidingWindowStrategy`, when deciding where to cut:
1. Never break a tool_use/tool_result pair — always keep or drop both
2. Prefer cutting at message boundaries (between complete messages)
3. If a tool result is very large (>10KB), truncate the tool result content itself with `[... truncated N chars]` rather than dropping the entire message pair

**Acceptance criteria**:
- Truncation never breaks tool_use/tool_result pairs
- Large tool results are truncated inline rather than pair-dropped
- Messages are cut at boundaries, not mid-message
- Add test: `test_truncation_preserves_tool_pairs`
- Add test: `test_large_tool_result_truncated_inline`

### Story 2.3: Prompt caching hints

For Anthropic models, add cache control hints to reduce cost on repeated context.

**Approach**: When building the API request for Anthropic, mark the system prompt and early conversation history with `cache_control: { type: "ephemeral" }`. This tells the API to cache these blocks.

```rust
// In anthropic.rs, when building the request:
// Add cache_control to system message
// Add cache_control to the first few user messages (stable context)
```

**Acceptance criteria**:
- Anthropic requests include cache_control on system prompt
- Only Anthropic provider is affected (other providers ignore this)
- No behavioral change — just cost optimization
- Existing tests pass

## Theme 3: Resilience (from Sprint 36 + 43)

### Story 3.1: Basic self-correction

When a tool call fails, the agent should acknowledge the error and try a different approach instead of repeating the same failed call.

**Approach**: After a tool execution error, inject a system-level hint into the conversation:

```rust
// After tool error:
let hint = format!(
    "The tool call `{}` failed with: {}. Try a different approach — \
     don't repeat the same call.",
    tool_call.name, error_message
);
// Add as a system/user message before the next LLM call
```

The stuck detector already catches exact-repeat tool calls. This enhancement adds a prompt-level nudge so the model is more likely to self-correct on the first retry.

**Acceptance criteria**:
- Failed tool calls get an error hint injected
- Agent is less likely to repeat the same failing call
- Hint is concise (1-2 sentences), not verbose
- Add test: `test_error_hint_injected_after_tool_failure`

### Story 3.2: Circuit breaker for providers

When a provider fails repeatedly (e.g., rate limited, server errors), stop hammering it. Add a circuit breaker pattern.

**Approach**: Extend `RetryBudget` in `crates/ava-llm/src/retry.rs`:

```rust
pub struct CircuitBreaker {
    failure_count: AtomicU32,
    failure_threshold: u32,     // e.g., 5 consecutive failures
    cooldown: Duration,         // e.g., 30 seconds
    last_failure: Mutex<Option<Instant>>,
    state: AtomicU8,            // Closed (normal), Open (rejecting), HalfOpen (testing)
}

impl CircuitBreaker {
    pub fn record_success(&self);
    pub fn record_failure(&self);
    pub fn is_available(&self) -> bool;  // false when Open
}
```

States:
- **Closed** (normal): requests flow through. After N consecutive failures → Open
- **Open** (circuit tripped): reject immediately with `AvaError::ProviderUnavailable`. After cooldown → HalfOpen
- **HalfOpen** (testing): allow 1 request. Success → Closed. Failure → Open

Integrate into `send_with_retry()` in `common.rs`:
```rust
if !circuit_breaker.is_available() {
    return Err(AvaError::ProviderUnavailable { provider: name });
}
// ... attempt request ...
match result {
    Ok(_) => circuit_breaker.record_success(),
    Err(e) if e.is_retryable() => circuit_breaker.record_failure(),
    _ => {}
}
```

Add `ProviderUnavailable` variant to `AvaError` if it doesn't exist.

**Acceptance criteria**:
- CircuitBreaker with Closed/Open/HalfOpen states
- Opens after 5 consecutive failures
- Cooldown of 30 seconds before HalfOpen
- Integrated into send_with_retry
- Add test: `test_circuit_breaker_opens_after_failures`
- Add test: `test_circuit_breaker_recovers_after_cooldown`
- All existing retry tests pass

## Implementation Order

1. Story 1.1 (natural completion) — quickest win, biggest impact
2. Story 1.2 (system prompt nudge) — trivial
3. Story 2.1 (token counting) — foundation for 2.2
4. Story 2.2 (chunk-aware truncation) — depends on 2.1
5. Story 3.1 (self-correction hints) — standalone
6. Story 3.2 (circuit breaker) — standalone
7. Story 2.3 (prompt caching) — standalone, do last

## Constraints

- **Rust only**
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- Don't break existing agent loop, tool execution, or provider behavior
- Keep changes focused — don't refactor unrelated code
- Add tests for every story

## Validation

```bash
cargo test --workspace
cargo clippy --workspace
cargo test -p ava-agent -- --nocapture
cargo test -p ava-llm -- --nocapture
cargo test -p ava-context -- --nocapture
```
