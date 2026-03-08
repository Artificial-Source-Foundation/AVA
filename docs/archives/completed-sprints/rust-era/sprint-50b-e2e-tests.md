# Sprint 50b: Headless E2E Test Suite

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in "Key Files to Read"
2. Read `CLAUDE.md` for conventions
3. Enter plan mode and produce a detailed implementation plan
4. Get the plan confirmed before proceeding

## Goal

Create an end-to-end test suite that runs real agent loops against a live provider, validating every core tool and feature works in the full pipeline.

## Key Files to Read

```
CLAUDE.md
crates/ava-tui/src/headless.rs           # Headless mode — run_single_agent, run_multi_agent
crates/ava-tui/src/config/cli.rs         # CliArgs
crates/ava-agent/src/stack.rs            # AgentStack, AgentStackConfig, AgentRunResult
crates/ava-agent/src/loop.rs             # AgentEvent variants
crates/ava-agent/tests/e2e_test.rs       # Existing E2E test (if any)
crates/ava-tools/src/core/mod.rs         # All registered tools
crates/ava-session/src/lib.rs            # SessionManager
crates/ava-memory/src/lib.rs             # MemorySystem
```

## Implementation

### File: `crates/ava-tui/tests/e2e_headless.rs` (NEW)

Each test creates an `AgentStack` with a real provider, runs a goal, and validates the result.

**Setup helper:**
```rust
async fn create_test_stack(temp_dir: &Path) -> AgentStack {
    AgentStack::new(AgentStackConfig {
        data_dir: temp_dir.to_path_buf(),
        provider: Some("openrouter".to_string()),
        model: Some("anthropic/claude-haiku-4-5-20251001".to_string()),
        max_turns: 5,
        yolo: true,  // Skip approval in tests
        ..Default::default()
    }).await.unwrap()
}
```

**Skip guard:**
```rust
fn require_api_key() -> String {
    std::env::var("OPENROUTER_API_KEY")
        .expect("Skipping E2E test: OPENROUTER_API_KEY not set")
}
```

### Test Cases

| Test | Goal | Validates | Assert |
|------|------|-----------|--------|
| `e2e_simple_question` | "What is 2+2? Answer with just the number." | Basic agent loop | Result contains "4" |
| `e2e_read_file` | "Read the file Cargo.toml in the current directory and tell me the package name" | Read tool | Result contains a package name |
| `e2e_write_file` | "Create a file at /tmp/ava-e2e-{uuid}.txt containing 'hello from ava'" | Write tool | File exists with content |
| `e2e_edit_file` | "Create /tmp/ava-e2e-edit-{uuid}.txt with 'foo', then edit it to say 'bar'" | Write + Edit | File contains "bar" |
| `e2e_glob` | "List all .toml files in the crates/ directory" | Glob tool | Result mentions "Cargo.toml" |
| `e2e_grep` | "Search for 'AgentStack' in crates/ava-agent/src/" | Grep tool | Result mentions "stack.rs" |
| `e2e_bash` | "Run the command: echo 'ava-e2e-test'" | Bash tool | Result contains "ava-e2e-test" |
| `e2e_multi_tool` | "Read Cargo.toml, then grep for 'ava' in it" | Multiple tools in sequence | Completes successfully |
| `e2e_memory` | "Remember that ava_test_key_xyz = 'test_value_123', then recall ava_test_key_xyz" | Memory tools | Result contains "test_value_123" |
| `e2e_session_saved` | Run agent, then check session exists | Session persistence | `session_manager.list()` has entry |
| `e2e_cost_tracking` | Run agent, check TokenUsage events | Cost tracker | At least one TokenUsage event emitted |
| `e2e_completion` | "Say hello" | Natural completion (no tool calls) | Completes with success=true |

### Test Configuration

- **Provider**: OpenRouter with `anthropic/claude-haiku-4-5-20251001` (fast, cheap)
- **Max turns**: 5 per test (prevent runaway)
- **Yolo mode**: true (skip tool approval)
- **Temp directory**: `tempdir::TempDir` per test (auto-cleanup)
- **Timeout**: 60s per test via `#[tokio::test]` + `tokio::time::timeout`
- **Skip**: Tests return early if `OPENROUTER_API_KEY` not set

### Acceptance Criteria

- All 12 tests pass against live OpenRouter
- Tests skip gracefully without API key (not fail)
- No test pollution between tests (isolated temp dirs)
- Each test completes in < 60s
- Cleanup: temp files removed after tests

## Constraints

- **Rust only**
- `cargo test --workspace` — all existing tests still pass
- `cargo clippy --workspace` — no warnings
- E2E tests MUST skip (not fail) without API key
- Keep tests simple and deterministic where possible
- Don't assert exact LLM output — assert structural properties (file exists, contains substring, event emitted)

## Validation

```bash
cargo test --workspace
cargo clippy --workspace

# Run E2E tests (requires API key)
OPENROUTER_API_KEY=sk-... cargo test -p ava-tui --test e2e_headless -- --nocapture --test-threads=1
```
