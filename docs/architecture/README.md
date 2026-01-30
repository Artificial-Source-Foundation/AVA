# Architecture

> System design for Estela - Single LLM Chat (Epic 1)

---

## Current Phase

**Epic 1: Single LLM Chat** - A functional chat interface with session management, multi-provider LLM support, and streaming responses.

Future epics will add multi-agent orchestration, tool execution, and LSP integration.

---

## Documents

| Document | Description |
|----------|-------------|
| [project-structure.md](./project-structure.md) | Current file organization |
| [database-schema.md](./database-schema.md) | SQLite schema and migrations |
| [data-flow.md](./data-flow.md) | How data flows through the app |
| [components.md](./components.md) | UI component hierarchy |
| [services.md](./services.md) | Service layer documentation |
| [types.md](./types.md) | TypeScript type definitions |

---

## Stack Overview

```
┌─────────────────────────────────────────┐
│         SolidJS Frontend                │
│   Components → Stores → Services        │
├─────────────────────────────────────────┤
│         Tauri SQL Plugin                │
│         (SQLite Database)               │
├─────────────────────────────────────────┤
│         Tauri Runtime                   │
│         (Rust Backend)                  │
└─────────────────────────────────────────┘
```

---

## Key Patterns

### State Management

SolidJS signals and stores with reactive updates:

```typescript
// Global store pattern
const [sessions, setSessions] = createSignal<SessionWithStats[]>([]);
const [currentSession, setCurrentSession] = createSignal<Session | null>(null);

export function useSession() {
  return {
    sessions,
    currentSession,
    // ... actions
  };
}
```

### Service Architecture

Provider-agnostic LLM client with credential resolution:

```
useChat() → resolveAuth() → createClient() → stream()
                ↓
    1. OAuth token (if available)
    2. Direct API key (Anthropic, OpenAI)
    3. OpenRouter gateway (fallback)
```

### Database Pattern

SQLite with versioned migrations:

```typescript
// Migration system
const SCHEMA_VERSION = 1;
await runMigrations(db);  // Auto-runs pending migrations
```

---

## Directory Structure

```
src/
├── components/        # UI by feature
│   ├── chat/          # Chat interface
│   ├── sessions/      # Session management
│   ├── settings/      # Settings modal
│   ├── layout/        # App shell
│   └── common/        # Shared components
├── config/            # Constants, env
├── hooks/             # Custom hooks
├── services/          # Business logic
│   ├── auth/          # Credentials, OAuth
│   ├── llm/           # LLM streaming
│   └── database.ts    # SQLite operations
├── stores/            # Global state
└── types/             # TypeScript types
```

---

## Architecture Score: 8.4/10

| Area | Score |
|------|-------|
| Folder Structure | 8.5 |
| Separation of Concerns | 8.0 |
| Component Organization | 8.5 |
| Service Abstraction | 8.0 |
| Type Organization | 8.5 |
| Index Files (Barrel Exports) | 9.0 |
| Scalability | 8.0 |
