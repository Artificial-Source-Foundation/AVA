# AVA Architecture & Conventions

## Quick Commands

```bash
npm run tauri dev
npm run lint
npm run format:check
npx tsc --noEmit
npm run test:run
```

Release verification:

```bash
npm run tauri build
cargo test --workspace
```

## Hybrid Architecture

AVA uses a hybrid architecture:

- Rust crates for compute-heavy and safety-sensitive hotpaths
- `packages/core-v2/` as the orchestration kernel
- extension-first capability surface in `packages/extensions/`
- `packages/core/` as a compatibility re-export shim

Orchestration is primarily in TypeScript (core-v2 + 18 orchestration extensions), with all major compute paths routed through Rust when available.

## Project Structure

```text
AVA/
├── crates/                   # 19 Rust crates (compute/safety/runtime services)
├── packages/
│   ├── core-v2/              # minimal runtime kernel
│   ├── extensions/           # 20 built-in extension modules
│   ├── core/                 # compatibility shim (re-exports)
│   ├── platform-node/
│   └── platform-tauri/
├── src/                      # desktop frontend (SolidJS)
├── src-tauri/                # desktop native host + commands
├── cli/                      # ACP-compatible CLI
└── tests/
```

## Tool Surface (~39)

| Group | Count | Notes |
|---|---:|---|
| Core tools | 7 | read/write/edit/bash/glob/grep/ls |
| Extended tools | 15 | multiedit, apply-patch, task, webfetch/search, question, completion, etc. |
| Git tools | 4 | status/diff/commit helper flows |
| Memory tools | 4 | remember/recall/search/recent |
| LSP tools | 9 | diagnostics, definition, references, rename, hover, symbols, format |

Total: ~39

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

## dispatchCompute Pattern

Use this pattern for Rust-backed features:

```ts
dispatchCompute<T>(rustCommand, rustArgs, tsFallback)
```

- Tauri runtime: execute Rust command
- Node/CLI runtime: execute TS fallback

Apply this in edit/grep/validation/permissions/memory/sandbox or any new compute-heavy path.

## Code Style

### TypeScript / SolidJS

- strict mode, no `any`
- explicit exported return types
- SolidJS only in `src/` (no React patterns)
- use `.js` import suffix where package config requires it

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
2. choose explicit priority (lower number runs earlier)
3. register in extension `activate()`
4. add ordering/behavior tests

### Add Rust Hotpath

1. add command in `src-tauri/src/commands/`
2. export in `mod.rs` + register handler in `lib.rs`
3. route via `dispatchCompute` with TS fallback
4. test both native path and fallback

## Documentation Priority

1. `CLAUDE.md`
2. `docs/backend.md`
3. `docs/backend/architecture-guide.md`
4. `docs/troubleshooting.md`
5. `docs/plugins/PLUGIN_SDK.md`
6. `docs/reference-code/`
