# Sprint 50d: Performance Regression Check

## IMPORTANT: Start in Plan Mode

**Before writing ANY code**, you MUST:

1. Read ALL files listed in "Key Files to Read"
2. Read `CLAUDE.md` for conventions
3. Enter plan mode and produce a detailed implementation plan
4. Get the plan confirmed before proceeding

## Goal

Verify performance hasn't degraded across 35+ sprints. Measure key metrics, compare against targets, fix any regressions. Document results.

## Key Files to Read

```
CLAUDE.md
docs/development/benchmarks/              # Existing benchmark data (if any)
crates/ava-tui/src/app.rs                 # App startup path
crates/ava-agent/src/stack.rs             # AgentStack::new() — startup cost
crates/ava-codebase/src/indexer.rs        # index_project() — indexing time
crates/ava-session/src/lib.rs             # SessionManager — query performance
crates/ava-llm/src/pool.rs               # ConnectionPool — pre-warming
Cargo.toml                                # Workspace deps, features
```

## Story 1: Performance Benchmarks

Create a benchmark script that measures key metrics.

**Implementation:**
- File: `crates/ava-tui/tests/bench_performance.rs` (NEW)
- Or a script: `scripts/benchmark.sh` (NEW)

### Metrics to Measure

| Metric | Target | How to Measure |
|--------|--------|---------------|
| Binary size (release) | < 25MB | `cargo build --release && ls -la target/release/ava` |
| `--help` latency | < 200ms | `time ./target/release/ava --help` (10 runs, median) |
| AgentStack::new() time | < 500ms | Instrument with `Instant::now()` in test |
| Codebase indexing (this repo) | < 5s | `index_project()` timed in test |
| Session create + list (100) | < 500ms | Create 100 sessions, time `list()` |
| Memory baseline (idle) | < 50MB | `/proc/self/status` VmRSS after AgentStack::new() |
| Tool registry setup | < 10ms | Time `build_tool_registry()` + all register calls |

### Implementation

For each metric:
1. Measure it
2. Assert it meets the target
3. Print the result for documentation

```rust
#[tokio::test]
async fn bench_agent_stack_startup() {
    let temp = tempdir::TempDir::new("ava-bench").unwrap();
    let start = std::time::Instant::now();
    let _stack = AgentStack::new(AgentStackConfig {
        data_dir: temp.path().to_path_buf(),
        ..Default::default()
    }).await.unwrap();
    let elapsed = start.elapsed();
    println!("AgentStack::new(): {:?}", elapsed);
    assert!(elapsed.as_millis() < 500, "AgentStack startup took {}ms", elapsed.as_millis());
}
```

### Acceptance Criteria

- All metrics measured and printed
- All targets met (or regressions documented with explanation)
- Results saved to `docs/development/benchmarks/benchmark-sprint-50.md`

## Story 2: Release Build Verification

Build a release binary and verify it works end-to-end.

**Checks:**

| Check | Command | Expected |
|-------|---------|----------|
| Release builds | `cargo build --release` | No errors |
| Help works | `./target/release/ava --help` | Shows help text |
| Version info | Check binary for version string | Correct |
| No debug assertions | Run with invalid input | No panics, clean error |
| Clean environment | Delete ~/.ava, run ava | Creates dir, shows "no provider" error |
| Binary size | `ls -lh target/release/ava` | < 25MB |

**Implementation:**
- Add test or script that performs these checks
- If release build has issues, fix them

### Acceptance Criteria

- Release binary builds without errors
- All checks pass
- Binary size documented
- Works on first run with no prior config

## Constraints

- **Rust only**
- `cargo test --workspace` — all tests pass
- `cargo clippy --workspace` — no warnings
- **NO new features** — only measurement and fixes
- Performance tests should be fast (< 30s total)
- Don't break existing tests

## Validation

```bash
cargo test --workspace
cargo clippy --workspace

# Release build
cargo build --release
ls -lh target/release/ava
time ./target/release/ava --help

# Performance tests
cargo test -p ava-tui --test bench_performance -- --nocapture
```
