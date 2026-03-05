<!-- Last verified: 2026-03-05. Run 'npm run test:run && cargo test --workspace' to revalidate. -->

# AVA Architecture & Conventions

## Quick Commands

```bash
npm run tauri dev
npm run lint
npm run format:check
npx tsc --noEmit
npm run test:run
```

If `npm run tauri dev` fails with `ENOSPC` watcher errors on Linux, see `docs/troubleshooting.md`.

Release verification:

```bash
npm run tauri build
cargo test --workspace
```

## Architecture

AVA uses a hybrid architecture:

- Rust crates for compute-heavy and safety-sensitive hotpaths
- `packages/core-v2/` as the orchestration kernel
- extension-first capability surface in `packages/extensions/`
- `packages/core/` as a compatibility re-export shim

Orchestration is primarily in TypeScript (core-v2 + 20 feature extensions), with all major compute paths routed through Rust when available.

## Project Structure

```text
AVA/
├── crates/                   # ~19 Rust crates (compute/safety/runtime services)
├── packages/
│   ├── core-v2/              # execution kernel (~90 files including tests)
│   ├── extensions/           # ~20 built-in extension modules
│   ├── core/                 # compatibility shim (re-exports from core-v2)
│   ├── platform-node/
│   └── platform-tauri/
├── src/                      # desktop frontend (SolidJS)
├── src-tauri/                # desktop native host + commands
├── cli/                      # ACP-compatible CLI
└── tests/
```

## Tool Surface (~41)

| Group | Count | Notes |
|---|---:|---|
| Core tools | 6 | read, write, edit, bash, glob, grep |
| Extended tools | ~16 | multiedit, apply-patch, task, webfetch/search, question, completion, plan_enter, plan_exit |
| Git tools | 4 | status/diff/commit helper flows |
| Memory tools | 4 | remember/recall/search/recent |
| LSP tools | 9 | diagnostics, definition, references, rename, hover, symbols, format |
| Recall tools | 1 | recall |
| Delegate tools | 4 | delegate_coder, delegate_reviewer, delegate_researcher, delegate_explorer |

Total: ~41 static tools (plus dynamic MCP and custom tools)

## Extensions Map (20)

1. `agent-modes`
2. `commander`
3. `context`
4. `diff`
5. `git`
6. `hooks`
7. `instructions`
8. `lsp`
9. `mcp`
10. `memory`
11. `models`
12. `permissions`
13. `plugins`
14. `prompts`
15. `providers`
16. `recall`
17. `server`
18. `slash-commands`
19. `tools-extended`
20. `validator`

Runtime extension count explanation:
- Feature extensions: 20 total (always loaded)
- Provider extensions: ~15 at runtime (sub-extensions within providers/)
- Disabled in CLI: `lsp`, `mcp`, `server`, `litellm` (4)
- Typical CLI activation: ~31 extensions (20 + 15 - 4)

## dispatchCompute Pattern

Use this pattern for Rust-backed features:

```typescript
// Example from packages/extensions/tools-extended/src/edit.ts
import { dispatchCompute } from '@ava/core-v2/platform'

export const editTool = defineTool({
  name: 'edit',
  execute: async (input, context) => {
    return dispatchCompute<EditResult>(
      'edit_file',                    // Rust command name
      { path: input.path, content: input.content },  // Args for Rust
      async () => {
        // TypeScript fallback for Node/CLI runtime
        const fs = await import('fs/promises')
        await fs.writeFile(input.path, input.content)
        return { success: true }
      }
    )
  }
})
```

- Tauri runtime: execute Rust command
- Node/CLI runtime: execute TS fallback

Apply this in edit/grep/validation/permissions/memory/sandbox or any new compute-heavy path.

## Middleware Priority

Middleware runs in priority order (lower number = earlier execution):

| Middleware | Priority | Purpose |
|------------|----------|---------|
| sandbox | 3 | Route install-class commands through sandbox |
| reliability | 5 | Detect stuck loops, recovery handling |
| error-recovery | 15 | Checkpoint recovery before destructive actions |
| lsp-diagnostics | 20 | LSP-based diagnostics validation |

Register middleware via `api.addToolMiddleware({ priority, before, after })`.

## Code Style

### TypeScript / SolidJS

- strict mode, no `any`
- explicit exported return types
- SolidJS only in `src/` (no React patterns)
- use `.js` import suffix where package config requires it
- Biome for formatting, ESLint + oxlint for linting

### Rust

- prefer small command modules in `src-tauri/src/commands/`
- add serde `rename_all = "camelCase"` for TS IPC compatibility
- register every new command in `src-tauri/src/commands/mod.rs` and `src-tauri/src/lib.rs`
- keep error strings actionable and deterministic

## Common Workflows

### Add a Tool

1. implement tool in extension package (usually `packages/extensions/tools-extended/src/`)
2. register on activation
3. add unit tests

### Add Middleware

1. implement `ToolMiddleware`
2. choose explicit priority (see priority table above)
3. register in extension `activate()`
4. add ordering/behavior tests

### Add Rust Hotpath

1. add command in `src-tauri/src/commands/`
2. export in `mod.rs` + register handler in `lib.rs`
3. route via `dispatchCompute` with TS fallback
4. test both native path and fallback

## Documentation Priority

1. `CLAUDE.md` (this file)
2. `docs/backend.md`
3. `docs/troubleshooting.md`
4. `docs/plugins/PLUGIN_SDK.md`
5. `docs/reference-code/`
