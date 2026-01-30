# Active Context

> What we're working on RIGHT NOW - update frequently

---

## Current Focus

**Epic 2: File Tools** ✅ COMPLETE

All core file tools implemented: glob, read_file, grep, create_file, write_file, delete_file, bash

---

## Completed

| Epic/Sprint | Status | Notes |
|-------------|--------|-------|
| Epic 1: Chat | ✅ | All sprints done |
| Sprint 1.1: LLM Integration | ✅ | Multi-provider streaming |
| Sprint 1.2: Message Flow | ✅ | Retry, edit, tokens, OAuth |
| Sprint 1.3: Session Management | ✅ | Create, list, switch, rename, archive |
| Sprint 1.3.5: Architecture | ✅ | Barrel exports, AI docs |
| Dev Tooling | ✅ | Biome, Oxlint, Vitest, CI/CD |
| Sprint 2.1: File Reading | ✅ | glob, read_file, grep tools with LLM integration |
| Sprint 2.2: File Writing | ✅ | create_file, write_file, delete_file tools |
| Sprint 2.3: Bash Execution | ✅ | Shell command execution with timeout and truncation |

---

## Next Up: Epic 3 - Agentic Features

**Goal:** Multi-turn planning, task decomposition, parallel tool execution.

See [`docs/development/epics/`](../development/epics/)

### Remaining in Epic 2

| Sprint | Description |
|--------|-------------|
| 2.4 | File UI (tree view, preview, diff viewer) - Optional |

---

## Current Decisions

- **Frontend-first**: TypeScript LLM clients, not Rust backend
- **Auth priority**: OAuth → Direct API key → OpenRouter gateway
- **Streaming**: AsyncGenerator pattern with SSE parsing
- **Sessions**: SQLite with soft delete (archive)
- **Tooling**: Biome + Oxlint + ESLint + Lefthook + Vitest

---

## Blockers

_None currently_

---

## Architecture

```
src/
├── components/
│   ├── chat/        # MessageBubble, MessageList, Input, Actions, Edit
│   ├── common/      # ErrorBoundary
│   ├── layout/      # AppShell, Sidebar, TabBar, StatusBar
│   ├── sessions/    # SessionList, SessionListItem
│   └── settings/    # SettingsModal
├── config/          # constants, env
├── hooks/           # useChat
├── services/
│   ├── auth/        # credentials, oauth-codex
│   ├── llm/         # client, providers/
│   ├── tools/       # Tool system (glob, read, grep, create, write, delete, bash, registry)
│   ├── database.ts  # SQLite operations
│   └── migrations.ts
├── stores/          # session
└── types/           # index, llm
```

---

## Open Questions

- Design mockups needed for file tree UI
