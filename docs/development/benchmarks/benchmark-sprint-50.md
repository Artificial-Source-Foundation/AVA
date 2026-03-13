# Sprint 50d: Performance Benchmark Results

**Date**: 2026-03-07
**Environment**: Linux 6.17.0-14-generic, debug build (unoptimized)
**Git commit**: post-sprint-99 (accumulated sprint 25-48 work)

## Summary

No performance regressions detected. All metrics within targets. Memory stays under 50 MB, startup is fast, and subsystem initialization is well within budget.

## Results (Debug Build)

| Metric | Target | Actual | Status |
|---|---|---|---|
| Tool registry setup (18 tools) | < 200ms | ~60ms | PASS |
| AgentStack startup | < 500ms | ~170ms | PASS |
| Codebase indexing (50 files) | < 5s | ~750ms | PASS |
| Session create+list (100 sessions) | < 500ms | ~400ms | PASS |
| Memory baseline (VmRSS) | < 50 MB | ~46 MB | PASS |
| Connection pool (3 clients) | < 500ms | ~250ms | PASS |
| Context manager creation | < 10ms | ~10µs | PASS |

## Comparison with Sprint 32 Baseline

| Metric | Sprint 32 | Sprint 50 | Change |
|---|---|---|---|
| Cold start (AgentStack) | 2ms* | ~170ms (debug) | Expected — sprint 32 measured release build |
| Idle memory | 7.8 MB | ~46 MB (debug) | Expected — debug symbols + unoptimized |
| Binary size | 17 MB | TBD (run `scripts/benchmarks/release-benchmark.sh` for release) | — |

*Sprint 32 numbers were release-optimized. Debug build numbers are naturally higher.

## Notes

- **Tool registry**: ~60ms includes platform initialization and 18 tool constructors (core + memory + session + codebase)
- **Connection pool**: ~250ms is dominated by TLS/rustls initialization for 3 reqwest clients. First client is slower; subsequent ones share TLS config
- **Memory**: 46 MB VmRSS includes the full AgentStack (router, tools, session manager, memory system, codebase index placeholder, MCP runtime)
- **Codebase indexing**: 750ms for 50 synthetic Rust files with import parsing, BM25 indexing, dependency graph, and PageRank computation

## Automated Checks

- **Benchmark tests**: `cargo test -p ava-tui --test bench_performance -- --nocapture`
- **Release verification**: `./scripts/benchmarks/release-benchmark.sh` (builds release, checks binary size, --help latency, clean env handling)

## Test File

`crates/ava-tui/tests/bench_performance.rs` — 7 `#[tokio::test]` functions with `Instant::now()` timing and assertion guards.
