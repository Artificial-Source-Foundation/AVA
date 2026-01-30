# AI Coding Agent Instructions

> Universal instructions for AI assistants working on Estela

---

## Quick Start

```bash
npm run tauri dev      # Development mode
npm run lint           # Oxlint + ESLint
npm run format         # Biome format
npm run test           # Vitest tests
npm run knip           # Dead code detection
npx tsc --noEmit       # Type check
```

**Read First**: `CLAUDE.md` for memory bank workflow

---

## Project Overview

**Estela** is a multi-agent AI coding assistant - desktop app built with Tauri + SolidJS.

| Layer | Technology |
|-------|------------|
| Runtime | Tauri 2.0 (Rust + Web) |
| Frontend | SolidJS + TypeScript |
| Styling | TailwindCSS |
| Database | SQLite (Tauri SQL plugin) |
| State | SolidJS signals + stores |

---

## Architecture

```
src/
├── components/        # UI by feature
│   ├── chat/          # Chat interface
│   ├── sessions/      # Session management
│   ├── settings/      # Settings modal
│   ├── layout/        # App shell, sidebar
│   └── common/        # Shared components
├── config/            # Constants, env
├── hooks/             # Custom hooks
├── services/          # Business logic
│   ├── auth/          # Credentials, OAuth
│   ├── llm/           # LLM streaming clients
│   ├── database.ts    # SQLite operations
│   └── migrations.ts  # Schema versioning
├── stores/            # Global state
└── types/             # TypeScript types
```

---

## Data Flow

### Chat Message Flow
```
User Input → useChat.sendMessage()
           → saveMessage() to SQLite
           → resolveAuth() picks provider
           → createClient() streams response
           → session.updateMessageContent()
           → updateMessage() saves final
```

### Provider Priority
```
1. OAuth token (if available)
2. Direct API key (Anthropic, OpenAI)
3. OpenRouter gateway (fallback)
```

### Session Lifecycle
```
App.onMount → initDatabase()
           → loadAllSessions()
           → switchSession() | createNewSession()
```

---

## Code Standards

### TypeScript
- Strict mode, no `any`
- Explicit return types
- Barrel exports (index.ts)

### Files
- Max 300 lines per file
- kebab-case filenames
- camelCase functions
- PascalCase types/components

### Components
- Functional components only
- Props: `{Name}Props` interface
- SolidJS primitives: `createSignal`, `Show`, `For`

---

## Key Files

| File | Purpose |
|------|---------|
| `src/App.tsx` | Root with init logic |
| `src/stores/session.ts` | Global state |
| `src/hooks/useChat.ts` | Chat streaming |
| `src/services/database.ts` | SQLite CRUD |
| `src/services/llm/client.ts` | Provider abstraction |

---

## Common Tasks

### Add Component
1. Create in `src/components/{feature}/`
2. Export from feature's `index.ts`

### Add Service
1. Create in `src/services/`
2. Export from `src/services/index.ts`

### Add LLM Provider
1. Create `src/services/llm/providers/{name}.ts`
2. Implement `LLMClient` interface
3. Register in `client.ts`

### Modify Schema
1. Increment `SCHEMA_VERSION` in migrations.ts
2. Add `migrateVN()` function

---

## Development Tooling

| Tool | Purpose | Speed |
|------|---------|-------|
| **Biome** | Formatting + linting | 7-100x faster than Prettier |
| **Oxlint** | Linting | 50-100x faster than ESLint |
| **ESLint** | SolidJS-specific rules | eslint-plugin-solid |
| **Lefthook** | Git hooks (pre-commit) | Parallel execution |
| **commitlint** | Commit message validation | Conventional commits |
| **Vitest** | Testing | Native ESM, SolidJS support |
| **Knip** | Dead code detection | Finds unused exports/deps |
| **Renovate** | Dependency updates | Auto-PRs (weekly) |

### CI/CD (GitHub Actions)

- **CI**: Lint, typecheck, test, knip, build (on PR/push)
- **Release**: Cross-platform builds on tag push (v*)

---

## Before Committing

- [ ] `npm run lint` passes
- [ ] `npm run format:check` passes
- [ ] `npx tsc --noEmit` passes
- [ ] `npm run tauri dev` works
- [ ] No console errors
- [ ] Memory bank updated

---

## Don'ts

- No `any` types
- No files > 300 lines
- No parent directory imports
- No direct signal mutation
- No API keys in code

---

## Documentation

| Priority | Path |
|----------|------|
| 1 | `CLAUDE.md` - Memory bank workflow |
| 2 | `llms.txt` - Quick reference |
| 3 | `docs/architecture/` - System design |
| 4 | `docs/memory-bank/` - Current state |
| 5 | `docs/ROADMAP.md` - Epic overview |

---

## Current Status

- **Phase**: Epic 1 - Single LLM Chat
- **Completed**: Sprint 1.1, 1.2, 1.3
- **Architecture Score**: 8.4/10
