# Epic 1: Chat System

> ✅ Completed: 2025-01-29

---

## Goal

Build streaming chat with multi-provider LLM support.

---

## Sprints Completed

| Sprint | What | Lines |
|--------|------|-------|
| 1.1 | LLM Integration | ~955 |
| 1.2 | Message Flow | ~550 |
| 1.3 | Session Management | ~850 |
| 1.3.5 | Architecture Consolidation | ~400 |

**Total:** ~2755 lines

---

## What Was Built

### LLM Integration (1.1)
- Multi-provider streaming (OpenRouter, Anthropic)
- AsyncGenerator SSE parsing
- Auth resolution (OAuth → API key → gateway)
- useChat() SolidJS hook

### Message Flow (1.2)
- Load history on session open
- Token counting per message
- Retry failed messages
- Edit user messages (resend from that point)
- Regenerate assistant responses
- Codex OAuth PKCE flow

### Session Management (1.3)
- Create/list/switch sessions
- Rename sessions (inline edit)
- Archive/delete sessions
- Settings modal for API keys
- Database migrations system
- App initialization flow

### Architecture (1.3.5)
- Barrel exports throughout
- AI-friendly docs (llms.txt, AGENTS.md)
- Full architecture documentation

---

## Key Decisions

- **Frontend-first**: TypeScript LLM clients, not Rust backend
- **Auth priority**: OAuth → Direct API key → OpenRouter gateway
- **Streaming**: AsyncGenerator with SSE parsing
- **State**: SolidJS signals + SQLite persistence

---

## Files Created

```
src/
├── types/llm.ts
├── services/
│   ├── auth/credentials.ts, oauth-codex.ts
│   ├── llm/client.ts, providers/
│   ├── database.ts, migrations.ts
│   └── index.ts
├── hooks/useChat.ts, index.ts
├── stores/session.ts, index.ts
├── config/constants.ts, env.ts
└── components/
    ├── chat/TypingIndicator.tsx, EditForm.tsx, MessageActions.tsx, MessageBubble.tsx
    ├── sessions/SessionList.tsx, SessionListItem.tsx
    ├── settings/SettingsModal.tsx
    └── common/ErrorBoundary.tsx
```
