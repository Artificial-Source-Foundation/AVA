# AVA

> The Obsidian of AI Coding — Desktop AI coding app with a virtual dev team and community plugins

---

## What Is AVA

A **desktop-first AI coding app** (Tauri + SolidJS) for developers and vibe coders. Not an IDE replacement — an AI companion with:

- **Dev Team System** — Visible Team Lead → Senior Leads → Junior Devs hierarchy
- **Multi-Provider** — 14 LLM providers, use the best model for each task
- **Plugin Ecosystem** — Obsidian-style, easy to create, discover, and install
- **Extension-First Architecture** — Minimal core, everything else is a built-in extension
- **Open Source** — Community-first, MIT license

See [`docs/VISION.md`](docs/VISION.md) for the full product vision.

---

## Quick Commands

```bash
# Development
npm run tauri dev        # Run desktop app
pnpm build:all           # Build all packages + CLI
pnpm build:packages      # Build packages only (core, core-v2, extensions, platforms)
pnpm build:cli           # Build CLI
npm run lint             # Oxlint + ESLint
npm run format           # Biome format
npx tsc --noEmit         # Type check (root/desktop app)

# Testing
npm run test             # Vitest watch (all tests)
npm run test:run         # Single run (all tests)
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
├── src/                       # Desktop app (Tauri + SolidJS) ← PRIMARY
├── src-tauri/                 # Rust backend
├── packages/
│   ├── core/                  # Original backend (54K+ lines, being migrated)
│   ├── core-v2/               # NEW: Minimal core (~28 files, ~5K lines)
│   ├── extensions/            # NEW: Built-in extensions (25+ modules)
│   ├── platform-node/         # Node.js platform implementations
│   └── platform-tauri/        # Tauri platform implementations
└── cli/                       # CLI interface (secondary)
```

### Dual-Stack Architecture

AVA has two backend stacks running in parallel during migration:

**Original (`packages/core/`)** — Used by desktop app and CLI today
- Monolithic: 235 files, 54K lines, everything baked in
- Desktop app and CLI import from `@ava/core`

**New (`packages/core-v2/` + `packages/extensions/`)** — Extension-first
- Minimal core: ~28 files, ~5K lines (agent loop, tool registry, extension API)
- Everything else is a built-in extension using the same API as community plugins
- CLI has `ava agent-v2` command for testing the new stack

### Core-v2 Module Map

```
packages/core-v2/src/
├── agent/       # Simplified turn-based loop (~350 lines)
├── llm/         # LLM client interface + provider registry (no implementations)
├── tools/       # 6 core tools: read, write, edit, bash, glob, grep
├── extensions/  # ExtensionAPI + manager + loader
├── config/      # Extensible SettingsManager
├── session/     # Session CRUD + auto-save
├── bus/         # Pure pub/sub message bus
├── logger/      # Unified logger
└── platform.ts  # Platform abstraction (fs, shell, credentials, database)
```

### Extensions Module Map

```
packages/extensions/
├── providers/       # 14 LLM providers (anthropic, openai, google, etc.)
├── permissions/     # Safety & permission middleware
├── tools-extended/  # 18 additional tools (browser, websearch, etc.)
├── prompts/         # System prompt building
├── context/         # Token tracking + compaction
├── agent-modes/     # Plan mode, minimal mode, doom loop, recovery
├── hooks/           # Lifecycle hooks as middleware
├── validator/       # QA pipeline (syntax, types, lint, test)
├── commander/       # Team hierarchy (Team Lead → Senior Leads → Junior Devs)
├── mcp/             # MCP protocol client
├── codebase/        # Repo map, symbols, PageRank
├── git/             # Git snapshots, auto-commit
├── lsp/             # Language Server Protocol
├── diff/            # Diff tracking
├── focus-chain/     # Task progress tracking
├── instructions/    # Project instruction loading
├── models/          # Model registry
├── scheduler/       # Background task scheduler
├── skills/          # Auto-invoked knowledge modules
├── custom-commands/ # TOML user commands
├── slash-commands/  # Built-in /commands
├── integrations/    # External APIs (Exa search)
└── sandbox/         # Docker sandboxed execution
```

### Extension API

Every extension uses the same `ExtensionAPI` interface — built-in and community:

```typescript
interface ExtensionAPI {
  registerTool(tool: Tool): Disposable
  registerCommand(command: SlashCommand): Disposable
  registerAgentMode(mode: AgentMode): Disposable
  registerValidator(validator: Validator): Disposable
  registerProvider(name: string, factory: LLMClientFactory): Disposable
  addToolMiddleware(middleware: ToolMiddleware): Disposable
  on(event: string, handler: EventHandler): Disposable
  emit(event: string, data: unknown): void
  readonly bus: MessageBus
  readonly log: SimpleLogger
  readonly storage: ExtensionStorage  // per-extension private storage
}
```

