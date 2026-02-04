# Estela

> Multi-Agent AI Coding Assistant (~19,100 lines)

---

## Memory Bank

**IMPORTANT: Read these files at the start of every session:**

| File | Purpose | Update |
|------|---------|--------|
| [`activeContext.md`](docs/memory-bank/activeContext.md) | Current tasks & focus | Every session |
| [`progress.md`](docs/memory-bank/progress.md) | What's been done | Every session |
| [`techContext.md`](docs/memory-bank/techContext.md) | Architecture & patterns | When arch changes |
| [`projectbrief.md`](docs/memory-bank/projectbrief.md) | What is Estela | Rarely |

**Workflow:**
1. Start → Read `activeContext.md`
2. Work → Update as focus changes
3. End → Update `progress.md`
4. Context full? → "Update memory bank" then `/clear`

---

## Quick Commands

```bash
# Development
npm run tauri dev      # Run app
npm run lint           # Oxlint + ESLint
npm run lint:fix       # Auto-fix lint issues
npm run format         # Biome format
npx tsc --noEmit       # Type check

# Testing
npm run test           # Vitest watch
npm run test:run       # Single run
npm run test:coverage  # Coverage report

# Code Quality
npm run knip           # Dead code detection
npm run knip:fix       # Remove dead code
npm run analyze        # Bundle size analysis
```

---

## Architecture

### Project Structure
```
Estela/
├── packages/
│   ├── core/              # Shared business logic (~15,400 lines)
│   ├── platform-node/     # Node.js implementations
│   └── platform-tauri/    # Tauri implementations
├── cli/                   # CLI with ACP agent
└── src/                   # Tauri SolidJS frontend (~3,700 lines)
```

### Core Modules (`packages/core/src/`)
```
├── agent/         # Autonomous loop, subagents, recovery
├── commander/     # Hierarchical delegation, workers
│   └── parallel/  # Concurrent execution, conflict detection
├── tools/         # 15 tools (file, web, task)
├── context/       # Token tracking, compaction strategies
├── memory/        # Episodic, semantic, procedural memory
├── config/        # Settings, credentials, validation
├── validator/     # QA pipeline (syntax, types, lint, test)
├── codebase/      # Repo understanding, PageRank, symbols
├── permissions/   # Safety, risk assessment, CorrectedError
├── session/       # State management, checkpoints, forking
├── mcp/           # MCP protocol client and registry
├── diff/          # Change tracking, unified diffs
├── git/           # Snapshots, rollback
├── question/      # LLM-to-user questions
├── instructions/  # Project/directory instructions
├── scheduler/     # Background task scheduling
├── llm/           # Provider clients
├── models/        # Model registry, pricing
├── auth/          # OAuth, PKCE, credentials
└── types/         # Shared TypeScript types
```

### Data Flow
```
CLI/Frontend → AgentExecutor.run() → Tools → LLM → Response
                    ↓
              Commander delegates
                    ↓
        Workers (parallel) → Validator → Results
```

---

## Tools (15 total)

| Tool | Purpose |
|------|---------|
| glob | Find files by pattern |
| read | Read file contents |
| grep | Search file contents |
| create | Create new file |
| write | Overwrite file |
| delete | Delete file |
| bash | Execute shell commands |
| edit | Fuzzy-matching file editing |
| ls | Directory listing with tree |
| todoread | Read session todo list |
| todowrite | Update session todo list |
| question | Ask user clarifying questions |
| websearch | Web search (Tavily/Exa) |
| webfetch | Fetch and process web pages |
| task | Spawn subagents for complex tasks |

---

## Planning

| Doc | Purpose |
|-----|---------|
| [`ROADMAP.md`](docs/ROADMAP.md) | High-level epic overview (17 complete) |
| [`development/completed/`](docs/development/completed/) | All completed sprints (1-17) |

---

## Reference Code

**IMPORTANT:** Compare implementations against SOTA projects in `docs/reference-code/`:

| Project | Stars | Key Patterns |
|---------|-------|--------------|
| OpenCode | 70k+ | Tool registry, fuzzy edit, metadata streaming |
| Gemini CLI | 50k+ | ToolBuilder, workers-as-tools, error types |

---

## Code Style

- TypeScript strict, no `any`
- Max 300 lines per file
- kebab-case files, camelCase functions, PascalCase types

---

## Tooling Stack

| Tool | Purpose |
|------|---------|
| Biome | Fast formatter + linter |
| Oxlint | Fast linter (50-100x ESLint) |
| ESLint | SolidJS-specific rules |
| Lefthook | Git hooks (pre-commit, commit-msg) |
| commitlint | Conventional commit validation |
| Vitest | Test runner |
| Knip | Dead code finder |
| Renovate | Auto dependency updates |
