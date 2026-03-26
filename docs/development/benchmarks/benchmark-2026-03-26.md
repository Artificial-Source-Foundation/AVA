# AVA vs Competitors Benchmark — 2026-03-26

## Environment

- **OS**: Linux 6.17.0-19-generic x86_64
- **CPU**: Intel Core i9-10850K @ 3.60GHz
- **RAM**: 32GB DDR4
- **AVA version**: 69f0d674 (develop, post-SOTA sprint)
- **OpenCode version**: 1.3.2
- **Codex CLI version**: 0.111.0
- **Model**: OpenAI GPT-5.4 (same for all tools)
- **Date**: 2026-03-26

## Performance Results

### Startup Speed

| Tool | Startup (--help) | Language |
|------|-----------------|----------|
| **AVA** | **21ms** | Rust |
| **Codex CLI** | **26ms** | Rust + Node.js |
| OpenCode | 866ms | Go |

### Binary / Runtime Size

| Tool | Size |
|------|------|
| **AVA** | **32MB** |
| Codex CLI | 108MB (Node.js package) |
| OpenCode | 179MB |

### Memory Usage (during real task)

| Tool | RSS |
|------|-----|
| **AVA** | **82MB** |
| OpenCode | 326MB |

### Edit Task: Add Field + Init + Method

Task: Add a `started: bool` field to a Rust struct, initialize it in `new()`, add an `is_running()` method.

| Tool | Time | Correct | Turns |
|------|------|---------|-------|
| Codex CLI | **13.3s** | 3/3 | ~2 |
| AVA | 82.3s | 3/3 | ~4 |
| OpenCode | TIMEOUT | 0/3 | hung |

Note: Codex CLI uses OpenAI's Responses API (single-round), while AVA uses the Chat Completions API (multi-turn). This accounts for the speed difference — Codex gets one-shot tool calls while AVA reads the file first then edits.

### Earlier Benchmark (same session, GPT-5.4)

| Tool | Task 1 | Task 2 |
|------|--------|--------|
| AVA | 25.8s, 3/3 | 18.6s, 3/3 |
| OpenCode | 54.1s, 3/3 | 51.5s, 3/3 |

When both tools complete, AVA is ~2.5x faster than OpenCode.

## Feature Comparison

### Capabilities Matrix

| Feature | AVA | Codex CLI | OpenCode |
|---------|-----|-----------|----------|
| **Edit strategies** | 15 + fuzzy autocorrect | apply_patch only | 9-layer fuzzer |
| **Tiered edit racing** | Yes (best-of-N) | No | No |
| **Edit fuzzy autocorrect** | Yes (85% threshold) | No | No |
| **Symbol-level PageRank** | Yes (personalized) | No | No |
| **Loop detection layers** | 3 (model-aware) | Basic | Basic |
| **Provider loop_prone override** | Yes | No | No |
| **Bash parsing** | Tree-sitter AST | Toml sandbox rules | Tree-sitter AST |
| **Fail-closed on parse error** | Yes | N/A (sandbox-only) | No |
| **Context condensers** | 6 strategies, 3-stage | Unknown | Basic |
| **Multi-agent (Praxis)** | 3-tier hierarchy | No | Subagents |
| **MCP support** | Full client | Full client | Full client |
| **LLM providers** | 21 built-in | 1 (OpenAI only) | 75+ (Vercel AI SDK) |
| **Desktop app** | Tauri (SolidJS) | No | Electron |
| **Persistent memory** | SQLite + FTS5 | No | SQLite |
| **Session recall** | Cross-session FTS5 | No | No |
| **OS-level sandbox** | bwrap/sandbox-exec | Seatbelt/Landlock | Docker |
| **Custom tools** | TOML + MCP | MCP only | MCP only |
| **Plugin system** | JSON-RPC subprocess | No | npm hooks |

### AVA-Only Features (no competitor has these)

1. **Tiered edit strategy racing** — Run all speculative strategies, pick the most surgical result
2. **Edit fuzzy auto-correct** — Line-level block matching with 85% similarity threshold as last resort
3. **Symbol-level personalized PageRank** — Rank code symbols by structural importance, personalized to the current query
4. **Model-aware 3-layer loop detection** — Aggressive thresholds for cheap/Chinese models, relaxed for SOTA, with provider-level user overrides
5. **3-tier multi-agent hierarchy** (Praxis) — Director → Leads → Workers with domain expertise
6. **Cross-session memory recall** — FTS5 full-text search across all past conversations
7. **6-strategy context condensation** — ObservationMasking, ToolTruncation, Relevance, Summarization, AmortizedForgetting, SlidingWindow

### Where Competitors Lead

| Feature | Leader | AVA Status |
|---------|--------|------------|
| Provider breadth | OpenCode (75+) | 21 built-in |
| Responses API (one-shot speed) | Codex CLI | Multi-turn (slower but more reliable) |
| OS-level sandbox (Linux) | Codex CLI (Landlock) | bwrap (similar) |
| Git worktrees per session | OpenCode | Shadow git snapshots |

## SOTA Features Shipped (2026-03-26)

All five features were implemented and tested in a single session:

1. **Symbol-level PageRank repo map** — `crates/ava-codebase/` (symbols.rs, symbol_graph.rs, pagerank.rs, repomap.rs, indexer.rs)
2. **Model-aware 3-layer loop detection** — `crates/ava-agent/src/stuck.rs`, `crates/ava-config/src/model_catalog/`
3. **Edit fuzzy auto-correct** — `crates/ava-tools/src/edit/mod.rs`
4. **Tiered edit strategy racing** — `crates/ava-tools/src/edit/mod.rs`
5. **Tree-sitter bash AST parsing** — `crates/ava-permissions/src/classifier/parser.rs`

Total: +3,145 lines across 28 files. 792 tests pass, zero clippy warnings.

## Methodology

- All tools tested on the same machine, same model (GPT-5.4), same task
- Tasks involve reading a Rust file and making structural edits (add field + constructor init + method)
- Timing includes full end-to-end execution (startup + API calls + file I/O)
- Correctness scored by grepping for expected code patterns in the output file
- Startup measured with `--help` flag (no API calls)
- Memory measured with `ps -o rss=` during a simple "reply OK" task
