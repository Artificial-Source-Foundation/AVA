# Architecture

> System design and technical decisions

---

## Documents

| Document | Description |
|----------|-------------|
| [project-structure.md](./project-structure.md) | Full project structure and file organization |
| [database-schema.md](./database-schema.md) | SQLite schema for sessions, messages, agents |
| [agent-communication.md](./agent-communication.md) | How Commander, Operators, and Validator communicate |
| [tool-system.md](./tool-system.md) | File editing, bash execution, LSP integration |

---

## Quick Overview

### Stack

```
┌─────────────────────────────────────────┐
│           SolidJS Frontend              │
│   (Components, Stores, Services)        │
├─────────────────────────────────────────┤
│           Tauri IPC Bridge              │
├─────────────────────────────────────────┤
│           Rust Backend                  │
│   (Commands, LSP, Tools, Database)      │
└─────────────────────────────────────────┘
```

### Key Directories

```
project/
├── src/                  # Frontend (SolidJS + TypeScript)
│   ├── components/       # UI components
│   ├── stores/           # State management
│   ├── services/         # Business logic (agents, LLM, tools)
│   └── types/            # TypeScript types
│
├── src-tauri/            # Backend (Rust)
│   └── src/
│       ├── commands/     # Tauri IPC commands
│       ├── lsp/          # LSP client
│       ├── tools/        # Tool implementations
│       └── db/           # SQLite layer
│
└── docs/                 # Documentation
```
