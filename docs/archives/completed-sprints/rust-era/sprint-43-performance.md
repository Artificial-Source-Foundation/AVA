# Sprint 43: Performance & Resilience

> Combines Sprints 43 + 44 from the roadmap.

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in "Key Files to Read"
2. Read `CLAUDE.md` for conventions
3. Enter plan mode and produce a detailed implementation plan
4. Get the plan confirmed before proceeding

## Goal

Make AVA faster and more resilient. Optimize connection handling, enable background tool execution, and add streaming tool results so the agent can think while tools run.

## Key Files to Read

```
crates/ava-llm/src/pool.rs               # ConnectionPool (RwLock HashMap, per-base-url)
crates/ava-llm/src/retry.rs              # RetryBudget
crates/ava-llm/src/circuit_breaker.rs    # CircuitBreaker (Closed/Open/HalfOpen)
crates/ava-llm/src/providers/common.rs   # send_with_retry()
crates/ava-llm/src/providers/openai.rs   # OpenAI provider
crates/ava-llm/src/providers/anthropic.rs
crates/ava-llm/src/providers/openrouter.rs

crates/ava-agent/src/loop.rs             # Agent loop — tool execution
crates/ava-agent/src/stack.rs            # AgentStack

crates/ava-tools/src/registry.rs         # ToolRegistry::execute()
crates/ava-tools/src/core/bash.rs        # Bash tool (longest running)
crates/ava-tools/src/core/read.rs        # Read tool

crates/ava-tui/src/widgets/token_buffer.rs  # TokenBuffer (60fps)
crates/ava-tui/src/app.rs                   # App event loop
```

## What Already Exists

- **ConnectionPool**: RwLock HashMap, lazy per-base-url client creation, configurable timeouts
- **CircuitBreaker**: 3-state (Closed/Open/HalfOpen), 5-failure threshold, 30s cooldown
- **RetryBudget**: Exponential backoff, is_retryable() check
- **send_with_retry**: Retries 429/5xx with Retry-After support
- **TokenBuffer**: 60fps frame-rate buffered flush

## Theme 1: Connection Optimization

### Story 1.1: Connection Pre-Warming

Pre-create the HTTP client for the configured provider on startup instead of waiting for the first request.

**Implementation:**
- In `AgentStack::new()`, after determining the provider, call `pool.get_client(base_url)` eagerly
- This triggers client creation (TLS handshake, connection pool setup) before the user sends their first message
- Saves ~100-200ms on first request

**Acceptance criteria:**
- Client pre-warmed on startup
- No behavior change for subsequent requests
- Add trace log: "Pre-warmed connection to {base_url}"

### Story 1.2: Request Pipelining

When the agent makes multiple tool calls in sequence, pipeline the responses where possible.

**Implementation:**
- In the agent loop, when multiple tool calls are returned by the LLM, execute them concurrently using `tokio::join!` or `futures::join_all`
- Currently tool calls are sequential in `execute_tool_calls_collect()`
- Independent tools (read + grep) can run in parallel
- Dependent tools (write then read) should stay sequential

**Heuristic for independence:**
- All read-only tools (read, glob, grep, diagnostics, codebase_search) are independent
- Write tools (write, edit, bash, multiedit, apply_patch) are sequential
- Mix: run all reads first (parallel), then writes (sequential)

**Implementation:**
- In `loop.rs`, partition tool calls into read-only and write groups
- Execute read-only group with `join_all`
- Execute write group sequentially
- Collect all results in original order

**Acceptance criteria:**
- Read-only tools execute in parallel
- Write tools execute sequentially
- Results maintain correct order for API response
- Add test: `test_parallel_read_tools`
- Add test: `test_sequential_write_tools`

### Story 1.3: Streaming Cost Tracker

Track and display token usage and cost in real-time during streaming.

**Implementation:**
- In the provider response, parse `usage` field (input_tokens, output_tokens)
- Pipe token counts through AgentEvent to TUI
- Status bar already shows cost — wire it to real data
- Running cost: `input_tokens * input_price + output_tokens * output_price`
- Price table per model (hardcoded initially)

