# AVA

> The Obsidian of AI Coding — Desktop AI coding app with a virtual dev team and community plugins

---

## What Is AVA

A **desktop-first AI coding app** (Tauri + SolidJS) for developers and vibe coders. Not an IDE replacement — an AI companion with:

- **Dev Team System** — Visible Team Lead → Senior Leads → Junior Devs hierarchy
- **Multi-Provider** — 12+ LLM providers, use the best model for each task
- **Plugin Ecosystem** — Obsidian-style, easy to create, discover, and install
- **Open Source** — Community-first, MIT license

See [`docs/VISION.md`](docs/VISION.md) for the full product vision.

---

## Quick Commands

```bash
# Development
npm run tauri dev        # Run desktop app
npm run build:packages   # Build core + platform packages
npm run build:cli        # Build CLI (secondary interface)
npm run lint             # Oxlint + ESLint
npm run format           # Biome format
npx tsc --noEmit         # Type check

# Testing
npm run test             # Vitest watch
npm run test:run         # Single run
npx vitest run <path>    # Run specific test file

# Code Quality
npm run knip             # Dead code detection
npm run analyze          # Bundle size analysis
```

---

## Architecture

### Project Structure

```
AVA/
├── src/                   # Desktop app (Tauri + SolidJS) ← PRIMARY
├── src-tauri/             # Rust backend
├── packages/
│   ├── core/              # Shared business logic (54,000+ lines)
│   ├── platform-node/     # Node.js implementations
│   └── platform-tauri/    # Tauri implementations
└── cli/                   # CLI interface (secondary)
```

### The Dev Team (Agent System)

```
Team Lead (AgentExecutor + Commander)
    │
    ├─→ Senior Frontend Lead (Worker with filtered tools)
    │   ├─→ Jr. Dev: Component work
    │   └─→ Jr. Dev: Styling
    │
    ├─→ Senior Backend Lead (Worker with filtered tools)
    │   ├─→ Jr. Dev: API routes
    │   └─→ Jr. Dev: Database
    │
    └─→ Validator (QA verification)
```

**In the UI:**
- Main chat shows Team Lead planning and delegating
- Agent cards show Senior Leads and Junior Devs working
- Workers auto-report when done; user can click into any agent to chat directly
- User can intervene, fix issues, and send results back up the chain

**In the code:**
- `packages/core/src/commander/` — Hierarchical delegation, worker definitions
- `packages/core/src/agent/` — Agent loop, planning, recovery, subagents
- `packages/core/src/validator/` — QA pipeline (syntax, types, lint, test, self-review)

### Core Modules (`packages/core/src/`) — [Full docs](docs/backend/)

```
Agent System:
├── agent/         # Agent loop, prompts, modes, subagents
├── commander/     # Team Lead → Senior Leads → Junior Devs
├── validator/     # QA pipeline

Tools (24):
├── tools/         # read, write, edit, glob, grep, bash, browser, etc.

Intelligence:
├── codebase/      # Repo map, symbols, PageRank
├── context/       # Token tracking, compaction, compression
├── lsp/           # Language Server Protocol (5 languages)
├── diff/          # Diff tracking, unified format
├── focus-chain/   # Task progress tracking

Extensibility:
├── extensions/    # Plugin system (install, enable, reload)
├── custom-commands/ # TOML custom commands
├── hooks/         # Lifecycle hooks (PreToolUse, PostToolUse, etc.)
├── mcp/           # MCP protocol client
├── skills/        # Auto-invoked knowledge modules
├── slash-commands/ # User-invocable /commands

Safety:
├── permissions/   # Risk assessment, auto-approval, trusted folders
├── policy/        # Priority rules, wildcards, regex

Infrastructure:
├── llm/           # 13 provider clients
├── config/        # Settings, credentials
├── session/       # State, checkpoints, forking, resume
├── auth/          # OAuth + PKCE
├── bus/           # Message bus (pub/sub)
├── models/        # Model registry (~16 LLM models)
├── scheduler/     # Background task scheduler
├── question/      # LLM-to-user question system
├── git/           # Git snapshots, auto-commit
├── instructions/  # Project/directory instructions
├── integrations/  # External APIs (Exa search)
├── types/         # Shared type definitions

```

