# Active Context

> What we're working on RIGHT NOW - update frequently

---

## Current Focus

**Epic 1: Single LLM Chat** → **Sprint 1.3: Session Management** ✅ COMPLETE

**Next:** Sprint 1.4: Testing & Polish OR Epic 2: Multi-Agent

---

## Active Tasks

Sprint 1.3 completed. All tasks done:

| # | Task | Status | Notes |
|---|------|--------|-------|
| 1 | Database migrations system | ✅ | migrations.ts with versioning |
| 2 | Constants & env config | ✅ | src/config/ module |
| 3 | Session CRUD operations | ✅ | Full database.ts update |
| 4 | Session store management | ✅ | session.ts with all actions |
| 5 | SessionList UI components | ✅ | SessionListItem + SessionList |
| 6 | SettingsModal component | ✅ | API key configuration |
| 7 | Sidebar + App init | ✅ | Full initialization flow |
| 8 | Extract MessageBubble | ✅ | Architecture improvement |
| 9 | ErrorBoundary + exports | ✅ | Common components |

---

## Recently Completed

- ✅ Sprint 1.3: Session Management (create, list, switch, rename, archive, settings)
- ✅ Sprint 1.2: Message Flow (load history, tokens, retry, edit/regen, OAuth)
- ✅ Sprint 1.1: Multi-provider LLM integration

---

## Current Decisions

- **Frontend-first**: TypeScript LLM clients, not Rust backend
- **Auth priority**: OAuth → Direct API key → OpenRouter gateway
- **Streaming**: AsyncGenerator pattern with SSE parsing
- **Sessions**: SQLite with soft delete (archive), cascading message deletion

---

## Blockers

_None currently_

---

## Next Up

Options:
1. Sprint 1.4: Testing & Polish (unit tests, E2E, error boundaries)
2. Epic 2: Multi-Agent Architecture

---

## Architecture After Sprint 1.3

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

- Design mockups needed for UI (add to `docs/design-refs/`)
