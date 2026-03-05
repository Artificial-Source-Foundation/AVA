<!-- Last verified: 2026-03-05. Run 'npm run test:run && cargo test --workspace' to revalidate. -->

# Backend Overview

This document describes AVA's backend as of v2.

## Runtime Model

AVA runs a hybrid backend:

- `packages/core-v2/`: minimal execution kernel (agent loop, tools, extension API)
- `packages/extensions/`: feature modules layered onto core-v2
- Rust/Tauri commands (`src-tauri/src/commands/`) for compute and safety hotpaths

`packages/core/` remains as a compatibility re-export shim.

## Current Shape

- Built-in extensions: ~20
- Tool surface: ~41 tools

### Runtime Extension Count

Typical runtime extension activation count is ~31:
- 20 feature extensions (always enabled)
- ~16 provider extensions (varies by configuration)
- Minus 4 commonly disabled: `lsp`, `mcp`, `server`, `litellm`

### Extension Module Index (~20)

- agent-modes
- commander
- context
- diff
- git
- hooks
- instructions
- lsp
- mcp
- memory
- models
- permissions
- plugins
- prompts
- providers
- recall
- server
- slash-commands
- tools-extended
- validator

### Rust Crate Index (19)

- ava-agent
- ava-codebase
- ava-commander
- ava-config
- ava-context
- ava-db
- ava-extensions
- ava-llm
- ava-logger
- ava-lsp
- ava-mcp
- ava-memory
- ava-permissions
- ava-platform
- ava-sandbox
- ava-session
- ava-tools
- ava-types
- ava-validator

Counts are approximate and may drift between releases. Use runtime registries for exact values.

## Rust Hotpath Pattern

Use `dispatchCompute<T>(rustCommand, rustArgs, tsFallback)` for performance-sensitive logic.

- Tauri desktop runtime: execute Rust command
- Node/CLI runtime: use TypeScript fallback

Examples include edit/grep compute paths, permission evaluation, and validation checks.

## Extension Responsibilities

Common extension responsibilities include:

- middleware (`before` / `after`) with explicit priority
- tool registration
- settings registration and settings sync
- event subscriptions through the bus

## Safety and Reliability Layers

- sandbox routing for install-class shell commands
- dynamic permission learning with dangerous-command safeguards
- checkpoint refs for recovery before destructive actions
- agent reliability middleware for stuck-loop and recovery handling

## Where To Read Next

- `docs/backend/architecture-guide.md`
- `docs/troubleshooting.md`
- `CLAUDE.md`
