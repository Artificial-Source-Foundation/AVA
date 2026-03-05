# Sprint Plan v2: Cut & Deepen (Hybrid Rust + TS)

> Replaces the 41-sprint full-Rust rewrite with a 10-sprint hybrid approach.
> Rust for compute hotpaths. TypeScript for orchestration. Delete everything else.

## Epics

| Epic | Sprints | Goal | Duration |
|------|---------|------|----------|
| **A: Cut & Clean** | 1-2 | Delete 60K LOC, merge extensions, wire Rust hotpaths | 2 weeks |
| **B: Deepen** | 3-6 | Best-in-class edit, context, reliability, safety | 4 weeks (2 parallel pairs) |
| **C: Ship** | 7-10 | UX polish, plugins, testing, docs + release | 4 weeks (2 parallel pairs) |

## Competitive "Best of Breed" Map

Each sprint bases its implementation on the best competitor for that capability.
All reference code is local at `docs/reference-code/`.

| Capability | Inspire from | Key reference file |
|---|---|---|
| Streaming edit + fuzzy | **Zed** | `zed/crates/agent/src/tools/streaming_edit_file_tool.rs` |
| 4-tier edit cascade | **Gemini CLI** | `gemini-cli/packages/core/src/tools/edit.ts` |
| LLM self-correction | **Gemini CLI** | `gemini-cli/packages/core/src/tools/edit.ts` (line 445) |
| Relative indentation | **Aider** | `aider/aider/coders/search_replace.py` (RelativeIndenter) |
| Unicode normalization | **Pi-Mono** | `pi-mono/packages/coding-agent/src/core/tools/edit-diff.ts` |
| 9 edit replacers | **OpenCode** | `opencode/packages/opencode/src/tool/edit.ts` |
| PageRank repo map | **Aider** | `aider/aider/repomap.py` |
| OS-level sandbox | **Codex CLI** | `codex-cli/codex-rs/core/src/landlock.rs` |
| Ghost checkpoints | **Codex CLI** | `codex-cli/codex-rs/utils/git/src/ghost_commits.rs` |
| Permission rules | **Codex CLI** | `codex-cli/codex-rs/core/src/exec_policy.rs` |
| Stuck/loop detection | **Goose** | `goose/crates/goose/src/agents/tool_inspection.rs` |
| Context compaction | **Cline** | `cline/src/core/context/context-management/ContextManager.ts` |
| MCP auto-approval | **Cline** | `cline/src/core/task/tools/handlers/` |
| LSP in tool output | **OpenCode** | `opencode/packages/opencode/src/lsp/` |
| Agent loop (batched) | **Zed** | `zed/crates/agent/src/thread.rs` (line 1723) |
| MCP-first extensions | **Goose** | `goose/crates/goose/src/agents/extension_manager.rs` |
| Browser sessions | **Cline** | `cline/src/core/task/tools/handlers/` (BrowserToolHandler) |
| Subagent spawning | **OpenCode** | `opencode/packages/opencode/src/tool/task.ts` |

## Timeline (with parallelization: ~7 weeks)

```
Week 1:  Sprint 1 (The Great Deletion)
Week 2:  Sprint 2 (Wire Rust Hotpaths)
Week 3:  Sprint 3 (Edit Excellence)    ─┐ parallel
Week 4:  Sprint 4 (Context Intel)      ─┘
Week 5:  Sprint 5 (Agent Reliability)  ─┐ parallel
Week 6:  Sprint 6 (Sandbox & Safety)   ─┘
Week 7:  Sprint 7 (Desktop UX)        ─┐ parallel
Week 8:  Sprint 8 (Plugins)           ─┘
Week 9:  Sprint 9 (Testing & QA)
Week 10: Sprint 10 (Docs & Ship)
```

## Architecture After v2

```
SolidJS Frontend (src/)
    ├── invoke() ──→ Rust crates (compute, storage, validation)
    └── import ──→ core-v2 + 18 extensions (orchestration, LLM, MCP)
```

**Total backend: ~18.5K LOC** (down from ~79K)
**Tools: ~30** (down from 55+)
**Extensions: 18** (down from 37)
