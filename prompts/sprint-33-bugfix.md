# Sprint 33: Critical Bug Fixes

## Goal

Fix the 4 bugs found during Sprint 32 integration testing. Priority order: Bug 3 (TUI broken) → Bug 2 (large file crash) → Bug 1 (empty turns) → Bug 4 (default config).

## Bug 3: TUI API calls fail with 400 (HIGH)

**File**: `crates/ava-tui/src/state/agent.rs` + `crates/ava-agent/src/loop.rs`

**Symptom**: TUI boots fine, input works, but every API call returns `400 Bad Request`. Headless mode with the same provider/model (`--provider openrouter --model anthropic/claude-sonnet-4`) works perfectly. Both use `AgentStack::run` → `AgentLoop::run_streaming`.

**Investigation steps**:
1. Compare how TUI vs headless constructs the initial message list
2. Check if TUI sends a system message differently or duplicates messages
3. Check if provider/model name is passed correctly in TUI path
4. Add tracing/debug logging to the API request body in the TUI code path
5. Compare the exact JSON request body sent in headless vs TUI mode

**Likely causes**:
- TUI may be adding messages in wrong format (e.g., empty system message, or missing role field)
- Model name may be getting corrupted or not passed through correctly
- Messages may have wrong ordering (tool_result before tool_use)

**Acceptance criteria**:
- TUI can send a message and receive a streaming response
- Same provider/model works in both TUI and headless modes
- `cargo test --workspace` passes

## Bug 2: Large file read causes tool_use/tool_result mismatch (HIGH)

**File**: `crates/ava-agent/src/loop.rs` (message construction, lines ~276-293)

**Symptom**: Reading a 10K-line file causes next API call to fail:
```
Provider 'OpenAI' error: request failed (400 Bad Request):
"tool_use ids were found without tool_result blocks immediately after"
```

**Root cause**: Large tool results break the `tool_use` → `tool_result` message pairing that Anthropic/OpenAI APIs require. Either the context gets truncated mid-pair, or the message construction doesn't properly pair them.

**Fix approach**:
1. **Truncate oversized tool results** — add a `MAX_TOOL_RESULT_SIZE` constant (e.g., 50KB / ~12,500 tokens). If a tool result exceeds this, truncate with a `[truncated — showing first N chars of M total]` suffix
2. **Ensure tool_use/tool_result pairing** — when building the message list, verify every `tool_use` block has a matching `tool_result` immediately after. If context compaction breaks a pair, remove both
3. **Add the truncation in `add_tool_results()`** in `loop.rs` before adding to the message list

**Acceptance criteria**:
- Agent can read a 10K-line file without crashing
- Tool results over 50KB are truncated with a notice
- tool_use/tool_result pairs are always kept together
- `cargo test --workspace` passes

## Bug 1: Empty turn waste (MEDIUM)

**File**: `crates/ava-agent/src/loop.rs`

**Symptom**: After the agent finishes answering, it generates 2-3 empty turns before stuck detector fires. Wastes ~2 API calls per run (2-4 seconds).

**Root cause**: No "natural completion" signal. Model finishes but loop keeps requesting turns.

**Fix approach**: Add a completion heuristic — if the model returns content (non-empty text) with NO tool calls, treat it as a natural completion and stop the loop. This is how Claude Code, Goose, and other agents work.

```rust
// In the agent loop, after getting a response:
if !response.content.is_empty() && response.tool_calls.is_empty() {
    // Model gave a final answer with no more tool calls — done
    break;
}
```

**Edge case**: The model may give intermediate text + tool calls (thinking aloud). Only stop when there are ZERO tool calls AND non-empty content.

**Acceptance criteria**:
- Agent stops immediately after giving a final text response (no extra empty turns)
- Agent continues looping when tool calls are present
- Stuck detector still fires for genuinely stuck cases (empty responses, loops)
- `cargo test --workspace` passes
- Update existing stuck detector tests if needed

## Bug 4: No default config (MEDIUM)

**File**: `crates/ava-tui/src/config/cli.rs` + `crates/ava-config/src/lib.rs`

**Symptom**: Running `ava` without `--provider` and `--model` errors. Must type `--provider openrouter --model anthropic/claude-sonnet-4` every time.

**Fix approach**:
1. Add a config file at `~/.ava/config.yaml` (or `.toml`):
   ```yaml
   provider: openrouter
   model: anthropic/claude-sonnet-4
   ```
2. Load order: config file → environment variables → CLI flags (CLI flags override all)
3. If no config and no CLI flags, show a helpful error: "No provider configured. Set defaults in ~/.ava/config.yaml or use --provider/--model flags."
4. Read config in `crates/ava-config/` and use it in `crates/ava-tui/src/config/cli.rs`

**Acceptance criteria**:
- `ava` works without `--provider`/`--model` if `~/.ava/config.yaml` exists
- CLI flags override config file values
- Helpful error message if neither config nor flags are provided
- Config file is optional — no crash if missing
- `cargo test --workspace` passes

## Bonus: System prompt nudge for native tools

**File**: `crates/ava-agent/src/system_prompt.rs`

**Symptom**: Agent sometimes calls `bash(ls -la)` instead of `glob(*)`.

**Fix**: Add a line to the system prompt:
```
Prefer native tools (read, write, edit, glob, grep) over bash equivalents when possible. Native tools are faster, sandboxed, and produce structured output.
```

## Constraints

- **Rust only**
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- Don't break existing functionality
- Keep changes minimal and focused on the bugs

## Validation

```bash
cargo test --workspace
cargo clippy --workspace

# Bug 3: TUI should work
cargo run --bin ava -- --provider openrouter --model anthropic/claude-sonnet-4
# Type "hello" → should get streaming response

# Bug 2: Large file should not crash
cargo run --bin ava -- "Read the file crates/ava-tui/src/app.rs and summarize it" --headless --yolo --provider openrouter --model anthropic/claude-sonnet-4

# Bug 1: Should stop after final answer (no extra turns)
cargo run --bin ava -- "What is 2+2?" --headless --provider openrouter --model anthropic/claude-sonnet-4

# Bug 4: Should work without flags if config exists
echo "provider: openrouter\nmodel: anthropic/claude-sonnet-4" > ~/.ava/config.yaml
cargo run --bin ava -- "hello" --headless
```
