# Project Structure

> Current file organization (Sprint 1.3.5)

---

## Frontend (SolidJS + TypeScript)

```
src/
├── App.tsx                    # Root with initialization
├── index.tsx                  # SolidJS entry point
├── index.css                  # Global styles + Tailwind
│
├── components/                # UI Components
│   ├── index.ts               # Barrel export
│   │
│   ├── chat/                  # Chat Interface
│   │   ├── index.ts
│   │   ├── ChatView.tsx       # Main chat container
│   │   ├── MessageList.tsx    # Message list
│   │   ├── MessageBubble.tsx  # Single message display
│   │   ├── MessageInput.tsx   # User input area
│   │   ├── MessageActions.tsx # Copy, edit, retry actions
│   │   ├── EditForm.tsx       # Inline message editing
│   │   └── TypingIndicator.tsx
│   │
│   ├── sessions/              # Session Management
│   │   ├── index.ts
│   │   ├── SessionList.tsx    # Session sidebar list
│   │   └── SessionListItem.tsx # Single session item
│   │
│   ├── settings/              # Settings
│   │   ├── index.ts
│   │   └── SettingsModal.tsx  # API key configuration
│   │
│   ├── layout/                # App Shell
│   │   ├── index.ts
│   │   ├── AppShell.tsx       # Main layout
│   │   ├── Sidebar.tsx        # Left sidebar
│   │   ├── TabBar.tsx         # Top tabs
│   │   └── StatusBar.tsx      # Bottom status
│   │
│   └── common/                # Shared Components
│       ├── index.ts
│       └── ErrorBoundary.tsx  # Error handling
│
├── config/                    # Configuration
│   ├── index.ts
│   ├── constants.ts           # Magic strings, defaults
│   └── env.ts                 # Environment validation
│
├── hooks/                     # Custom Hooks
│   ├── index.ts
│   └── useChat.ts             # Chat streaming logic
│
├── services/                  # Business Logic
│   ├── index.ts               # Barrel export
│   ├── database.ts            # SQLite CRUD operations
│   ├── migrations.ts          # Schema versioning
│   │
│   ├── auth/                  # Authentication
│   │   ├── index.ts
│   │   ├── credentials.ts     # API key storage
│   │   └── oauth-codex.ts     # OAuth PKCE flow
│   │
│   └── llm/                   # LLM Integration
│       ├── index.ts
│       ├── client.ts          # Provider abstraction
│       └── providers/
│           ├── index.ts
│           ├── anthropic.ts   # Anthropic client
│           └── openrouter.ts  # OpenRouter client
│
├── stores/                    # State Management
│   ├── index.ts
│   └── session.ts             # Session + messages state
│
└── types/                     # TypeScript Types
    ├── index.ts               # Core types
    └── llm.ts                 # LLM-specific types
```

---

## Backend (Tauri + Rust)

```
src-tauri/
├── Cargo.toml                 # Rust dependencies
├── tauri.conf.json            # Tauri config
├── capabilities/
│   └── default.json           # Permissions
│
└── src/
    ├── main.rs                # Entry point
    └── lib.rs                 # Library root
```

---

## Documentation

```
docs/
├── ROADMAP.md                 # Epic overview
│
├── architecture/              # System Design
│   ├── README.md              # This index
│   ├── project-structure.md   # File organization
│   ├── database-schema.md     # SQLite schema
│   ├── data-flow.md           # Data flow diagrams
│   ├── components.md          # Component hierarchy
│   ├── services.md            # Service layer
│   └── types.md               # Type definitions
│
├── memory-bank/               # Session Context
│   ├── activeContext.md       # Current focus
│   ├── progress.md            # What's done
│   ├── techContext.md         # Technical decisions
│   └── projectbrief.md        # Project overview
│
└── development/               # Development
    ├── epics/                 # Sprint plans
    └── completed/             # Archived sprints
```

---

## Root Files

```
/
├── CLAUDE.md                  # AI assistant instructions
├── AGENTS.md                  # Agent-specific instructions
├── llms.txt                   # AI navigation file
├── package.json
├── tsconfig.json
├── tailwind.config.js
├── vite.config.ts
└── README.md
```

---

## File Counts

| Directory | Files | Purpose |
|-----------|-------|---------|
| `src/components/` | 15 | UI components |
| `src/services/` | 11 | Business logic |
| `src/stores/` | 2 | State management |
| `src/types/` | 2 | Type definitions |
| `src/hooks/` | 2 | Custom hooks |
| `src/config/` | 3 | Configuration |
| `docs/` | 15+ | Documentation |
