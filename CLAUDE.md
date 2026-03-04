# AVA

> The Obsidian of AI Coding — Desktop AI coding app with a virtual dev team and community plugins

---

## What Is AVA

A **desktop-first AI coding app** (Tauri + SolidJS) for developers and vibe coders. Not an IDE replacement — an AI companion with:

- **Dev Team System** — Visible Team Lead → Senior Leads → Junior Devs hierarchy
- **Multi-Provider** — 16 LLM providers, use the best model for each task
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

# CLI (after pnpm build:all)
node cli/dist/index.js run "goal" --mock             # Run agent with mock LLM
node cli/dist/index.js run "goal" --provider openrouter --model "anthropic/claude-sonnet-4" --verbose
node cli/dist/index.js tool list                      # List all 70+ tools
node cli/dist/index.js tool read_file --path README.md
node cli/dist/index.js validate src/index.ts          # Run validation pipeline
node cli/dist/index.js auth status                    # Show OAuth status
node cli/dist/index.js plugin init my-plugin          # Scaffold a plugin
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
│   ├── core-v2/               # NEW: Minimal core (~40 files, ~5K lines)
│   ├── extensions/            # NEW: Built-in extensions (35+ modules)
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
- Minimal core: ~40 files, ~7K lines (agent loop, tool registry, extension API, session DAG)
- Everything else is a built-in extension using the same API as community plugins
- CLI: `ava run` (unified, default), `ava agent-v2 run` (full-featured), `ava tool` (individual tools) — all load 48 extensions

### Core-v2 Module Map

```
packages/core-v2/src/
├── agent/       # Turn-based loop (~730 lines) + repair, output-files, structured-output, efficient-results
├── llm/         # LLM client interface + provider registry (no implementations)
├── tools/       # 7 core tools: read, write, edit, bash, glob, grep, pty
├── extensions/  # ExtensionAPI + manager + loader + hook system
├── config/      # Extensible SettingsManager
├── session/     # Session CRUD + auto-save + archival + slug + busy state + DAG/branching
├── bus/         # Pure pub/sub message bus
├── logger/      # Unified logger
└── platform.ts  # Platform abstraction (fs, shell, credentials, database, pty)
```

### Extensions Module Map

