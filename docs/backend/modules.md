<!-- Last verified: 2026-03-05. Run 'npm run test:run && cargo test --workspace' to revalidate. -->

# Backend Modules

> Current architecture uses `packages/core-v2/` + `packages/extensions/`
>
> `packages/core/` is now a compatibility re-export shim.

## Module Organization

### Core Runtime (`packages/core-v2/`)

The execution kernel contains:

| Module | Purpose |
|--------|---------|
| `agent/` | Agent loop, turn execution, tool dispatch, event handling |
| `tools/` | Core tool registry and built-in tool definitions |
| `llm/` | LLM client factory and message handling |
| `session/` | Session CRUD, persistence, forking |
| `context/` | Token tracking and context window management |
| `extensions/` | Extension loading, lifecycle, manifest parsing |
| `permissions/` | Permission checks and security pipeline |
| `bus/` | Pub/sub message bus |

### Extensions (`packages/extensions/`)

20 built-in extension modules:

1. **agent-modes** — Agent mode management (plan mode, etc.)
2. **commander** — Worker delegation and batch execution
3. **context** — Context strategies and compaction
4. **diff** — Diff tracking and patch application
5. **git** — Git operations and auto-commit
6. **hooks** — Lifecycle hook system
7. **instructions** — Project instruction loading
8. **lsp** — Language Server Protocol client
9. **mcp** — Model Context Protocol support
10. **memory** — Persistent memory and recall
11. **models** — Model registry and availability
12. **permissions** — Runtime permission system
13. **plugins** — Plugin management UI
14. **prompts** — Prompt building and variants
15. **providers** — LLM provider implementations (16 sub-providers)
16. **recall** — Memory recall integration
17. **server** — Local server for external integrations
18. **slash-commands** — User-defined slash commands
19. **tools-extended** — Extended tool surface (~15 tools)
20. **validator** — QA validation pipeline

### Rust Crates (`crates/`)

19 Rust crates for compute/safety hotpaths:

- `ava-agent`, `ava-codebase`, `ava-commander`, `ava-config`, `ava-context`
- `ava-db`, `ava-extensions`, `ava-llm`, `ava-logger`, `ava-lsp`
- `ava-mcp`, `ava-memory`, `ava-permissions`, `ava-platform`, `ava-sandbox`
- `ava-session`, `ava-tools`, `ava-types`, `ava-validator`

## File Counts

- `packages/core-v2/`: ~50 source files, ~40 test files
- `packages/extensions/`: ~20 extension directories
- `crates/`: 19 Rust crates

Counts are approximate and may drift between releases.

## Migration Notes

The previous `packages/core/` monolith has been restructured:
- Business logic moved to `packages/core-v2/`
- Features modularized into `packages/extensions/`
- `packages/core/` now re-exports from core-v2 for compatibility

---

*See also: [architecture-guide.md](architecture-guide.md), [CLAUDE.md](../../CLAUDE.md)*
