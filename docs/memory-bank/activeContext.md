# Active Context

> What we're working on RIGHT NOW - update frequently

---

## Current Focus

**Epic 1: Single LLM Chat** ✅ COMPLETE

**Next:** Epic 2: File Tools

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

---

## Next Up: Epic 2 - File Tools

**Goal:** Enable LLM to read, write, and edit files in the codebase.

See [`docs/development/epics/2-files.md`](../development/epics/2-files.md)

### Potential Sprints

| Sprint | Description |
|--------|-------------|
| 2.1 | File reading (glob, read contents) |
| 2.2 | File writing (create, overwrite) |
| 2.3 | File editing (diff, patch, search/replace) |
| 2.4 | File UI (tree view, preview, diff viewer) |

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
│   ├── database.ts  # SQLite operations
│   └── migrations.ts
├── stores/          # session
└── types/           # index, llm
```

---

## Open Questions

- Design mockups needed for file tree UI
- Should file operations go through Rust (Tauri) or TypeScript?
