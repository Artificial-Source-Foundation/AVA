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

**UPDATED 2026-03-05** based on deep audit of 12 competitors.

| Capability | Best Implementation | Key Reference File | Notes |
|---|---|---|---|
| **EDIT SYSTEM** ||||
| StreamingDiff (apply as LLM streams) | **Zed** | `zed/crates/streaming_diff/src/streaming_diff.rs` | Character-level incremental diffing |
| Per-hunk accept/reject UI | **Zed** | `zed/crates/agent_ui/src/buffer_codegen.rs` | Granular change review |
| 4-tier edit cascade | **Gemini CLI** | `gemini-cli/packages/core/src/tools/edit.ts` | Exact→flexible→regex→fuzzy |
| LLM self-correction | **Gemini CLI** | `gemini-cli/packages/core/src/utils/llm-edit-fixer.ts` | Dedicated LLM for fixing edits |
| 10 edit formats | **Aider** | `aider/aider/coders/` | Most comprehensive |
| RelativeIndenter | **Aider** | `aider/aider/coders/search_replace.py:18-171` | Delta-encoded indentation |
| 9 replacer cascade | **OpenCode** | `opencode/packages/opencode/src/tool/edit.ts` | Fuzzy matching chain |
| Windowed editing | **SWE Agent** | `swe-agent/tools/windowed/` | 100-line context window |
| **CONTEXT & MEMORY** ||||
| PageRank repo map | **Aider** | `aider/aider/repomap.py` | Graph-based relevance |
| 9 condenser strategies | **OpenHands** | `openhands/memory/condenser/impl/` | Most sophisticated compaction |
| History processors pipeline | **SWE Agent** | `sweagent/agent/history_processors.py` | Chain-of-responsibility |
| Event-sourced architecture | **OpenHands** | `openhands/events/stream.py` | Full replay capability |
| Session DAG | **Pi Mono** | `pi-mono/packages/coding-agent/src/core/session-manager.ts` | Tree navigation |
| Cross-provider normalization | **Pi Mono** | `pi-mono/packages/ai/src/providers/transform-messages.ts` | Handles thinking blocks, tool IDs |
| **AGENT LOOP** ||||
| 3-layer loop detection | **Gemini CLI** | `gemini-cli/packages/core/src/services/loopDetectionService.ts` | Hash+chanting+LLM judge |
| 5-scenario stuck detection | **OpenHands** | `openhands/controller/stuck.py` | Comprehensive detection |
| Action samplers (best-of-N) | **SWE Agent** | `sweagent/agent/action_sampler.py` | Generate N, pick best |
| Reviewer agent loop | **SWE Agent** | `sweagent/agent/reviewer.py` | LLM validates outputs |
| Concurrent builds (race) | **Plandex** | `plandex/app/server/model/plan/build.go` | 4 strategies compete |
| **SAFETY** ||||
| OS-level sandbox (macOS) | **Codex CLI** | `codex-cli/codex-rs/core/src/seatbelt.rs` | Seatbelt sandbox |
| OS-level sandbox (Linux) | **Codex CLI** | `codex-cli/codex-rs/linux-sandbox/src/landlock.rs` | Landlock+seccomp |
| Ghost snapshots | **Codex CLI** | `codex-cli/codex-rs/utils/git/src/ghost_commits.rs` | Invisible rollback |
| 3-layer inspection | **Goose** | `goose/crates/goose/src/tool_inspection.rs` | Security→Permission→Repetition |
| Dynamic policy generation | **Gemini CLI** | `gemini-cli/packages/core/src/safety/conseca/` | Conseca system |
| Terminal security evaluator | **Continue** | `continue/packages/terminal-security/src/` | Deep shell parsing |
| **UX & EXTENSIBILITY** ||||
| MCP-first design | **Goose** | `goose/crates/goose/src/agents/extension.rs` | Extensions ARE MCP servers |
| MCP server mode | **Zed** | `zed/crates/agent/src/native_agent_server.rs` | Expose tools to others |
| Context providers (30+) | **Continue** | `continue/core/context/providers/` | Unified interface |
| Tab autocomplete | **Continue** | `continue/core/autocomplete/` | Inline suggestions |
| Streaming fuzzy matcher | **Zed** | `zed/crates/agent/src/edit_agent/streaming_fuzzy_matcher.rs` | Incremental matching |
| A2A protocol | **Gemini CLI** | `gemini-cli/packages/a2a-server/src/` | Agent-to-agent |
| Shadow git snapshots | **OpenCode** | `opencode/packages/opencode/src/snapshot/index.ts` | Isolated rollback repos |
| Git worktree isolation | **OpenCode** | `opencode/packages/opencode/src/worktree/index.ts` | Per-session worktrees |

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
