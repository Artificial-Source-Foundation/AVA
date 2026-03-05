# AI Coding Agent Instructions

> Universal instructions for AI assistants working on AVA

---

## Quick Start

```bash
npm run tauri dev
npm run lint
npm run format:check
npx tsc --noEmit
npm run test
```

Read first: `CLAUDE.md`

---

## Project Overview

AVA is a multi-agent AI coding assistant desktop app.

- Runtime: Tauri 2 (Rust + Web)
- Frontend: SolidJS + TypeScript
- Core runtime: `packages/core-v2/`
- Features: extension-first in `packages/extensions/`
- Compatibility shim: `packages/core/`
- CLI: `cli/` (ACP integration)

---

## Architecture (Current)

### Repository Layout

```text
AVA/
├── packages/
│   ├── core-v2/         # execution kernel
│   ├── extensions/      # built-in extension modules (20)
│   ├── core/            # legacy re-export shim
│   ├── platform-node/
│   └── platform-tauri/
├── cli/
├── src/
└── src-tauri/
```

### Important Counts

- Built-in extensions: 20
- Tool surface: ~39

### Rust Hotpath Rule

Always prefer:

```ts
dispatchCompute<T>(rustCommand, rustArgs, tsFallback)
```

- Tauri runtime -> Rust command path
- Node/CLI runtime -> TS fallback path

Do not introduce direct `invoke()` calls in feature code where dispatch compute already applies.

---

## Data Flow

```text
AgentExecutor.run(goal, context)
  -> prepare history/context
  -> model response
  -> parse + execute tools
  -> middleware before/after hooks
  -> emit events/usage
  -> completion/termination
```

Middleware priorities are contract-sensitive. Lower numeric priority runs earlier.

---

## Code Standards

### TypeScript

- Strict mode; no `any`
- Explicit return types for exported functions
- ESM imports with `.js` where required by package config

### Files

- Max 300 lines per file
- kebab-case filenames for non-components
- camelCase functions
- PascalCase component/types

### Components

- SolidJS only (no React patterns)
- Functional components
- Props as `{Name}Props`
- Use Solid primitives (`createSignal`, `Show`, `For`, `onCleanup`)

---

## Common Tasks

### Add Tool

1. Implement in extension package (usually `packages/extensions/tools-extended/src/`)
2. Register via extension activation
3. Add tests and export wiring as needed

### Add Middleware

1. Implement `ToolMiddleware` in extension
2. Set explicit `priority`
3. Register in extension `activate()`
4. Add focused tests for ordering/behavior

### Add Rust-Accelerated Feature

1. Add Rust command in `src-tauri/src/commands/`
2. Register in `src-tauri/src/lib.rs`
3. Route via `dispatchCompute` with TS fallback
4. Add tests for both native and fallback paths

---

## Before Committing

- `npm run lint`
- `npm run format:check`
- `npx tsc --noEmit`
- `npm run test`
- `npm run tauri dev` (or `npm run tauri build` for release verification)

---

## Do Not

- Commit secrets or credentials
- Add parent-directory imports
- Rely on migration-era `packages/core/src/*` paths for new work
- Use React patterns in `src/`

---

## Documentation Priority

1. `CLAUDE.md`
2. `docs/README.md`
3. `docs/ROADMAP.md`
4. `docs/VISION.md`
5. `docs/development/opencode-comparison.md`
6. `docs/reference-code/`