```
packages/extensions/
├── providers/       # 16 LLM providers (anthropic, openai, google, azure, litellm, etc.)
├── permissions/     # Safety middleware + bash parsing + arity fingerprinting
├── tools-extended/  # 27 additional tools (websearch, vision, voice, inline suggest, etc.)
├── prompts/         # System prompt building + model-family variants
├── context/         # Token tracking + compaction + prune strategy
├── agent-modes/     # Plan mode, minimal mode, doom loop, recovery, best-of-N sampling
├── hooks/           # Lifecycle hooks as middleware + auto-formatter
├── validator/       # QA pipeline (syntax, types, lint, test)
├── commander/       # Praxis hierarchy (Commander → Leads → Workers) + explore subagent
├── mcp/             # MCP protocol client (stdio, SSE, HTTP streaming) + ACP protocol
├── codebase/        # Repo map, symbols, PageRank
├── git/             # Git snapshots, checkpoints, PR/branch/issue tools, worktrees
├── lsp/             # Language Server Protocol (9 tools)
├── diff/            # Diff tracking + per-hunk review + undo/redo + session summary
├── file-watcher/    # File system polling (.git/HEAD, configurable paths)
├── sharing/         # Session sharing (stub)
├── focus-chain/     # Task progress tracking
├── instructions/    # Project instruction loading + subdirectory AGENTS.md + URL loader
├── memory/          # Persistent cross-session memory + auto-learning
├── models/          # Model registry + availability tracking + fallback + packs
├── plugins/         # Plugin install/uninstall backend + catalog API + reviews/ratings
├── scheduler/       # Background task scheduler
├── skills/          # Auto-invoked knowledge modules
├── custom-commands/ # TOML user commands
├── slash-commands/  # Built-in /commands
├── integrations/    # External APIs (Exa search) + well-known config
├── sandbox/         # Docker + OS-level sandboxed execution (bwrap, sandbox-exec)
├── server/          # HTTP server for remote agent control (ACP REST API)
├── recipes/         # YAML/JSON composable workflows with param substitution
├── recall/          # Chat recall — FTS5 full-text search across sessions
├── profiles/        # Agent profiles — save/load/list, tool filtering, built-in presets
└── github-bot/      # GitHub webhook bot — @ava mention handling
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
  registerHook<TInput, TOutput>(name: HookName, handler: HookHandler<TInput, TOutput>): Disposable
  callHook<TInput, TOutput>(name: HookName, input: TInput, output: TOutput): Promise<HookResult<TOutput>>
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
- Plugin hooks → `registerHook()` / `callHook()` for sequential chaining pipelines
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

## Tools (70+)

| Tool | Location | Purpose |
|------|----------|---------|
| **Core (7)** | | |
| read_file | core-v2 | Read file contents |
| write_file | core-v2 | Overwrite file |
| edit | core-v2 | Fuzzy text edits (8 strategies) |
| bash | core-v2 | Shell commands |
| glob | core-v2 | Find files by pattern |
| grep | core-v2 | Search file contents |
| pty | core-v2 | PTY terminal (interactive commands) |
| **Extended (27)** | | |
| create_file | tools-extended | Create new file |
| delete_file | tools-extended | Delete file |
| apply_patch | tools-extended | Apply unified diffs (streaming) |
| multiedit | tools-extended | Edit multiple files concurrently |
| ls | tools-extended | Directory listing |
| batch | tools-extended | Batch execute multiple tools |
| codesearch | tools-extended | Search API docs/code via Exa |
| repo_map | tools-extended | Project structure overview |
| question | tools-extended | Ask user clarifying questions |
| todoread | tools-extended | Read session todo list |
| todowrite | tools-extended | Update session todo list |
| task | tools-extended | Spawn subagents (+ task resumption) |
| websearch | tools-extended | Web search (DuckDuckGo default, Tavily/Exa optional) |
| webfetch | tools-extended | Fetch + convert web pages |
| attempt_completion | tools-extended | Finish task with summary |
| plan_enter | tools-extended | Enter plan mode |
| plan_exit | tools-extended | Exit plan mode (+ save to file) |
| bash_background | tools-extended | Spawn background process, return PID |
| bash_output | tools-extended | Read stdout/stderr from background PID |
| bash_kill | tools-extended | Kill background process by PID |
| view_image | tools-extended | View image with base64 encoding for vision models |
| voice_transcribe | tools-extended | Transcribe audio via Whisper API or local whisper |
| inline_suggest | tools-extended | FIM-based inline code autocomplete with LRU cache |
| edit_benchmark | tools-extended | Benchmark all 8 edit strategies on real diffs |
| session_cost | tools-extended | Get cost tracking for current session |
| create_rule | tools-extended | Create a new safety/policy rule |
| create_skill | tools-extended | Create a new auto-invoked skill |
| **Delegation (13)** | | |
| delegate_frontend-lead | commander | Delegate to Frontend Senior Lead |
| delegate_backend-lead | commander | Delegate to Backend Senior Lead |
| delegate_qa-lead | commander | Delegate to QA Senior Lead |
| delegate_fullstack-lead | commander | Delegate to Fullstack Senior Lead |
| delegate_coder | commander | Delegate coding tasks to Coder worker |
| delegate_tester | commander | Delegate testing tasks to Tester worker |
| delegate_reviewer | commander | Delegate review tasks to Reviewer worker |
| delegate_researcher | commander | Delegate research tasks to Researcher worker |
| delegate_debugger | commander | Delegate debugging tasks to Debugger worker |
| delegate_architect | commander | Delegate architecture tasks to Architect worker |
| delegate_planner | commander | Delegate planning tasks to Planner worker |
| delegate_devops | commander | Delegate DevOps tasks to DevOps worker |
| delegate_explorer | commander | Delegate read-only exploration tasks |
| **Memory (4)** | | |
| memory_read | memory | Read a persistent memory entry |
| memory_write | memory | Save a persistent memory entry |
| memory_list | memory | List all memory entries |
| memory_delete | memory | Delete a memory entry |
| **LSP (9)** | | |
| lsp_diagnostics | lsp | Get LSP diagnostics for a file |
| lsp_hover | lsp | Get hover info for a symbol |
| lsp_definition | lsp | Go to definition of a symbol |
| lsp_document_symbols | lsp | List symbols in a document |
| lsp_workspace_symbols | lsp | Search symbols across workspace |
| lsp_code_actions | lsp | Get available code actions |
| lsp_rename | lsp | Rename a symbol across files |
| lsp_references | lsp | Find all references to a symbol |
| lsp_completions | lsp | Get completion suggestions |
| **Git (4)** | | |
| create_pr | git | Create GitHub pull request via `gh` |
| create_branch | git | Create and switch to new git branch |
| switch_branch | git | Switch to existing git branch |
| read_issue | git | Read GitHub issue details via `gh` |
| **Other (7)** | | |
| diff_review | diff | Per-hunk diff review (list/accept/reject/apply) |
| sandbox_run | sandbox | Execute code in OS-level sandbox (bwrap/docker) |
| profile_save | profiles | Save or update an agent profile |
| profile_load | profiles | Load and activate an agent profile |
| profile_list | profiles | List built-in and saved agent profiles |
| load_skill | skills | Load a skill by name for the agent |
| recall | recall | Full-text search across past sessions |

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
- **Reference code is local** — When the user asks to look at another codebase or how other tools handle features (OpenCode, Cline, Gemini CLI, etc.), ALWAYS check `docs/reference-code/` and `docs/research/` first. Do NOT search the internet for source code unless the user specifically asks. The local copies are the authoritative reference

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

---

## SOTA Models for Testing

See [`docs/testing-models.md`](docs/testing-models.md) for the full model table with IDs for OpenRouter testing.
