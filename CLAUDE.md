# Estela

> Multi-Agent AI Coding Assistant (Tauri 2.0 + SolidJS)

---

## Commands

```bash
npm run tauri dev      # Development mode
npm run tauri build    # Production build
npm test               # Run tests
npm run lint           # ESLint check
npm run typecheck      # TypeScript check
```

---

## Documentation

| Document | Description |
|----------|-------------|
| `docs/VISION.md` | Project vision, architecture, roadmap |
| `docs/BACKLOG.md` | Current development tasks |
| `docs/architecture/` | System design documents |
| `docs/agents/` | Agent specifications |

---

## Tech Stack

| Layer | Technology |
|-------|------------|
| **Desktop** | Tauri 2.0 (Rust) |
| **Frontend** | SolidJS + TypeScript |
| **Styling** | Tailwind CSS |
| **Database** | SQLite |
| **LSP** | Rust client |

---

## Project Structure

```
estela/
├── src/                    # SolidJS frontend
│   ├── components/
│   │   ├── layout/         # AppShell, Sidebar, TabBar, StatusBar
│   │   ├── chat/           # MessageList, MessageInput, StreamingText
│   │   └── agents/         # AgentCard, AgentTree
│   ├── stores/             # State management
│   ├── services/           # LLM, agents, file operations
│   ├── hooks/              # Custom hooks
│   └── types/              # TypeScript definitions
│
├── src-tauri/              # Rust backend
│   └── src/
│       ├── commands/       # Tauri IPC commands
│       │   ├── file_ops.rs # str_replace, create_file, read_file
│       │   ├── bash.rs     # Shell execution
│       │   └── llm.rs      # Streaming API calls
│       ├── lsp/            # Language server client
│       ├── tools/          # Tool implementations
│       └── db/             # SQLite operations
│
└── docs/                   # Documentation
```

---

## Agent Architecture

```
COMMANDER (Planning)
    │
    ├── Analyzes requests
    ├── Creates task breakdown
    ├── Assigns files to operators
    └── Reviews results
         │
         ▼
    OPERATORS (Execution)
         │
         ├── One file per operator
         ├── Parallel execution
         └── Reports summary
              │
              ▼
         VALIDATOR (QA)
              │
              └── PASS / FIXABLE / FAIL
```

**Key Rule**: Commander NEVER writes code. Only plans and delegates.

---

## Coding Conventions

### TypeScript (Frontend)
- Strict mode, no `any`
- Zod for runtime validation
- kebab-case files, camelCase functions, PascalCase types

### Rust (Backend)
- Safe Rust, no unnecessary unsafe
- Error handling with `Result<T, E>`
- Serde for serialization

### General
- Max 300 lines per file
- One component per file
- Test before committing

---

## Current Status

**Phase**: Planning complete, ready for scaffolding
**Next**: `npm create tauri-app@latest estela -- --template solid-ts`