### Data Flow

```
Desktop App / CLI
    → Team Lead (AgentExecutor)
        → Delegates to Senior Leads (Workers)
            → Senior Leads spawn Junior Devs (Sub-workers)
                → Execute tools → LLM → Results
            → Auto-report back to Team Lead
        → Validator verifies results
    → Response shown in UI
```

---

## Tools (24)

| Tool | Purpose |
|------|---------|
| read_file | Read file contents |
| create_file | Create new file |
| write_file | Overwrite file |
| delete_file | Delete file |
| edit | Fuzzy text edits (8 strategies) |
| apply_patch | Apply unified diffs to files |
| multiedit | Edit multiple files at once |
| glob | Find files by pattern |
| grep | Search file contents |
| ls | Directory listing |
| bash | Shell commands (PTY, requires approval) |
| batch | Batch execute multiple tools |
| codesearch | Search codebase with context |
| question | Ask user clarifying questions |
| skill | Auto-invoke skills from plugins |
| todoread | Read session todo list |
| todowrite | Update session todo list |
| task | Spawn subagents |
| websearch | Web search (Tavily, Exa) |
| webfetch | Fetch + convert web pages |
| browser | Puppeteer browser automation |
| attempt_completion | Finish task with summary |
| plan_enter | Enter plan mode |
| plan_exit | Exit plan mode |

---

## Plugin System

Two types of user-facing extensions:

| Type | Trigger | Use Case |
|------|---------|----------|
| **Skills** | Auto-invoked by context (file globs, project type) | "When editing .tsx files, follow React patterns" |
| **Commands** | User types `/command-name` | "/deploy", "/test", "/review" |

Plugins bundle skills + commands + hooks + MCP servers into a single installable package.

---

## Code Style

- TypeScript strict, no `any`
- Max 300 lines per file
- kebab-case files, camelCase functions, PascalCase types
- Tests use Vitest: `npx vitest run <path>`

---

## Important Notes

- **Desktop app is Priority 1** — CLI and protocols are secondary
- **Never use Puppeteer to test the Tauri app** — Tauri uses native webview
- The `browser` tool is for web pages only; use `npm run tauri dev` for UI testing
- Build sequence: `npm run build:packages` → `npm run build:cli`
- Core tsconfig excludes test files from build: `"exclude": ["src/**/*.test.ts"]`
- `export *` from multiple modules can cause name collisions — use `as` renames

---

## Naming Convention

| Old Name | New Name | Role |
|----------|----------|------|
| Commander | Team Lead | Plans, delegates, coordinates |
| Worker | Senior Lead | Domain specialist, leads a group |
| Operator | Junior Dev | Executes specific file-level tasks |
| Validator | Validator | QA verification (unchanged) |

---

## Key Docs

| Doc | Purpose |
|-----|---------|
| [`docs/VISION.md`](docs/VISION.md) | Product vision |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | Phase overview and progress |
| [`docs/frontend/`](docs/frontend/) | **Desktop app: file map, settings, appearance, data flow, Tauri** |
| [`docs/frontend/changelog.md`](docs/frontend/changelog.md) | Frontend changelog (session by session) |
| [`docs/frontend/backlog.md`](docs/frontend/backlog.md) | Frontend backlog (what's missing, prioritized) |
| [`docs/frontend/design-system.md`](docs/frontend/design-system.md) | Design system (colors, glass, typography, motion) |
| [`docs/backend/`](docs/backend/) | Backend modules, test coverage, backlog, changelog |
| [`docs/backend/gap-analysis.md`](docs/backend/gap-analysis.md) | Competitive gap analysis (15 features vs 8 codebases) |
| [`docs/architecture/`](docs/architecture/) | System design |

---

## Tooling

| Tool | Purpose |
|------|---------|
| Biome | Formatter + linter |
| Oxlint | Fast linter (50-100x ESLint) |
| ESLint | SolidJS-specific rules |
| Lefthook | Git hooks (pre-commit, commit-msg) |
| commitlint | Conventional commit validation |
| Vitest | Test runner |
| Knip | Dead code finder |
