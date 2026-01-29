# AGENTS.md

> Universal agent instructions for Estela

---

## Quick Reference

```bash
npm run tauri dev      # Start development
npm run tauri build    # Production build
npm test               # Run tests
```

**Start Here**: `docs/VISION.md` for architecture and roadmap

---

## Project Structure

```
estela/
├── src/                    # SolidJS frontend
│   ├── components/         # UI components
│   ├── stores/             # State management
│   ├── services/           # Business logic
│   └── types/              # TypeScript types
│
├── src-tauri/              # Rust backend
│   └── src/
│       ├── commands/       # Tauri IPC
│       ├── lsp/            # LSP client
│       └── db/             # SQLite
│
└── docs/                   # Documentation
```

---

## Agent Hierarchy

| Agent | Role | Model |
|-------|------|-------|
| **Commander** | Planning & delegation | Opus/Sonnet |
| **Operators** | Task execution | Sonnet/Haiku |
| **Validator** | QA verification | Haiku |

### Commander
- Analyzes user requests
- Creates task breakdowns
- Assigns files to operators
- Reviews aggregate results
- **NEVER writes code**

### Operators
- Execute file-specific tasks
- One operator per file
- Run in parallel
- Report summaries back

### Validator
- Runs linter, type checker
- Verifies acceptance criteria
- Returns: PASS / FIXABLE / FAIL

---

## Tech Stack

| Layer | Technology |
|-------|------------|
| Desktop | Tauri 2.0 (Rust) |
| Frontend | SolidJS + TypeScript |
| Styling | Tailwind CSS |
| Database | SQLite |
| LSP | Rust client |

---

## Code Standards

- **TypeScript**: Strict mode, no `any`, Zod validation
- **Rust**: Safe, `Result<T, E>` error handling
- **Files**: Max 300 lines, one component each
- **Naming**: kebab-case files, camelCase functions, PascalCase types

---

## Boundaries

### Always
- Read `docs/VISION.md` for architecture decisions
- Use Tauri commands for backend operations
- Stream LLM responses
- Validate all input
- Test before committing

### Never
- Let Commander write code
- Skip the Validator gate
- Block the UI thread
- Store secrets in code
- Use `any` type

---

## Key Documentation

| Priority | Path |
|----------|------|
| 1 | `docs/VISION.md` - Vision and roadmap |
| 2 | `docs/BACKLOG.md` - Current tasks |
| 3 | `docs/architecture/` - System design |
| 4 | `docs/agents/` - Agent specs |

---

## Current Status

- **Phase**: Ready to scaffold
- **Name**: Estela
- **Next**: Initialize Tauri + SolidJS project
