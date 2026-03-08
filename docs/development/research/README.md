# Research Documentation

> Consolidated research on AI coding assistants and competitive analysis for AVA.

---

## Table of Contents

### Overview Documents

| Document | Description |
|----------|-------------|
| [competitive-analysis-2026-03.md](competitive-analysis-2026-03.md) | Comprehensive 8-tool comparison matrix |
| [rust-competitive-analysis-2026-03.md](rust-competitive-analysis-2026-03.md) | Rust-based competitors analysis |
| [SOTA-gap-analysis-2026-03.md](SOTA-gap-analysis-2026-03.md) | State-of-the-art gap analysis |
| [tui-comparison-matrix.md](tui-comparison-matrix.md) | TUI implementation comparison |
| [tui-implementation-research.md](tui-implementation-research.md) | TUI technical research |

### Strategic Vision

| Document | Description |
|----------|-------------|
| [AVA-3.0-RUST-VISION.md](AVA-3.0-RUST-VISION.md) | Vision for AVA 3.0 Rust architecture |
| [pi-coding-agent.md](pi-coding-agent.md) | PI Coding Agent architecture analysis |

### Competitor Deep Dives

Individual competitor analyses:

| Competitor | Language | Stars | Key Innovation |
|------------|----------|-------|----------------|
| [aider.md](competitors/aider.md) | Python | 41k | PageRank repo map, 12-strategy edit cascade |
| [cline.md](competitors/cline.md) | TypeScript | 58k | Two-phase tool execution, shadow git |
| [codex-cli.md](competitors/codex-cli.md) | TypeScript | 20k | Sandboxed execution, React Ink TUI |
| [continue.md](competitors/continue.md) | TypeScript | 25k | Rich context providers, autocomplete |
| [gemini-cli.md](competitors/gemini-cli.md) | TypeScript | 15k | A2A protocol, policy engine |
| [goose.md](competitors/goose.md) | Rust | 10k | Rust core, Go extensions |
| [opencode.md](competitors/opencode.md) | TypeScript/Bun | 115k | 9-layer edit fuzzer, tree-sitter bash |
| [openhands.md](competitors/openhands.md) | Python | 40k | Micro-agent architecture |
| [pi-mono.md](competitors/pi-mono.md) | - | 5k | Minimalism, single-file focus |
| [plandex.md](competitors/plandex.md) | Go | 8k | Plan-then-execute, batch operations |
| [swe-agent.md](competitors/swe-agent.md) | Python | 15k | ReAct prompting, research-backed |
| [zed.md](competitors/zed.md) | Rust | 50k | AI-native editor, CRDT collaboration |

---

## Consolidation Notes

This research directory was consolidated in March 2026 from multiple sources:
- `audits/` — Competitor audit files (high-level summaries)
- `backend-analysis/` — Detailed architecture analyses
- `{cline,gemini-cli,opencode}/` — Competitor-specific deep dives

All content has been merged into `competitors/{name}.md` files preserving the most valuable insights while removing redundancy.

---

## How to Use This Research

1. **Start with overview documents** — For strategic context
2. **Read specific competitor analyses** — For implementation details
3. **Focus on "What AVA Can Learn" sections** — For actionable insights
4. **Check file references** — For source code locations

---

*Last updated: 2026-03-07*