**Acceptance criteria:**
- Token counts updated after each API response
- Cost calculated and shown in status bar
- Accumulates across turns in a session
- Add price table for top 5 models

## Theme 2: Background Tool Execution

### Story 2.1: Async Tool Results

Allow long-running tools (bash, test_runner) to stream their output instead of blocking.

**Implementation:**
- Add `StreamingToolResult` type:
  ```rust
  pub enum ToolOutput {
      Complete(ToolResult),
      Streaming { chunks: Pin<Box<dyn Stream<Item = String> + Send>> },
  }
  ```
- Modify the `Tool` trait to optionally support streaming:
  ```rust
  async fn execute_streaming(&self, args: Value) -> Result<ToolOutput> {
      // Default: wrap execute() in Complete
      Ok(ToolOutput::Complete(self.execute(args).await?))
  }
  ```
- Implement streaming for `BashTool` and `TestRunnerTool`:
  - Spawn subprocess, stream stdout line-by-line
  - Collect final result when process exits

**Acceptance criteria:**
- Bash tool streams output line-by-line
- TUI shows streaming tool output (lines appear as they come)
- Non-streaming tools work unchanged
- Final result still collected for agent context
- Add test

### Story 2.2: Tool Execution Timeout UI

Show a progress indicator when tools are running, with elapsed time.

**Implementation:**
- When a tool starts executing, the TUI shows: `⟳ Executing: bash (3.2s...)`
- Timer updates every second
- If tool exceeds timeout, show warning color
- When tool completes, briefly flash result summary

**Already partially exists** in `AgentActivity::ExecutingTool(String)`. Extend with:
- Start time tracking
- Elapsed time display in status bar

**Acceptance criteria:**
- Elapsed time shown during tool execution
- Updates every second
- Warning style when approaching timeout
- Clean transition on completion

## Theme 3: Provider Resilience

### Story 3.1: Automatic Provider Fallback

When the primary provider fails (circuit breaker opens), automatically try a fallback provider.

**Implementation:**
- Add fallback config to `~/.ava/config.yaml`:
  ```yaml
  provider: openrouter
  model: anthropic/claude-sonnet-4
  fallback:
    provider: openrouter
    model: openai/gpt-4o
  ```
- In `AgentStack`, when primary provider returns `ProviderUnavailable`:
  1. Log warning
  2. Switch to fallback provider
  3. Show status message: "Primary provider unavailable, using fallback: gpt-4o"
  4. Retry the request with fallback

**Acceptance criteria:**
- Fallback configured in config file
- Automatic switch on circuit breaker open
- Status message shown
- Fallback is optional (no crash if not configured)
- Add test

### Story 3.2: Request Deduplication

Prevent duplicate API calls when the user double-sends or the agent retries too quickly.

**Implementation:**
- Add a simple dedup guard in the agent loop:
  ```rust
  let request_hash = hash(messages.last());
  if self.last_request_hash == Some(request_hash) && self.last_request_time.elapsed() < Duration::from_secs(2) {
      tracing::warn!("Duplicate request detected, skipping");
      continue;
  }
  ```
- Only dedup within a 2-second window
- Hash based on last message content

**Acceptance criteria:**
- Rapid duplicate requests are skipped
- Normal sequential requests work fine
- Warning logged when dedup fires
- Add test

## Implementation Order

1. Story 1.1 (connection pre-warming) — trivial, immediate benefit
2. Story 1.3 (cost tracker) — visible, user-facing
3. Story 2.2 (timeout UI) — extends existing activity display
4. Story 1.2 (request pipelining) — moderate complexity, big speedup
5. Story 3.2 (request dedup) — safety measure
6. Story 3.1 (provider fallback) — resilience
7. Story 2.1 (streaming tool results) — most complex, do last

## Constraints

- **Rust only**
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- Don't break existing agent loop or provider behavior
- Streaming tool output is optional (tools that don't support it are unaffected)
- Fallback provider is optional config

## Validation

```bash
cargo test --workspace
cargo clippy --workspace
cargo test -p ava-llm -- --nocapture
cargo test -p ava-agent -- --nocapture
```
