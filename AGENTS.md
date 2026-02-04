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

**Estela** is a multi-agent AI coding assistant (~19,100 lines) - desktop app built with Tauri + SolidJS.

| Layer | Technology |
|-------|------------|
| Runtime | Tauri 2.0 (Rust + Web) |
| Frontend | SolidJS + TypeScript |
| Styling | TailwindCSS |
| Database | SQLite (Tauri SQL plugin) |
| Core | TypeScript monorepo |

---

## Architecture

### Project Structure
```
Estela/
├── packages/
│   ├── core/              # Business logic (~15,400 lines)
│   ├── platform-node/     # Node.js implementations
│   └── platform-tauri/    # Tauri implementations
├── cli/                   # CLI with ACP agent
└── src/                   # Tauri SolidJS frontend (~3,700 lines)
```

### Core Modules (`packages/core/src/`)
```
├── agent/         # Autonomous loop, subagents, recovery (~1,900 lines)
├── commander/     # Hierarchical delegation (~1,000 lines)
│   └── parallel/  # Concurrent execution (~1,400 lines)
├── tools/         # 15 tools (~3,500 lines)
├── context/       # Token tracking, compaction (~1,450 lines)
├── memory/        # Long-term memory, RAG (~1,400 lines)
├── config/        # Settings, credentials (~1,150 lines)
├── validator/     # QA pipeline (~1,000 lines)
├── codebase/      # Repo understanding (~1,200 lines)
├── permissions/   # Safety (~1,100 lines)
├── session/       # State management
├── mcp/           # MCP protocol (~950 lines)
├── diff/          # Change tracking
├── git/           # Snapshots
├── question/      # LLM-to-user questions (~370 lines)
├── instructions/  # Project instructions (~300 lines)
├── scheduler/     # Background tasks (~350 lines)
├── llm/           # Provider clients
├── models/        # Model registry
└── auth/          # OAuth, PKCE
```

---

## Data Flow

### Agent Loop
```
AgentExecutor.run(goal, context)
       │
       ▼
    [Turn Loop]
       │
       ├─→ LLM generates response
       ├─→ Parse tool calls
       ├─→ Execute tools (with retry)
       ├─→ Send results to LLM
       └─→ Check termination conditions
```

### Commander Delegation
```
Commander → delegate_coder → Worker AgentExecutor
         → delegate_tester → Worker AgentExecutor
         → delegate_reviewer → Worker AgentExecutor
```

### Provider Priority
```
1. OAuth token (if available)
2. Direct API key (Anthropic, OpenAI)
3. OpenRouter gateway (fallback)
```

---

## Tools (15 total)

| Tool | File | Purpose |
|------|------|---------|
| glob | glob.ts | Find files by pattern |
| read | read.ts | Read file contents |
| grep | grep.ts | Search file contents |
| create | create.ts | Create new file |
| write | write.ts | Overwrite file |
| delete | delete.ts | Delete file |
| bash | bash.ts | Execute shell commands |
| edit | edit.ts | Fuzzy-matching file editing |
| ls | ls.ts | Directory listing with tree |
| todoread | todo.ts | Read session todo list |
| todowrite | todo.ts | Update session todo list |
| question | question.ts | Ask user clarifying questions |
| websearch | websearch.ts | Web search (Tavily, Exa) |
| webfetch | webfetch.ts | Fetch and process web pages |
| task | task.ts | Spawn subagents |

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
| `packages/core/src/index.ts` | Core exports |
| `packages/core/src/agent/loop.ts` | AgentExecutor |
| `packages/core/src/tools/index.ts` | Tool registry (15 tools) |
| `packages/core/src/commander/executor.ts` | Worker delegation |
| `cli/src/index.ts` | CLI entry |
| `src/App.tsx` | Frontend root |
| `src/hooks/useChat.ts` | Chat streaming |

---

## Common Tasks

### Add Tool
1. Create `packages/core/src/tools/{name}.ts`
2. Export from `packages/core/src/tools/index.ts`
3. Tool auto-registers on import

### Add Worker
1. Add to `packages/core/src/commander/workers/definitions.ts`
2. Workers are tools via `delegate_{name}` pattern

### Add Module
1. Create directory in `packages/core/src/`
2. Add `index.ts` barrel export
3. Export from `packages/core/src/index.ts`

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
| 3 | `docs/memory-bank/` - Current state |
| 4 | `docs/ROADMAP.md` - All 17 epics |
| 5 | `docs/development/completed/` - Sprint details |

---

## Current Status

- **Phase**: Epic 17 Complete (all core features)
- **Next**: Epic 18 - Tauri Desktop GUI
- **Lines**: ~19,100 (core: ~15,400, frontend: ~3,700)
- **Tools**: 15 registered
- **Modules**: 20 in packages/core/src/
