# Roadmap

> High-level epic overview - all 17 epics complete

---

## Epics

### Foundation (Complete)

| # | Epic | Goal | Status |
|---|------|------|--------|
| 1 | [Chat](./development/completed/epic-1-chat.md) | Streaming chat with multi-provider LLM | ✅ Complete |
| 2 | [File Tools](./development/completed/epic-2-files.md) | Read, write, edit, glob, grep, bash | ✅ Complete |
| 3 | [ACP + Core](./development/completed/epic-3-acp.md) | Monorepo, platform abstraction, CLI agent | ✅ Complete |

### Infrastructure (Complete)

| # | Epic | Goal | Status |
|---|------|------|--------|
| 4 | [Safety](./development/completed/epic-4-safety.md) | Permissions, file locking, process control | ✅ Complete |
| 5 | [Context](./development/completed/epic-5-context.md) | Token tracking, compaction, session state | ✅ Complete |
| 6 | [DX](./development/completed/epic-6-dx.md) | Tool.define(), diffs, git snapshots | ✅ Complete |
| 7 | [Platform](./development/completed/epic-7-platform.md) | PTY allocation, MCP integration | ✅ Complete |

### Agent System (Complete)

| # | Epic | Goal | Status |
|---|------|------|--------|
| 8 | [Agent](./development/completed/epic-8-agent.md) | Autonomous agent loop | ✅ Complete |
| 9 | [Commander](./development/completed/epic-9-commander.md) | Hierarchical delegation | ✅ Complete |
| 10 | [Parallel](./development/completed/epic-10-parallel.md) | Concurrent operators | ✅ Complete |
| 11 | [Validator](./development/completed/epic-11-validator.md) | QA verification gate | ✅ Complete |
| 12 | [Codebase](./development/completed/epic-12-codebase.md) | Codebase understanding, repo map | ✅ Complete |
| 13 | [Config](./development/completed/epic-13-config.md) | Settings UI, preferences | ✅ Complete |
| 14 | [Memory](./development/completed/epic-14-memory.md) | Long-term memory, RAG | ✅ Complete |
| 15 | [Comparison](./development/completed/epic-15-comparison.md) | Feature comparison vs SOTA agents | ✅ Complete |

### Enhancement (Complete)

| # | Epic | Goal | Status |
|---|------|------|--------|
| 16 | [OpenCode Features](./development/completed/epic-16-opencode-features.md) | Metadata streaming, session forking, instructions | ✅ Complete |
| 17 | [Missing Tools](./development/completed/epic-17-missing-tools.md) | 8 new tools: edit, ls, todo, question, web, task | ✅ Complete |

---

## Epic Dependencies

```
Epic 1-3 (Foundation) ──► Epic 4-7 (Infrastructure) ──► Epic 8-15 (Agents) ──► Epic 16-17 (Enhancement)

Specifically:
├── Epic 4 (Safety) required for Epic 8 (Agent) - permissions for autonomous ops
├── Epic 5 (Context) required for Epic 8 (Agent) - long conversations
├── Epic 6 (DX) improves all subsequent development
├── Epic 7 (Platform) required for Epic 9 (Commander) - sub-agent spawning, MCP
├── Epic 12 (Codebase) builds on Epic 5 (Context) - session + repo understanding
├── Epic 16 (OpenCode) enhances Epic 4-7 infrastructure
└── Epic 17 (Tools) adds 8 new tools to complete tool suite
```

---

## Code Statistics

| Module | Lines | Epic |
|--------|-------|------|
| Agent | ~1,900 | 8 |
| Commander + Parallel | ~2,400 | 9-10 |
| Validator | ~1,000 | 11 |
| Codebase | ~1,200 | 12 |
| Config | ~1,150 | 13 |
| Memory | ~1,400 | 14 |
| Instructions | ~300 | 16 |
| Scheduler | ~350 | 16 |
| Question | ~370 | 17 |
| Tools (8 new) | ~3,100 | 17 |
| **Total Core** | **~19,100** | |

---

## Completed Sprints

See [`development/completed/`](./development/completed/) for all 17 epics.

---

## Future (Planned)

| # | Epic | Goal | Status |
|---|------|------|--------|
| 18 | Tauri Desktop | Frontend GUI with SolidJS | ⬜ Planned |
| 19 | Cloud Sync | Session sync across devices | ⬜ Planned |
| 20 | Plugin System | MCP-based extensibility | ⬜ Planned |

---

## Ideas / Backlog

- Voice input
- Vision models
- Browser automation tools
- Team collaboration
- Mobile companion app
