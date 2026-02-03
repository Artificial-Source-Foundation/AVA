# Roadmap

> High-level epic overview

---

## Epics

### Foundation (Complete)

| # | Epic | Goal | Status |
|---|------|------|--------|
| 1 | [Chat](./development/completed/epic-1-chat.md) | Streaming chat with multi-provider LLM | ✅ Complete |
| 2 | [File Tools](./development/completed/epic-2-files.md) | Read, write, edit, glob, grep, bash | ✅ Complete |
| 3 | [ACP + Core](./development/completed/epic-3-acp.md) | Monorepo, platform abstraction, CLI agent | ✅ Complete |

### Infrastructure (Current)

| # | Epic | Goal | Status |
|---|------|------|--------|
| 4 | [Safety](./development/epics/4-safety.md) | Permissions, file locking, process control | ✅ Complete |
| 5 | [Context](./development/epics/5-context.md) | Token tracking, compaction, session state | ✅ Complete |
| 6 | [DX](./development/epics/6-dx.md) | Tool.define(), diffs, git snapshots | ✅ Complete |
| 7 | [Platform](./development/epics/7-platform.md) | PTY allocation, MCP integration | ✅ Complete |

### Agent System (Current)

| # | Epic | Goal | Status |
|---|------|------|--------|
| 8 | [Agent](./development/epics/8-agent.md) | Autonomous agent loop | ✅ Complete |
| 9 | [Commander](./development/epics/9-commander.md) | Hierarchical delegation | ✅ Complete |
| 10 | [Parallel](./development/epics/10-parallel.md) | Concurrent operators | 🟡 Next |
| 11 | [Validator](./development/epics/11-validator.md) | QA verification gate | ⬜ |
| 12 | [Codebase](./development/epics/12-codebase.md) | Codebase understanding, repo map | ⬜ |
| 13 | [Config](./development/epics/13-config.md) | Settings UI, preferences | ⬜ |

---

## Epic Dependencies

```
Epic 1-3 (Foundation) ──► Epic 4-7 (Infrastructure) ──► Epic 8-13 (Agents)

Specifically:
├── Epic 4 (Safety) required for Epic 8 (Agent) - permissions for autonomous ops
├── Epic 5 (Context) required for Epic 8 (Agent) - long conversations
├── Epic 6 (DX) improves all subsequent development
├── Epic 7 (Platform) required for Epic 9 (Commander) - sub-agent spawning, MCP
└── Epic 12 (Codebase) builds on Epic 5 (Context) - session + repo understanding
```

---

## Completed Sprints

See [`development/completed/`](./development/completed/)

---

## Parallel Execution Guide

Epics 4-7 can be parallelized with careful file ownership:

| Instance | Epic | Directory Ownership |
|----------|------|---------------------|
| A | Epic 4 (Safety) | `packages/core/src/permissions/` |
| B | Epic 5 (Context) | `packages/core/src/context/`, `packages/core/src/session/` |
| C | Epic 6 (DX) | `packages/core/src/tools/define.ts`, `packages/core/src/diff/` |
| D | Epic 7 (Platform) | `packages/platform-node/src/pty.ts`, `packages/core/src/mcp/` |

**Shared files requiring coordination:**
- `packages/core/src/index.ts` (exports)
- `packages/core/src/types/` (interfaces)

---

## Ideas / Future

- Cloud sync
- Team collaboration
- Plugin system (via MCP)
- Voice input
- Vision models
- Browser automation tools
