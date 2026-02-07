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

**Estela** is a multi-agent AI coding assistant - a Tauri 2.0 + SolidJS desktop app with a TypeScript core monorepo and a CLI that speaks ACP for editor integration.

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
│   ├── core/              # Business logic
│   ├── platform-node/     # Node.js implementations
│   └── platform-tauri/    # Tauri implementations
├── cli/                   # CLI with ACP agent
└── src/                   # Tauri SolidJS frontend
```

### Core Modules (`packages/core/src/`)
```
├── agent/         # Autonomous loop, prompts, recovery, modes
├── auth/          # OAuth + PKCE flows
├── codebase/      # Repo map, symbols, tree-sitter
├── commander/     # Hierarchical delegation, workers
│   └── parallel/  # Concurrent execution
├── config/        # Settings + validation
├── context/       # Token tracking + compaction
├── diff/          # Change tracking
├── git/           # Snapshots + rollback
├── hooks/         # Lifecycle hooks
├── instructions/  # Project instructions
├── llm/           # Provider clients
├── lsp/           # Language Server Protocol
├── mcp/           # MCP client + discovery
├── memory/        # Long-term memory + RAG
├── models/        # Model registry + pricing
├── permissions/   # Safety + rules
├── question/      # LLM-to-user questions
├── scheduler/     # Background tasks
├── session/       # State + checkpoints + forking
├── tools/         # Tool registry + implementations
├── types/         # Shared types
└── validator/     # QA pipeline
```

---

## Data Flow

```
AgentExecutor.run(goal, context)
       │
       ▼
    [Turn Loop]
       │
       ├─→ LLM generates response
       ├─→ Parse tool calls
       ├─→ Execute tools (with retry)
       ├─→ Stream metadata + record usage
       ├─→ Check termination (attempt_completion)
       └─→ Persist session + checkpoints
```

### Commander Delegation
```
Commander → delegate_coder → Worker AgentExecutor
         → delegate_tester → Worker AgentExecutor
         → delegate_reviewer → Worker AgentExecutor
         → delegate_researcher → Worker AgentExecutor
         → delegate_debugger → Worker AgentExecutor
```

### Provider Resolution
- OAuth token (if available)
- Direct API key (provider-specific)
- OpenRouter as gateway fallback (if configured)

---

## Tools (22 total)

| Tool | File | Purpose |
|------|------|---------|
| read_file | read.ts | Read file contents |
| create_file | create.ts | Create new file |
| write_file | write.ts | Overwrite file |
| delete_file | delete.ts | Delete file |
| edit | edit.ts | Fuzzy text edits |
| apply_patch | apply-patch/index.ts | Apply unified diffs to files |
| multiedit | multiedit.ts | Edit multiple files at once |
| glob | glob.ts | Find files by pattern |
| grep | grep.ts | Search file contents |
| ls | ls.ts | Directory listing |
| bash | bash.ts | Execute shell commands (PTY supported) |
| batch | batch.ts | Batch execute multiple tools |
| codesearch | codesearch.ts | Search codebase with context |
| question | question.ts | Ask user clarifying questions |
| skill | skill.ts | Auto-invoke skills from plugins |
| todoread | todo.ts | Read session todo list |
| todowrite | todo.ts | Update session todo list |
| task | task.ts | Spawn subagents |
| websearch | websearch.ts | Web search |
| webfetch | webfetch.ts | Fetch + convert web pages |
| browser | browser/index.ts | Puppeteer browser automation |
| attempt_completion | completion.ts | Finish task with summary |
| plan_enter | agent/modes/plan.ts | Enter plan mode |
| plan_exit | agent/modes/plan.ts | Exit plan mode |

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
| `packages/core/src/tools/index.ts` | Tool registry |
| `packages/core/src/commander/executor.ts` | Worker delegation |
| `cli/src/index.ts` | CLI entry |
| `src/App.tsx` | Frontend root |
| `src/hooks/useChat.ts` | Chat streaming |

---

## Common Tasks

### Add Tool
1. Create `packages/core/src/tools/{name}.ts`
2. Define with `defineTool()` if possible
3. Export + register in `packages/core/src/tools/index.ts`

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
| 2 | `docs/README.md` - Doc index |
| 3 | `docs/ROADMAP.md` - Epics + status |
| 4 | `docs/VISION.md` - Product vision |
| 5 | `docs/development/FEATURE_GAP_ANALYSIS.md` - Gaps vs SOTA |
| 6 | `docs/development/opencode-comparison.md` - OpenCode comparison |
| 7 | `docs/reference-code/` - Local reference repos |

---

## Current Status

- **Core**: Tool registry, permissions, PTY, compaction, session persistence, LSP, browser tool, plan mode
- **Tools**: 22 registered
- **Modules**: 22 core modules + platform abstraction
- **Next**: Frontend polish + UX integration of approvals/metadata (see `docs/ROADMAP.md`)