**How extensions hook into the agent loop:**
- Permissions → `addToolMiddleware()` at priority 0
- Hooks → `addToolMiddleware()` at priority 10
- Plan mode → `registerAgentMode()` — filters available tools
- Team hierarchy → `registerAgentMode()` — replaces loop with delegation
- Validator → `on('agent:completing')` — blocks completion if validation fails

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

### Data Flow

```
Desktop App / CLI
    → setPlatform(createNodePlatform())
    → Load extensions (activate in priority order)
    → Team Lead (AgentExecutor)
        → Stream LLM → collect tool calls → run middleware → execute
        → Extensions intercept via middleware and events
    → Response shown in UI
```

---

## How To Add...

### A new tool
```typescript
// packages/extensions/tools-extended/src/my-tool.ts
import { defineTool } from '@ava/core-v2/tools'
export const myTool = defineTool({
  name: 'my_tool',
  description: '...',
  schema: z.object({ ... }),
  async execute(input, ctx) { return { success: true, output: '...' } },
})

// In extension activate():
api.registerTool(myTool)
```

### A new LLM provider
```typescript
// packages/extensions/providers/my-provider/src/index.ts
import { createOpenAICompatClient } from '../../_shared/src/openai-compat.js'
const Client = createOpenAICompatClient({
  provider: 'my-provider', baseUrl: '...', defaultModel: '...',
})
export function activate(api) {
  return api.registerProvider('my-provider', () => new Client())
}
```

### A new agent mode
```typescript
// packages/extensions/agent-modes/src/my-mode.ts
export const myMode: AgentMode = {
  name: 'my-mode',
  description: '...',
  filterTools(tools) { return tools.filter(...) },
  buildPromptSection() { return 'Extra instructions...' },
}
// In extension activate():
api.registerAgentMode(myMode)
```

---

## Tools (24)

| Tool | Location | Purpose |
|------|----------|---------|
| read_file | core-v2 | Read file contents |
| write_file | core-v2 | Overwrite file |
| edit | core-v2 | Fuzzy text edits (8 strategies) |
| bash | core-v2 | Shell commands |
| glob | core-v2 | Find files by pattern |
| grep | core-v2 | Search file contents |
| create_file | extensions | Create new file |
| delete_file | extensions | Delete file |
| apply_patch | extensions | Apply unified diffs |
| multiedit | extensions | Edit multiple files |
| ls | extensions | Directory listing |
| batch | extensions | Batch execute multiple tools |
| codesearch | extensions | Search codebase with context |
| question | extensions | Ask user clarifying questions |
| skill | extensions | Auto-invoke skills |
| todoread | extensions | Read session todo list |
| todowrite | extensions | Update session todo list |
| task | extensions | Spawn subagents |
| websearch | extensions | Web search |
| webfetch | extensions | Fetch + convert web pages |
| browser | extensions | Puppeteer browser automation |
| attempt_completion | extensions | Finish task with summary |
| plan_enter | extensions | Enter plan mode |
| plan_exit | extensions | Exit plan mode |

---

## Plugin System

Two types of user-facing extensions:

| Type | Trigger | Use Case |
|------|---------|----------|
| **Skills** | Auto-invoked by context (file globs, project type) | "When editing .tsx files, follow React patterns" |
| **Commands** | User types `/command-name` | "/deploy", "/test", "/review" |

Plugins bundle skills + commands + hooks + MCP servers into a single installable package.

---

## Key Singletons

| Singleton | Get | Set | Reset |
|-----------|-----|-----|-------|
| Platform | `getPlatform()` | `setPlatform()` | — |
| Message Bus | `getMessageBus()` | `setMessageBus()` | `resetMessageBus()` |
| Logger | `getLogger()` | `setLogger()` | `resetLogger()` |

---

## Code Style

- TypeScript strict, no `any`
- Max 300 lines per file
- kebab-case files, camelCase functions, PascalCase types
- ESM with `.js` extensions in imports
- Tests use Vitest: `npx vitest run <path>`
- Tests co-located: `foo.test.ts` next to `foo.ts`

---

## Important Notes

- **Desktop app is Priority 1** — CLI and protocols are secondary
- **Never use Puppeteer to test the Tauri app** — Tauri uses native webview
- The `browser` tool is for web pages only; use `npm run tauri dev` for UI testing
- Build order: core → core-v2 → extensions → platform-node → platform-tauri → CLI
- `pnpm build:all` handles the correct build order automatically
- Core tsconfig excludes test files from build: `"exclude": ["src/**/*.test.ts"]`
- `export *` from multiple modules can cause name collisions — use `as` renames
- Platform abstraction: never use `node:fs` directly — use `getPlatform().fs`

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
| [`docs/frontend/`](docs/frontend/) | Desktop app: file map, settings, design system |
| [`docs/frontend/design-system.md`](docs/frontend/design-system.md) | Design system (colors, glass, typography, motion) |
| [`docs/backend/`](docs/backend/) | Backend modules, test coverage |

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
